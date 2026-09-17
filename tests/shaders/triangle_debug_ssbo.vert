#version 450

layout(set = 0, binding = 0, std430) buffer PosDebug {
    vec4 positions[3];
} dbg;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    vec4 pos = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    gl_Position = pos;
    dbg.positions[gl_VertexIndex] = pos;
}
