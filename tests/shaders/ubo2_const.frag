#version 450
/* T4.2.6 — same two sets as ubo2.frag, but the BLUE channel is a hardcoded
 * constant that needs no descriptor at all.
 *
 * That single constant separates two failure modes which otherwise look
 * identical (both give a black pixel):
 *   geometry failed        -> no triangle, 0 non-black pixels
 *   descriptor read failed -> triangle present and BLUE, sets read as zero
 */
layout(set = 0, binding = 0) uniform UboA { vec4 v; } a;
layout(set = 1, binding = 0) uniform UboB { vec4 v; } b;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(a.v.r, b.v.g, 0.5, 1.0);
}
