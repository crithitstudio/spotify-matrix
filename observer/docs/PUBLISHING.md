# THE OBSERVER — Publishing Runbook

Everything in this repo builds store-ready packages. The only steps that
cannot be automated from here are the ones bound to *your* accounts (Steam
partner fee, app review, butler credentials). This runbook is the exact
click-path for those.

## 1. Build the packages

Linux (any machine with Vulkan headers + glslang):

```sh
cd observer
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/observer_tests                # must print ALL TESTS PASSED
bash tools/package.sh linux-x64       # -> dist/TheObserver-linux-x64.tar.gz
```

Windows (Visual Studio 2022 or newer):

```powershell
cd observer
cmake -B build
cmake --build build --config Release
build\Release\observer_tests.exe
bash tools/package.sh windows-x64 build/Release   # git-bash; -> dist/...zip
```

CI produces both automatically for every push
(`.github/workflows/observer-ci.yml`); download them from the run's
artifacts if you'd rather not build locally.

## 2. Steam

1. **Partner account** — <https://partner.steamgames.com>, one-time
   $100 app credit, tax/banking interview. Allow a few days.
2. **Create the app** — you receive an AppID. Two depots by convention:
   AppID+1 (Windows), AppID+2 (Linux). Create both under
   *SteamPipe ▸ Depots*.
3. **Launch options** (Installation ▸ General):
   - Windows: executable `TheObserver.exe`, OS Windows.
   - Linux: executable `TheObserver`, OS Linux + SteamOS.
4. **Store assets** — everything is pre-cut in `assets/store/`:

   | Steamworks slot            | File                                  |
   |----------------------------|---------------------------------------|
   | Header capsule 460×215     | `header_460x215.png`                  |
   | Small capsule 231×87       | `small_capsule_231x87.png`            |
   | Main capsule 616×353       | `main_capsule_616x353.png`            |
   | Vertical capsule 748×896   | `vertical_capsule_748x896.png`        |
   | Library capsule 600×900    | `library_600x900.png`                 |
   | Library hero 3840×1240     | `hero_3840x1240.png`                  |
   | Screenshots (min 5)        | capture in-game (F12 saves a PNG next to the binary) |

5. **Store copy** — short description:
   > *A night shift. An empty building. A figure that becomes more real the
   > longer you look at it. THE OBSERVER is a psychological horror game
   > about attention — the one resource you can't stop spending.*
   Mature content survey: psychological horror, no gore, no sexual content.
6. **Upload** — edit `packaging/steam/app_build_observer.vdf`
   (AppID + depot IDs), then:
   ```sh
   packaging/steam/push_build.sh your_steam_login
   ```
   Set the build live under *SteamPipe ▸ Builds*.
7. **Review** — store page review and build review are separate; request
   both. First release requires ~2 weeks of "coming soon" visibility.

## 3. itch.io (the "and co.")

```sh
butler login
packaging/itch/push.sh yourname/the-observer 0.9.0
```

Page assets: `keyart_vertical.png` (cover 630×1000 — crop from the master),
`keyart_horizontal.png` for the banner.

## 4. Versioning

`CMakeLists.txt` carries the version (`project(TheObserver VERSION …)`).
Tag releases `observer-vX.Y.Z`; CI artifacts are the canonical builds.

## 5. Pre-flight checklist

- [ ] `observer_tests` green on both platforms
- [ ] `iris_smoke` renders on lavapipe (CI does this)
- [ ] Fresh-profile playthrough: intro → act 2 without touching a save
- [ ] Save/Continue round-trip mid-act 3
- [ ] Ending reachable via act 5 (`--newgame` + `do:act:5` sim if needed)
- [ ] Settings persist across restart
- [ ] Package runs from a clean directory with no dev files present
