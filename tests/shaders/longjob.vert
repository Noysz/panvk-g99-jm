#version 450
// tests/graphics/longjob_test.c. mode 0: fullscreen quad (the fragment
// shader does the long loop). mode 1: one grid cell per draw, the long loop
// runs here and the result goes to the fragment shader as a flat colour.
layout(push_constant) uniform P { uint iters; uint mode; uint cell; uint grid; } pc;
layout(location = 0) flat out uvec4 col;

void main()
{
   const vec2 c[6] = vec2[](vec2(0, 0), vec2(1, 0), vec2(0, 1),
                            vec2(1, 0), vec2(1, 1), vec2(0, 1));
   vec2 p = c[gl_VertexIndex % 6];
   if (pc.mode == 0u) {
      gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
      col = uvec4(0u);
      return;
   }
   uint cx = pc.cell % pc.grid, cy = pc.cell / pc.grid;
   vec2 q = (vec2(cx, cy) + p) / float(pc.grid);
   gl_Position = vec4(q * 2.0 - 1.0, 0.0, 1.0);
   uint h = pc.cell * 2654435761u + 12345u;
   for (uint i = 0u; i < pc.iters; i++)
      h = h * 1664525u + 1013904223u;
   col = uvec4(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u, (h >> 24) & 255u);
}
