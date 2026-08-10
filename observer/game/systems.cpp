// THE OBSERVER - systems: observation, paranoia profiling, The Witness,
// the anomaly director, photography, movement, persistence.
//
// The rule that runs everything: ATTENTION MAKES THINGS REAL.
#include "game.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>

using nlohmann::json;

// ============================================================== channels ===
Channel channelFromName(const std::string& s) {
    if (s == "BEHIND") return CH_BEHIND;
    if (s == "MIRROR") return CH_MIRROR;
    if (s == "CCTV") return CH_CCTV;
    if (s == "DOOR") return CH_DOOR;
    if (s == "LIGHT") return CH_LIGHT;
    if (s == "PHOTO") return CH_PHOTO;
    if (s == "SOUND") return CH_SOUND;
    if (s == "SPACE") return CH_SPACE;
    if (s == "WITNESS") return CH_WITNESS;
    return CH_SOUND;
}

float ParanoiaProfile::weight(Channel c) const {
    float sum = 1e-3f;
    for (float s : score) sum += s;
    return clampf(score[c] / sum * (float)CH_COUNT * 0.5f, 0.0f, 1.0f);
}

void ParanoiaProfile::decay(float dt) {
    for (float& s : score) s = std::fmax(0.0f, s - dt * 0.004f);
}

// ============================================================ observation ==
float ObservationSystem::gazeBoost(vec3 camPos, vec3 camFwd, const Observable& o) const {
    vec3 to = o.pos - camPos;
    float d = length(to);
    if (d < 0.05f || d > 26.0f) return 0;
    vec3 dir = to / d;
    float c = dot(camFwd, dir);
    // center-weighted: full value only when it's what you're LOOKING at
    float focus = smoothstepf(0.906f, 0.999f, c);   // ~25 deg .. ~2.5 deg
    float distK = d < 3 ? 1.0f : clampf(1.0f - (d - 3) / 26.0f, 0.25f, 1.0f);
    return focus * distK;
}

void ObservationSystem::update(float dt, vec3 camPos, vec3 camFwd, const World& world, float) {
    for (auto& o : obs) {
        if (!o.active) continue;
        float g = gazeBoost(camPos, camFwd, o);
        if (g > 0.05f && world.lineOfSight(camPos, o.pos)) {
            o.dwell += dt;
            float gain = g * dt * (0.6f + 0.4f * std::fmin(o.dwell, 4.0f) / 4.0f);
            o.manifest += gain;
            attention += gain * 0.12f;
        } else {
            o.dwell = std::fmax(0.0f, o.dwell - dt * 2.0f);
        }
    }
}

Observable* ObservationSystem::find(const std::string& id) {
    for (auto& o : obs)
        if (o.id == id) return &o;
    return nullptr;
}

Observable* ObservationSystem::spawn(const std::string& id, vec3 pos, int room, bool witness) {
    Observable* o = find(id);
    if (!o) {
        obs.push_back({});
        o = &obs.back();
        o->id = id;
    }
    o->pos = pos;
    o->room = room;
    o->active = true;
    o->witness = witness;
    return o;
}

void ObservationSystem::despawn(const std::string& id) {
    if (Observable* o = find(id)) o->active = false;
}

// ================================================================ witness ==
void WitnessSystem::gainKnowledge(float k) {
    knowledge += k;
    int s = std::min(stageForKnowledge(), stageCap);
    if (s > stage) stage = s;
}

int WitnessSystem::stageForKnowledge() const {
    float k = knowledge + photoKnowledge * 1.6f;
    if (k > 46) return 4;
    if (k > 26) return 3;
    if (k > 11) return 2;
    if (k > 2.5f) return 1;
    return 0;
}

// =============================================================== director ==
const EventSpec* Director::pick(const GameContent& c, int act, const ParanoiaProfile& prof,
                                const World& world, int playerRoom, double now,
                                const std::function<bool(const EventSpec&)>& extraFilter) {
    std::vector<const EventSpec*> cands;
    std::vector<float> weights;
    std::vector<int> vis;
    world.visibleRooms(playerRoom, vis);
    std::unordered_set<int> visSet(vis.begin(), vis.end());

    for (const auto& ev : c.events) {
        if (act < ev.minAct || act > ev.maxAct) continue;
        if (ev.once && spent.count(ev.id)) continue;
        auto lf = lastFired.find(ev.id);
        if (lf != lastFired.end() && now - lf->second < ev.cooldown) continue;
        if (ev.needUnobservedRoom && !ev.room.empty()) {
            auto it = world.roomIdx.find(ev.room);
            if (it != world.roomIdx.end() && visSet.count(it->second)) continue;
        }
        if (extraFilter && !extraFilter(ev)) continue;
        float w = ev.weight * (0.55f + 2.2f * prof.weight(channelFromName(ev.channel)));
        cands.push_back(&ev);
        weights.push_back(w);
    }
    if (cands.empty()) return nullptr;
    float total = 0;
    for (float w : weights) total += w;
    float roll = rng.range(0, total);
    for (size_t i = 0; i < cands.size(); ++i) {
        roll -= weights[i];
        if (roll <= 0) {
            lastFired[cands[i]->id] = now;
            if (cands[i]->once) spent.insert(cands[i]->id);
            return cands[i];
        }
    }
    lastFired[cands.back()->id] = now;
    return cands.back();
}

// ================================================================= player ==
void Game::updatePlayer(float dt) {
    Input& in = eng.input();

    // ---- look
    float sens = 0.0022f * settings.sensitivity;
    pYaw -= in.mouseDX * sens;
    pPitch -= in.mouseDY * sens;
    pPitch = clampf(pPitch, radians(-86), radians(86));
    pYaw = wrapAngle(pYaw);

    // behind-check detection from yaw history
    yawHistory.push_back({worldTime, pYaw});
    while (!yawHistory.empty() && worldTime - yawHistory.front().first > 0.5) yawHistory.pop_front();
    static double lastBehind = -10;
    if (worldTime - lastBehind > 1.6 && yawHistory.size() > 3) {
        float total = 0;
        for (size_t i = 1; i < yawHistory.size(); ++i)
            total += std::fabs(wrapAngle(yawHistory[i].second - yawHistory[i - 1].second));
        if (total > radians(130)) {
            lastBehind = worldTime;
            profile.behindChecks++;
            profile.score[CH_BEHIND] += 1.0f;
        }
    }

    // ---- move
    crouching = in.down[KEY_CTRL];
    bool running = in.down[KEY_SHIFT] && !crouching;
    float speed = crouching ? 1.35f : (running ? 4.1f : 2.5f);
    vec3 fwd = camForward(pYaw, 0);
    vec3 right = camRight(pYaw);
    vec3 wish{0, 0, 0};
    if (in.down[KEY_W]) wish += fwd;
    if (in.down[KEY_S]) wish -= fwd;
    if (in.down[KEY_D]) wish += right;
    if (in.down[KEY_A]) wish -= right;
    float wl = length(wish);
    if (wl > 0.01f) wish = wish / wl * speed;

    pVel.x = damp(pVel.x, wish.x, 14, dt);
    pVel.z = damp(pVel.z, wish.z, 14, dt);
    pVel.y -= 18.0f * dt;

    float height = crouching ? 1.25f : 1.72f;
    bool wasGrounded = grounded;
    pPos = world.resolveCollision(pPos, {0.26f, height * 0.5f, 0.26f}, pVel, dt, &grounded);
    if (grounded && pVel.y < 0) pVel.y = 0;
    if (!wasGrounded && grounded) playSnd("step_concrete", 0.4f);

    playerRoom = world.roomOf(pPos + vec3{0, 0.4f, 0});

    // ---- head bob + footsteps
    float hSpeed = length(vec3{pVel.x, 0, pVel.z});
    if (grounded && hSpeed > 0.4f) {
        bobPhase += dt * (running ? 11.5f : 7.4f);
        bobAmp = damp(bobAmp, 1.0f, 6, dt);
        double stepInterval = running ? 0.34 : (crouching ? 0.75 : 0.52);
        if (worldTime - lastFootstep > stepInterval) {
            lastFootstep = worldTime;
            std::string mat = "step_concrete";
            if (playerRoom >= 0) {
                const std::string& ft = content.rooms[playerRoom].floorTex;
                if (ft.find("carpet") != std::string::npos) mat = "step_carpet";
                else if (ft.find("parquet") != std::string::npos || ft.find("wood") != std::string::npos)
                    mat = "step_wood";
                else if (ft.find("lino") != std::string::npos || ft.find("tile") != std::string::npos)
                    mat = "step_lino";
            }
            playSnd(mat, running ? 0.85f : (crouching ? 0.25f : 0.55f));
        }
    } else {
        bobAmp = damp(bobAmp, 0.0f, 8, dt);
    }

    // ---- flashlight
    if (in.pressed[KEY_F]) {
        flashlight = !flashlight;
        playSnd("switch_click", 0.8f);
        profile.lightToggles++;
        profile.score[CH_LIGHT] += 0.4f;
    }
    // flashlight stutters near strong manifestations
    float nearManifest = 0;
    for (auto& o : observation.obs)
        if (o.active) {
            float d = distance(o.pos, pPos);
            if (d < 7) nearManifest = std::fmax(nearManifest, std::fmin(o.manifest / 8.0f, 1.0f) * (1.0f - d / 7.0f));
        }
    if (witness.visible) {
        float d = distance(witness.pos, pPos);
        if (d < 9) nearManifest = std::fmax(nearManifest, 0.35f + 0.1f * witness.stage);
    }
    float targetFlicker = nearManifest > 0.25f ? (0.7f + 0.3f * std::sin((float)worldTime * 37.0f) *
                                                             std::sin((float)worldTime * 13.0f))
                                               : 1.0f;
    flashFlicker = damp(flashFlicker, targetFlicker, 18, dt);

    // ---- camera device
    if (in.pressed[KEY_C]) {
        viewfinder = !viewfinder;
        playSnd(viewfinder ? "camera_up" : "camera_down", 0.7f);
    }
    if (viewfinder && (in.mousePressed[0] || in.pressed[KEY_SPACE])) takePhoto();

    if (in.pressed[KEY_E]) interact();

    flashWhite = std::fmax(0.0f, flashWhite - dt * 2.8f);
}

// ============================================================ observation ==
void Game::updateObservation(float dt) {
    vec3 eye = pPos + vec3{0, crouching ? 1.15f : 1.62f, 0};
    vec3 fwd = camForward(pYaw, pPitch);
    observation.update(dt, eye, fwd, world, 0.9f);

    // mirror gazing feeds the MIRROR channel
    for (const auto& m : content.mirrors) {
        vec3 to = m.pos - eye;
        float d = length(to);
        if (d > 4.5f || d < 0.2f) continue;
        if (dot(fwd, to / d) > 0.94f && world.lineOfSight(eye, m.pos)) {
            profile.mirrorGaze += dt;
            profile.score[CH_MIRROR] += dt * 0.55f;
        }
    }

    // gazing at the Witness teaches it
    if (witness.visible) {
        Observable wo;
        wo.pos = witness.pos + vec3{0, 1.35f, 0};
        float g = observation.gazeBoost(eye, fwd, wo);
        if (g > 0.05f && world.lineOfSight(eye, wo.pos)) {
            float mult = viewfinder ? 2.1f : 1.0f;
            witness.playerGazeOnMe += dt * g;
            witness.gainKnowledge(dt * g * mult * (1.0f + 0.25f * witness.stage));
            witness.lastSeen = worldTime;
            fearLevel = std::fmin(1.0f, fearLevel + dt * g * 0.3f);
            profile.score[CH_WITNESS] += dt * g * 0.5f;
        }
    }

    profile.decay(dt);
    fearLevel = std::fmax(0.0f, fearLevel - dt * 0.02f);
}

// ================================================================ witness ==
void Game::updateWitness(float dt) {
    WitnessSystem& w = witness;
    w.stageCap = std::max(w.stageCap, content.acts[act - 1].witnessCap);
    if (!w.visible) {
        w.playerGazeOnMe = 0;
        return;
    }

    vec3 eye = pPos + vec3{0, 1.62f, 0};
    float distToPlayer = distance(w.pos, pPos);
    bool observedNow = false;
    {
        vec3 to = (w.pos + vec3{0, 1.35f, 0}) - eye;
        float d = length(to);
        vec3 fwd = camForward(pYaw, pPitch);
        observedNow = d > 0.1f && dot(fwd, to / d) > 0.80f && world.lineOfSight(eye, w.pos + vec3{0, 1.35f, 0});
    }

    static double unobservedSince = 0;
    if (observedNow) unobservedSince = worldTime;

    if (w.mode == "distant") {
        // it does not move while watched; it accumulates
        if (distToPlayer < 5.0f || w.playerGazeOnMe > (3.5f + w.stage) || worldTime > w.vanishAt) {
            w.visible = false;
            w.mode = "hidden";
            playSnd("static_burst", 0.5f);
            if (w.stage >= 2 && distToPlayer < 14) playSnd("whisper", 0.6f);
            say("", 0.1f);   // clears any stale prompt
        }
    } else if (w.mode == "stalking") {
        // moves only when unobserved; being seen roots it
        if (!observedNow && worldTime - unobservedSince > 1.1) {
            vec3 dir = pPos - w.pos;
            dir.y = 0;
            float dl = length(dir);
            if (dl > 2.6f && w.stage >= 3) {
                vec3 step = dir / dl * 1.4f;
                vec3 cand = w.pos + step;
                if (world.roomOf(cand + vec3{0, 0.4f, 0}) >= 0) {
                    w.pos = cand;
                    playSnd("step_wood", 0.35f, false, w.pos, true);
                }
                unobservedSince = worldTime;
            } else if (w.stage < 3 || dl <= 2.6f) {
                w.visible = false;
                w.mode = "hidden";
                playSnd("static_burst", 0.45f);
            }
        }
        if (worldTime > w.vanishAt) {
            w.visible = false;
            w.mode = "hidden";
        }
        if (distToPlayer < 1.4f) {
            // contact: blackout relocation. No death, only doubt.
            flags.insert("touched");
            fearLevel = 1.0f;
            playSnd("witness_touch", 1.0f);
            fade = 1.0f;
            w.visible = false;
            w.mode = "hidden";
            // wake somewhere that makes no sense
            auto it = world.roomIdx.find(playerRoom >= 0 && content.rooms[playerRoom].zone > 0
                                             ? "hall_1"
                                             : "laundry");
            if (it != world.roomIdx.end()) {
                const RoomSpec& r = content.rooms[it->second];
                pPos = (r.mn + r.mx) * 0.5f;
                pPos.y = r.mn.y;
            }
            say(content.strings.count("wake") ? content.strings.at("wake") : "...how did I get here?",
                4.0f);
        }
    }
    w.stage = std::min(std::max(w.stage, w.stageForKnowledge()), w.stageCap == 0 ? w.stage : w.stageCap);
}

// =============================================================== director ==
void Game::updateDirector(float dt) {
    (void)dt;
    if (worldTime < director.nextPickAt) return;
    const ActSpec& a = content.acts[act - 1];
    float interval = a.directorInterval;
    // more attention -> the building answers faster
    interval *= clampf(1.25f - observation.attention * 0.02f, 0.45f, 1.25f);
    director.nextPickAt = worldTime + interval * (0.75 + 0.5 * director.rng.uniform());

    const EventSpec* ev = director.pick(
        content, act, profile, world, playerRoom, worldTime, [&](const EventSpec& e) {
            if (e.effect == "witness_distant" || e.effect == "witness_behind" ||
                e.effect == "witness_stalk")
                return !witness.visible && witness.stageCap > 0;
            if ((e.effect == "prop_move" || e.effect == "prop_vanish") && !e.target.empty()) {
                auto it = world.propIdx.find(e.target);
                if (it == world.propIdx.end()) return false;
                if (e.effect == "prop_vanish" && world.props[it->second].hidden) return false;
            }
            return true;
        });
    if (ev) applyEvent(*ev);
}

// ================================================================ interact ==
void Game::toggleDoor(int di) {
    DoorState& d = world.doors[di];
    const DoorSpec& spec = content.doors[di];
    if (d.locked) {
        if (!spec.keyId.empty() && items.count(spec.keyId)) {
            d.locked = false;
            playSnd("unlock", 0.9f);
            say(content.strings.count("unlocked") ? content.strings.at("unlocked") : "Unlocked.", 2.0f);
            runBeats("unlock", spec.id);
        } else {
            playSnd("door_locked", 0.8f);
            say(spec.label.empty() ? "Locked." : spec.label + " — locked.", 2.0f);
            profile.score[CH_DOOR] += 0.3f;
        }
        return;
    }
    if (worldTime - d.lastInteract < 20.0) {
        if (++d.interactBurst >= 3) {
            profile.doorRechecks++;
            profile.score[CH_DOOR] += 1.2f;
            d.interactBurst = 0;
        }
    } else {
        d.interactBurst = 0;
    }
    d.lastInteract = worldTime;
    d.open = !d.open;
    playSnd(d.open ? "door_open" : "door_close", 0.85f);
    if (spec.isThreshold && d.open) runBeats("threshold", spec.id);
}

void Game::interact() {
    vec3 eye = pPos + vec3{0, crouching ? 1.15f : 1.62f, 0};
    vec3 fwd = camForward(pYaw, pPitch);
    int prop = -1, door = -1;
    float t = world.raycast(eye, fwd, 2.3f, &prop, &door);
    (void)t;
    if (door >= 0) {
        const DoorSpec& spec = content.doors[door];
        if (spec.isThreshold) {
            if (world.doors[door].locked) {
                playSnd("door_locked", 0.8f);
                say(spec.label.empty() ? "Locked." : spec.label + " — locked.", 2.0f);
                return;
            }
            // The front door: in the finale it refuses, in the morning it releases.
            if (spec.id == "lobby_exit") {
                if (flags.count("morning")) {
                    // final CCTV shot: two of you leave
                    endingPhase = 4;
                    endingStartedAt = worldTime;
                    screen = Screen::Ending;
                    monitorCam = 0;   // cam_lobby
                    noiseBurst = 0.6f;
                    playSnd("crt_on", 0.9f);
                    return;
                }
                if (act >= 5) {
                    int tries = 0;
                    for (int i = 1; i <= 3; ++i)
                        if (flags.count("exit_try_" + std::to_string(i))) tries = i;
                    tries++;
                    flags.insert("exit_try_" + std::to_string(tries));
                    fade = 1.0f;
                    playSnd("stairs_transition", 0.9f);
                    if (tries == 1) {
                        say("The door opens back into the lobby.", 3.5f);
                        execAction("sndbehind:whisper");
                    } else if (tries == 2) {
                        noiseBurst = 0.8f;
                        playSnd("static_burst", 0.9f);
                        execAction("witness:behind");
                        say("It is very politely not letting you leave.", 3.5f);
                    } else if (tries >= 3 && !flags.count("exit_denied")) {
                        execAction("lightsroom:lobby:off");
                        execAction("flag:exit_denied");
                    }
                    return;   // emerge where you entered
                }
            }
            // stepping through darkness
            runBeats("threshold", spec.id);
            std::string target = spec.thresholdTarget;
            // impossible-space redirects
            if (flags.count("redirect_" + spec.id)) {
                for (auto& f : flags)
                    if (f.rfind("redirect_" + spec.id + ":", 0) == 0)
                        target = f.substr(("redirect_" + spec.id + ":").size());
            }
            auto it = world.doorIdx.find(target);
            if (it != world.doorIdx.end()) {
                const DoorSpec& dst = content.doors[it->second];
                fade = 1.0f;
                playSnd("stairs_transition", 0.9f);
                vec3 out = dst.pos;
                // step out of the doorway into roomA
                auto ra = world.roomIdx.find(dst.roomA);
                if (ra != world.roomIdx.end()) {
                    vec3 c = (content.rooms[ra->second].mn + content.rooms[ra->second].mx) * 0.5f;
                    vec3 dir = normalize(vec3{c.x - out.x, 0, c.z - out.z});
                    out = out + dir * 1.0f;
                    out.y = content.rooms[ra->second].mn.y;
                }
                pPos = out;
                playerRoom = world.roomOf(pPos + vec3{0, 0.4f, 0});
                runBeats("enter", dst.roomA);
            }
            return;
        }
        toggleDoor(door);
        return;
    }
    if (prop >= 0) {
        const PropSpec& ps = content.props[prop];
        if (ps.interact.empty()) return;
        std::string verb = ps.interact.substr(0, ps.interact.find(':'));
        std::string arg = ps.interact.find(':') == std::string::npos
                              ? ""
                              : ps.interact.substr(ps.interact.find(':') + 1);
        if (verb == "pickup") {
            items.insert(arg);
            world.props[prop].hidden = true;
            playSnd("pickup", 0.8f);
            std::string nice = content.strings.count("item_" + arg) ? content.strings.at("item_" + arg) : arg;
            say("Taken: " + nice, 2.2f);
            runBeats("pickup", arg);
        } else if (verb == "read") {
            readingDoc = arg;
            screen = Screen::Reading;
            eng.setMouseCaptured(false);
            playSnd("paper", 0.7f);
        } else if (verb == "switch") {
            playSnd("switch_click", 0.9f);
            profile.lightToggles++;
            profile.score[CH_LIGHT] += 0.5f;
            runBeats("switch", arg);
            execAction("script:switch:" + arg);
        } else if (verb == "monitor") {
            screen = Screen::Monitor;
            monitorCam = 0;
            eng.setMouseCaptured(false);
            playSnd("crt_on", 0.8f);
            runBeats("monitor", arg);
        } else if (verb == "phone") {
            if (flags.count("phone_ringing")) {
                flags.erase("phone_ringing");
                if (phoneVoice >= 0) audio.stop(phoneVoice);
                phoneVoice = -1;
                runBeats("phone", "answered");
            } else {
                say("Dead line.", 2.0f);
                playSnd("phone_dead", 0.7f);
            }
        } else if (verb == "action") {
            runBeats("action", arg);
        }
    }
}

// ============================================================ photography ==
void Game::takePhoto() {
    if (worldTime - lastPhotoAt < 1.6) return;
    lastPhotoAt = worldTime;
    flashWhite = 1.0f;
    playSnd("camera_shutter", 0.95f);
    flags.insert("pending_photo");   // resolved after this frame renders (with reveals)
    profile.photos++;
    profile.score[CH_PHOTO] += 0.8f;
}

// The pending photo is developed by render() right after endFrame().

// ================================================================= audio ===
void Game::playSnd(const std::string& name, float vol, bool loop, vec3 pos, bool spatial) {
    auto it = snd.find(name);
    if (it == snd.end()) return;
    audio.play(it->second, vol * settings.volume, 1.0f, loop, spatial, pos);
}

void Game::updateAudioBeds() {
    std::string want = "amb_lobby";
    if (playerRoom >= 0) {
        const RoomSpec& r = content.rooms[playerRoom];
        bool basement = false, anomaly = false;
        for (auto& tg : r.tags) {
            if (tg == "basement") basement = true;
            if (tg == "anomaly") anomaly = true;
        }
        if (anomaly) want = "amb_anomaly";
        else if (basement) want = "amb_basement";
        else if (r.zone >= 1) want = "amb_hall";
    }
    if (act >= 4 && want == "amb_hall") want = "amb_late";
    if (want != ambientCurrent) {
        if (ambientVoice >= 0) audio.stop(ambientVoice);
        auto it = snd.find(want);
        ambientVoice = it != snd.end()
                           ? audio.play(it->second, 0.55f * settings.volume, 1.0f, true, false)
                           : -1;
        ambientCurrent = want;
    }
    vec3 eye = pPos + vec3{0, 1.6f, 0};
    audio.setListener(eye, camForward(pYaw, pPitch));
}

// ==================================================================== say ===
void Game::say(const std::string& text, float seconds, vec4 color) {
    if (text.empty()) return;
    // word-wrap to the subtitle column so long lines never overflow
    float maxW = eng.width() * 0.84f;
    std::string line, word;
    std::vector<std::string> lines;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string cand = line.empty() ? word : line + " " + word;
        if (font && eng.textWidth(font, cand) > maxW && !line.empty()) {
            lines.push_back(line);
            line = word;
        } else {
            line = cand;
        }
        word.clear();
    };
    for (char c : text) {
        if (c == ' ') flushWord();
        else if (c == '\n') { flushWord(); lines.push_back(line); line.clear(); }
        else word += c;
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    for (auto& l : lines) {
        subs.push_back({l, worldTime + seconds, color});
    }
    while (subs.size() > 4) subs.pop_front();
}

std::string Game::clockString() const {
    double mins = clockBase + (worldTime - actStartedAt) / 60.0 * 8.0;   // 8x time
    int h = ((int)(mins / 60)) % 24;
    int m = (int)mins % 60;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return buf;
}

// ================================================================== save ====
void Settings::load(const std::string& path) {
    std::string txt;
    if (!readFileText(path, txt)) return;
    auto j = json::parse(txt, nullptr, false);
    if (j.is_discarded()) return;
    volume = j.value("volume", volume);
    sensitivity = j.value("sensitivity", sensitivity);
    grain = j.value("grain", grain);
    subtitles = j.value("subtitles", subtitles);
    fullscreen = j.value("fullscreen", fullscreen);
    width = j.value("width", width);
    height = j.value("height", height);
}

void Settings::save(const std::string& path) const {
    json j{{"volume", volume},   {"sensitivity", sensitivity}, {"grain", grain},
           {"subtitles", subtitles}, {"fullscreen", fullscreen},   {"width", width},
           {"height", height}};
    writeFileText(path, j.dump(2));
}

bool Game::saveGame() {
    json j;
    j["version"] = 1;
    j["act"] = act;
    j["worldTime"] = worldTime;
    j["actStartedAt"] = actStartedAt;
    j["clockBase"] = clockBase;
    j["pos"] = {pPos.x, pPos.y, pPos.z};
    j["yaw"] = pYaw;
    j["pitch"] = pPitch;
    j["items"] = std::vector<std::string>(items.begin(), items.end());
    j["flags"] = std::vector<std::string>(flags.begin(), flags.end());
    j["docs"] = std::vector<std::string>(docsRead.begin(), docsRead.end());
    j["attention"] = observation.attention;
    j["witness"] = {{"stage", witness.stage},
                    {"cap", witness.stageCap},
                    {"knowledge", witness.knowledge},
                    {"photo", witness.photoKnowledge}};
    j["profile"] = {{"behind", profile.behindChecks},
                    {"door", profile.doorRechecks},
                    {"light", profile.lightToggles},
                    {"photos", profile.photos},
                    {"mirror", profile.mirrorGaze},
                    {"cctv", profile.cctvTime}};
    json scores = json::array();
    for (float s : profile.score) scores.push_back(s);
    j["scores"] = scores;
    json tj = json::array();
    for (auto& t : tasks) tj.push_back({{"id", t.id}, {"text", t.text}, {"done", t.done}});
    j["tasks"] = tj;
    json dj = json::array();
    for (auto& d : world.doors) dj.push_back({{"open", d.open}, {"locked", d.locked}});
    j["doors"] = dj;
    json pj = json::array();
    for (auto& p : world.props)
        pj.push_back({{"x", p.pos.x}, {"y", p.pos.y}, {"z", p.pos.z}, {"yaw", p.yaw},
                      {"hidden", p.hidden}, {"tex", p.tex}, {"moved", p.moved}});
    j["props"] = pj;
    json lj = json::array();
    for (auto& l : world.lights) lj.push_back({{"on", l.on}, {"flicker", l.flicker}});
    j["lights"] = lj;
    json phj = json::array();
    for (auto& p : photos)
        phj.push_back({{"file", p.file}, {"caption", p.caption}, {"subjects", p.subjects}});
    j["photos"] = phj;
    return writeFileText(userDir + "/save.json", j.dump());
}

bool Game::loadGame() {
    std::string txt;
    if (!readFileText(userDir + "/save.json", txt)) return false;
    auto j = json::parse(txt, nullptr, false);
    if (j.is_discarded()) return false;
    act = j.value("act", 1);
    worldTime = j.value("worldTime", 0.0);
    actStartedAt = j.value("actStartedAt", 0.0);
    clockBase = j.value("clockBase", 23 * 60.0);
    auto pos = j["pos"];
    pPos = {pos[0], pos[1], pos[2]};
    pYaw = j.value("yaw", 0.0f);
    pPitch = j.value("pitch", 0.0f);
    items.clear();
    for (auto& s : j["items"]) items.insert(s.get<std::string>());
    flags.clear();
    for (auto& s : j["flags"]) flags.insert(s.get<std::string>());
    flags.erase("pending_photo");
    docsRead.clear();
    for (auto& s : j["docs"]) docsRead.insert(s.get<std::string>());
    observation.attention = j.value("attention", 0.0f);
    witness.stage = j["witness"].value("stage", 0);
    witness.stageCap = j["witness"].value("cap", 0);
    witness.knowledge = j["witness"].value("knowledge", 0.0f);
    witness.photoKnowledge = j["witness"].value("photo", 0.0f);
    witness.visible = false;
    witness.mode = "hidden";
    profile = {};
    profile.behindChecks = j["profile"].value("behind", 0);
    profile.doorRechecks = j["profile"].value("door", 0);
    profile.lightToggles = j["profile"].value("light", 0);
    profile.photos = j["profile"].value("photos", 0);
    profile.mirrorGaze = j["profile"].value("mirror", 0.0f);
    profile.cctvTime = j["profile"].value("cctv", 0.0f);
    if (j.contains("scores"))
        for (size_t i = 0; i < std::min((size_t)CH_COUNT, j["scores"].size()); ++i)
            profile.score[i] = j["scores"][i];
    tasks.clear();
    for (auto& t : j["tasks"]) tasks.push_back({t["id"], t["text"], t["done"]});
    for (size_t i = 0; i < world.doors.size() && i < j["doors"].size(); ++i) {
        world.doors[i].open = j["doors"][i]["open"];
        world.doors[i].locked = j["doors"][i]["locked"];
        world.doors[i].openT = world.doors[i].open ? 1.f : 0.f;
    }
    for (size_t i = 0; i < world.props.size() && i < j["props"].size(); ++i) {
        auto& p = j["props"][i];
        world.props[i].pos = {p["x"], p["y"], p["z"]};
        world.props[i].yaw = p["yaw"];
        world.props[i].hidden = p["hidden"];
        world.props[i].tex = p["tex"];
        world.props[i].moved = p.value("moved", false);
    }
    for (size_t i = 0; i < world.lights.size() && i < j["lights"].size(); ++i) {
        world.lights[i].on = j["lights"][i]["on"];
        world.lights[i].flicker = j["lights"][i]["flicker"];
    }
    photos.clear();
    if (j.contains("photos"))
        for (auto& p : j["photos"]) {
            PhotoRecord r;
            r.file = p["file"];
            r.caption = p["caption"];
            for (auto& s : p["subjects"]) r.subjects.push_back(s.get<std::string>());
            if (fileExists(r.file)) r.tex = eng.loadTexture(r.file, true, false);
            photos.push_back(r);
        }
    photoCount = (int)photos.size();
    director.nextPickAt = worldTime + 20;
    return true;
}
