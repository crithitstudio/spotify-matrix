#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNrm;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inCol;

struct Light {
    vec4 posRadius;
    vec4 colorIntensity;
    vec4 dirInner;
    vec4 params;   // x=outerCos y=type
};

layout(set = 0, binding = 0) uniform View {
    mat4 view;
    mat4 proj;
    vec4 camPosTime;
    vec4 ambientFog;   // rgb ambient, w fog density
    vec4 fogColor;
    vec4 clipPlane;
    ivec4 counts;      // x=lightCount y=isReflection
    Light lights[24];
} u;

layout(push_constant) uniform Push {
    mat4 model;
    vec4 tint;
    vec4 misc;      // x=texIdx y=emissive z=flags
    vec4 uvScale;
} pc;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNrm;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vCol;

void main() {
    vec4 world = pc.model * vec4(inPos, 1.0);
    vWorld = world.xyz;
    vNrm = normalize(mat3(pc.model) * inNrm);
    vUv = inUv * pc.uvScale.xy;
    vCol = inCol;
    gl_Position = u.proj * u.view * world;
}
