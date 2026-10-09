#version 450
// tests/graphics/longjob_test.c, see longjob.vert.
layout(push_constant) uniform P { uint iters; uint mode; uint cell; uint grid; } pc;
layout(location = 0) flat in uvec4 col;
layout(location = 0) out vec4 o;

void main()
{
   if (pc.mode == 1u) {
      o = vec4(col) / 255.0;
      return;
   }
   uint x = uint(gl_FragCoord.x), y = uint(gl_FragCoord.y);
   uint h = (x * 73856093u) ^ (y * 19349663u);
   for (uint i = 0u; i < pc.iters; i++)
      h = h * 1664525u + 1013904223u;
   o = vec4(uvec4(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u, (h >> 24) & 255u)) / 255.0;
}
