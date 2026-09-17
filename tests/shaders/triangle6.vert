#version 450
/* Phase 4.1 — six positions holding TWO distinct triangles, so an index buffer
 * has something to actually choose between.
 *
 *   0,1,2  triangle A: the exact geometry of the validated baseline.
 *          Covers the centre pixel. Expect 512/4096 non-black.
 *   3,4,5  triangle B: a small right triangle tucked in one corner.
 *          Does NOT cover the centre. Different pixel count.
 *
 * "A triangle appeared" is therefore not enough to pass. The test can tell
 * which triangle appeared, which is what proves the indices were read.
 */
void main() {
    vec2 positions[6] = vec2[](
        vec2(-0.5, -0.5),
        vec2( 0.5, -0.5),
        vec2( 0.0,  0.5),

        vec2(-0.9, -0.9),
        vec2(-0.2, -0.9),
        vec2(-0.9, -0.2)
    );

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
