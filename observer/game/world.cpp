// THE OBSERVER - world: procedural building geometry, collision, doors.
#include "game.h"

namespace {
constexpr float WT = 0.06f;         // wall inset (half thickness)
constexpr float DOOR_W = 1.02f;
constexpr float DOOR_H = 2.06f;

struct RoomMeshes {
    MeshId walls = -1, floor = -1, ceil = -1;
};

struct Collider {
    AABB box;
    int prop = -1;   // live prop index, or
    int door = -1;   // live door index (leaf/doorway), or static wall (-1,-1)
    bool doorway = false;   // interaction volume, not solid
};
} // namespace

struct WorldGeo {
    std::vector<RoomMeshes> roomMeshes;
    std::vector<AABB> statics;              // walls + floors, always solid
    std::vector<Collider> doorVolumes;      // doorway interaction volumes
    MeshId doorLeaf = -1;                   // 1x1 unit, hinge at x=0, spans +x
    MeshId cube = -1, quad = -1, billboard = -1;
};

MeshId World::cubeMesh() const { return geo->cube; }
MeshId World::quadMesh() const { return geo->quad; }
MeshId World::billboardMesh() const { return geo->billboard; }

// ---------------------------------------------------------------- meshes ---
static void addQuad(std::vector<Vertex>& v, std::vector<uint32_t>& idx, vec3 a, vec3 b, vec3 c,
                    vec3 d, vec3 n, float uvScale, vec4 col, int uvAxisA = -1) {
    // uv from world coords: pick two axes not aligned with normal
    auto uv = [&](vec3 p) -> vec2 {
        if (std::fabs(n.y) > 0.9f) return {p.x * uvScale, p.z * uvScale};
        if (std::fabs(n.x) > 0.9f) return {p.z * uvScale, -p.y * uvScale};
        return {p.x * uvScale, -p.y * uvScale};
    };
    uint32_t base = (uint32_t)v.size();
    v.push_back({a, n, uv(a), col});
    v.push_back({b, n, uv(b), col});
    v.push_back({c, n, uv(c), col});
    v.push_back({d, n, uv(d), col});
    for (uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + t);
    (void)uvAxisA;
}

// Wall on plane `axis` (0:x fixed, 1:z fixed) at coordinate `fix`, spanning
// [t0,t1] with holes; normal points toward `inside` (+1/-1).
struct Hole { float c, w, h; };
static void addWall(std::vector<Vertex>& v, std::vector<uint32_t>& idx, int axis, float fix, float t0,
                    float t1, float y0, float y1, float inside, std::vector<Hole> holes,
                    float uvScale) {
    std::sort(holes.begin(), holes.end(), [](const Hole& a, const Hole& b) { return a.c < b.c; });
    vec3 n = axis == 0 ? vec3{inside, 0, 0} : vec3{0, 0, inside};
    auto P = [&](float t, float y) {
        return axis == 0 ? vec3{fix, y, t} : vec3{t, y, fix};
    };
    // vertex color: subtle vertical gradient (dark ceiling line, grounded base)
    auto emit = [&](float a, float b, float yb, float yt) {
        if (b - a < 0.01f || yt - yb < 0.01f) return;
        vec4 cTop{0.62f, 0.62f, 0.66f, 1};
        vec4 cBot{0.85f, 0.85f, 0.85f, 1};
        uint32_t base = (uint32_t)v.size();
        auto uv = [&](float t, float y) { return vec2{t * uvScale, -y * uvScale}; };
        float kb = (yb - y0) / (y1 - y0), kt = (yt - y0) / (y1 - y0);
        vec4 colB = {lerpf(cBot.x, cTop.x, kb), lerpf(cBot.y, cTop.y, kb), lerpf(cBot.z, cTop.z, kb), 1};
        vec4 colT = {lerpf(cBot.x, cTop.x, kt), lerpf(cBot.y, cTop.y, kt), lerpf(cBot.z, cTop.z, kt), 1};
        v.push_back({P(a, yb), n, uv(a, yb), colB});
        v.push_back({P(b, yb), n, uv(b, yb), colB});
        v.push_back({P(b, yt), n, uv(b, yt), colT});
        v.push_back({P(a, yt), n, uv(a, yt), colT});
        bool flip = inside < 0;
        if (axis == 1) flip = !flip;
        if (!flip)
            for (uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + t);
        else
            for (uint32_t t : {0u, 2u, 1u, 0u, 3u, 2u}) idx.push_back(base + t);
    };
    float cursor = t0;
    for (auto& h : holes) {
        float l = h.c - h.w * 0.5f, r = h.c + h.w * 0.5f;
        emit(cursor, l, y0, y1);
        emit(l, r, y0 + h.h, y1);   // lintel
        cursor = r;
    }
    emit(cursor, t1, y0, y1);
}

// ----------------------------------------------------------------- build ---
static MeshId buildUnitCube(Engine& eng) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const vec3 u[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    for (int f = 0; f < 6; ++f) {
        vec3 N = n[f], U = u[f], V = cross(N, U);
        uint32_t base = (uint32_t)v.size();
        for (int i = 0; i < 4; ++i) {
            float su = (i == 1 || i == 2) ? 0.5f : -0.5f;
            float sv = (i >= 2) ? 0.5f : -0.5f;
            vec3 p = N * 0.5f + U * su + V * sv;
            v.push_back({p, N, {su + 0.5f, sv + 0.5f}, {1, 1, 1, 1}});
        }
        for (uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + t);
    }
    return eng.createMesh(v, idx);
}

static MeshId buildQuad(Engine& eng, bool bottomPivot) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    float y0 = bottomPivot ? 0.f : -0.5f;
    float y1 = bottomPivot ? 1.f : 0.5f;
    vec3 n{0, 0, 1};
    v.push_back({{-0.5f, y0, 0}, n, {0, 1}, {1, 1, 1, 1}});
    v.push_back({{0.5f, y0, 0}, n, {1, 1}, {1, 1, 1, 1}});
    v.push_back({{0.5f, y1, 0}, n, {1, 0}, {1, 1, 1, 1}});
    v.push_back({{-0.5f, y1, 0}, n, {0, 0}, {1, 1, 1, 1}});
    for (uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(t);
    // back face so figures read from both sides
    uint32_t b = (uint32_t)v.size();
    for (int i = 0; i < 4; ++i) {
        Vertex vv = v[i];
        vv.nrm = {0, 0, -1};
        v.push_back(vv);
    }
    for (uint32_t t : {0u, 2u, 1u, 0u, 3u, 2u}) idx.push_back(b + t);
    return eng.createMesh(v, idx);
}

static MeshId buildDoorLeaf(Engine& eng) {
    // hinge at origin, panel spans x 0..1, y 0..1, thin z
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    float zt = 0.02f;
    auto face = [&](vec3 a, vec3 b, vec3 c, vec3 d, vec3 n, vec2 ua, vec2 ub, vec2 uc, vec2 ud) {
        uint32_t base = (uint32_t)v.size();
        v.push_back({a, n, ua, {1, 1, 1, 1}});
        v.push_back({b, n, ub, {1, 1, 1, 1}});
        v.push_back({c, n, uc, {1, 1, 1, 1}});
        v.push_back({d, n, ud, {1, 1, 1, 1}});
        for (uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + t);
    };
    face({0, 0, zt}, {1, 0, zt}, {1, 1, zt}, {0, 1, zt}, {0, 0, 1}, {0, 1}, {1, 1}, {1, 0}, {0, 0});
    face({1, 0, -zt}, {0, 0, -zt}, {0, 1, -zt}, {1, 1, -zt}, {0, 0, -1}, {0, 1}, {1, 1}, {1, 0}, {0, 0});
    face({1, 0, zt}, {1, 0, -zt}, {1, 1, -zt}, {1, 1, zt}, {1, 0, 0}, {0.98f, 1}, {1, 1}, {1, 0}, {0.98f, 0});
    return eng.createMesh(v, idx);
}

static TexId placeholderTexture(Engine& eng, const std::string& name) {
    // deterministic tinted noise so missing assets are visible but not ugly
    uint32_t hsh = 2166136261u;
    for (char c : name) hsh = (hsh ^ (uint8_t)c) * 16777619u;
    Rng rng;
    rng.seed(hsh);
    float baseR = 0.25f + 0.5f * rng.uniform();
    float baseG = 0.25f + 0.5f * rng.uniform();
    float baseB = 0.25f + 0.5f * rng.uniform();
    std::vector<uint8_t> px(64 * 64 * 4);
    for (int i = 0; i < 64 * 64; ++i) {
        float n = 0.85f + 0.3f * rng.uniform();
        px[i * 4 + 0] = (uint8_t)clampf(baseR * n * 255, 0, 255);
        px[i * 4 + 1] = (uint8_t)clampf(baseG * n * 255, 0, 255);
        px[i * 4 + 2] = (uint8_t)clampf(baseB * n * 255, 0, 255);
        px[i * 4 + 3] = 255;
    }
    return eng.createTexture(px.data(), 64, 64);
}

TexId World::texOf(const std::string& name) const {
    auto it = tex.find(name);
    return it == tex.end() ? TEX_WHITE : it->second;
}

void World::initState(const GameContent& c) {
    content = &c;
    roomIdx.clear();
    doorIdx.clear();
    propIdx.clear();
    lightIdx.clear();
    for (size_t i = 0; i < c.rooms.size(); ++i) roomIdx[c.rooms[i].id] = (int)i;
    for (size_t i = 0; i < c.doors.size(); ++i) doorIdx[c.doors[i].id] = (int)i;
    for (size_t i = 0; i < c.props.size(); ++i) propIdx[c.props[i].id] = (int)i;
    for (size_t i = 0; i < c.lights.size(); ++i) lightIdx[c.lights[i].id] = (int)i;

    doors.resize(c.doors.size());
    props.resize(c.props.size());
    lights.resize(c.lights.size());
    resetState();
    for (size_t i = 0; i < c.lights.size(); ++i)
        lights[i].flickerPhase = (float)(hash32((uint32_t)i) % 628) / 100.0f;
}

bool World::build(Engine& engine, const GameContent& c) {
    eng = &engine;
    content = &c;
    geo = new WorldGeo();

    // textures
    for (auto& [name, path] : c.textures) {
        TexId id = TEX_INVALID;
        if (fileExists(path)) id = engine.loadTexture(path);
        if (id == TEX_INVALID || id == TEX_WHITE) id = placeholderTexture(engine, name);
        tex[name] = id;
    }

    geo->cube = buildUnitCube(engine);
    geo->quad = buildQuad(engine, false);
    geo->billboard = buildQuad(engine, true);
    geo->doorLeaf = buildDoorLeaf(engine);

    initState(c);

    // ---- per-room geometry
    geo->roomMeshes.resize(c.rooms.size());
    for (size_t ri = 0; ri < c.rooms.size(); ++ri) {
        const RoomSpec& r = c.rooms[ri];
        std::vector<Vertex> wv, fv, cv;
        std::vector<uint32_t> wi, fi, ci;
        float x0 = r.mn.x + WT, x1 = r.mx.x - WT;
        float z0 = r.mn.z + WT, z1 = r.mx.z - WT;
        float y0 = r.mn.y, y1 = r.mx.y;

        // find holes per wall
        auto holesFor = [&](int axis, float fix) {
            std::vector<Hole> hs;
            for (size_t di = 0; di < c.doors.size(); ++di) {
                const DoorSpec& d = c.doors[di];
                if (d.roomA != r.id && d.roomB != r.id) continue;
                if (d.axis != axis) continue;
                float dfix = axis == 0 ? d.pos.x : d.pos.z;
                float wallCoord = axis == 0 ? (std::fabs(fix - x0) < std::fabs(fix - x1) ? r.mn.x : r.mx.x)
                                            : (std::fabs(fix - z0) < std::fabs(fix - z1) ? r.mn.z : r.mx.z);
                if (std::fabs(dfix - wallCoord) > 0.35f) continue;
                hs.push_back({axis == 0 ? d.pos.z : d.pos.x, DOOR_W, DOOR_H});
            }
            return hs;
        };

        addWall(wv, wi, 0, x0, z0, z1, y0, y1, +1, holesFor(0, x0), r.wallUv);
        addWall(wv, wi, 0, x1, z0, z1, y0, y1, -1, holesFor(0, x1), r.wallUv);
        addWall(wv, wi, 1, z0, x0, x1, y0, y1, +1, holesFor(1, z0), r.wallUv);
        addWall(wv, wi, 1, z1, x0, x1, y0, y1, -1, holesFor(1, z1), r.wallUv);

        addQuad(fv, fi, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, 1, 0}, r.floorUv,
                {0.9f, 0.9f, 0.9f, 1});
        addQuad(cv, ci, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {0, -1, 0}, 0.4f,
                {0.55f, 0.55f, 0.58f, 1});

        RoomMeshes rm;
        rm.walls = engine.createMesh(wv, wi);
        rm.floor = engine.createMesh(fv, fi);
        rm.ceil = engine.createMesh(cv, ci);
        geo->roomMeshes[ri] = rm;

        // ---- static colliders: wall segments (with door gaps)
        auto wallColliders = [&](int axis, float innerFix, float outerFix, float t0, float t1) {
            std::vector<Hole> hs = holesFor(axis, innerFix);
            std::sort(hs.begin(), hs.end(), [](const Hole& a, const Hole& b) { return a.c < b.c; });
            float lo = std::fmin(innerFix, outerFix), hi = std::fmax(innerFix, outerFix);
            float cursor = t0;
            auto solid = [&](float a, float b) {
                if (b - a < 0.02f) return;
                AABB box;
                if (axis == 0) {
                    box.mn = {lo, y0, a};
                    box.mx = {hi, y1, b};
                } else {
                    box.mn = {a, y0, lo};
                    box.mx = {b, y1, hi};
                }
                geo->statics.push_back(box);
            };
            for (auto& h : hs) {
                solid(cursor, h.c - h.w * 0.5f);
                cursor = h.c + h.w * 0.5f;
            }
            solid(cursor, t1);
        };
        wallColliders(0, x0, r.mn.x - WT, r.mn.z, r.mx.z);
        wallColliders(0, x1, r.mx.x + WT, r.mn.z, r.mx.z);
        wallColliders(1, z0, r.mn.z - WT, r.mn.x, r.mx.x);
        wallColliders(1, z1, r.mx.z + WT, r.mn.x, r.mx.x);
        // floor slab
        geo->statics.push_back({{r.mn.x, y0 - 0.3f, r.mn.z}, {r.mx.x, y0, r.mx.z}});
        // ceiling slab (keeps photos/rays inside)
        geo->statics.push_back({{r.mn.x, y1, r.mn.z}, {r.mx.x, y1 + 0.3f, r.mx.z}});
    }

    // doorway interaction volumes
    for (size_t di = 0; di < c.doors.size(); ++di) {
        const DoorSpec& d = c.doors[di];
        Collider cv2;
        vec3 p = d.pos;
        if (d.axis == 0)
            cv2.box = {{p.x - 0.35f, p.y, p.z - DOOR_W * 0.5f}, {p.x + 0.35f, p.y + DOOR_H, p.z + DOOR_W * 0.5f}};
        else
            cv2.box = {{p.x - DOOR_W * 0.5f, p.y, p.z - 0.35f}, {p.x + DOOR_W * 0.5f, p.y + DOOR_H, p.z + 0.35f}};
        cv2.door = (int)di;
        cv2.doorway = true;
        geo->doorVolumes.push_back(cv2);
    }
    return true;
}

void World::destroy() {
    delete geo;
    geo = nullptr;
}

// --------------------------------------------------------------- queries ---
int World::roomOf(vec3 p) const {
    for (size_t i = 0; i < content->rooms.size(); ++i) {
        const RoomSpec& r = content->rooms[i];
        if (p.x >= r.mn.x - 0.1f && p.x <= r.mx.x + 0.1f && p.y >= r.mn.y - 0.4f &&
            p.y <= r.mx.y + 0.4f && p.z >= r.mn.z - 0.1f && p.z <= r.mx.z + 0.1f)
            return (int)i;
    }
    return -1;
}

static AABB doorLeafBox(const DoorSpec& d) {
    vec3 p = d.pos;
    if (d.axis == 0)
        return {{p.x - 0.06f, p.y, p.z - DOOR_W * 0.5f}, {p.x + 0.06f, p.y + DOOR_H, p.z + DOOR_W * 0.5f}};
    return {{p.x - DOOR_W * 0.5f, p.y, p.z - 0.06f}, {p.x + DOOR_W * 0.5f, p.y + DOOR_H, p.z + 0.06f}};
}

bool World::lineOfSight(vec3 a, vec3 b) const {
    if (!geo) return true;   // logic-only mode (unit tests)
    vec3 d = b - a;
    float len = length(d);
    if (len < 1e-4f) return true;
    d = d / len;
    for (const AABB& box : geo->statics) {
        float t;
        if (rayAABB(a, d, box, len - 0.05f, &t) && t > 0.01f) return false;
    }
    for (size_t i = 0; i < doors.size(); ++i) {
        if (doors[i].openT > 0.5f) continue;
        float t;
        if (rayAABB(a, d, doorLeafBox(content->doors[i]), len - 0.05f, &t) && t > 0.01f) return false;
    }
    return true;
}

float World::raycast(vec3 ro, vec3 rd, float tMax, int* hitProp, int* hitDoor) const {
    float best = tMax;
    if (hitProp) *hitProp = -1;
    if (hitDoor) *hitDoor = -1;
    for (const AABB& box : geo->statics) {
        float t;
        if (rayAABB(ro, rd, box, best, &t)) {
            best = t;
            if (hitProp) *hitProp = -1;
            if (hitDoor) *hitDoor = -1;
        }
    }
    for (size_t i = 0; i < props.size(); ++i) {
        const PropSpec& ps = content->props[i];
        const PropState& st = props[i];
        if (st.hidden || ps.type == PropType::Billboard) continue;
        AABB box;
        vec3 half = ps.size * 0.5f;
        box.mn = st.pos - vec3{half.x, 0, half.z};
        box.mx = st.pos + vec3{half.x, ps.size.y, half.z};
        float t;
        if (rayAABB(ro, rd, box, best, &t)) {
            best = t;
            if (hitProp) *hitProp = (int)i;
            if (hitDoor) *hitDoor = -1;
        }
    }
    for (size_t i = 0; i < doors.size(); ++i) {
        AABB box = doors[i].openT > 0.5f ? geo->doorVolumes[i].box : doorLeafBox(content->doors[i]);
        float t;
        if (rayAABB(ro, rd, box, best, &t)) {
            best = t;
            if (hitDoor) *hitDoor = (int)i;
            if (hitProp) *hitProp = -1;
        }
    }
    return best;
}

vec3 World::resolveCollision(vec3 pos, vec3 half, vec3 vel, float dt, bool* grounded) const {
    // pos = feet center. Swept per-axis AABB resolution.
    if (grounded) *grounded = false;
    auto overlaps = [&](vec3 p, const AABB& b) {
        return p.x - half.x < b.mx.x && p.x + half.x > b.mn.x && p.y < b.mx.y &&
               p.y + half.y * 2 > b.mn.y && p.z - half.z < b.mx.z && p.z + half.z > b.mn.z;
    };
    auto collectAndResolve = [&](vec3 p, int axis, float dir) {
        auto resolveBox = [&](const AABB& b) {
            if (!overlaps(p, b)) return;
            if (axis == 0) p.x = dir > 0 ? b.mn.x - half.x : b.mx.x + half.x;
            if (axis == 2) p.z = dir > 0 ? b.mn.z - half.z : b.mx.z + half.z;
            if (axis == 1) {
                if (dir <= 0) {
                    p.y = b.mx.y;
                    if (grounded) *grounded = true;
                } else {
                    p.y = b.mn.y - half.y * 2;
                }
            }
        };
        for (const AABB& b : geo->statics) resolveBox(b);
        for (size_t i = 0; i < doors.size(); ++i)
            if (doors[i].openT <= 0.5f) resolveBox(doorLeafBox(content->doors[i]));
        for (size_t i = 0; i < props.size(); ++i) {
            const PropSpec& ps = content->props[i];
            if (!ps.collide || props[i].hidden || ps.type != PropType::Box) continue;
            vec3 hh = ps.size * 0.5f;
            AABB b{props[i].pos - vec3{hh.x, 0, hh.z}, props[i].pos + vec3{hh.x, ps.size.y, hh.z}};
            resolveBox(b);
        }
        return p;
    };
    vec3 p = pos;
    p.x += vel.x * dt;
    p = collectAndResolve(p, 0, vel.x);
    p.z += vel.z * dt;
    p = collectAndResolve(p, 2, vel.z);
    p.y += vel.y * dt;
    p = collectAndResolve(p, 1, vel.y);
    return p;
}

void World::visibleRooms(int povRoom, std::vector<int>& out) const {
    out.clear();
    if (povRoom < 0) {
        for (size_t i = 0; i < content->rooms.size(); ++i) out.push_back((int)i);
        return;
    }
    std::unordered_set<int> seen{povRoom};
    out.push_back(povRoom);
    // BFS 2 hops through open doors
    std::vector<int> frontier{povRoom};
    for (int hop = 0; hop < 2; ++hop) {
        std::vector<int> next;
        for (int room : frontier) {
            const std::string& rid = content->rooms[room].id;
            for (size_t di = 0; di < content->doors.size(); ++di) {
                if (doors[di].openT < 0.05f) continue;
                const DoorSpec& d = content->doors[di];
                std::string other;
                if (d.roomA == rid) other = d.roomB;
                else if (d.roomB == rid) other = d.roomA;
                else continue;
                auto it = roomIdx.find(other);
                if (it == roomIdx.end() || seen.count(it->second)) continue;
                seen.insert(it->second);
                out.push_back(it->second);
                next.push_back(it->second);
            }
        }
        frontier = std::move(next);
    }
}

// ------------------------------------------------------------- draw list ---
void World::buildDrawList(const GameContent& c, int povRoom, std::vector<DrawItem>& out,
                          std::vector<Light>& outLights, bool reflectionPass, float timeSec) {
    std::vector<int> rooms;
    visibleRooms(povRoom, rooms);
    std::unordered_set<int> roomSet(rooms.begin(), rooms.end());
    std::unordered_set<int> doorsDrawn;

    for (int ri : rooms) {
        const RoomSpec& r = c.rooms[ri];
        const RoomMeshes& rm = geo->roomMeshes[ri];
        DrawItem w;
        w.mesh = rm.walls;
        w.tex = texOf(r.wallTex);
        out.push_back(w);
        DrawItem f;
        f.mesh = rm.floor;
        f.tex = texOf(r.floorTex);
        out.push_back(f);
        DrawItem ce;
        ce.mesh = rm.ceil;
        ce.tex = texOf(r.ceilTex);
        out.push_back(ce);

        // door leaves touching this room
        for (size_t di = 0; di < c.doors.size(); ++di) {
            const DoorSpec& d = c.doors[di];
            if (d.roomA != r.id && d.roomB != r.id) continue;
            if (doorsDrawn.count((int)di)) continue;
            doorsDrawn.insert((int)di);
            float swing = doors[di].openT * radians(105.0f);
            float baseYaw = d.axis == 0 ? 0.0f : radians(90.0f);
            vec3 hinge = d.pos;
            if (d.axis == 0) hinge.z -= DOOR_W * 0.5f;
            else hinge.x -= DOOR_W * 0.5f;
            DrawItem leaf;
            leaf.mesh = geo->doorLeaf;
            leaf.tex = texOf(d.tex);
            leaf.model = mat4::translate(hinge) * mat4::rotateY(baseYaw + swing) *
                         mat4::scale({DOOR_W, DOOR_H, 1});
            out.push_back(leaf);
        }

        // props
        for (size_t pi = 0; pi < c.props.size(); ++pi) {
            const PropSpec& ps = c.props[pi];
            const PropState& st = props[pi];
            if (roomIdx.at(ps.room) != ri) continue;
            if (st.hidden) continue;
            if (ps.mirrorOnly && !reflectionPass) continue;
            DrawItem it;
            it.tex = texOf(st.tex);
            it.tint = st.tint;
            it.emissive = ps.emissive;
            it.uvScaleX = ps.uvx;
            it.uvScaleY = ps.uvy;
            if (ps.type == PropType::Box) {
                it.mesh = geo->cube;
                it.model = mat4::translate(st.pos + vec3{0, ps.size.y * 0.5f, 0}) *
                           mat4::rotateY(st.yaw) * mat4::scale(ps.size);
            } else if (ps.type == PropType::Quad) {
                it.mesh = geo->quad;
                it.model = ps.flat
                               ? mat4::translate(st.pos) * mat4::rotateY(st.yaw) *
                                     mat4::rotateX(radians(-90)) * mat4::scale({ps.size.x, ps.size.y, 1})
                               : mat4::translate(st.pos) * mat4::rotateY(st.yaw) *
                                     mat4::scale({ps.size.x, ps.size.y, 1});
            } else {
                it.mesh = geo->billboard;
                it.model = mat4::translate(st.pos) * mat4::rotateY(st.yaw) *
                           mat4::scale({ps.size.x, ps.size.y, 1});
                it.flags |= DRAW_ALPHA | DRAW_ALPHA_TEST;
            }
            if (ps.mirrorOnly) it.flags |= DRAW_ONLY_REFLECT;
            out.push_back(it);
        }

        // lights
        for (size_t li = 0; li < c.lights.size(); ++li) {
            const LightSpec& ls = c.lights[li];
            if (roomIdx.at(ls.room) != ri) continue;
            const LightState& st = lights[li];
            if (!st.on) continue;
            Light L;
            L.pos = ls.pos;
            L.color = ls.color;
            L.radius = ls.radius;
            L.intensity = ls.intensity;
            if (st.flicker > 0.001f) {
                float t = timeSec * 13.0f + st.flickerPhase * 7.0f;
                float n = std::sin(t) * std::sin(t * 1.7f + 1.3f) * std::sin(t * 0.31f);
                float dip = n > (0.92f - st.flicker * 0.5f) ? 0.15f : 1.0f;
                L.intensity *= lerpf(1.0f, dip, st.flicker) * (0.92f + 0.08f * std::sin(t * 3.1f));
            }
            outLights.push_back(L);
        }
    }
}
