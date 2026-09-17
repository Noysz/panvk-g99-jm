#!/usr/bin/env python3
path = "/data/data/com.termux/files/home/panvk-g57/mesa/src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c"

with open(path, "r") as f:
    content = f.read()

old = '''   shader_desc_state->driver_set.dev_addr = driver_set.gpu;
   shader_desc_state->driver_set.size = desc_count * PANVK_DESCRIPTOR_SIZE;
   gfx_state_set_dirty(cmdbuf, DESC_STATE);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,'''

n = content.count(old)
print("full anchor count:", n)

if n == 1:
    idx = content.find(old)
    print("--- konteks sekitar match (200 char sebelum & sesudah) ---")
    print(content[max(0, idx-200):idx+len(old)+200])
