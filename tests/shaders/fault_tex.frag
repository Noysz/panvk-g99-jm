#version 450
// tests/graphics/fault_frag_test.c: every pixel fetches the texture
// pc.iters times, so the fragment job is still reading it when the CPU frees
// its memory. Vertex shader: longjob.vert (mode 0, fullscreen quad).
layout(set = 0, binding = 0) uniform sampler2D tex;
layout(push_constant) uniform P { uint iters; uint mode; uint cell; uint grid; } pc;
layout(location = 0) out vec4 o;

void main()
{
   vec4 acc = vec4(0.0);
   uint x = uint(gl_FragCoord.x), y = uint(gl_FragCoord.y);
   for (uint i = 0u; i < pc.iters; i++)
      acc += texelFetch(tex, ivec2((x + i * 7u) & 255u, (y + i * 3u) & 255u), 0);
   o = acc / float(max(pc.iters, 1u));
}
