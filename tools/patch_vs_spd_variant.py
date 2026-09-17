path = 'src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c'
with open(path) as f:
    content = f.read()

old = '''      cfg.shader = panvk_priv_mem_dev_addr(vs->spd);'''
new = '''      cfg.shader = panvk_priv_mem_dev_addr(
         draw->info.prim == MESA_PRIM_POINTS ? vs->spds.pos_points
                                              : vs->spds.pos_triangles);'''
n = content.count(old)
print('anchor count:', n)
assert n == 1, f"Anchor ketemu {n}x, harus persis 1 -- cek manual dulu sebelum lanjut"
content = content.replace(old, new, 1)

with open(path, 'w') as f:
    f.write(content)
print('WRITTEN')
