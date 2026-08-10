// THE OBSERVER - logic unit tests. No GPU required: world runs in
// logic-only mode (initState), so these gate CI on any machine.
#include "game.h"

#include <cstdio>

static int failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);    \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static std::string dataDir() {
    for (const char* d : {"content", "../content", "../../content"})
        if (fileExists(std::string(d) + "/building.json")) return d;
    return "content";
}

// ---------------------------------------------------------------- math -----
static void testMath() {
    // ray vs box
    AABB b;
    b.expand({-1, -1, -1});
    b.expand({1, 1, 1});
    float t;
    CHECK(rayAABB({0, 0, -5}, {0, 0, 1}, b, 100, &t) && std::fabs(t - 4) < 1e-4);
    CHECK(!rayAABB({0, 0, -5}, {0, 0, -1}, b, 100, &t));
    CHECK(!rayAABB({0, 5, -5}, {0, 0, 1}, b, 100, &t));

    // frustum contains what the camera looks at
    mat4 view = camView({0, 0, 0}, 0, 0);   // looking down -Z
    mat4 proj = perspectiveVk(radians(70), 16.0f / 9.0f, 0.1f, 50);
    Frustum f = Frustum::fromViewProj(proj * view);
    CHECK(f.containsPoint({0, 0, -5}));
    CHECK(!f.containsPoint({0, 0, 5}));
    CHECK(!f.containsPoint({0, 0, -60}));
    CHECK(f.intersectsSphere({8, 0, -10}, 3.0f));

    // wrapAngle stays in [-pi, pi]
    for (float a = -20; a < 20; a += 0.37f) {
        float w = wrapAngle(a);
        CHECK(w >= -PI - 1e-4 && w <= PI + 1e-4);
    }
    // inverse sanity
    mat4 m = mat4::translate({3, -2, 7}) * mat4::rotateY(0.8f) * mat4::scale({2, 2, 2});
    vec3 p{1.5f, 0.5f, -2.0f};
    vec3 rt = inverse(m).transformPoint(m.transformPoint(p));
    CHECK(distance(rt, p) < 1e-3f);
}

// -------------------------------------------------------------- content ----
static void testContent(GameContent& c) {
    std::string err;
    CHECK(loadContent(c, dataDir(), err));
    if (!err.empty()) std::printf("  content error: %s\n", err.c_str());
    CHECK(c.rooms.size() >= 18);
    CHECK(c.acts.size() == 5);
    CHECK(c.events.size() >= 35);
    CHECK(c.documents.size() >= 15);

    std::unordered_set<std::string> roomIds, doorIds, propIds, lightIds, docIds, evIds;
    for (auto& r : c.rooms) {
        CHECK(!roomIds.count(r.id));   // unique
        roomIds.insert(r.id);
        CHECK(r.mx.x > r.mn.x && r.mx.y > r.mn.y && r.mx.z > r.mn.z);
        CHECK(c.textures.count(r.wallTex));
        CHECK(c.textures.count(r.floorTex));
        CHECK(c.textures.count(r.ceilTex));
    }
    for (auto& d : c.doors) {
        CHECK(!doorIds.count(d.id));
        doorIds.insert(d.id);
        CHECK(roomIds.count(d.roomA));
        CHECK(roomIds.count(d.roomB));
        CHECK(c.textures.count(d.tex));
    }
    for (auto& d : c.doors)
        if (d.isThreshold) CHECK(doorIds.count(d.thresholdTarget));
    for (auto& p : c.props) {
        CHECK(!propIds.count(p.id));
        propIds.insert(p.id);
        CHECK(roomIds.count(p.room));
        if (!p.tex.empty()) CHECK(c.textures.count(p.tex));
    }
    for (auto& l : c.lights) {
        CHECK(!lightIds.count(l.id));
        lightIds.insert(l.id);
        CHECK(roomIds.count(l.room));
    }
    for (auto& cam : c.cams) CHECK(roomIds.count(cam.room));
    for (auto& m : c.mirrors) CHECK(roomIds.count(m.room));
    for (auto& doc : c.documents) {
        CHECK(!docIds.count(doc.id));
        docIds.insert(doc.id);
        CHECK(!doc.lines.empty());
    }
    // every readable prop points at a real document
    for (auto& p : c.props)
        if (p.interact.rfind("read:", 0) == 0) CHECK(docIds.count(p.interact.substr(5)));
    // every event references real things
    for (auto& e : c.events) {
        CHECK(!evIds.count(e.id));
        evIds.insert(e.id);
        if (!e.room.empty()) CHECK(roomIds.count(e.room));
        if (e.effect.rfind("prop_", 0) == 0 || e.effect == "writing_reveal")
            CHECK(propIds.count(e.target));
        if (e.effect.rfind("door_", 0) == 0) CHECK(doorIds.count(e.target));
        if (e.effect == "light_flicker") CHECK(lightIds.count(e.target));
        if (e.effect == "corridor_loop") CHECK(doorIds.count(e.target));
    }
    // beats: every referenced forced event exists; props in prop: actions exist
    for (auto& a : c.acts)
        for (auto& bt : a.beats)
            for (auto& action : bt.actions) {
                if (action.rfind("event:", 0) == 0) CHECK(evIds.count(action.substr(6)));
                if (action.rfind("prop:", 0) == 0) {
                    std::string rest = action.substr(5);
                    CHECK(propIds.count(rest.substr(0, rest.find(':'))));
                }
                if (action.rfind("teleport:", 0) == 0) CHECK(roomIds.count(action.substr(9)));
            }
    // player start valid
    CHECK(roomIds.count(c.playerStartRoom));
    // witness caps rise monotonically and reach 4
    int cap = 0;
    for (auto& a : c.acts) {
        CHECK(a.witnessCap >= cap);
        cap = a.witnessCap;
    }
    CHECK(cap == 4);
}

// -------------------------------------------------------------- director ---
static void testDirector(GameContent& c) {
    World w;
    w.initState(c);

    ParanoiaProfile mirrorLover;
    mirrorLover.score[CH_MIRROR] = 10;
    Director dir;
    dir.rng.seed(42);
    int mirrorPicks = 0, total = 0;
    std::unordered_map<std::string, int> perChannel;
    for (int i = 0; i < 400; ++i) {
        dir.lastFired.clear();   // ignore cooldowns for the statistic
        dir.spent.clear();
        const EventSpec* ev = dir.pick(c, 3, mirrorLover, w, 0, i * 1000.0, nullptr);
        if (!ev) continue;
        total++;
        perChannel[ev->channel]++;
        if (ev->channel == "MIRROR") mirrorPicks++;
    }
    CHECK(total > 300);
    // a mirror-obsessed profile must get disproportionately many mirror events
    int maxOther = 0;
    for (auto& [ch, n] : perChannel)
        if (ch != "MIRROR") maxOther = std::max(maxOther, n);
    CHECK(mirrorPicks * 2 > maxOther);

    // act gating: act 1 must never produce a stage-capable witness stalk
    ParanoiaProfile flat;
    for (int i = 0; i < 200; ++i) {
        dir.lastFired.clear();
        const EventSpec* ev = dir.pick(c, 1, flat, w, 0, i * 1000.0, nullptr);
        if (ev) CHECK(ev->minAct <= 1 && 1 <= ev->maxAct);
    }

    // cooldowns: the same event cannot fire twice inside its window
    Director dir2;
    dir2.rng.seed(7);
    std::unordered_map<std::string, double> lastSeen;
    for (int i = 0; i < 300; ++i) {
        double now = i * 5.0;
        const EventSpec* ev = dir2.pick(c, 3, flat, w, 0, now, nullptr);
        if (!ev) continue;
        auto it = lastSeen.find(ev->id);
        if (it != lastSeen.end()) CHECK(now - it->second >= ev->cooldown - 1e-6);
        lastSeen[ev->id] = now;
    }

    // once-events never repeat
    Director dir3;
    dir3.rng.seed(9);
    std::unordered_set<std::string> onceSeen;
    for (int i = 0; i < 500; ++i) {
        const EventSpec* ev = dir3.pick(c, 4, flat, w, 0, i * 1000.0, nullptr);
        if (ev && ev->once) {
            CHECK(!onceSeen.count(ev->id));
            onceSeen.insert(ev->id);
        }
    }
}

// ------------------------------------------------------------ observation --
static void testObservation(GameContent& c) {
    World w;
    w.initState(c);
    ObservationSystem obs;
    obs.spawn("thing", {0, 1.5f, -5}, 0);

    // staring at it makes it real
    for (int i = 0; i < 240; ++i)
        obs.update(1 / 60.0f, {0, 1.5f, 0}, {0, 0, -1}, w, 0.9f);
    float focused = obs.find("thing")->manifest;
    CHECK(focused > 1.0f);

    // looking away does nothing
    ObservationSystem obs2;
    obs2.spawn("thing", {0, 1.5f, -5}, 0);
    for (int i = 0; i < 240; ++i)
        obs2.update(1 / 60.0f, {0, 1.5f, 0}, {0, 0, 1}, w, 0.9f);
    CHECK(obs2.find("thing")->manifest < 0.01f);

    // peripheral glance is weaker than a stare
    ObservationSystem obs3;
    obs3.spawn("thing", {3.4f, 1.5f, -5}, 0);
    for (int i = 0; i < 240; ++i)
        obs3.update(1 / 60.0f, {0, 1.5f, 0}, {0, 0, -1}, w, 0.9f);
    CHECK(obs3.find("thing")->manifest < focused);
}

// ---------------------------------------------------------------- witness --
static void testWitness() {
    WitnessSystem w;
    w.stageCap = 4;
    CHECK(w.stageForKnowledge() == 0);
    w.gainKnowledge(3);
    CHECK(w.stage == 1);
    w.gainKnowledge(10);
    CHECK(w.stage == 2);
    w.gainKnowledge(30);
    CHECK(w.stage >= 3);
    // photos accelerate it
    WitnessSystem w2;
    w2.stageCap = 4;
    w2.photoKnowledge = 10;   // 16 effective
    w2.gainKnowledge(0.1f);
    CHECK(w2.stage == 2);
    // cap holds it back no matter the knowledge
    WitnessSystem w3;
    w3.stageCap = 1;
    w3.gainKnowledge(500);
    CHECK(w3.stage == 1);
}

// ---------------------------------------------------------------- profile --
static void testProfile() {
    ParanoiaProfile p;
    for (int c2 = 0; c2 < CH_COUNT; ++c2) CHECK(p.weight((Channel)c2) < 0.51f);
    p.score[CH_BEHIND] = 8;
    CHECK(p.weight(CH_BEHIND) > 0.9f);
    CHECK(p.weight(CH_MIRROR) < 0.1f);
    float before = p.score[CH_BEHIND];
    p.decay(10);
    CHECK(p.score[CH_BEHIND] < before);
    CHECK(channelFromName("MIRROR") == CH_MIRROR);
    CHECK(channelFromName("nonsense") == CH_SOUND);
}

// -------------------------------------------------------------- building ---
static void testBuildingGraph(GameContent& c) {
    World w;
    w.initState(c);
    // closed doors: only the starting room is visible
    std::vector<int> vis;
    int lobby = w.roomIdx.at("lobby");
    for (auto& d : w.doors) d.openT = 0;
    w.visibleRooms(lobby, vis);
    CHECK(vis.size() == 1 && vis[0] == lobby);
    // opening the security door exposes exactly one more room
    w.doors[w.doorIdx.at("d_sec")].openT = 1;
    w.visibleRooms(lobby, vis);
    CHECK(vis.size() == 2);
    // roomOf finds rooms by position (logic mode has no geometry, use volumes)
    const RoomSpec& r = c.rooms[lobby];
    vec3 center = (r.mn + r.mx) * 0.5f;
    CHECK(w.roomOf(center) == lobby);
    CHECK(w.roomOf({999, 0, 999}) == -1);
}

int main() {
    testMath();
    GameContent c;
    testContent(c);
    if (failures == 0) {
        testDirector(c);
        testObservation(c);
        testBuildingGraph(c);
    }
    testWitness();
    testProfile();
    if (failures) {
        std::printf("TESTS FAILED: %d\n", failures);
        return 1;
    }
    std::printf("ALL TESTS PASSED\n");
    return 0;
}
