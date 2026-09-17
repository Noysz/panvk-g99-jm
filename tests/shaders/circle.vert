#version 450
/* Circle approximated by a 64-segment triangle fan, emitted as a triangle list
 * (3 vertices per segment, 192 total). Radius 0.7 in NDC, centred at the origin.
 *
 * A circle has no closed-form pixel count, so this shape is NOT validated
 * against a hand-picked number. The test rasterizes the identical polygon on the
 * CPU using Vulkan's own coverage rule and compares per pixel.
 */
#define NSEG 64
void main() {
    int tri = gl_VertexIndex / 3;
    int v   = gl_VertexIndex % 3;
    vec2 pos;
    if (v == 0) {
        pos = vec2(0.0, 0.0);
    } else {
        int k = tri + (v - 1);
        float a = 6.28318530717958647692 * float(k) / float(NSEG);
        pos = vec2(cos(a), sin(a)) * 0.7;
    }
    gl_Position = vec4(pos, 0.0, 1.0);
}
