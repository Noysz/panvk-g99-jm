#version 450
/* Generates one thin vertical triangle per group of three vertices, marching
 * left to right. Pixel coverage therefore grows with vertexCount, which is what
 * makes it useful for stressing the indirect placeholder: the CPU records
 * vertex.count = 1, so drawing 90 vertices is 90x the placeholder. If anything
 * were sized at record time from the placeholder, a large count would fault or
 * come back short. */
void main() {
    int tri = gl_VertexIndex / 3;
    int v   = gl_VertexIndex % 3;
    float x = -0.95 + float(tri) * 0.0625;   /* 32 columns fit in NDC */
    vec2 p[3] = vec2[]( vec2(x,        -0.6),
                        vec2(x + 0.03, -0.6),
                        vec2(x,         0.6) );
    gl_Position = vec4(p[v], 0.0, 1.0);
}
