// IRIS engine - math library
// Right-handed, +Y up. Column-major mat4 (GLSL std140 compatible).
// Projection produces Vulkan clip space: depth [0,1], Y flipped in-matrix.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace iris {

constexpr float PI = 3.14159265358979323846f;
constexpr float TAU = 6.28318530717958647692f;
inline float radians(float d) { return d * (PI / 180.0f); }
inline float degrees(float r) { return r * (180.0f / PI); }
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smoothstepf(float e0, float e1, float x) {
    float t = saturate((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}
// Framerate-independent exponential approach: k per second.
inline float damp(float cur, float target, float k, float dt) {
    return lerpf(target, cur, std::exp(-k * dt));
}
inline float wrapAngle(float a) {  // wrap to [-PI, PI]
    a = std::fmod(a + PI, TAU);
    if (a < 0) a += TAU;
    return a - PI;
}

struct vec2 {
    float x = 0, y = 0;
    vec2() = default;
    vec2(float x_, float y_) : x(x_), y(y_) {}
    vec2 operator+(vec2 o) const { return {x + o.x, y + o.y}; }
    vec2 operator-(vec2 o) const { return {x - o.x, y - o.y}; }
    vec2 operator*(float s) const { return {x * s, y * s}; }
    vec2 operator/(float s) const { return {x / s, y / s}; }
};
inline float dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(vec2 v) { return std::sqrt(dot(v, v)); }

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3 operator+(vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator*(vec3 o) const { return {x * o.x, y * o.y, z * o.z}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3& operator+=(vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(vec3 o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};
inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(vec3 v) { return std::sqrt(dot(v, v)); }
inline float length2(vec3 v) { return dot(v, v); }
inline vec3 normalize(vec3 v) {
    float l = length(v);
    return l > 1e-8f ? v / l : vec3{0, 0, 0};
}
inline vec3 lerp(vec3 a, vec3 b, float t) { return a + (b - a) * t; }
inline float distance(vec3 a, vec3 b) { return length(a - b); }

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    vec4(vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    vec3 xyz() const { return {x, y, z}; }
    vec4 operator+(vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
};
inline float dot(vec4 a, vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

// Column-major 4x4. m[c*4+r].
struct mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int r, int c) { return m[c * 4 + r]; }
    float at(int r, int c) const { return m[c * 4 + r]; }

    static mat4 identity() { return mat4{}; }

    static mat4 translate(vec3 t) {
        mat4 r;
        r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
        return r;
    }
    static mat4 scale(vec3 s) {
        mat4 r;
        r.m[0] = s.x; r.m[5] = s.y; r.m[10] = s.z;
        return r;
    }
    static mat4 rotateX(float a) {
        mat4 r; float c = std::cos(a), s = std::sin(a);
        r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c;
        return r;
    }
    static mat4 rotateY(float a) {
        mat4 r; float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c;
        return r;
    }
    static mat4 rotateZ(float a) {
        mat4 r; float c = std::cos(a), s = std::sin(a);
        r.m[0] = c; r.m[1] = s; r.m[4] = -s; r.m[5] = c;
        return r;
    }

    mat4 operator*(const mat4& o) const {
        mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row) {
                float sum = 0;
                for (int k = 0; k < 4; ++k) sum += at(row, k) * o.at(k, c);
                r.at(row, c) = sum;
            }
        return r;
    }
    vec4 operator*(vec4 v) const {
        vec4 r;
        r.x = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12] * v.w;
        r.y = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13] * v.w;
        r.z = m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w;
        r.w = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w;
        return r;
    }
    vec3 transformPoint(vec3 p) const {
        vec4 r = (*this) * vec4{p, 1.0f};
        return r.xyz();
    }
    vec3 transformDir(vec3 d) const {
        vec4 r = (*this) * vec4{d, 0.0f};
        return r.xyz();
    }
};

inline mat4 transpose(const mat4& a) {
    mat4 r;
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) r.at(c, row) = a.at(row, c);
    return r;
}

// General inverse (Cramer / adjugate). Fine for per-frame camera use.
inline mat4 inverse(const mat4& mm) {
    const float* m = mm.m;
    float inv[16];
    inv[0] = m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4] = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8] = m[4]*m[9]*m[15] - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14] + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1] = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5] = m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9] = -m[0]*m[9]*m[15] + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] = m[0]*m[9]*m[14] - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2] = m[1]*m[6]*m[15] - m[1]*m[7]*m[14] - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7] - m[13]*m[3]*m[6];
    inv[6] = -m[0]*m[6]*m[15] + m[0]*m[7]*m[14] + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7] + m[12]*m[3]*m[6];
    inv[10] = m[0]*m[5]*m[15] - m[0]*m[7]*m[13] - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7] - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14] + m[0]*m[6]*m[13] + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6] + m[12]*m[2]*m[5];
    inv[3] = -m[1]*m[6]*m[11] + m[1]*m[7]*m[10] + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7] + m[9]*m[3]*m[6];
    inv[7] = m[0]*m[6]*m[11] - m[0]*m[7]*m[10] - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7] - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11] + m[0]*m[7]*m[9] + m[4]*m[1]*m[11] - m[4]*m[3]*m[9] - m[8]*m[1]*m[7] + m[8]*m[3]*m[5];
    inv[15] = m[0]*m[5]*m[10] - m[0]*m[6]*m[9] - m[4]*m[1]*m[10] + m[4]*m[2]*m[9] + m[8]*m[1]*m[6] - m[8]*m[2]*m[5];
    float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    mat4 r;
    if (std::fabs(det) < 1e-12f) return r;
    float id = 1.0f / det;
    for (int i = 0; i < 16; ++i) r.m[i] = inv[i] * id;
    return r;
}

inline mat4 lookAt(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    mat4 r;
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye);
    r.m[13] = -dot(u, eye);
    r.m[14] = dot(f, eye);
    return r;
}

// Vulkan clip space: z in [0,1], y down. We flip Y here so app code stays GL-like.
inline mat4 perspectiveVk(float fovyRad, float aspect, float zNear, float zFar) {
    float t = std::tan(fovyRad * 0.5f);
    mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = 1.0f / (aspect * t);
    r.m[5] = -1.0f / t;                       // Y flip for Vulkan
    r.m[10] = zFar / (zNear - zFar);          // depth 0..1
    r.m[11] = -1.0f;
    r.m[14] = -(zFar * zNear) / (zFar - zNear);
    return r;
}

inline mat4 orthoVk(float l, float r_, float b, float t, float zn, float zf) {
    mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = 2.0f / (r_ - l);
    r.m[5] = -2.0f / (t - b);                 // Y flip
    r.m[10] = 1.0f / (zn - zf);
    r.m[12] = -(r_ + l) / (r_ - l);
    r.m[13] = (t + b) / (t - b);
    r.m[14] = zn / (zn - zf);
    r.m[15] = 1.0f;
    return r;
}

// Reflect across plane (n, d): points p with dot(n,p)+d=0.
inline mat4 reflectionMatrix(vec3 n, float d) {
    mat4 r;
    r.m[0] = 1 - 2 * n.x * n.x; r.m[4] = -2 * n.x * n.y; r.m[8] = -2 * n.x * n.z; r.m[12] = -2 * n.x * d;
    r.m[1] = -2 * n.y * n.x; r.m[5] = 1 - 2 * n.y * n.y; r.m[9] = -2 * n.y * n.z; r.m[13] = -2 * n.y * d;
    r.m[2] = -2 * n.z * n.x; r.m[6] = -2 * n.z * n.y; r.m[10] = 1 - 2 * n.z * n.z; r.m[14] = -2 * n.z * d;
    return r;
}

struct AABB {
    vec3 mn{1e30f, 1e30f, 1e30f}, mx{-1e30f, -1e30f, -1e30f};
    void expand(vec3 p) {
        mn.x = std::fmin(mn.x, p.x); mn.y = std::fmin(mn.y, p.y); mn.z = std::fmin(mn.z, p.z);
        mx.x = std::fmax(mx.x, p.x); mx.y = std::fmax(mx.y, p.y); mx.z = std::fmax(mx.z, p.z);
    }
    vec3 center() const { return (mn + mx) * 0.5f; }
    vec3 extent() const { return (mx - mn) * 0.5f; }
    bool contains(vec3 p) const {
        return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y && p.z >= mn.z && p.z <= mx.z;
    }
    bool overlaps(const AABB& o) const {
        return mn.x <= o.mx.x && mx.x >= o.mn.x && mn.y <= o.mx.y && mx.y >= o.mn.y &&
               mn.z <= o.mx.z && mx.z >= o.mn.z;
    }
    AABB inflated(float r) const {
        AABB b = *this;
        b.mn -= vec3{r, r, r}; b.mx += vec3{r, r, r};
        return b;
    }
};

// Slab test. Returns t of entry if ray hits within [0, tMax].
inline bool rayAABB(vec3 ro, vec3 rd, const AABB& b, float tMax, float* tOut) {
    float t0 = 0.0f, t1 = tMax;
    const float* o = &ro.x; const float* d = &rd.x;
    const float* bmn = &b.mn.x; const float* bmx = &b.mx.x;
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(d[i]) < 1e-9f) {
            if (o[i] < bmn[i] || o[i] > bmx[i]) return false;
        } else {
            float inv = 1.0f / d[i];
            float ta = (bmn[i] - o[i]) * inv;
            float tb = (bmx[i] - o[i]) * inv;
            if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
            t0 = std::fmax(t0, ta);
            t1 = std::fmin(t1, tb);
            if (t0 > t1) return false;
        }
    }
    if (tOut) *tOut = t0;
    return true;
}

struct Plane { vec3 n; float d; };  // dot(n,p)+d >= 0 is inside

struct Frustum {
    Plane p[6];
    static Frustum fromViewProj(const mat4& vp) {
        Frustum f;
        auto row = [&](int r) {
            return vec4{vp.at(r, 0), vp.at(r, 1), vp.at(r, 2), vp.at(r, 3)};
        };
        vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto set = [](Plane& pl, vec4 v) {
            float l = length(vec3{v.x, v.y, v.z});
            if (l < 1e-9f) l = 1;
            pl.n = vec3{v.x, v.y, v.z} / l;
            pl.d = v.w / l;
        };
        set(f.p[0], r3 + r0);   // left
        set(f.p[1], r3 + r0 * -1.0f);  // right  (r3 - r0)
        set(f.p[2], r3 + r1);   // bottom
        set(f.p[3], r3 + r1 * -1.0f);  // top
        set(f.p[4], r2);        // near (Vulkan z>=0)
        set(f.p[5], r3 + r2 * -1.0f);  // far
        return f;
    }
    bool containsPoint(vec3 pt) const {
        for (const auto& pl : p)
            if (dot(pl.n, pt) + pl.d < 0) return false;
        return true;
    }
    bool intersectsSphere(vec3 c, float r) const {
        for (const auto& pl : p)
            if (dot(pl.n, c) + pl.d < -r) return false;
        return true;
    }
    bool intersectsAABB(const AABB& b) const {
        for (const auto& pl : p) {
            vec3 v{pl.n.x >= 0 ? b.mx.x : b.mn.x,
                   pl.n.y >= 0 ? b.mx.y : b.mn.y,
                   pl.n.z >= 0 ? b.mx.z : b.mn.z};
            if (dot(pl.n, v) + pl.d < 0) return false;
        }
        return true;
    }
};

// Deterministic PCG32 - game logic must be reproducible for tests.
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL;
    uint64_t inc = 0xda3e39cb94b95bdbULL;
    void seed(uint64_t s) { state = s * 6364136223846793005ULL + 1442695040888963407ULL; inc = (s << 1) | 1; next(); }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32 - rot) & 31));
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }             // [0,1)
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int rangeInt(int a, int b) { return b <= a ? a : a + (int)(next() % (uint32_t)(b - a + 1)); }  // [a,b]
    bool chance(float p) { return uniform() < p; }
};

inline uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
    return x;
}

} // namespace iris
