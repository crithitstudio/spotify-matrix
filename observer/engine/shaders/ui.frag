#version 450
#extension GL_EXT_nonuniform_qualifier : enable

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vCol;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D texArr[512];

layout(push_constant) uniform Push {
    vec4 p0;
    vec4 p1;
    vec4 p2;
    vec4 p3;
    vec4 tint;
    vec4 misc;   // x = texture index
    vec4 uvScale;
} pc;

void main() {
    int idx = int(pc.misc.x + 0.5);
    vec4 t = texture(texArr[nonuniformEXT(idx)], vUv);
    outColor = t * vCol;
}
