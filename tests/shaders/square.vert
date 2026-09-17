#version 450
/* Axis-aligned square as two triangles, 6 vertices.
 *
 * NDC +/-0.5 was chosen so the edges land on EXACT pixel boundaries in a 64x64
 * viewport: px = (ndc*0.5 + 0.5)*64, so -0.5 -> 16.0 and +0.5 -> 48.0.
 * Under Vulkan's coverage rule (a pixel is covered iff its centre is inside),
 * that is pixel centres 16.5 .. 47.5, i.e. exactly 32x32 = 1024 pixels, with no
 * pixel centre sitting on an edge. The expected count is therefore arithmetic,
 * not measured and not guessed.
 */
void main() {
    vec2 p[6] = vec2[](
        vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2( 0.5,  0.5),
        vec2(-0.5, -0.5), vec2( 0.5,  0.5), vec2(-0.5,  0.5)
    );
    gl_Position = vec4(p[gl_VertexIndex], 0.0, 1.0);
}
