// IRIS engine smoke test: boots headless Vulkan (CPU lavapipe in CI), renders
// a lit test scene through every pipeline (opaque, alpha, RT, post, UI) and
// verifies the output has actual image content. Exit 0 = pass.
#include "iris.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace iris;

static MeshId makeCube(Engine& eng, float sx, float sy, float sz) {
    std::vector<Vertex> v;
    std::vector<uint32_t> idx;
    const vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    const vec3 u[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
    for (int f = 0; f < 6; ++f) {
        vec3 N = n[f], U = u[f], V = cross(N, U);
        uint32_t base = (uint32_t)v.size();
        for (int i = 0; i < 4; ++i) {
            float su = (i == 1 || i == 2) ? 1.f : -1.f;
            float sv = (i >= 2) ? 1.f : -1.f;
            vec3 p = N * 0.5f + U * (0.5f * su) + V * (0.5f * sv);
            v.push_back({{p.x * sx, p.y * sy, p.z * sz}, N, {su * .5f + .5f, sv * .5f + .5f}, {1, 1, 1, 1}});
        }
        for (uint32_t t : {0u, 1u, 2u, 0u, 2u, 3u}) idx.push_back(base + t);
    }
    return eng.createMesh(v, idx);
}

int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "smoke.png";
    EngineConfig cfg;
    cfg.appName = "iris-smoke";
    cfg.width = 640;
    cfg.height = 360;
    cfg.headless = true;
    cfg.shaderDir = argc > 2 ? argv[2] : "engine/shaders/spv";

    Engine eng;
    if (!eng.init(cfg)) {
        std::fprintf(stderr, "engine init failed\n");
        return 1;
    }

    // checkerboard texture
    std::vector<uint8_t> checker(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            bool on = ((x / 8) ^ (y / 8)) & 1;
            uint8_t* p = &checker[(y * 64 + x) * 4];
            p[0] = on ? 200 : 60;
            p[1] = on ? 180 : 60;
            p[2] = on ? 150 : 70;
            p[3] = 255;
        }
    TexId tex = eng.createTexture(checker.data(), 64, 64);

    MeshId cube = makeCube(eng, 1, 1, 1);
    MeshId floor = makeCube(eng, 10, 0.1f, 10);

    RtId rt = eng.createRenderTarget(160, 120);

    std::vector<DrawItem> items;
    {
        DrawItem f;
        f.mesh = floor;
        f.tex = tex;
        f.model = mat4::translate({0, -0.05f, 0});
        f.uvScaleX = 8;
        f.uvScaleY = 8;
        items.push_back(f);
        DrawItem c1;
        c1.mesh = cube;
        c1.tex = tex;
        c1.model = mat4::translate({-1.2f, 0.5f, -3}) * mat4::rotateY(0.6f);
        c1.tint = {1.0f, 0.6f, 0.5f, 1};
        items.push_back(c1);
        DrawItem c2;
        c2.mesh = cube;
        c2.tex = tex;
        c2.model = mat4::translate({1.4f, 0.5f, -4.2f}) * mat4::rotateY(-0.3f);
        c2.tint = {0.5f, 0.7f, 1.0f, 1};
        items.push_back(c2);
        DrawItem ghost;   // alpha-blended
        ghost.mesh = cube;
        ghost.tex = TEX_WHITE;
        ghost.model = mat4::translate({0, 0.8f, -2.2f}) * mat4::scale({0.5f, 1.6f, 0.5f});
        ghost.tint = {0.9f, 0.95f, 1.0f, 0.35f};
        ghost.flags = DRAW_ALPHA;
        items.push_back(ghost);
        // monitor quad showing the RT
        DrawItem mon;
        mon.mesh = cube;
        mon.tex = eng.renderTargetTexture(rt);
        mon.model = mat4::translate({2.6f, 1.2f, -3}) * mat4::scale({1.2f, 0.9f, 0.05f});
        mon.emissive = 0.8f;
        mon.flags = DRAW_UNLIT;
        items.push_back(mon);
    }

    std::vector<Light> lights;
    {
        Light L;
        L.pos = {0, 2.4f, -2};
        L.radius = 9;
        L.color = {1.0f, 0.9f, 0.75f};
        L.intensity = 1.4f;
        lights.push_back(L);
        Light spot;
        spot.type = LIGHT_SPOT;
        spot.pos = {0, 1.6f, 0.5f};
        spot.dir = normalize(vec3{0.2f, -0.3f, -1});
        spot.radius = 14;
        spot.intensity = 2.2f;
        spot.innerCos = 0.93f;
        spot.outerCos = 0.85f;
        lights.push_back(spot);
    }

    for (int frame = 0; frame < 6; ++frame) {
        if (!eng.beginFrame()) continue;

        // CCTV-style RT view from above
        SceneView rtView;
        rtView.view = camView({4, 3, 1}, radians(-130), radians(-25));
        rtView.proj = perspectiveVk(radians(60), 160.0f / 120.0f, 0.05f, 60.f);
        rtView.camPos = {4, 3, 1};
        rtView.target = rt;
        rtView.time = frame * 0.033f;
        eng.renderScene(rtView, items, lights);

        SceneView main;
        vec3 cam{0, 1.5f, 2.5f};
        main.view = camView(cam, radians(-6.0f), radians(-8));
        main.proj = perspectiveVk(radians(70), 640.0f / 360.0f, 0.05f, 60.f);
        main.camPos = cam;
        main.time = frame * 0.033f;
        eng.renderScene(main, items, lights);

        PostParams post;
        post.time = frame * 0.033f;
        post.grain = 0.05f;
        post.vignette = 0.35f;
        eng.uiRect(20, 20, 180, 8, {1, 1, 1, 0.8f});
        eng.renderPostAndUi(post);
        eng.endFrame();
    }

    if (!eng.saveScreenshot(out)) {
        std::fprintf(stderr, "screenshot failed\n");
        return 1;
    }

    // content check: enough distinct luminance in the capture
    std::vector<uint8_t> px;
    int w, h;
    eng.captureSceneRGBA(px, w, h);
    long long sum = 0;
    int nonzero = 0;
    for (size_t i = 0; i < px.size(); i += 4) {
        int lum = (px[i] + px[i + 1] + px[i + 2]) / 3;
        sum += lum;
        if (lum > 8) nonzero++;
    }
    double mean = (double)sum / (px.size() / 4);
    double cover = (double)nonzero / (px.size() / 4);
    std::printf("smoke: mean=%.1f coverage=%.2f (%dx%d)\n", mean, cover, w, h);
    eng.shutdown();
    if (mean < 2.0 || cover < 0.15) {
        std::fprintf(stderr, "image looks empty\n");
        return 1;
    }
    std::printf("SMOKE OK\n");
    return 0;
}
