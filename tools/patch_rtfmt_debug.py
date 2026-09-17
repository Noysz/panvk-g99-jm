path = 'src/panfrost/lib/pan_fb.c'
with open(path) as f:
    content = f.read()

old = '''   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      emit_rgb_rt_desc(info, ct, rt, tile_rt_offset_B, rts);'''
new = '''   for (unsigned rt = 0; rt < fb->rt_count; rt++) {
      fprintf(stderr, "[PANVK_DEBUG_RTFMT] rt=%u fmt=%d rt_count=%u\\n", rt, fb->rt_formats[rt], fb->rt_count);
      emit_rgb_rt_desc(info, ct, rt, tile_rt_offset_B, rts);'''
n = content.count(old)
print('anchor count:', n)
assert n == 1
content = content.replace(old, new, 1)

with open(path, 'w') as f:
    f.write(content)
print('WRITTEN')
