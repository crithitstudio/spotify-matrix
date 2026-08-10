// THE OBSERVER - presentation: world view composition, HUD, menus, papers.
#include "game.h"

#include <stb/stb_image_write.h>

#include <algorithm>
#include <filesystem>

bool observerWritePng(const std::string& path, const uint8_t* px, int w, int h) {
    return stbi_write_png(path.c_str(), w, h, 4, px, w * 4) != 0;
}

static vec4 INK{0.13f, 0.11f, 0.10f, 1};
static vec4 PAPER{0.86f, 0.82f, 0.72f, 1};

// ============================================================ world views ==
void Game::renderWorldViews() {
    float aspect = (float)eng.width() / (float)eng.height();
    bool pendingPhoto = flags.count("pending_photo") > 0;

    // camera
    float eyeH = crouching ? 1.18f : 1.62f;
    float bob = std::sin(bobPhase) * 0.035f * bobAmp;
    vec3 eye = pPos + vec3{0, eyeH + bob, 0};
    float fov = viewfinder ? radians(44.0f) : radians(71.0f);

    // ---- base draw list
    std::vector<DrawItem> items;
    std::vector<Light> lights;
    world.buildDrawList(content, playerRoom, items, lights, false, (float)worldTime);

    // photo-reveal props become momentarily real for the capture frame
    std::vector<int> revealed;
    if (pendingPhoto) {
        for (size_t i = 0; i < content.props.size(); ++i) {
            if (world.props[i].hidden && flags.count("photoreveal_" + content.props[i].id)) {
                world.props[i].hidden = false;
                revealed.push_back((int)i);
            }
        }
        if (!revealed.empty()) {
            items.clear();
            lights.clear();
            world.buildDrawList(content, playerRoom, items, lights, false, (float)worldTime);
        }
    }

    // witness billboard
    auto witnessSprite = [&](int stage) {
        return world.texOf(stage <= 1 ? "witness_s1" : stage == 2 ? "witness_s2"
                                       : stage == 3              ? "witness_s3"
                                                                 : "witness_s4");
    };
    if (witness.visible) {
        DrawItem w;
        w.mesh = world.billboardMesh();
        w.tex = witnessSprite(witness.stage);
        vec3 d = pPos - witness.pos;
        float yaw = std::atan2(d.x, d.z);
        float h = 2.05f + 0.06f * witness.stage;
        w.model = mat4::translate(witness.pos) * mat4::rotateY(yaw) * mat4::scale({h * 0.42f, h, 1});
        float solidity = 0.45f + 0.13f * witness.stage +
                         std::fmin(witness.knowledge / 60.0f, 0.25f);
        w.tint = {1, 1, 1, clampf(solidity, 0, 0.98f)};
        w.flags = DRAW_ALPHA | DRAW_UNLIT;
        w.emissive = 0.02f;
        items.push_back(w);
    }

    // flashlight
    if (flashlight) {
        Light spot;
        spot.type = LIGHT_SPOT;
        spot.pos = eye + camRight(pYaw) * 0.12f;
        spot.dir = camForward(pYaw, pPitch);
        spot.radius = 17;
        spot.intensity = 2.7f * flashFlicker;
        spot.color = {1.0f, 0.97f, 0.88f};
        spot.innerCos = 0.93f;
        spot.outerCos = 0.80f;
        lights.push_back(spot);
    }
    if (lights.size() > 24) {
        std::sort(lights.begin(), lights.end(), [&](const Light& a, const Light& b) {
            return length2(a.pos - eye) < length2(b.pos - eye);
        });
        lights.resize(24);
    }

    const ActSpec& a = content.acts[act - 1];
    vec3 ambient = flags.count("morning")
                       ? vec3{0.35f, 0.33f, 0.30f}
                       : vec3{0.052f, 0.052f, 0.062f} * a.ambientScale;
    FogParams fog;
    if (flags.count("morning")) fog = {{0.35f, 0.32f, 0.27f}, 0.03f};
    else fog = {{0.010f, 0.010f, 0.014f}, 0.11f + fearLevel * 0.05f};

    // ---- mirror reflections (up to 2 visible mirrors)
    int mirrorSlot = 0;
    for (const auto& m : content.mirrors) {
        if (mirrorSlot >= (int)mirrorRts.size()) break;
        auto rit = world.roomIdx.find(m.room);
        if (rit == world.roomIdx.end()) continue;
        std::vector<int> vis;
        world.visibleRooms(playerRoom, vis);
        if (std::find(vis.begin(), vis.end(), rit->second) == vis.end()) continue;
        if (distance(eye, m.pos) > 12) continue;

        vec3 n = {std::sin(m.yaw), 0, std::cos(m.yaw)};   // mirror faces +Z rotated by yaw
        float dplane = -dot(n, m.pos);
        float side = dot(n, eye) + dplane;
        if (side < 0.05f) continue;   // behind the mirror

        vec3 eyeR = eye - n * (2.0f * side);
        // portal-fitted view: from reflected eye toward mirror center
        float distToMirror = distance(eyeR, m.pos);
        float fovFit = 2.0f * std::atan((m.size.y * 0.75f) / std::fmax(distToMirror, 0.2f));
        fovFit = clampf(fovFit, radians(25.0f), radians(110.0f));

        SceneView rv;
        rv.view = lookAt(eyeR, m.pos, {0, 1, 0});
        rv.proj = perspectiveVk(fovFit, m.size.x / m.size.y, 0.05f, 60.0f);
        rv.camPos = eyeR;
        rv.time = (float)worldTime;
        rv.ambient = ambient;
        rv.fog = fog;
        rv.target = mirrorRts[mirrorSlot];
        rv.isReflection = true;
        rv.clipPlane = {n.x, n.y, n.z, dplane};

        std::vector<DrawItem> ritems = items;
        // the reflection sometimes has one more person in it than the room does
        if (flags.count("mirror_witness")) {
            DrawItem w;
            w.mesh = world.billboardMesh();
            w.tex = witnessSprite(std::max(witness.stage, 1));
            vec3 wp = eye - camForward(pYaw, 0) * 1.6f;
            wp.y = pPos.y;
            vec3 dd = eyeR - wp;
            w.model = mat4::translate(wp) * mat4::rotateY(std::atan2(dd.x, dd.z)) *
                      mat4::scale({0.86f, 2.05f, 1});
            w.tint = {1, 1, 1, 0.85f};
            w.flags = DRAW_ALPHA | DRAW_UNLIT | DRAW_ONLY_REFLECT;
            ritems.push_back(w);
        }
        eng.renderScene(rv, ritems, lights);

        // mirror surface quad in the main view
        DrawItem mq;
        mq.mesh = world.quadMesh();
        mq.tex = eng.renderTargetTexture(mirrorRts[mirrorSlot]);
        mq.model = mat4::translate(m.pos) * mat4::rotateY(m.yaw + PI) *
                   mat4::scale({m.size.x, m.size.y, 1});
        mq.flags = DRAW_UNLIT;
        mq.emissive = 0.0f;
        mq.tint = {0.82f, 0.86f, 0.88f, 1};   // glass dimming
        items.push_back(mq);
        mirrorSlot++;
    }

    // ---- CCTV camera view (monitor mode / ending shot replaces main view)
    bool camAsMain = (screen == Screen::Monitor) || endingPhase == 4;
    SceneView main;
    if (camAsMain && !content.cams.empty()) {
        const CamSpec& cam = content.cams[monitorCam % content.cams.size()];
        auto crit = world.roomIdx.find(cam.room);
        int camRoom = crit != world.roomIdx.end() ? crit->second : playerRoom;
        std::vector<DrawItem> citems;
        std::vector<Light> clights;
        world.buildDrawList(content, camRoom, citems, clights, false, (float)worldTime);

        // apparitions that exist only on this feed
        auto addFigure = [&](const std::string& texName, vec3 at, float phase) {
            DrawItem f;
            f.mesh = world.billboardMesh();
            f.tex = world.texOf(texName);
            vec3 d = cam.pos - at;
            f.model = mat4::translate(at) * mat4::rotateY(std::atan2(d.x, d.z)) *
                      mat4::scale({0.8f, 1.95f, 1});
            f.tint = {1, 1, 1, 0.92f};
            f.flags = DRAW_ALPHA | DRAW_UNLIT;
            (void)phase;
            citems.push_back(f);
        };
        const RoomSpec& cr = content.rooms[camRoom];
        vec3 rc = (cr.mn + cr.mx) * 0.5f;
        rc.y = cr.mn.y;
        if (flags.count("cctv_witness_" + cam.id)) {
            addFigure(witness.stage >= 3 ? "witness_s3" : "witness_s2", rc, 0);
            profile.cctvTime += 0.016f;
            witness.gainKnowledge(0.02f);
        }
        if (flags.count("cctv_you_" + cam.id)) addFigure("protagonist", rc + vec3{0.8f, 0, 0.4f}, 0);

        if (endingPhase == 4) {
            // two of you leave, several seconds apart
            double t = worldTime - endingStartedAt;
            auto exitWalk = [&](double t0, double dur, float lane) -> vec3 {
                float k = clampf((float)((t - t0) / dur), 0.0f, 1.0f);
                return {lerpf(cr.mn.x + 1.5f, cr.mx.x - 1.2f, k), cr.mn.y, rc.z + lane};
            };
            if (t > 1.0 && t < 6.4) addFigure("protagonist", exitWalk(1.0, 5.0, -0.4f), 0);
            if (t > 6.8 && t < 12.4) addFigure("protagonist", exitWalk(6.8, 5.2, 0.35f), 0);
        }

        main.view = camView(cam.pos, cam.yaw, cam.pitch);
        main.proj = perspectiveVk(radians(68.0f), aspect, 0.05f, 60.0f);
        main.camPos = cam.pos;
        main.time = (float)worldTime;
        main.ambient = ambient * 1.6f;   // CCTV gain
        main.fog = fog;
        main.target = -1;
        eng.renderScene(main, citems, clights);
        if (screen == Screen::Monitor) {
            profile.cctvTime += 0.016f;
            profile.score[CH_CCTV] += 0.016f * 0.4f;
        }
    } else {
        main.view = camView(eye, pYaw, pPitch);
        main.proj = perspectiveVk(fov, aspect, 0.045f, 80.0f);
        main.camPos = eye;
        main.time = (float)worldTime;
        main.ambient = ambient;
        main.fog = fog;
        main.target = -1;
        eng.renderScene(main, items, lights);
    }

    // restore reveals
    for (int i : revealed) world.props[i].hidden = true;
}

// ================================================================ render ====
void Game::render() {
    if (!eng.beginFrame()) return;

    bool worldVisible = screen == Screen::Playing || screen == Screen::Paused ||
                        screen == Screen::Reading || screen == Screen::Clipboard ||
                        screen == Screen::Gallery || screen == Screen::Monitor ||
                        screen == Screen::Ending;
    if (worldVisible && !content.rooms.empty()) renderWorldViews();

    PostParams post;
    post.time = (float)worldTime;
    bool cctv = screen == Screen::Monitor || endingPhase == 4;
    post.grain = (0.040f + fearLevel * 0.05f) * settings.grain;
    post.vignette = 0.36f + fearLevel * 0.22f;
    post.aberration = 0.0011f + fearLevel * 0.0035f;
    post.warp = fearLevel > 0.35f ? (fearLevel - 0.35f) * 0.5f : 0.0f;
    post.desaturate = fearLevel * 0.22f;
    post.scanline = viewfinder ? 0.30f : 0.0f;
    post.cctv = cctv ? 1.0f : 0.0f;
    post.noiseBurst = noiseBurst;
    post.flashWhite = flashWhite;
    post.fadeBlack = fade;
    post.lowHealth = fearLevel * 0.6f;
    if (screen == Screen::Menu || screen == Screen::Credits || screen == Screen::Settings ||
        screen == Screen::Boot) {
        post.fadeBlack = 1.0f;   // UI over black
        post.grain = 0.05f;
    }

    renderUi();
    eng.renderPostAndUi(post);
    eng.endFrame();

    // ---- develop pending photo (scene buffer holds this frame, reveals included)
    if (flags.count("pending_photo")) {
        flags.erase("pending_photo");
        std::vector<uint8_t> px;
        int w, h;
        if (eng.captureSceneRGBA(px, w, h)) {
            // instant-camera look: mild desaturate + lift + vignette baked in
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    uint8_t* p = &px[(y * w + x) * 4];
                    float fx = (float)x / w - 0.5f, fy = (float)y / h - 0.5f;
                    float vig = 1.0f - (fx * fx + fy * fy) * 0.9f;
                    float lum = 0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2];
                    for (int c = 0; c < 3; ++c) {
                        float v = lerpf((float)p[c], lum, 0.35f);
                        v = (v * 0.92f + 14.0f) * vig;
                        p[c] = (uint8_t)clampf(v, 0, 255);
                    }
                    p[3] = 255;
                }
            PhotoRecord rec;
            rec.takenAt = worldTime;
            std::filesystem::create_directories(userDir + "/photos");
            rec.file = userDir + "/photos/photo_" + std::to_string((int)photos.size()) + ".png";
            // subjects: what the camera saw that the player may not have
            vec3 eye = pPos + vec3{0, 1.62f, 0};
            vec3 fwd = camForward(pYaw, pPitch);
            for (auto& o : observation.obs)
                if (o.active) {
                    vec3 to = o.pos - eye;
                    float d = length(to);
                    if (d < 20 && dot(fwd, to / d) > 0.75f && world.lineOfSight(eye, o.pos)) {
                        o.manifest += 2.2f;
                        observation.attention += 0.5f;
                        rec.subjects.push_back(o.id);
                    }
                }
            if (witness.visible) {
                vec3 to = witness.pos + vec3{0, 1.3f, 0} - eye;
                float d = length(to);
                if (d < 24 && dot(fwd, to / d) > 0.72f) {
                    witness.photoKnowledge += 2.2f;
                    witness.gainKnowledge(0.8f);
                    rec.subjects.push_back("witness");
                    fearLevel = std::fmin(1.0f, fearLevel + 0.18f);
                }
            }
            rec.caption = clockString();
            if (!rec.subjects.empty()) rec.caption += "  —  something is in this one";
            // write png via engine helper (reuse screenshot writer by temp path)
            // stb write here to keep engine API small:
            extern bool observerWritePng(const std::string&, const uint8_t*, int, int);
            observerWritePng(rec.file, px.data(), w, h);
            // thumbnail texture (downscale x4)
            int tw = w / 4, th = h / 4;
            std::vector<uint8_t> tpx((size_t)tw * th * 4);
            for (int y = 0; y < th; ++y)
                for (int x = 0; x < tw; ++x)
                    for (int c = 0; c < 4; ++c)
                        tpx[(y * tw + x) * 4 + c] = px[((y * 4) * w + x * 4) * 4 + c];
            rec.tex = eng.createTexture(tpx.data(), tw, th, true, false);
            photos.push_back(rec);
            photoCount = (int)photos.size();
            playSnd("polaroid", 0.8f);
            runBeats("photo", rec.subjects.empty() ? "empty" : rec.subjects[0]);
        }
    }
}

// =================================================================== HUD ====
void Game::uiSubtitles() {
    if (!settings.subtitles) return;
    float W = (float)eng.width(), H = (float)eng.height();
    float y = H - 110;
    while (!subs.empty() && subs.front().until < worldTime) subs.pop_front();
    for (auto it = subs.rbegin(); it != subs.rend(); ++it) {
        float tw = eng.textWidth(font, it->text);
        eng.uiRect(W * 0.5f - tw * 0.5f - 14, y - 6, tw + 28, eng.fontHeight(font) + 12,
                   {0, 0, 0, 0.55f});
        eng.uiText(font, it->text, W * 0.5f - tw * 0.5f, y, it->color);
        y -= eng.fontHeight(font) + 18;
    }
}

void Game::uiHud() {
    float W = (float)eng.width(), H = (float)eng.height();

    if (screen == Screen::Playing && !viewfinder && endingPhase != 4) {
        eng.uiRect(W * 0.5f - 1.5f, H * 0.5f - 1.5f, 3, 3, {1, 1, 1, 0.35f});
        // interaction prompt
        vec3 eye = pPos + vec3{0, crouching ? 1.15f : 1.62f, 0};
        vec3 fwd = camForward(pYaw, pPitch);
        int prop = -1, door = -1;
        world.raycast(eye, fwd, 2.3f, &prop, &door);
        std::string prompt;
        if (door >= 0) {
            const DoorSpec& d = content.doors[door];
            if (d.isThreshold) prompt = "[E] " + (d.label.empty() ? "Take the stairs" : d.label);
            else if (world.doors[door].locked) prompt = "[E] " + (d.label.empty() ? "Door" : d.label) + " (locked)";
            else prompt = world.doors[door].open ? "[E] Close" : "[E] Open";
        } else if (prop >= 0 && !content.props[prop].interact.empty()) {
            const std::string& ia = content.props[prop].interact;
            std::string verb = ia.substr(0, ia.find(':'));
            std::string arg = ia.find(':') == std::string::npos ? "" : ia.substr(ia.find(':') + 1);
            if (verb == "pickup")
                prompt = "[E] Take " + (content.strings.count("item_" + arg) ? content.strings.at("item_" + arg) : arg);
            else if (verb == "read") prompt = "[E] Read";
            else if (verb == "switch") prompt = "[E] Use";
            else if (verb == "monitor") prompt = "[E] View cameras";
            else if (verb == "phone") prompt = flags.count("phone_ringing") ? "[E] Answer" : "[E] Phone";
            else if (verb == "action") prompt = "[E] " + (content.strings.count("act_" + arg) ? content.strings.at("act_" + arg) : "Use");
        }
        if (!prompt.empty()) {
            float tw = eng.textWidth(font, prompt);
            eng.uiText(font, prompt, W * 0.5f - tw * 0.5f, H * 0.62f, {0.95f, 0.95f, 0.9f, 0.9f});
        }
    }

    // viewfinder chrome
    if (viewfinder && screen == Screen::Playing) {
        vec4 c{0.9f, 0.9f, 0.9f, 0.8f};
        float m = 60, L = 46, t = 2;
        eng.uiRect(m, m, L, t, c); eng.uiRect(m, m, t, L, c);
        eng.uiRect(W - m - L, m, L, t, c); eng.uiRect(W - m - t, m, t, L, c);
        eng.uiRect(m, H - m - t, L, t, c); eng.uiRect(m, H - m - L, t, L, c);
        eng.uiRect(W - m - L, H - m - t, L, t, c); eng.uiRect(W - m - t, H - m - L, t, L, c);
        eng.uiRect(W * 0.5f - 9, H * 0.5f, 18, 1, c);
        eng.uiRect(W * 0.5f, H * 0.5f - 9, 1, 18, c);
        double blink = worldTime - std::floor(worldTime);
        if (blink < 0.6) {
            eng.uiRect(m + 8, m + 14, 10, 10, {0.9f, 0.15f, 0.1f, 0.9f});
            eng.uiText(fontMono, "REC", m + 26, m + 8, {0.95f, 0.3f, 0.2f, 0.95f});
        }
        eng.uiText(fontMono, clockString(), W - m - 100, m + 8, {0.85f, 0.95f, 0.85f, 0.9f});
        char buf[32];
        std::snprintf(buf, sizeof(buf), "EXP %02d", photoCount);
        eng.uiText(fontMono, buf, m + 8, H - m - 34, {0.85f, 0.95f, 0.85f, 0.9f});
        eng.uiText(font, "[LMB] expose", W - m - 150, H - m - 34, {0.8f, 0.8f, 0.8f, 0.55f});
    }

    // CCTV monitor chrome
    if (screen == Screen::Monitor || endingPhase == 4) {
        const CamSpec& cam = content.cams[monitorCam % content.cams.size()];
        std::string label = "CAM " + std::to_string(monitorCam % content.cams.size() + 1) + " — " + cam.label;
        if (endingPhase == 4) label = "CAM 1 — LOBBY";
        eng.uiRect(0, 0, W, 34, {0, 0, 0, 0.5f});
        eng.uiText(fontMono, label, 18, 6, {0.8f, 1.0f, 0.85f, 0.9f});
        std::string ts = (endingPhase == 4 ? "06:1" + std::to_string(1 + ((int)(worldTime * 0.2)) % 8)
                                           : clockString());
        eng.uiText(fontMono, ts + "  " + (endingPhase == 4 ? "" : "REC"), W - 190, 6,
                   {0.8f, 1.0f, 0.85f, 0.9f});
        if (screen == Screen::Monitor)
            eng.uiText(font, "[Q/E] switch camera   [Esc] step away", 18, H - 40,
                       {0.7f, 0.75f, 0.7f, 0.6f});
    }

    // title card
    if (worldTime < titleCardUntil && !titleCard.empty()) {
        float k = (float)(titleCardUntil - worldTime);
        float alpha = clampf(std::fmin(k, 1.2f) / 1.2f, 0, 1) * 0.92f;
        float tw = eng.textWidth(fontBig, titleCard);
        eng.uiText(fontBig, titleCard, W * 0.5f - tw * 0.5f, H * 0.24f, {0.9f, 0.9f, 0.92f, alpha});
    }
    uiSubtitles();
}

// ================================================================ screens ===
static void menuItem(Game& g, const std::string& text, int idx, int sel, float y) {
    float W = (float)g.eng.width();
    vec4 c = idx == sel ? vec4{0.95f, 0.93f, 0.85f, 1} : vec4{0.55f, 0.55f, 0.58f, 1};
    std::string t = idx == sel ? "> " + text + " <" : text;
    float tw = g.eng.textWidth(g.font, t);
    g.eng.uiText(g.font, t, W * 0.5f - tw * 0.5f, y, c);
}

void Game::uiMenu() {
    float W = (float)eng.width(), H = (float)eng.height();
    TexId key = world.texOf("keyart_menu");
    if (key != TEX_WHITE) {
        float ar = 2.0f / 3.0f;   // vertical art
        float ih = H, iw = ih * ar;
        eng.uiImage(key, W - iw - 40, 0, iw, ih, {0.85f, 0.85f, 0.85f, 0.9f});
    }
    eng.uiText(fontBig, "THE OBSERVER", 90, H * 0.22f, {0.92f, 0.92f, 0.95f, 1});
    eng.uiText(font, "a night shift at Halcyon Court", 92, H * 0.22f + 64, {0.5f, 0.5f, 0.55f, 1});
    float y = H * 0.46f;
    int i = 0;
    bool hasSave = fileExists(userDir + "/save.json");
    std::vector<std::string> entries{"NEW SHIFT"};
    if (hasSave) entries.push_back("CONTINUE");
    entries.push_back("SETTINGS");
    entries.push_back("QUIT");
    for (auto& e : entries) {
        vec4 c = i == menuSel ? vec4{0.95f, 0.93f, 0.85f, 1} : vec4{0.5f, 0.5f, 0.55f, 1};
        eng.uiText(font, (i == menuSel ? "> " : "  ") + e, 92, y, c);
        y += 44;
        i++;
    }
    eng.uiText(fontMono, "IRIS build " __DATE__, 20, H - 34, {0.35f, 0.35f, 0.38f, 1});
}

void Game::uiSettings() {
    float W = (float)eng.width(), H = (float)eng.height();
    eng.uiText(fontBig, "SETTINGS", 90, H * 0.18f, {0.9f, 0.9f, 0.92f, 1});
    auto bar = [&](float y, const std::string& label, float v, int idx) {
        vec4 c = idx == menuSel ? vec4{0.95f, 0.93f, 0.85f, 1} : vec4{0.55f, 0.55f, 0.6f, 1};
        eng.uiText(font, label, 92, y, c);
        eng.uiRect(340, y + 8, 300, 10, {0.25f, 0.25f, 0.28f, 1});
        eng.uiRect(340, y + 8, 300 * clampf(v, 0, 1), 10, c);
    };
    bar(H * 0.36f, "Volume", settings.volume, 0);
    bar(H * 0.36f + 48, "Mouse sensitivity", settings.sensitivity / 2.0f, 1);
    bar(H * 0.36f + 96, "Film grain", settings.grain, 2);
    vec4 c3 = menuSel == 3 ? vec4{0.95f, 0.93f, 0.85f, 1} : vec4{0.55f, 0.55f, 0.6f, 1};
    eng.uiText(font, std::string("Subtitles: ") + (settings.subtitles ? "ON" : "OFF"), 92,
               H * 0.36f + 144, c3);
    vec4 c4 = menuSel == 4 ? vec4{0.95f, 0.93f, 0.85f, 1} : vec4{0.55f, 0.55f, 0.6f, 1};
    eng.uiText(font, "Back", 92, H * 0.36f + 200, c4);
    eng.uiText(font, "[←/→] adjust   [↑/↓] select", 92, H - 80, {0.4f, 0.4f, 0.45f, 1});
    (void)W;
}

void Game::uiClipboard() {
    float W = (float)eng.width(), H = (float)eng.height();
    float pw = std::fmin(680.0f, W * 0.62f), ph = H * 0.78f;
    float x = W * 0.5f - pw * 0.5f, y = H * 0.10f;
    TexId paper = world.texOf("paper_aged");
    if (paper != TEX_WHITE) eng.uiImage(paper, x, y, pw, ph, {1, 1, 1, 0.97f});
    else eng.uiRect(x, y, pw, ph, PAPER);
    eng.uiText(fontBig, "HALCYON COURT", x + 40, y + 34, INK);
    eng.uiText(fontMono, "NIGHT LOG — " + clockString(), x + 42, y + 96, {0.3f, 0.25f, 0.22f, 1});
    float ty = y + 150;
    for (auto& t : tasks) {
        eng.uiText(font, t.done ? "[x]" : "[ ]", x + 44, ty, t.done ? vec4{0.35f, 0.4f, 0.3f, 1} : INK);
        eng.uiText(font, t.text, x + 96, ty, t.done ? vec4{0.45f, 0.42f, 0.38f, 1} : INK);
        ty += 40;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "exposures: %d", photoCount);
    eng.uiText(fontMono, buf, x + 42, y + ph - 64, {0.35f, 0.3f, 0.28f, 1});
    eng.uiText(font, "[Tab] put away    [G] photographs", x + pw - 330, y + ph - 60,
               {0.4f, 0.35f, 0.3f, 0.8f});
}

void Game::uiReading() {
    float W = (float)eng.width(), H = (float)eng.height();
    const DocumentSpec* doc = nullptr;
    for (auto& d : content.documents)
        if (d.id == readingDoc) doc = &d;
    if (!doc) {
        screen = Screen::Playing;
        return;
    }
    float pw = std::fmin(760.0f, W * 0.66f), ph = H * 0.84f;
    float x = W * 0.5f - pw * 0.5f, y = H * 0.07f;
    TexId paper = world.texOf("paper_aged");
    if (paper != TEX_WHITE) eng.uiImage(paper, x, y, pw, ph, {1, 1, 1, 0.98f});
    else eng.uiRect(x, y, pw, ph, PAPER);
    Font* body = doc->type == "report" || doc->type == "terminal" ? fontMono : font;
    vec4 ink = doc->type == "terminal" ? vec4{0.15f, 0.3f, 0.18f, 1} : INK;
    eng.uiText(font, doc->title, x + 46, y + 38, ink);
    eng.uiRect(x + 46, y + 78, pw - 92, 2, {ink.x, ink.y, ink.z, 0.5f});
    float ty = y + 100;
    for (auto& line : doc->lines) {
        eng.uiText(body, line, x + 46, ty, ink, 0.86f);
        ty += eng.fontHeight(body) * 0.86f + 10;
        if (ty > y + ph - 60) break;
    }
    eng.uiText(font, "[E] put down", x + pw - 190, y + ph - 52, {0.4f, 0.35f, 0.3f, 0.8f});
}

void Game::uiGallery() {
    float W = (float)eng.width(), H = (float)eng.height();
    eng.uiRect(0, 0, W, H, {0.04f, 0.04f, 0.05f, 0.94f});
    eng.uiText(fontBig, "EXPOSURES", 60, 40, {0.9f, 0.9f, 0.92f, 1});
    if (photos.empty()) {
        eng.uiText(font, "No photographs yet.", 64, 140, {0.5f, 0.5f, 0.55f, 1});
    } else {
        gallerySel = (int)clampf((float)gallerySel, 0, (float)photos.size() - 1);
        const PhotoRecord& p = photos[gallerySel];
        float iw = std::fmin(W * 0.62f, 900.0f), ih = iw * 0.5625f;
        float x = W * 0.5f - iw * 0.5f, y = H * 0.16f;
        eng.uiRect(x - 14, y - 14, iw + 28, ih + 70, {0.9f, 0.88f, 0.84f, 1});   // instant border
        if (p.tex != TEX_INVALID) eng.uiImage(p.tex, x, y, iw, ih);
        eng.uiText(fontMono, p.caption, x + 4, y + ih + 12, {0.25f, 0.22f, 0.2f, 1});
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d / %d", gallerySel + 1, (int)photos.size());
        eng.uiText(font, buf, W * 0.5f - 30, H - 70, {0.6f, 0.6f, 0.65f, 1});
    }
    eng.uiText(font, "[←/→] browse   [Esc] close", 64, H - 70, {0.45f, 0.45f, 0.5f, 1});
}

void Game::uiMonitor() { /* chrome drawn in uiHud */ }

void Game::uiEnding() {
    float W = (float)eng.width(), H = (float)eng.height();
    if (screen != Screen::Credits) return;
    double t = worldTime - endingStartedAt;
    float tw = eng.textWidth(fontBig, "THE OBSERVER");
    if (t < 6) {
        eng.uiText(fontBig, "THE OBSERVER", W * 0.5f - tw * 0.5f, H * 0.42f, {0.92f, 0.92f, 0.95f, 1});
    }
    if (t > 3 && t < 9) {
        std::string line = content.strings.count("post_" + endingFlavor)
                               ? content.strings.at("post_" + endingFlavor)
                               : "It knows the way now.";
        float lw = eng.textWidth(font, line);
        float a = clampf((float)(t - 3.0), 0.f, 1.f) * (t > 8 ? (float)(9 - t) : 1.0f);
        eng.uiText(font, line, W * 0.5f - lw * 0.5f, H * 0.42f + 80, {0.6f, 0.6f, 0.65f, a});
    }
    if (t > 9) {
        float scroll = (float)(t - 9) * 36.0f;
        std::vector<std::string> credits{
            "a game about being seen",
            "",
            "design / engine / everything",
            "CRIT HIT STUDIO",
            "",
            "built on IRIS — a custom Vulkan engine",
            "\"It Renders In Silence\"",
            "",
            "textures, portraits and voices",
            "generated with Higgsfield",
            "",
            "you were the only person in the building",
            "",
            "THE OBSERVER",
            "case one of the CATALOGUE",
        };
        float y = H + 60 - scroll;
        for (auto& line : credits) {
            float lw = eng.textWidth(font, line);
            eng.uiText(font, line, W * 0.5f - lw * 0.5f, y, {0.65f, 0.65f, 0.7f, 1});
            y += 44;
        }
        if (y < -60) {
            screen = Screen::Menu;
            menuSel = 0;
            eng.setMouseCaptured(false);
        }
    }
    eng.uiRect(0, 0, 0, 0, {0, 0, 0, 0});
}

void Game::renderUi() {
    switch (screen) {
        case Screen::Menu: uiMenu(); break;
        case Screen::Settings: uiSettings(); break;
        case Screen::Clipboard: uiClipboard(); uiSubtitles(); break;
        case Screen::Reading: uiReading(); break;
        case Screen::Gallery: uiGallery(); break;
        case Screen::Credits: uiEnding(); break;
        case Screen::Paused: {
            float W = (float)eng.width(), H = (float)eng.height();
            eng.uiRect(0, 0, W, H, {0, 0, 0, 0.55f});
            eng.uiText(fontBig, "PAUSED", W * 0.5f - eng.textWidth(fontBig, "PAUSED") * 0.5f,
                       H * 0.3f, {0.9f, 0.9f, 0.92f, 1});
            menuItem(*this, "RESUME", 0, menuSel, H * 0.46f);
            menuItem(*this, "SAVE", 1, menuSel, H * 0.46f + 44);
            menuItem(*this, "SETTINGS", 2, menuSel, H * 0.46f + 88);
            menuItem(*this, "QUIT TO MENU", 3, menuSel, H * 0.46f + 132);
            uiSubtitles();
            break;
        }
        default: uiHud(); break;
    }
}
