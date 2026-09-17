path = "src/panfrost/vulkan/panvk_vX_shader.c"
with open(path) as f:
    content = f.read()

old = '''   shader->code_mem = panvk_pool_upload_aligned(
      &dev->mempools.exec, shader->bin_ptr, shader->bin_size, 128);
   if (!panvk_priv_mem_check_alloc(shader->code_mem))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);'''

new = '''   shader->code_mem = panvk_pool_upload_aligned(
      &dev->mempools.exec, shader->bin_ptr, shader->bin_size, 128);
   if (!panvk_priv_mem_check_alloc(shader->code_mem))
      return panvk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   {
      const char *stage_name =
         shader->info.stage == MESA_SHADER_VERTEX ? "VERTEX" :
         shader->info.stage == MESA_SHADER_FRAGMENT ? "FRAGMENT" : "OTHER";
      fprintf(stderr, "[PANVK_DEBUG_SHADERBIN] stage=%s bin_size=%u bytes:",
              stage_name, shader->bin_size);
      const uint8_t *b = shader->bin_ptr;
      for (uint32_t i = 0; i < shader->bin_size; i++)
         fprintf(stderr, "%s%02x", (i % 16 == 0) ? "\\n  " : " ", b[i]);
      fprintf(stderr, "\\n");
   }'''

count = content.count(old)
print(f"anchor ketemu {count}x")
if count != 1:
    raise SystemExit("SKIP - anchor gagal, cek manual")
content = content.replace(old, new, 1)
with open(path, "w") as f:
    f.write(content)
print("APPLIED")
