// THE OBSERVER - entry point and master loop.
#include "game.h"

#include <cstring>
#include <filesystem>

// ==================================================================== boot ==
static std::string findDataRoot(const char* argv0) {
    auto has = [](const std::string& d) { return fileExists(d + "/content/building.json"); };
    if (has(".")) return ".";
    if (has("..")) return "..";
    std::string exe = argv0 ? argv0 : "";
    size_t slash = exe.find_last_of("/\\");
    if (slash != std::string::npos) {
        std::string dir = exe.substr(0, slash);
        if (has(dir)) return dir;
        if (has(dir + "/..")) return dir + "/..";
    }
    return ".";
}

bool Game::boot(int argc, char** argv) {
    std::string dataRoot;
    uint32_t cliW = 0, cliH = 0;
    bool wantNewGame = false, validation = false, mute = false;
    int frames = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--headless") headless = true;
        else if (a == "--frames") frames = std::atoi(next().c_str());
        else if (a == "--sim") simScript = next();
        else if (a == "--data") dataRoot = next();
        else if (a == "--width") cliW = std::atoi(next().c_str());
        else if (a == "--height") cliH = std::atoi(next().c_str());
        else if (a == "--newgame") wantNewGame = true;
        else if (a == "--validation") validation = true;
        else if (a == "--mute") mute = true;
        else if (a == "--shot") flags.insert("cli_shot:" + next());
    }
    if (frames > 0) flags.insert("cli_frames:" + std::to_string(frames));
    if (dataRoot.empty()) dataRoot = findDataRoot(argc > 0 ? argv[0] : nullptr);

    userDir = userDataDir("TheObserver");
    std::filesystem::create_directories(userDir);
    settings.load(userDir + "/settings.json");

    EngineConfig cfg;
    cfg.appName = "THE OBSERVER";
    cfg.width = cliW ? cliW : settings.width;
    cfg.height = cliH ? cliH : settings.height;
    cfg.headless = headless;
    cfg.fullscreen = settings.fullscreen;
    cfg.validation = validation;
    cfg.shaderDir = fileExists(dataRoot + "/engine/shaders/spv/scene.vert.spv")
                        ? dataRoot + "/engine/shaders/spv"
                        : dataRoot + "/shaders/spv";
    if (!eng.init(cfg)) return false;
    audio.init(headless || mute);

    std::string err;
    if (!loadContent(content, dataRoot + "/content", err)) {
        IRIS_ERROR("content: %s", err.c_str());
        return false;
    }
    // resolve asset paths relative to data root
    for (auto& [k, v] : content.textures)
        if (!v.empty() && v[0] != '/') v = dataRoot + "/" + v;
    for (auto& [k, v] : content.sounds)
        if (!v.empty() && v[0] != '/') v = dataRoot + "/" + v;

    if (!world.build(eng, content)) return false;
    for (auto& [name, path] : content.sounds)
        if (fileExists(path)) snd[name] = audio.load(path);

    std::string fontDir = dataRoot + "/assets/fonts/";
    font = eng.loadFont(fontDir + "DejaVuSans.ttf", 26);
    fontBig = eng.loadFont(fontDir + "DejaVuSans-Bold.ttf", 52);
    fontMono = eng.loadFont(fontDir + "DejaVuSansMono.ttf", 22);
    if (!font || !fontBig || !fontMono) return false;

    mirrorRts.push_back(eng.createRenderTarget(384, 384));
    mirrorRts.push_back(eng.createRenderTarget(384, 384));

    // sim script -> command list
    if (!simScript.empty()) {
        for (auto part : [&] {
                 std::vector<std::string> out;
                 size_t s = 0;
                 while (s <= simScript.size()) {
                     size_t p = simScript.find(';', s);
                     if (p == std::string::npos) {
                         out.push_back(simScript.substr(s));
                         break;
                     }
                     out.push_back(simScript.substr(s, p - s));
                     s = p + 1;
                 }
                 return out;
             }()) {
            while (!part.empty() && part.front() == ' ') part.erase(part.begin());
            if (!part.empty()) simCmds.push_back({0, part});
        }
    }

    screen = Screen::Boot;
    fade = 1.0f;
    fadeTarget = 0.0f;
    if (wantNewGame) newGame();
    return true;
}

// ===================================================================== sim ==
// walk:<s>  back:<s>  turn:<deg>:<s>  pitch:<deg>:<s>  wait:<s>  press:<key>
// do:<action>  shot:<path>  view  quit
void gameTickSim(Game& g, float dt) {
    if (g.simCursor >= g.simCmds.size()) return;
    auto& [elapsed, cmd] = g.simCmds[g.simCursor];
    auto tok = [&](int i) {
        std::vector<std::string> parts;
        size_t s = 0;
        while (s <= cmd.size()) {
            size_t p = cmd.find(':', s);
            if (p == std::string::npos) {
                parts.push_back(cmd.substr(s));
                break;
            }
            parts.push_back(cmd.substr(s, p - s));
            s = p + 1;
        }
        return i < (int)parts.size() ? parts[i] : std::string();
    };
    std::string verb = tok(0);
    bool done = false;
    if (verb == "wait") {
        done = elapsed >= std::atof(tok(1).c_str());
    } else if (verb == "walk" || verb == "back") {
        g.eng.input().injectKey(verb == "walk" ? KEY_W : KEY_S, true);
        done = elapsed >= std::atof(tok(1).c_str());
        if (done) g.eng.input().injectKey(verb == "walk" ? KEY_W : KEY_S, false);
    } else if (verb == "turn") {
        float total = radians((float)std::atof(tok(1).c_str()));
        float dur = std::fmax(0.01f, (float)std::atof(tok(2).c_str()));
        g.pYaw += total * (float)(dt / dur);
        done = elapsed >= dur;
    } else if (verb == "face") {
        g.pYaw = radians((float)std::atof(tok(1).c_str()));
        done = true;
    } else if (verb == "lookat") {
        g.pPitch = radians((float)std::atof(tok(1).c_str()));
        done = true;
    } else if (verb == "pitch") {
        float total = radians((float)std::atof(tok(1).c_str()));
        float dur = std::fmax(0.01f, (float)std::atof(tok(2).c_str()));
        g.pPitch = clampf(g.pPitch + total * (float)(dt / dur), radians(-86), radians(86));
        done = elapsed >= dur;
    } else if (verb == "press") {
        std::string k = tok(1);
        Key key = k == "E" ? KEY_E : k == "C" ? KEY_C : k == "F" ? KEY_F : k == "TAB" ? KEY_TAB
                  : k == "SPACE" ? KEY_SPACE : k == "ENTER" ? KEY_ENTER : k == "ESC" ? KEY_ESC
                  : k == "Q" ? KEY_Q : k == "G" ? KEY_C : KEY_E;
        g.eng.input().pressed[key] = true;
        g.eng.input().down[key] = true;
        done = true;
    } else if (verb == "do") {
        g.execAction(cmd.substr(3));
        done = true;
    } else if (verb == "shot") {
        g.flags.insert("cli_shot_now:" + tok(1));
        done = true;
    } else if (verb == "quit") {
        g.eng.requestQuit();
        done = true;
    } else {
        done = true;
    }
    elapsed += dt;
    if (done) {
        g.simCursor++;
        if (g.simCursor < g.simCmds.size()) g.simCmds[g.simCursor].first = 0;
    }
}

// ==================================================================== tick ==
void Game::tickPlaying(float dt) {
    updatePlayer(dt);
    updateObservation(dt);
    updateWitness(dt);
    if (endingPhase == 0) updateDirector(dt);
    updateAudioBeds();
    runBeats("time", "");

    static int lastRoom = -2;
    if (playerRoom != lastRoom && playerRoom >= 0) {
        lastRoom = playerRoom;
        runBeats("enter", content.rooms[playerRoom].id);
    }

    Input& in = eng.input();
    if (in.pressed[KEY_TAB]) {
        screen = Screen::Clipboard;
        eng.setMouseCaptured(false);
        playSnd("clipboard", 0.6f);
    }
    if (in.pressed[KEY_ESC]) {
        screen = Screen::Paused;
        menuSel = 0;
        eng.setMouseCaptured(false);
    }
    if (in.pressed[KEY_F5]) {
        saveGame();
        say("Saved.", 1.5f);
    }
    if (in.pressed[KEY_F9] && loadGame()) say("Loaded.", 1.5f);
}

void Game::tick(float dt) {
    bool worldAdvances = screen == Screen::Playing || screen == Screen::Monitor ||
                         screen == Screen::Reading || screen == Screen::Clipboard ||
                         screen == Screen::Gallery || screen == Screen::Ending ||
                         screen == Screen::Credits;
    if (worldAdvances) worldTime += dt;

    fade = damp(fade, fadeTarget, 2.4f, dt);
    if (fade < fadeTarget) fade = std::fmin(fade + dt * 1.5f, fadeTarget);
    noiseBurst = std::fmax(0.0f, noiseBurst - dt * 2.2f);

    // door animation
    for (auto& d : world.doors) {
        float target = d.open ? 1.0f : 0.0f;
        float rate = 3.2f * dt;
        d.openT = d.openT < target ? std::fmin(d.openT + rate, target)
                                   : std::fmax(d.openT - rate, target);
    }
    // timed cues
    for (size_t i = 0; i < cues.size();) {
        if (worldTime >= cues[i].at) {
            std::string a = cues[i].action;
            cues.erase(cues.begin() + i);
            execAction(a);
        } else {
            ++i;
        }
    }

    if (!simCmds.empty() && (screen == Screen::Playing || screen == Screen::Monitor))
        gameTickSim(*this, dt);

    Input& in = eng.input();
    if (in.pressed[KEY_F12]) {
        static int shotNo = 0;
        eng.saveScreenshot(userDir + "/screenshot_" + std::to_string(shotNo++) + ".png");
    }
    switch (screen) {
        case Screen::Boot:
            screen = Screen::Menu;
            eng.setMouseCaptured(false);
            break;
        case Screen::Menu: {
            bool hasSave = fileExists(userDir + "/save.json");
            int n = hasSave ? 4 : 3;
            if (in.pressed[KEY_DOWN] || in.pressed[KEY_S]) menuSel = (menuSel + 1) % n;
            if (in.pressed[KEY_UP] || in.pressed[KEY_W]) menuSel = (menuSel + n - 1) % n;
            if (in.pressed[KEY_ENTER] || in.pressed[KEY_E]) {
                int idx = menuSel;
                if (!hasSave && idx >= 1) idx++;   // skip CONTINUE slot
                if (idx == 0) newGame();
                else if (idx == 1) {
                    if (loadGame()) {
                        screen = Screen::Playing;
                        eng.setMouseCaptured(true);
                        fade = 1;
                        fadeTarget = 0;
                    }
                } else if (idx == 2) {
                    pauseReturn = Screen::Menu;
                    screen = Screen::Settings;
                    menuSel = 0;
                } else {
                    eng.requestQuit();
                }
            }
            break;
        }
        case Screen::Settings: {
            if (in.pressed[KEY_DOWN] || in.pressed[KEY_S]) menuSel = (menuSel + 1) % 5;
            if (in.pressed[KEY_UP] || in.pressed[KEY_W]) menuSel = (menuSel + 4) % 5;
            float dir = (in.pressed[KEY_RIGHT] ? 1.0f : 0.0f) - (in.pressed[KEY_LEFT] ? 1.0f : 0.0f);
            if (dir != 0) {
                if (menuSel == 0) settings.volume = clampf(settings.volume + dir * 0.1f, 0, 1);
                if (menuSel == 1) settings.sensitivity = clampf(settings.sensitivity + dir * 0.1f, 0.2f, 2);
                if (menuSel == 2) settings.grain = clampf(settings.grain + dir * 0.1f, 0, 1.5f);
                if (menuSel == 3) settings.subtitles = !settings.subtitles;
                audio.setMasterVolume(settings.volume);
            }
            if (in.pressed[KEY_ESC] || ((in.pressed[KEY_ENTER] || in.pressed[KEY_E]) && menuSel == 4)) {
                settings.save(userDir + "/settings.json");
                screen = pauseReturn;
                menuSel = 0;
                if (pauseReturn == Screen::Playing) eng.setMouseCaptured(true);
            }
            break;
        }
        case Screen::Paused: {
            if (in.pressed[KEY_DOWN] || in.pressed[KEY_S]) menuSel = (menuSel + 1) % 4;
            if (in.pressed[KEY_UP] || in.pressed[KEY_W]) menuSel = (menuSel + 3) % 4;
            if (in.pressed[KEY_ESC]) {
                screen = Screen::Playing;
                eng.setMouseCaptured(true);
            }
            if (in.pressed[KEY_ENTER] || in.pressed[KEY_E]) {
                if (menuSel == 0) {
                    screen = Screen::Playing;
                    eng.setMouseCaptured(true);
                } else if (menuSel == 1) {
                    saveGame();
                    say("Saved.", 1.5f);
                    screen = Screen::Playing;
                    eng.setMouseCaptured(true);
                } else if (menuSel == 2) {
                    pauseReturn = Screen::Playing;
                    screen = Screen::Settings;
                    menuSel = 0;
                } else {
                    saveGame();
                    screen = Screen::Menu;
                    menuSel = 0;
                }
            }
            break;
        }
        case Screen::Reading:
            if (in.pressed[KEY_E] || in.pressed[KEY_ESC]) {
                if (!docsRead.count(readingDoc)) {
                    docsRead.insert(readingDoc);
                    runBeats("doc_read", readingDoc);
                }
                screen = Screen::Playing;
                eng.setMouseCaptured(true);
            }
            break;
        case Screen::Clipboard:
            if (in.pressed[KEY_TAB] || in.pressed[KEY_ESC]) {
                screen = Screen::Playing;
                eng.setMouseCaptured(true);
            }
            if (in.pressed[KEY_C]) {   // G unavailable; C = photographs
                screen = Screen::Gallery;
                gallerySel = (int)photos.size() - 1;
            }
            break;
        case Screen::Gallery:
            if (in.pressed[KEY_LEFT]) gallerySel--;
            if (in.pressed[KEY_RIGHT]) gallerySel++;
            if (in.pressed[KEY_ESC] || in.pressed[KEY_TAB]) {
                screen = Screen::Playing;
                eng.setMouseCaptured(true);
            }
            break;
        case Screen::Monitor: {
            int n = (int)content.cams.size();
            if (n > 0) {
                if (in.pressed[KEY_Q]) monitorCam = (monitorCam + n - 1) % n;
                if (in.pressed[KEY_E]) monitorCam = (monitorCam + 1) % n;
                for (int k = 0; k < std::min(n, 9); ++k)
                    if (in.pressed[(Key)(KEY_1 + k)]) monitorCam = k;
            }
            if (in.pressed[KEY_ESC]) {
                screen = Screen::Playing;
                eng.setMouseCaptured(true);
                playSnd("crt_off", 0.7f);
            }
            break;
        }
        case Screen::Playing:
            tickPlaying(dt);
            if (endingPhase > 0) tickEnding(dt);
            break;
        case Screen::Ending:
            tickEnding(dt);
            break;
        case Screen::Credits:
            tickEnding(dt);
            if (in.pressed[KEY_ENTER] || in.pressed[KEY_ESC]) {
                screen = Screen::Menu;
                menuSel = 0;
            }
            break;
    }
}

// ===================================================================== run ==
int Game::run() {
    double last = eng.timeSeconds();
    int framesLeft = -1;
    std::string finalShot;
    for (auto& f : flags) {
        if (f.rfind("cli_frames:", 0) == 0) framesLeft = std::atoi(f.substr(11).c_str());
        if (f.rfind("cli_shot:", 0) == 0) finalShot = f.substr(9);
    }

    while (eng.pumpEvents()) {
        double now = eng.timeSeconds();
        float dt = (float)clampf((float)(now - last), 0.0f, 0.05f);
        last = now;
        if (headless) dt = 1.0f / 60.0f;   // deterministic headless step

        tick(dt);
        render();

        // sim-triggered screenshots
        for (auto it = flags.begin(); it != flags.end();) {
            if (it->rfind("cli_shot_now:", 0) == 0) {
                eng.saveScreenshot(it->substr(13));
                it = flags.erase(it);
            } else {
                ++it;
            }
        }
        if (framesLeft > 0 && --framesLeft == 0) break;
    }
    if (!finalShot.empty()) eng.saveScreenshot(finalShot);
    settings.save(userDir + "/settings.json");
    audio.shutdown();
    world.destroy();
    eng.shutdown();
    return 0;
}

#ifndef OBSERVER_UNIT_TESTS
int main(int argc, char** argv) {
    Game* game = new Game();
    if (!game->boot(argc, argv)) {
        IRIS_ERROR("boot failed");
        return 1;
    }
    int rc = game->run();
    delete game;
    return rc;
}
#endif
