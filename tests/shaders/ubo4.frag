#version 450
/* T4.2.3 - four descriptor sets, one channel each. */
layout(set = 0, binding = 0) uniform U0 { vec4 v; } u0;
layout(set = 1, binding = 0) uniform U1 { vec4 v; } u1;
layout(set = 2, binding = 0) uniform U2 { vec4 v; } u2;
layout(set = 3, binding = 0) uniform U3 { vec4 v; } u3;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(u0.v.r, u1.v.g, u2.v.b, u3.v.a);
}
