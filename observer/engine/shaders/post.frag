#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D texArr[512];

// Post params are packed into the shared 112-byte push block.
layout(push_constant) uniform Push {
    vec4 p0;   // grain, vignette, aberration, scanline
    vec4 p1;   // noiseBurst, desaturate, warp, fadeBlack
    vec4 p2;   // flashWhite, cctv, time, lowPulse
    vec4 p3;
    vec4 tint;
    vec4 misc; // x = scene texture index
    vec4 uvScale;
} pc;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    float grain = pc.p0.x, vig = pc.p0.y, aber = pc.p0.z, scan = pc.p0.w;
    float burst = pc.p1.x, desat = pc.p1.y, warp = pc.p1.z, fadeB = pc.p1.w;
    float flashW = pc.p2.x, cctv = pc.p2.y, time = pc.p2.z, pulse = pc.p2.w;
    int sceneTex = int(pc.misc.x + 0.5);

    vec2 uv = vUv;

    // fear warp: slow breathing distortion toward edges
    if (warp > 0.0001) {
        vec2 c = uv - 0.5;
        float r = length(c);
        uv += c * sin(time * 1.7 + r * 9.0) * warp * r;
    }
    // cctv barrel distortion
    if (cctv > 0.001) {
        vec2 c = uv - 0.5;
        uv = 0.5 + c * (1.0 + 0.12 * cctv * dot(c, c) * 4.0);
    }

    vec3 col;
    if (aber > 0.00001) {
        vec2 dir = (uv - 0.5) * aber * 6.0;
        col.r = texture(texArr[nonuniformEXT(sceneTex)], uv + dir).r;
        col.g = texture(texArr[nonuniformEXT(sceneTex)], uv).g;
        col.b = texture(texArr[nonuniformEXT(sceneTex)], uv - dir).b;
    } else {
        col = texture(texArr[nonuniformEXT(sceneTex)], uv).rgb;
    }

    // outside sampled area (after distortion) -> black
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) col = vec3(0.0);

    // desaturate / cctv mono
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, vec3(lum), clamp(desat + cctv, 0.0, 1.0));
    if (cctv > 0.001) {
        col *= mix(vec3(1.0), vec3(0.75, 1.05, 0.8), cctv);   // green phosphor
        col = col * (0.75 + 0.5 * cctv);                       // gain
    }

    // scanlines
    float scanAmt = max(scan, cctv * 0.55);
    if (scanAmt > 0.001) {
        float s = sin(vUv.y * 480.0 * 3.14159 + time * 2.0) * 0.5 + 0.5;
        col *= 1.0 - scanAmt * 0.35 * s;
        // rolling bar
        float bar = fract(vUv.y * 0.5 - time * 0.07);
        col *= 1.0 - scanAmt * 0.20 * smoothstep(0.93, 1.0, bar);
    }

    // film grain (luminance-weighted so blacks stay black) + static burst
    float n = hash12(vUv * vec2(1920.0, 1080.0) + fract(time) * 173.13);
    float glum = dot(col, vec3(0.299, 0.587, 0.114));
    col += (n - 0.5) * grain * (0.30 + 0.70 * smoothstep(0.0, 0.35, glum));
    if (burst > 0.001) {
        float sn = hash12(vUv * vec2(917.0, 533.0) + fract(time * 7.31) * 91.7);
        col = mix(col, vec3(sn), clamp(burst, 0.0, 1.0));
    }

    // vignette
    vec2 vc = vUv - 0.5;
    float v = 1.0 - dot(vc, vc) * 2.0 * vig;
    col *= clamp(v, 0.0, 1.0);

    // subtle dread pulse (no HUD meter; the frame itself breathes)
    if (pulse > 0.001) {
        float p = (sin(time * 2.4) * 0.5 + 0.5) * pulse;
        col *= 1.0 - 0.12 * p;
        col.r += 0.02 * p;
    }

    col = mix(col, vec3(1.0), clamp(flashW, 0.0, 1.0));
    col = mix(col, vec3(0.0), clamp(fadeB, 0.0, 1.0));

    outColor = vec4(col, 1.0);
}
