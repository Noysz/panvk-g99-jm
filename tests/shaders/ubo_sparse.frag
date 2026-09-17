#version 450
/* T4.2.5 - non-contiguous sets 0 and 3, sets 1 and 2 deliberately unused.
 * Expect used_set_mask = 0b1001, first_unused_set = 4, res_count = 8. */
layout(set = 0, binding = 0) uniform U0 { vec4 v; } u0;
layout(set = 3, binding = 0) uniform U3 { vec4 v; } u3;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(u0.v.r, 0.0, u3.v.b, 1.0);
}
