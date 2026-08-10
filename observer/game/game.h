// THE OBSERVER - game layer on the IRIS engine.
//
// Core loop: Notice -> Doubt -> Investigate -> Observe or Ignore ->
//            Consequence -> Doubt Again.
// Observation makes anomalies more real. The game quietly profiles HOW the
// player is afraid and answers in kind. There is no sanity meter.
#pragma once
#include "iris.h"

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace iris;

// ============================================================== content ====
struct RoomSpec {
    std::string id, name;
    vec3 mn, mx;                    // interior volume, world units (meters)
    std::string wallTex, floorTex, ceilTex;
    float wallUv = 0.5f, floorUv = 0.5f;
    int zone = 0;                   // floor index for streaming/light grouping
    std::vector<std::string> tags;  // "apartment","hallway","basement","mirror",...
};

struct DoorSpec {
    std::string id, label;
    std::string roomA, roomB;
    vec3 pos;                       // center of the doorway on the shared wall
    int axis = 0;                   // 0: wall runs along Z (door faces +-X), 1: along X (faces +-Z)
    bool locked = false, open = false;
    std::string keyId;              // item id that unlocks
    std::string tex = "door_wood";
    bool isThreshold = false;       // opens into darkness; walking in teleports
    std::string thresholdTarget;    // door id we emerge from
};

enum class PropType { Box, Quad, Billboard };

struct PropSpec {
    std::string id;
    PropType type = PropType::Box;
    std::string room;
    vec3 pos, size{1, 1, 1};
    float yaw = 0;
    std::string tex;
    vec4 tint{1, 1, 1, 1};
    float emissive = 0;
    bool collide = true;
    bool mirrorOnly = false;        // only visible in reflections
    bool hidden = false;            // starts invisible (anomaly reveals)
    bool flat = false;              // quad lies on the floor/ceiling plane
    std::string interact;           // "", "pickup:<item>", "read:<doc>", "switch:<id>", ...
    float uvx = 1, uvy = 1;
};

struct LightSpec {
    std::string id, room;
    vec3 pos;
    vec3 color{1.0f, 0.92f, 0.78f};
    float intensity = 1.0f, radius = 6.0f;
    bool on = true;
    float flicker = 0;              // 0 none .. 1 heavy
};

struct CamSpec {
    std::string id, label, room;
    vec3 pos;
    float yaw = 0, pitch = -0.35f;
};

struct MirrorSpec {
    std::string id, room;
    vec3 pos;                       // center of mirror plane
    float yaw = 0;                  // plane normal = yaw rotated -Z ... normal facing room
    vec2 size{0.8f, 1.2f};
};

struct DocumentSpec {
    std::string id, title;
    std::vector<std::string> lines;
    std::string type = "note";      // note | report | tape | terminal
};

// One authored anomaly event template for the director.
struct EventSpec {
    std::string id;
    std::string channel;            // BEHIND MIRROR CCTV DOOR LIGHT PHOTO SOUND SPACE WITNESS
    int minAct = 1, maxAct = 5;
    float cooldown = 90;            // seconds before this template can refire
    float weight = 1;
    bool once = false;
    bool needUnobservedRoom = false;// target room must not be visible
    std::string effect;             // handler name
    std::string room, target;       // effect-specific references
    std::string param;              // effect-specific string param
    float value = 0;                // effect-specific scalar
};

// A beat inside an act script.
struct BeatSpec {
    std::string id;
    std::string trigger;            // time | enter | task_done | observe | doc_read | flag | pickup
    std::string arg;                // room id / task id / doc id / flag name
    float atTime = 0;               // for time trigger: minutes since act start
    std::vector<std::string> actions;   // "task:add:...", "voice:...", "sub:...", "event:...",
                                        // "flag:...", "unlock:...", "witnesscap:N", "act:next", ...
    bool fired = false;
};

struct ActSpec {
    int index = 1;
    std::string title;              // "23:04", shown as a time card
    std::string clock;              // starting clock "23:00"
    float ambientScale = 1.0f;      // darkness ramps down over the night
    float directorInterval = 45;    // seconds between director picks
    int witnessCap = 0;             // max witness stage this act
    std::vector<BeatSpec> beats;
};

struct GameContent {
    std::vector<RoomSpec> rooms;
    std::vector<DoorSpec> doors;
    std::vector<PropSpec> props;
    std::vector<LightSpec> lights;
    std::vector<CamSpec> cams;
    std::vector<MirrorSpec> mirrors;
    std::vector<DocumentSpec> documents;
    std::vector<EventSpec> events;
    std::vector<ActSpec> acts;
    std::map<std::string, std::string> textures;   // logical name -> file path
    std::map<std::string, std::string> sounds;     // logical name -> file path
    std::map<std::string, std::string> strings;    // ui strings
    std::string playerStartRoom;
    vec3 playerStart{0, 0, 0};
    float playerStartYaw = 0;
};

bool loadContent(GameContent& out, const std::string& dir, std::string& err);

// ================================================================ world ====
struct DoorState {
    bool open = false, locked = false;
    float openT = 0;                // 0 closed .. 1 open
    double lastInteract = -100;
    int interactBurst = 0;          // re-check counter for the DOOR channel
};

struct PropState {
    vec3 pos;
    float yaw;
    bool hidden;
    vec4 tint;
    std::string tex;                // live texture (swappable)
    bool moved = false;             // diverged from authored state
};

struct LightState {
    bool on;
    float flicker;
    float flickerPhase = 0;
};

struct WorldGeo;   // opaque (world.cpp)

class World {
public:
    bool build(Engine& eng, const GameContent& c);   // meshes + collision
    void initState(const GameContent& c);            // indexes + live state only (no GPU; for tests)
    void destroy();

    // frame queries
    int roomOf(vec3 p) const;                        // -1 outside
    bool lineOfSight(vec3 a, vec3 b) const;          // vs static geo + closed doors
    float raycast(vec3 ro, vec3 rd, float tMax, int* hitProp = nullptr, int* hitDoor = nullptr) const;
    vec3 resolveCollision(vec3 pos, vec3 half, vec3 vel, float dt, bool* grounded) const;

    // draw list for one view
    void buildDrawList(const GameContent& c, int povRoom, std::vector<DrawItem>& out,
                       std::vector<Light>& lights, bool reflectionPass, float timeSec);

    // live state
    void resetState();              // back to authored content values
    std::vector<DoorState> doors;
    std::vector<PropState> props;
    std::vector<LightState> lights;
    std::unordered_map<std::string, int> roomIdx, doorIdx, propIdx, lightIdx;
    std::unordered_map<std::string, TexId> tex;      // logical -> engine id
    TexId texOf(const std::string& name) const;

    // visibility: rooms connected to pov through open doors (2 hops) + pov
    void visibleRooms(int povRoom, std::vector<int>& out) const;

    MeshId cubeMesh() const;
    MeshId quadMesh() const;        // 1x1 facing +Z, centered
    MeshId billboardMesh() const;   // 1x1 facing +Z, pivot at bottom-center

    const GameContent* content = nullptr;
    Engine* eng = nullptr;
    WorldGeo* geo = nullptr;
};

// ============================================================== systems ====
enum Channel { CH_BEHIND, CH_MIRROR, CH_CCTV, CH_DOOR, CH_LIGHT, CH_PHOTO, CH_SOUND, CH_SPACE,
               CH_WITNESS, CH_COUNT };
Channel channelFromName(const std::string& s);

// Hidden behavioral profile. Never surfaced to the player.
struct ParanoiaProfile {
    float score[CH_COUNT] = {};
    // raw counters
    int behindChecks = 0, doorRechecks = 0, lightToggles = 0, photos = 0;
    float mirrorGaze = 0, cctvTime = 0, corridorTime = 0;
    float weight(Channel c) const;   // normalized 0..1
    void decay(float dt);
};

// One live anomalous thing that can be observed.
struct Observable {
    std::string id;
    vec3 pos;
    float radius = 0.6f;
    int room = -1;
    float manifest = 0;              // grows with attention
    float dwell = 0;                 // current continuous gaze seconds
    double spawnedAt = 0;
    bool active = false;
    bool witness = false;
};

struct ObservationSystem {
    std::vector<Observable> obs;
    float attention = 0;             // global "how much has been made real"
    float gazeBoost(vec3 camPos, vec3 camFwd, const Observable& o) const;
    // returns manifest ticks fired this frame
    void update(float dt, vec3 camPos, vec3 camFwd, const World& world, float fovCos);
    Observable* find(const std::string& id);
    Observable* spawn(const std::string& id, vec3 pos, int room, bool witness = false);
    void despawn(const std::string& id);
};

// The Witness. Stages: 0 absent, 1 silhouette, 2 forming, 3 mimic, 4 copy.
struct WitnessSystem {
    int stage = 0;
    int stageCap = 0;                // act limit
    float knowledge = 0;             // total observation of it
    float photoKnowledge = 0;
    vec3 pos;
    int room = -1;
    bool visible = false;
    double lastSeen = -100, lastAppear = -100, vanishAt = 0;
    float playerGazeOnMe = 0;
    std::string mode = "hidden";     // hidden | distant | stalking | mirror | cctv | confront
    void gainKnowledge(float k);
    int stageForKnowledge() const;
};

struct PhotoRecord {
    std::string file;                // saved png path
    std::string caption;             // develops with... additions
    std::vector<std::string> subjects;
    TexId tex = TEX_INVALID;
    double takenAt = 0;
};

struct PendingCue {
    double at;
    std::string action;              // same action grammar as beats
};

// Director: rule-based paranoia-personalized event scheduler.
struct Director {
    std::unordered_map<std::string, double> lastFired;
    std::unordered_set<std::string> spent;   // once-events already used
    double nextPickAt = 0;
    Rng rng;
    const EventSpec* pick(const GameContent& c, int act, const ParanoiaProfile& prof,
                          const World& world, int playerRoom, double now,
                          const std::function<bool(const EventSpec&)>& extraFilter);
};

// =============================================================== UI/game ===
enum class Screen { Boot, Menu, Playing, Paused, Reading, Clipboard, Gallery, Settings, Monitor,
                    Ending, Credits };

struct TaskItem {
    std::string id, text;
    bool done = false;
};

struct Subtitle {
    std::string text;
    double until;
    vec4 color{1, 1, 1, 1};
};

struct Settings {
    float volume = 0.8f;
    float sensitivity = 1.0f;
    float grain = 1.0f;             // multiplier on post grain
    bool subtitles = true;
    bool fullscreen = false;
    int width = 1600, height = 900;
    void load(const std::string& path);
    void save(const std::string& path) const;
};

struct Game {
    Engine eng;
    AudioEngine audio;
    GameContent content;
    World world;
    Font* font = nullptr;
    Font* fontBig = nullptr;
    Font* fontMono = nullptr;

    Settings settings;
    std::string userDir;

    // player
    vec3 pPos;
    float pYaw = 0, pPitch = 0;
    vec3 pVel;
    bool grounded = true, crouching = false;
    float bobPhase = 0, bobAmp = 0;
    int playerRoom = -1;
    bool flashlight = false;
    float flashFlicker = 1.0f;
    double lastFootstep = 0;

    // camera device
    bool viewfinder = false;
    int photoCount = 0;
    std::vector<PhotoRecord> photos;
    double lastPhotoAt = -10;
    float flashWhite = 0;

    // inventory + flags
    std::unordered_set<std::string> items;
    std::unordered_set<std::string> flags;
    std::unordered_set<std::string> docsRead;

    // systems
    ObservationSystem observation;
    ParanoiaProfile profile;
    WitnessSystem witness;
    Director director;
    std::vector<PendingCue> cues;

    // acts
    int act = 1;
    double actStartedAt = 0;
    std::vector<TaskItem> tasks;
    double clockBase = 23 * 60;     // in-game minutes at act start
    double worldTime = 0;           // seconds since shift start (game speed)

    // CCTV
    std::vector<RtId> camRts;
    int monitorCam = 0;
    double lastCamRender = 0;
    int camRenderCursor = 0;

    // mirrors
    std::vector<RtId> mirrorRts;

    // presentation
    Screen screen = Screen::Boot;
    Screen pauseReturn = Screen::Playing;
    int menuSel = 0;
    std::string readingDoc;
    int gallerySel = 0;
    std::deque<Subtitle> subs;
    float fade = 1.0f;              // starts black
    float fadeTarget = 0;
    float fearLevel = 0;            // drives post warp/pulse; hidden
    float noiseBurst = 0;           // transient full-frame static
    double endingStartedAt = 0;
    int endingPhase = 0;
    std::string endingFlavor;

    // yaw history for behind-check detection
    std::deque<std::pair<double, float>> yawHistory;

    // headless / sim
    bool headless = false;
    std::string simScript;          // tiny command list for CI screenshots
    std::vector<std::pair<double, std::string>> simCmds;
    size_t simCursor = 0;
    double simTime = 0;

    // audio handles
    std::unordered_map<std::string, SoundId> snd;
    VoiceId ambientVoice = -1, droneVoice = -1, phoneVoice = -1;
    std::string ambientCurrent;

    // title cards ("23:02 — HALCYON COURT")
    std::string titleCard;
    double titleCardUntil = 0;

    // ---- lifecycle (main.cpp / script.cpp / systems.cpp / ui.cpp)
    bool boot(int argc, char** argv);
    int run();
    void newGame();
    bool saveGame();
    bool loadGame();

    // per-frame
    void tick(float dt);
    void tickPlaying(float dt);
    void render();
    void renderWorldViews();
    void renderUi();

    // subsystem hooks
    void updatePlayer(float dt);
    void updateObservation(float dt);
    void updateWitness(float dt);
    void updateDirector(float dt);
    void updateAudioBeds();
    void execAction(const std::string& action);
    void runBeats(const std::string& trigger, const std::string& arg);
    void interact();
    void takePhoto();
    void toggleDoor(int door);
    void applyEvent(const EventSpec& ev);
    void say(const std::string& text, float seconds = 3.5f, vec4 color = {1, 1, 1, 1});
    void playSnd(const std::string& name, float vol = 1, bool loop = false, vec3 pos = {},
                 bool spatial = false);
    void startEnding(const std::string& flavor);
    void tickEnding(float dt);
    std::string clockString() const;
    void enterAct(int a);

    // ui.cpp
    void uiMenu();
    void uiHud();
    void uiClipboard();
    void uiReading();
    void uiGallery();
    void uiSettings();
    void uiMonitor();
    void uiSubtitles();
    void uiEnding();
};

// implemented in script.cpp
void gameRegisterEffects();
