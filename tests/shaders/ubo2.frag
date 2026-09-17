#version 450
/* T4.2.2 - two descriptor sets, each contributing one colour channel.
 * Set 0 drives RED, set 1 drives GREEN. A wrong pixel therefore names
 * exactly which set failed to be read. */
layout(set = 0, binding = 0) uniform UboA { vec4 v; } a;
layout(set = 1, binding = 0) uniform UboB { vec4 v; } b;
layout(location = 0) out vec4 outColor;
void main() {
    outColor = vec4(a.v.r, b.v.g, 0.0, 1.0);
}
