// THE OBSERVER - script layer: beat interpreter, anomaly effects, acts,
// and the ending. The building's side of the conversation.
#include "game.h"

#include <sstream>

void gameRegisterEffects() {}

// ---- tiny tokenizer: verb:arg:arg... (args may contain spaces)
static std::vector<std::string> splitTokens(const std::string& s, char sep, int maxParts = -1) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        if (maxParts > 0 && (int)out.size() == maxParts - 1) {
            out.push_back(s.substr(start));
            break;
        }
        size_t p = s.find(sep, start);
        if (p == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return out;
}

void World::resetState() {
    const GameContent& c = *content;
    for (size_t i = 0; i < c.doors.size(); ++i) {
        doors[i].open = c.doors[i].open;
        doors[i].locked = c.doors[i].locked;
        doors[i].openT = c.doors[i].open ? 1.f : 0.f;
        doors[i].interactBurst = 0;
    }
    for (size_t i = 0; i < c.props.size(); ++i) {
        props[i].pos = c.props[i].pos;
        props[i].yaw = c.props[i].yaw;
        props[i].hidden = c.props[i].hidden;
        props[i].tint = c.props[i].tint;
        props[i].tex = c.props[i].tex;
        props[i].moved = false;
    }
    for (size_t i = 0; i < c.lights.size(); ++i) {
        lights[i].on = c.lights[i].on;
        lights[i].flicker = c.lights[i].flicker;
    }
}

// ================================================================ actions ===
void Game::execAction(const std::string& action) {
    if (action.empty()) return;
    auto tok = splitTokens(action, ':');
    const std::string& verb = tok[0];
    auto arg = [&](size_t i) { return i < tok.size() ? tok[i] : std::string(); };

    if (verb == "task") {
        if (arg(1) == "add") {
            std::string text = action.substr(action.find(':', action.find(':', 5) + 1) + 1);
            // format: task:add:<id>:<text with colons allowed>
            auto t3 = splitTokens(action, ':', 4);
            for (auto& t : tasks)
                if (t.id == t3[2]) return;
            tasks.push_back({t3[2], t3.size() > 3 ? t3[3] : text, false});
            playSnd("clipboard", 0.6f);
            say(content.strings.count("new_task") ? content.strings.at("new_task") : "Work order updated.",
                2.4f, {0.85f, 0.9f, 1.0f, 1});
        } else if (arg(1) == "done") {
            for (auto& t : tasks)
                if (t.id == arg(2) && !t.done) {
                    t.done = true;
                    playSnd("clipboard", 0.6f);
                    runBeats("task_done", t.id);
                }
        }
    } else if (verb == "sub") {
        say(action.substr(4), 3.6f);
    } else if (verb == "sublong") {
        say(action.substr(8), 6.5f);
    } else if (verb == "voice") {
        auto t3 = splitTokens(action, ':', 3);
        playSnd(t3[1], 1.0f);
        if (t3.size() > 2 && settings.subtitles) say(t3[2], 4.5f, {1.0f, 0.95f, 0.8f, 1});
    } else if (verb == "snd") {
        playSnd(arg(1), 0.9f);
    } else if (verb == "sndat") {
        auto it = world.roomIdx.find(arg(2));
        if (it != world.roomIdx.end()) {
            const RoomSpec& r = content.rooms[it->second];
            vec3 p = (r.mn + r.mx) * 0.5f;
            p.y = r.mn.y + 1.5f;
            playSnd(arg(1), 1.0f, false, p, true);
        }
    } else if (verb == "sndbehind") {
        vec3 p = pPos - camForward(pYaw, 0) * 2.4f + vec3{0, 1.4f, 0};
        playSnd(arg(1), 1.0f, false, p, true);
        profile.score[CH_BEHIND] += 0.2f;
    } else if (verb == "flag") {
        flags.insert(arg(1));
        runBeats("flag", arg(1));
    } else if (verb == "unflag") {
        flags.erase(arg(1));
    } else if (verb == "give") {
        items.insert(arg(1));
    } else if (verb == "door") {
        auto it = world.doorIdx.find(arg(1));
        if (it != world.doorIdx.end()) {
            DoorState& d = world.doors[it->second];
            const std::string& op = arg(2);
            if (op == "open") d.open = true;
            else if (op == "close") d.open = false;
            else if (op == "lock") { d.locked = true; d.open = false; }
            else if (op == "unlock") d.locked = false;
            else if (op == "slam") {
                d.open = false;
                d.openT = 0;
                playSnd("door_slam", 1.0f, false, content.doors[it->second].pos, true);
                observation.spawn("slam_" + arg(1), content.doors[it->second].pos, playerRoom);
                fearLevel = std::fmin(1.0f, fearLevel + 0.25f);
            }
        }
    } else if (verb == "light") {
        auto it = world.lightIdx.find(arg(1));
        if (it != world.lightIdx.end()) {
            if (arg(2) == "on") world.lights[it->second].on = true;
            else if (arg(2) == "off") world.lights[it->second].on = false;
            else if (arg(2) == "flicker")
                world.lights[it->second].flicker = tok.size() > 3 ? std::stof(tok[3]) : 0.6f;
        }
    } else if (verb == "lightsroom") {
        for (size_t i = 0; i < content.lights.size(); ++i)
            if (content.lights[i].room == arg(1)) world.lights[i].on = (arg(2) == "on");
    } else if (verb == "witnesscap") {
        witness.stageCap = std::stoi(arg(1));
        if (witness.stage > witness.stageCap) witness.stage = witness.stageCap;
    } else if (verb == "witness") {
        const std::string& mode = arg(1);
        if (mode == "hide") {
            witness.visible = false;
            witness.mode = "hidden";
        } else {
            auto it = world.roomIdx.find(arg(2));
            if (it != world.roomIdx.end()) {
                const RoomSpec& r = content.rooms[it->second];
                float x = tok.size() > 3 ? std::stof(tok[3]) : (r.mn.x + r.mx.x) * 0.5f;
                float z = tok.size() > 4 ? std::stof(tok[4]) : (r.mn.z + r.mx.z) * 0.5f;
                witness.pos = {x, r.mn.y, z};
                witness.room = it->second;
                witness.visible = true;
                witness.mode = mode == "stalk" ? "stalking" : "distant";
                witness.vanishAt = worldTime + (mode == "stalk" ? 45 : 18 + 4 * witness.stage);
                witness.lastAppear = worldTime;
                witness.playerGazeOnMe = 0;
                if (witness.stage == 0 && witness.stageCap > 0) witness.stage = 1;
            } else if (mode == "behind") {
                vec3 p = pPos - camForward(pYaw, 0) * 3.2f;
                int rm = world.roomOf(p + vec3{0, 0.4f, 0});
                if (rm >= 0) {
                    witness.pos = {p.x, content.rooms[rm].mn.y, p.z};
                    witness.room = rm;
                    witness.visible = true;
                    witness.mode = "stalking";
                    witness.vanishAt = worldTime + 20;
                    witness.playerGazeOnMe = 0;
                    if (witness.stage == 0 && witness.stageCap > 0) witness.stage = 1;
                }
            }
        }
    } else if (verb == "act") {
        enterAct(std::stoi(arg(1)));
    } else if (verb == "ending") {
        startEnding(arg(1));
    } else if (verb == "teleport") {
        auto it = world.roomIdx.find(arg(1));
        if (it != world.roomIdx.end()) {
            const RoomSpec& r = content.rooms[it->second];
            pPos = (r.mn + r.mx) * 0.5f;
            pPos.y = r.mn.y;
            fade = 1.0f;
            playerRoom = it->second;
            runBeats("enter", arg(1));
        }
    } else if (verb == "ambient") {
        ambientCurrent = "";   // force refresh
        if (ambientVoice >= 0) audio.stop(ambientVoice);
        auto it = snd.find(arg(1));
        ambientVoice = it != snd.end() ? audio.play(it->second, 0.55f * settings.volume, 1, true) : -1;
        ambientCurrent = arg(1);
    } else if (verb == "fear") {
        fearLevel = clampf(std::stof(arg(1)), 0, 1);
    } else if (verb == "prop") {
        auto it = world.propIdx.find(arg(1));
        if (it != world.propIdx.end()) {
            PropState& p = world.props[it->second];
            const std::string& op = arg(2);
            if (op == "hide") p.hidden = true;
            else if (op == "show") p.hidden = false;
            else if (op == "tex") p.tex = arg(3);
            else if (op == "move" && tok.size() > 4) {
                p.pos.x += std::stof(tok[3]);
                p.pos.z += std::stof(tok[4]);
                if (tok.size() > 5) p.yaw += radians(std::stof(tok[5]));
                p.moved = true;
            }
        }
    } else if (verb == "redirect") {
        flags.insert("redirect_" + arg(1));
        flags.insert("redirect_" + arg(1) + ":" + arg(2));
    } else if (verb == "unredirect") {
        for (auto f = flags.begin(); f != flags.end();)
            f = (f->rfind("redirect_" + arg(1), 0) == 0) ? flags.erase(f) : std::next(f);
    } else if (verb == "event") {
        for (const auto& e : content.events)
            if (e.id == arg(1)) {
                applyEvent(e);
                break;
            }
    } else if (verb == "obs") {
        auto it = world.roomIdx.find(arg(2));
        if (it != world.roomIdx.end() && tok.size() > 5)
            observation.spawn(arg(1), {std::stof(tok[3]), std::stof(tok[4]), std::stof(tok[5])},
                              it->second);
    } else if (verb == "title") {
        titleCard = action.substr(6);
        titleCardUntil = worldTime + 4.5;
    } else if (verb == "cue") {
        auto t3 = splitTokens(action, ':', 3);
        if (t3.size() > 2) cues.push_back({worldTime + std::stof(t3[1]), t3[2]});
    } else if (verb == "save") {
        saveGame();
    } else if (verb == "script") {
        // switches and compound behaviors
        if (arg(1) == "phone" && arg(2) == "timeout") {
            if (flags.count("phone_ringing")) {
                flags.erase("phone_ringing");
                if (phoneVoice >= 0) audio.stop(phoneVoice);
                phoneVoice = -1;
                say("The ringing stops mid-ring.", 2.5f);
            }
        }
        if (arg(1) == "switch") {
            const std::string& sw = arg(2);
            if (sw == "breaker") {
                flags.erase("breaker_tripped");
                for (size_t i = 0; i < content.lights.size(); ++i)
                    world.lights[i].on = content.lights[i].on;
                playSnd("breaker_thunk", 1.0f);
                say("Power restored.", 2.5f);
                runBeats("switch", "breaker_done");
            } else if (sw.rfind("elev_", 0) == 0) {
                // elevator floor buttons: elev_0 .. elev_4, elev_b1
                std::string floor = sw.substr(5);
                std::string target = "elevator_" + floor;
                auto it = world.roomIdx.find(target);
                if (it != world.roomIdx.end()) {
                    playSnd("elevator_move", 0.9f);
                    fade = 1.0f;
                    const RoomSpec& r = content.rooms[it->second];
                    pPos = (r.mn + r.mx) * 0.5f;
                    pPos.y = r.mn.y;
                    cues.push_back({worldTime + 1.6, "snd:elevator_ding"});
                    runBeats("enter", target);
                    runBeats("elevator", floor);
                }
            } else if (sw.rfind("lamp_", 0) == 0) {
                std::string room = sw.substr(5);
                bool any = false;
                for (size_t i = 0; i < content.lights.size(); ++i)
                    if (content.lights[i].room == room) any = any || !world.lights[i].on;
                for (size_t i = 0; i < content.lights.size(); ++i)
                    if (content.lights[i].room == room) world.lights[i].on = any;
            }
        }
    }
}

void Game::runBeats(const std::string& trigger, const std::string& arg) {
    if (act < 1 || act > (int)content.acts.size()) return;
    ActSpec& a = const_cast<ActSpec&>(content.acts[act - 1]);
    double actMinutes = (worldTime - actStartedAt) / 60.0;
    for (auto& b : a.beats) {
        if (b.fired) continue;
        bool hit = false;
        if (trigger == "time" && b.trigger == "time") hit = actMinutes >= b.atTime;
        else if (b.trigger == trigger && (b.arg.empty() || b.arg == arg)) hit = true;
        if (b.trigger == "flag" && trigger == "flag" && b.arg == arg) hit = true;
        if (!hit) continue;
        b.fired = true;
        for (auto& action : b.actions) execAction(action);
    }
}

// ================================================================= events ===
void Game::applyEvent(const EventSpec& ev) {
    Channel ch = channelFromName(ev.channel);
    auto roomCenter = [&](const std::string& id) -> vec3 {
        auto it = world.roomIdx.find(id);
        if (it == world.roomIdx.end()) return pPos;
        const RoomSpec& r = content.rooms[it->second];
        vec3 p = (r.mn + r.mx) * 0.5f;
        p.y = r.mn.y;
        return p;
    };

    if (ev.effect == "script") {
        for (auto& a : splitTokens(ev.param, ';')) execAction(a);
    } else if (ev.effect == "prop_move") {
        auto it = world.propIdx.find(ev.target);
        if (it == world.propIdx.end()) return;
        auto d = splitTokens(ev.param, ':');
        PropState& p = world.props[it->second];
        if (d.size() >= 2) {
            p.pos.x += std::stof(d[0]);
            p.pos.z += std::stof(d[1]);
        }
        if (d.size() >= 3) p.yaw += radians(std::stof(d[2]));
        p.moved = true;
        observation.spawn("moved_" + ev.target, p.pos + vec3{0, 0.6f, 0},
                          world.roomIdx.count(ev.room) ? world.roomIdx[ev.room] : -1);
    } else if (ev.effect == "prop_vanish") {
        auto it = world.propIdx.find(ev.target);
        if (it == world.propIdx.end()) return;
        vec3 old = world.props[it->second].pos;
        world.props[it->second].hidden = true;
        observation.spawn("gone_" + ev.target, old + vec3{0, 0.5f, 0},
                          world.roomIdx.count(ev.room) ? world.roomIdx[ev.room] : -1);
    } else if (ev.effect == "prop_appear") {
        auto it = world.propIdx.find(ev.target);
        if (it == world.propIdx.end()) return;
        world.props[it->second].hidden = false;
        observation.spawn("new_" + ev.target, world.props[it->second].pos + vec3{0, 0.5f, 0},
                          world.roomIdx.count(ev.room) ? world.roomIdx[ev.room] : -1);
    } else if (ev.effect == "prop_swap_tex") {
        auto it = world.propIdx.find(ev.target);
        if (it != world.propIdx.end()) world.props[it->second].tex = ev.param;
    } else if (ev.effect == "door_slam") {
        execAction("door:" + ev.target + ":slam");
        profile.score[CH_DOOR] += 0.4f;
    } else if (ev.effect == "door_open") {
        auto it = world.doorIdx.find(ev.target);
        if (it != world.doorIdx.end() && !world.doors[it->second].locked) {
            world.doors[it->second].open = true;
            playSnd("door_creak", 0.8f, false, content.doors[it->second].pos + vec3{0, 1, 0}, true);
            observation.spawn("opened_" + ev.target, content.doors[it->second].pos + vec3{0, 1, 0}, -1);
        }
    } else if (ev.effect == "light_flicker") {
        auto it = world.lightIdx.find(ev.target);
        if (it != world.lightIdx.end()) {
            world.lights[it->second].flicker = std::fmax(0.5f, ev.value);
            cues.push_back({worldTime + std::fmax(4.0f, ev.value * 10),
                            "light:" + ev.target + ":flicker:" +
                                std::to_string(content.lights[it->second].flicker)});
        }
    } else if (ev.effect == "lights_out") {
        for (size_t i = 0; i < content.lights.size(); ++i)
            if (content.lights[i].room == ev.room) world.lights[i].on = false;
        playSnd("power_down", 0.9f);
        if (ev.param == "breaker") {
            flags.insert("breaker_tripped");
            execAction("task:add:breaker:Reset the breaker in the basement");
        }
    } else if (ev.effect == "sound") {
        vec3 p = ch == CH_BEHIND ? pPos - camForward(pYaw, 0) * 2.6f + vec3{0, 1.3f, 0}
                                 : roomCenter(ev.room) + vec3{0, 1.4f, 0};
        playSnd(ev.param.empty() ? "knock_triple" : ev.param, 1.0f, false, p, true);
        if (ev.value > 0) {
            Observable* o = observation.spawn("snd_" + ev.id, p, -1);
            o->spawnedAt = worldTime;
        }
    } else if (ev.effect == "whisper") {
        execAction("sndbehind:whisper");
        fearLevel = std::fmin(1.0f, fearLevel + 0.12f);
    } else if (ev.effect == "cctv_figure") {
        flags.insert("cctv_witness_" + ev.target);
        cues.push_back({worldTime + (ev.value > 0 ? ev.value : 25), "unflag:cctv_witness_" + ev.target});
    } else if (ev.effect == "mirror_witness") {
        flags.insert("mirror_witness");
        cues.push_back({worldTime + (ev.value > 0 ? ev.value : 12), "unflag:mirror_witness"});
        profile.score[CH_MIRROR] += 0.5f;
    } else if (ev.effect == "witness_distant") {
        auto d = splitTokens(ev.param, ':');
        std::string cmd = "witness:distant:" + ev.room;
        if (d.size() >= 2) cmd += ":" + d[0] + ":" + d[1];
        execAction(cmd);
    } else if (ev.effect == "witness_behind") {
        execAction("witness:behind");
    } else if (ev.effect == "witness_stalk") {
        auto d = splitTokens(ev.param, ':');
        std::string cmd = "witness:stalk:" + (ev.room.empty() ? std::string("hall_1") : ev.room);
        if (d.size() >= 2) cmd += ":" + d[0] + ":" + d[1];
        execAction(cmd);
    } else if (ev.effect == "room_swap") {
        for (size_t i = 0; i < content.props.size(); ++i) {
            const std::string& id = content.props[i].id;
            if (id.rfind(ev.param + "@A", 0) == 0) world.props[i].hidden = true;
            if (id.rfind(ev.param + "@B", 0) == 0) world.props[i].hidden = false;
        }
        observation.spawn("swap_" + ev.param, roomCenter(ev.room) + vec3{0, 1.2f, 0},
                          world.roomIdx.count(ev.room) ? world.roomIdx[ev.room] : -1);
    } else if (ev.effect == "phone_ring") {
        if (!flags.count("phone_ringing")) {
            flags.insert("phone_ringing");
            auto it = snd.find("phone_ring");
            if (it != snd.end()) {
                auto pit = world.propIdx.find("lobby_phone");
                vec3 p = pit != world.propIdx.end() ? world.props[pit->second].pos + vec3{0, 1.1f, 0}
                                                    : roomCenter("lobby");
                phoneVoice = audio.play(it->second, 0.9f * settings.volume, 1, true, true, p);
            }
            cues.push_back({worldTime + 30, "script:phone:timeout"});
        }
    } else if (ev.effect == "writing_reveal") {
        flags.insert("photoreveal_" + ev.target);
    } else if (ev.effect == "corridor_loop") {
        // both stair thresholds on this floor lead back to themselves for a while
        execAction("redirect:" + ev.target + ":" + ev.target);
        cues.push_back({worldTime + (ev.value > 0 ? ev.value : 120), "unredirect:" + ev.target});
        profile.score[CH_SPACE] += 0.6f;
    }
}

// =================================================================== acts ===
void Game::enterAct(int a) {
    act = clampf((float)a, 1, (float)content.acts.size());
    actStartedAt = worldTime;
    const ActSpec& as = content.acts[act - 1];
    // parse clock "HH:MM"
    int hh = 23, mm = 0;
    std::sscanf(as.clock.c_str(), "%d:%d", &hh, &mm);
    clockBase = hh * 60 + mm;
    witness.stageCap = std::max(witness.stageCap, as.witnessCap);
    if (!as.title.empty()) {
        titleCard = as.title;
        titleCardUntil = worldTime + 5.0;
    }
    director.nextPickAt = worldTime + as.directorInterval * 0.6;
    runBeats("actstart", "");
    saveGame();
}

void Game::newGame() {
    world.resetState();
    items.clear();
    flags.clear();
    docsRead.clear();
    tasks.clear();
    photos.clear();
    photoCount = 0;
    observation = {};
    profile = {};
    witness = {};
    director = {};
    director.rng.seed(0x0B5E55ED);   // OBSESSED
    cues.clear();
    subs.clear();
    worldTime = 0;
    fearLevel = 0;
    fade = 1.0f;
    endingPhase = 0;
    // reset beat fired flags
    for (auto& a : const_cast<std::vector<ActSpec>&>(content.acts))
        for (auto& b : a.beats) b.fired = false;
    // player start
    auto it = world.roomIdx.find(content.playerStartRoom);
    if (it != world.roomIdx.end()) {
        pPos = content.playerStart;
        pPos.y = content.rooms[it->second].mn.y;
        playerRoom = it->second;
    } else {
        pPos = {0, 0, 0};
    }
    pYaw = content.playerStartYaw;
    pPitch = 0;
    pVel = {};
    flashlight = false;
    viewfinder = false;
    enterAct(1);
    screen = Screen::Playing;
    eng.setMouseCaptured(true);
}

// ================================================================= ending ===
void Game::startEnding(const std::string& flavorIn) {
    std::string flavor = flavorIn;
    if (flavor.empty() || flavor == "auto") {
        int witnessPhotos = 0;
        for (auto& p : photos)
            for (auto& s : p.subjects)
                if (s == "witness") witnessPhotos++;
        if (flags.count("touched")) flavor = "replaced";
        else if (witnessPhotos >= 5) flavor = "documented";
        else if (profile.mirrorGaze > 75) flavor = "reflected";
        else flavor = "witnessed";
    }
    endingFlavor = flavor;
    endingPhase = 0;
    endingStartedAt = worldTime;
    screen = Screen::Ending;
    fade = 0;
    witness.stageCap = 4;
    witness.stage = 4;
    saveGame();
}

void Game::tickEnding(float dt) {
    double t = worldTime - endingStartedAt;
    switch (endingPhase) {
        case 0: {   // descend into black
            fadeTarget = 1.0f;
            if (t > 2.5) {
                execAction("teleport:vault");
                execAction("ambient:amb_anomaly");
                // it is already there, facing you
                witness.pos = pPos + camForward(pYaw, 0) * 7.0f;
                witness.pos.y = pPos.y;
                witness.room = playerRoom;
                witness.visible = true;
                witness.mode = "confront";
                fadeTarget = 0.0f;
                endingPhase = 1;
                endingStartedAt = worldTime;
            }
            break;
        }
        case 1: {   // it approaches. Watching no longer stops it.
            vec3 d = pPos - witness.pos;
            d.y = 0;
            float dl = length(d);
            if (dl > 2.1f) {
                witness.pos += d / dl * dt * 0.55f;
            } else if (t > 3.0) {
                endingPhase = 2;
                endingStartedAt = worldTime;
                execAction("voice:witness_final:\"I only know what you showed me.\"");
                fearLevel = 1.0f;
            }
            break;
        }
        case 2: {   // the line lands; the room refuses to stay real
            if (t > 5.5) {
                playSnd("static_burst", 1.0f);
                fade = 1.0f;
                fadeTarget = 1.0f;
                endingPhase = 3;
                endingStartedAt = worldTime;
                witness.visible = false;
                // morning
                execAction("teleport:lobby");
                execAction("ambient:amb_morning");
                for (size_t i = 0; i < world.lights.size(); ++i) world.lights[i].on = false;
                for (size_t i = 0; i < content.props.size(); ++i) {
                    if (content.props[i].id.rfind("win@A", 0) == 0) world.props[i].hidden = true;
                    if (content.props[i].id.rfind("win@B", 0) == 0) world.props[i].hidden = false;
                }
                flags.insert("morning");
                execAction("door:lobby_exit:unlock");
                tasks.clear();
                execAction("task:add:leave:Leave");
            }
            break;
        }
        case 3: {   // morning lobby, walk out
            if (t > 1.5) fadeTarget = 0.0f;
            screen = Screen::Playing;   // control returns; exit beat fires phase 4
            break;
        }
        case 4: {   // final CCTV shot: two of you leave
            fadeTarget = 0.0f;
            if (t > 13.0) {
                endingPhase = 5;
                endingStartedAt = worldTime;
                fade = 1.0f;
            }
            break;
        }
        case 5: {   // title + post-line + credits
            screen = Screen::Credits;
            break;
        }
    }
}
