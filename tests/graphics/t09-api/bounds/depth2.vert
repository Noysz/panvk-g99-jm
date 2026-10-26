#version 450
/* T4.7 - two axis-aligned quads from gl_VertexIndex, flat colour each.
 * Quad A (vertices 0..5):  x,y in [-0.5, 0.25], z = 0.25, red
 * Quad B (vertices 6..11): x,y in [-0.25, 0.5], z = 0.75, green
 * In a 64x64 viewport NDC -0.5/-0.25/0.25/0.5 map to pixels 16/24/40/48, so
 * every edge sits on an integer and no pixel centre is ever on an edge:
 * A covers 24x24 = 576, B 576, overlap 16x16 = 256. The draw order is chosen by
 * firstVertex/vertexCount from the test, so the same shader serves every case. */
layout(location = 0) flat out vec4 vColor;
void main() {
    int q = gl_VertexIndex / 6, v = gl_VertexIndex % 6;
    vec2 c[6] = vec2[](vec2(0,0), vec2(1,0), vec2(0,1), vec2(1,0), vec2(1,1), vec2(0,1));
    float lo = q == 0 ? -0.5 : -0.25;
    vec2 p = vec2(lo) + c[v] * 0.75;
    gl_Position = vec4(p, q == 0 ? 0.25 : 0.75, 1.0);
    vColor = q == 0 ? vec4(1, 0, 0, 1) : vec4(0, 1, 0, 1);
}
