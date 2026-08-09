#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNrm;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vCol;

layout(location = 0) out vec4 outColor;

struct Light {
    vec4 posRadius;
    vec4 colorIntensity;
    vec4 dirInner;
    vec4 params;
};

layout(set = 0, binding = 0) uniform View {
    mat4 view;
    mat4 proj;
    vec4 camPosTime;
    vec4 ambientFog;
    vec4 fogColor;
    vec4 clipPlane;
    ivec4 counts;
    Light lights[24];
} u;

layout(set = 0, binding = 1) uniform sampler2D texArr[512];

layout(push_constant) uniform Push {
    mat4 model;
    vec4 tint;
    vec4 misc;
    vec4 uvScale;
} pc;

const uint F_UNLIT = 1u;
const uint F_ALPHA_TEST = 64u;

void main() {
    // reflection clip plane (mirror views only)
    if (u.counts.y == 1 && dot(u.clipPlane.xyz, u.clipPlane.xyz) > 0.0) {
        if (dot(u.clipPlane.xyz, vWorld) + u.clipPlane.w < 0.0) discard;
    }

    int texIdx = int(pc.misc.x + 0.5);
    uint flags = uint(pc.misc.z + 0.5);
    vec4 tex = texture(texArr[nonuniformEXT(texIdx)], vUv);
    float alpha = tex.a * pc.tint.a * vCol.a;
    if ((flags & F_ALPHA_TEST) != 0u && alpha < 0.5) discard;

    vec3 base = tex.rgb * pc.tint.rgb * vCol.rgb;
    vec3 color;
    if ((flags & F_UNLIT) != 0u) {
        color = base;
    } else {
        vec3 N = normalize(vNrm);
        vec3 acc = u.ambientFog.rgb;
        int n = u.counts.x;
        for (int i = 0; i < n; ++i) {
            Light L = u.lights[i];
            vec3 toL = L.posRadius.xyz - vWorld;
            float d = length(toL);
            float atten = clamp(1.0 - d / max(L.posRadius.w, 0.001), 0.0, 1.0);
            atten *= atten;
            if (atten <= 0.0001) continue;
            vec3 Ln = toL / max(d, 0.0001);
            float ndl = max(dot(N, Ln), 0.0);
            // soft wrap for gloomier falloff
            ndl = ndl * 0.85 + 0.15;
            float spot = 1.0;
            if (int(L.params.y + 0.5) == 1) {
                float cosAng = dot(-Ln, normalize(L.dirInner.xyz));
                spot = smoothstep(L.params.x, L.dirInner.w, cosAng);
            }
            acc += L.colorIntensity.rgb * (L.colorIntensity.w * atten * ndl * spot);
        }
        color = base * acc;
    }
    color += tex.rgb * pc.misc.y;   // emissive

    // exponential fog
    float dist = length(vWorld - u.camPosTime.xyz);
    float f = 1.0 - exp(-u.ambientFog.w * dist);
    color = mix(color, u.fogColor.rgb, clamp(f, 0.0, 1.0));

    outColor = vec4(color, alpha);
}
