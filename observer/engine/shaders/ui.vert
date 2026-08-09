#version 450

layout(location = 0) in vec2 inPos;   // pixels, top-left origin
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inCol;

layout(push_constant) uniform Push {
    vec4 p0;   // x=screenW y=screenH
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 tint;
    vec4 misc;
    vec4 uvScale;
} pc;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vCol;

void main() {
    vec2 ndc = inPos / pc.p0.xy * 2.0 - 1.0;
    vUv = inUv;
    vCol = inCol;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
