/* restab_descset_test.c
 *
 * Phase 4.2 — exercises the application descriptor-set path on Mali-G57 MC2
 * (v9/JM), which T4.2.1 showed is never touched by the existing triangle test
 * (runtime used_set_mask = 0x0).
 *
 * Each descriptor set drives exactly one colour channel, so a wrong pixel names
 * which set failed rather than just saying "wrong". Read the centre pixel as a
 * per-set report card:
 *
 *   2sets  : set0 -> R, set1 -> G          expect (191, 128,   0, 255)
 *   4sets  : set0 -> R, 1 -> G, 2 -> B, 3 -> A
 *                                          expect (191, 128,  64, 255)
 *   sparse : set0 -> R, set3 -> B          expect (191,   0,  64, 255)
 *
 * A black centre means no set was read. A partially-correct centre isolates the
 * failing set. That property is the whole point of the design.
 *
 * Geometry and framebuffer setup are copied unchanged from
 * triangle_draw_test_v2.c so the only new variable is the descriptor sets.
 *
 * Select a config with RESTAB_CONFIG=2sets|4sets|sparse (default 2sets).
 * Pair with PANVK_DEBUG_RESTAB=1 to see the resource table the driver builds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vulkan/vulkan_core.h"

#define CHECK(expr, msg) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { \
        printf("FAILED: %s (VkResult=%d)\n", msg, _r); \
        return 1; \
    } \
    printf("%s ok\n", msg); \
    fflush(stdout); \
} while (0)

typedef PFN_vkVoidFunction (*PFN_icdGetInstanceProcAddr)(VkInstance, const char*);

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *mp,
                                  uint32_t type_bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++) {
        if ((type_bits & (1u << i)) &&
            (mp->memoryTypes[i].propertyFlags & want) == want)
            return i;
    }
    return UINT32_MAX;
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAILED: cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        printf("FAILED: short read on %s\n", path); fclose(f); free(buf); return NULL;
    }
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

#ifndef PANVK_DEFAULT_ICD_SO
#define PANVK_DEFAULT_ICD_SO \
    "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so"
#endif

#define MAX_SETS 4

struct config {
    const char *name;
    const char *fs_spv;
    uint32_t layout_set_count;      /* size of pSetLayouts, indexed by set number */
    uint32_t used[MAX_SETS];        /* which set indices carry a UBO */
    uint32_t used_count;
    float    ubo[MAX_SETS][4];      /* vec4 contents per used set */
    uint8_t  expect[4];             /* expected centre pixel */
    uint32_t expect_mask;           /* predicted driver used_set_mask */
    uint32_t expect_res_count;      /* predicted res_count */
};

int main(void) {
    const char *cfg_name = getenv("RESTAB_CONFIG");
    if (!cfg_name || !cfg_name[0]) cfg_name = "2sets";

    /* 0.75 -> 191, 0.5 -> 128, 0.25 -> 64 after UNORM conversion (+/-1). */
    struct config cfgs[] = {
        { .name = "2sets", .fs_spv = "ubo2.frag.spv",
          .layout_set_count = 2, .used = {0,1}, .used_count = 2,
          .ubo = {{0.75f,0,0,0},{0,0.5f,0,0}},
          .expect = {191,128,0,255}, .expect_mask = 0x3, .expect_res_count = 4 },
        /* Control for 2sets: same shader, same bindings, DIFFERENT UBO contents.
         * If the centre pixel does not move to match, the shader is not really
         * reading the buffers and the 2sets pass was meaningless. */
        { .name = "2sets_alt", .fs_spv = "ubo2.frag.spv",
          .layout_set_count = 2, .used = {0,1}, .used_count = 2,
          .ubo = {{0.25f,0,0,0},{0,1.0f,0,0}},
          .expect = {64,255,0,255}, .expect_mask = 0x3, .expect_res_count = 4 },
        /* T4.2.6: blue is a shader constant, so geometry stays visible even when
         * every descriptor read returns zero. Lets "no triangle" be told apart
         * from "triangle with zeroed descriptors". */
        { .name = "2sets_const", .fs_spv = "ubo2_const.frag.spv",
          .layout_set_count = 2, .used = {0,1}, .used_count = 2,
          .ubo = {{0.75f,0,0,0},{0,0.5f,0,0}},
          .expect = {191,128,128,255}, .expect_mask = 0x3, .expect_res_count = 4 },
        { .name = "4sets", .fs_spv = "ubo4.frag.spv",
          .layout_set_count = 4, .used = {0,1,2,3}, .used_count = 4,
          .ubo = {{0.75f,0,0,0},{0,0.5f,0,0},{0,0,0.25f,0},{0,0,0,1.0f}},
          .expect = {191,128,64,255}, .expect_mask = 0xf, .expect_res_count = 8 },
        { .name = "sparse", .fs_spv = "ubo_sparse.frag.spv",
          .layout_set_count = 4, .used = {0,3}, .used_count = 2,
          .ubo = {{0.75f,0,0,0},{0,0,0.25f,0}},
          .expect = {191,0,64,255}, .expect_mask = 0x9, .expect_res_count = 8 },
    };
    struct config *cfg = NULL;
    for (unsigned i = 0; i < sizeof(cfgs)/sizeof(cfgs[0]); i++)
        if (!strcmp(cfg_name, cfgs[i].name)) cfg = &cfgs[i];
    if (!cfg) { printf("FAILED: unknown RESTAB_CONFIG=%s\n", cfg_name); return 1; }

    printf("=== config: %s ===\n", cfg->name);
    printf("used sets      :");
    for (uint32_t i = 0; i < cfg->used_count; i++) printf(" %u", cfg->used[i]);
    printf("\nlayout sets    : %u\n", cfg->layout_set_count);
    printf("predicted mask : 0x%x\n", cfg->expect_mask);
    printf("predicted count: %u\n", cfg->expect_res_count);
    printf("expected pixel : %u,%u,%u,%u\n",
           cfg->expect[0], cfg->expect[1], cfg->expect[2], cfg->expect[3]);
    fflush(stdout);

    const char *icd_path = getenv("PANVK_ICD_SO");
    if (!icd_path || !icd_path[0]) icd_path = PANVK_DEFAULT_ICD_SO;
    printf("loading ICD: %s\n", icd_path);
    void *lib = dlopen(icd_path, RTLD_NOW);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }

    PFN_icdGetInstanceProcAddr icd_gpa =
        (PFN_icdGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!icd_gpa) { printf("dlsym failed: %s\n", dlerror()); return 1; }

    PFN_vkCreateInstance CreateInstance =
        (PFN_vkCreateInstance)icd_gpa(NULL, "vkCreateInstance");

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };
    VkInstance instance;
    CHECK(CreateInstance(&inst_info, NULL, &instance), "vkCreateInstance");

    #define IPROC(name) (PFN_vk##name)icd_gpa(instance, "vk" #name)
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = IPROC(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = IPROC(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CreateDevice = IPROC(CreateDevice);
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProperties = IPROC(GetPhysicalDeviceMemoryProperties);

    uint32_t count = 0;
    CHECK(EnumeratePhysicalDevices(instance, &count, NULL), "EnumeratePhysicalDevices(count)");
    if (count == 0) { printf("FAILED: no physical device\n"); return 1; }
    VkPhysicalDevice pdev;
    CHECK(EnumeratePhysicalDevices(instance, &count, &pdev), "EnumeratePhysicalDevices(fetch)");

    VkPhysicalDeviceMemoryProperties mem_props;
    GetMemoryProperties(pdev, &mem_props);

    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue_info,
    };
    VkDevice device;
    CHECK(CreateDevice(pdev, &dev_info, NULL, &device), "vkCreateDevice");

    #define DPROC(name) (PFN_vk##name)GetDeviceProcAddr(device, "vk" #name)
    PFN_vkGetDeviceQueue GetDeviceQueue = DPROC(GetDeviceQueue);
    PFN_vkCreateCommandPool CreateCommandPool = DPROC(CreateCommandPool);
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = DPROC(AllocateCommandBuffers);
    PFN_vkBeginCommandBuffer BeginCommandBuffer = DPROC(BeginCommandBuffer);
    PFN_vkEndCommandBuffer EndCommandBuffer = DPROC(EndCommandBuffer);
    PFN_vkQueueSubmit QueueSubmit = DPROC(QueueSubmit);
    PFN_vkQueueWaitIdle QueueWaitIdle = DPROC(QueueWaitIdle);
    PFN_vkCreateImage CreateImage = DPROC(CreateImage);
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = DPROC(GetImageMemoryRequirements);
    PFN_vkGetImageSubresourceLayout GetImageSubresourceLayout = DPROC(GetImageSubresourceLayout);
    PFN_vkAllocateMemory AllocateMemory = DPROC(AllocateMemory);
    PFN_vkBindImageMemory BindImageMemory = DPROC(BindImageMemory);
    PFN_vkCreateImageView CreateImageView = DPROC(CreateImageView);
    PFN_vkMapMemory MapMemory = DPROC(MapMemory);
    PFN_vkUnmapMemory UnmapMemory = DPROC(UnmapMemory);
    PFN_vkCreateShaderModule CreateShaderModule = DPROC(CreateShaderModule);
    PFN_vkCreatePipelineLayout CreatePipelineLayout = DPROC(CreatePipelineLayout);
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = DPROC(CreateGraphicsPipelines);
    PFN_vkCmdBeginRendering CmdBeginRendering = DPROC(CmdBeginRendering);
    PFN_vkCmdEndRendering CmdEndRendering = DPROC(CmdEndRendering);
    PFN_vkCmdBindPipeline CmdBindPipeline = DPROC(CmdBindPipeline);
    PFN_vkCmdSetViewport CmdSetViewport = DPROC(CmdSetViewport);
    PFN_vkCmdSetScissor CmdSetScissor = DPROC(CmdSetScissor);
    PFN_vkCmdDraw CmdDraw = DPROC(CmdDraw);
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = DPROC(CmdPipelineBarrier);
    /* descriptor-set specific */
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = DPROC(CreateDescriptorSetLayout);
    PFN_vkCreateDescriptorPool CreateDescriptorPool = DPROC(CreateDescriptorPool);
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets = DPROC(AllocateDescriptorSets);
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = DPROC(UpdateDescriptorSets);
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = DPROC(CmdBindDescriptorSets);
    PFN_vkCreateBuffer CreateBuffer = DPROC(CreateBuffer);
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = DPROC(GetBufferMemoryRequirements);
    PFN_vkBindBufferMemory BindBufferMemory = DPROC(BindBufferMemory);

    if (!CreateDescriptorSetLayout || !CreateDescriptorPool || !AllocateDescriptorSets ||
        !UpdateDescriptorSets || !CmdBindDescriptorSets || !CreateBuffer ||
        !GetBufferMemoryRequirements || !BindBufferMemory) {
        printf("FAILED: a descriptor-set entry point is NULL -- "
               "NOT IMPLEMENTED in this ICD\n");
        return 1;
    }
    if (!CmdBeginRendering || !CmdDraw || !CreateGraphicsPipelines ||
        !GetImageSubresourceLayout || !MapMemory || !UnmapMemory) {
        printf("FAILED: essential proc addr is NULL\n");
        return 1;
    }

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);

    const uint32_t W = 64, H = 64;
    VkFormat FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

    /* ---------------- UBO buffers, one per used set ---------------- */
    VkBuffer ubo_buf[MAX_SETS];
    VkDeviceMemory ubo_mem[MAX_SETS];
    for (uint32_t i = 0; i < cfg->used_count; i++) {
        VkBufferCreateInfo bci = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = sizeof(float) * 4,
            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        CHECK(CreateBuffer(device, &bci, NULL, &ubo_buf[i]), "vkCreateBuffer(ubo)");
        VkMemoryRequirements mreq;
        GetBufferMemoryRequirements(device, ubo_buf[i], &mreq);
        uint32_t mt = find_memory_type(&mem_props, mreq.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mt == UINT32_MAX) { printf("FAILED: no host-visible memory for UBO\n"); return 1; }
        VkMemoryAllocateInfo mai = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mreq.size, .memoryTypeIndex = mt,
        };
        CHECK(AllocateMemory(device, &mai, NULL, &ubo_mem[i]), "vkAllocateMemory(ubo)");
        CHECK(BindBufferMemory(device, ubo_buf[i], ubo_mem[i], 0), "vkBindBufferMemory");
        void *p = NULL;
        CHECK(MapMemory(device, ubo_mem[i], 0, sizeof(float)*4, 0, &p), "vkMapMemory(ubo)");
        memcpy(p, cfg->ubo[i], sizeof(float)*4);
        UnmapMemory(device, ubo_mem[i]);
        printf("  UBO for set %u = (%.3f, %.3f, %.3f, %.3f)\n", cfg->used[i],
               cfg->ubo[i][0], cfg->ubo[i][1], cfg->ubo[i][2], cfg->ubo[i][3]);
    }

    /* ---------------- descriptor set layouts ----------------
     * pSetLayouts is indexed by set number, so unused indices in the sparse
     * config still need a (empty) layout to keep the numbering aligned. */
    VkDescriptorSetLayoutBinding binding0 = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkDescriptorSetLayout ubo_layout, empty_layout;
    VkDescriptorSetLayoutCreateInfo dsl_ubo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding0,
    };
    CHECK(CreateDescriptorSetLayout(device, &dsl_ubo, NULL, &ubo_layout),
          "vkCreateDescriptorSetLayout(ubo)");
    VkDescriptorSetLayoutCreateInfo dsl_empty = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    };
    CHECK(CreateDescriptorSetLayout(device, &dsl_empty, NULL, &empty_layout),
          "vkCreateDescriptorSetLayout(empty)");

    VkDescriptorSetLayout set_layouts[MAX_SETS];
    for (uint32_t s = 0; s < cfg->layout_set_count; s++) {
        set_layouts[s] = empty_layout;
        for (uint32_t i = 0; i < cfg->used_count; i++)
            if (cfg->used[i] == s) set_layouts[s] = ubo_layout;
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = cfg->layout_set_count,
        .pSetLayouts = set_layouts,
    };
    VkPipelineLayout pipeline_layout;
    CHECK(CreatePipelineLayout(device, &layout_info, NULL, &pipeline_layout),
          "vkCreatePipelineLayout");

    /* ---------------- descriptor pool + sets ---------------- */
    VkDescriptorPoolSize pool_size = {
        .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        .descriptorCount = cfg->used_count,
    };
    VkDescriptorPoolCreateInfo dpci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = cfg->used_count,
        .poolSizeCount = 1, .pPoolSizes = &pool_size,
    };
    VkDescriptorPool desc_pool;
    CHECK(CreateDescriptorPool(device, &dpci, NULL, &desc_pool), "vkCreateDescriptorPool");

    VkDescriptorSet desc_sets[MAX_SETS];
    for (uint32_t i = 0; i < cfg->used_count; i++) {
        VkDescriptorSetAllocateInfo dsai = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = desc_pool,
            .descriptorSetCount = 1, .pSetLayouts = &ubo_layout,
        };
        CHECK(AllocateDescriptorSets(device, &dsai, &desc_sets[i]),
              "vkAllocateDescriptorSets");
        VkDescriptorBufferInfo dbi = {
            .buffer = ubo_buf[i], .offset = 0, .range = sizeof(float)*4,
        };
        VkWriteDescriptorSet wds = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = desc_sets[i], .dstBinding = 0, .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &dbi,
        };
        UpdateDescriptorSets(device, 1, &wds, 0, NULL);
    }
    printf("descriptor sets allocated and updated: %u\n", cfg->used_count);
    fflush(stdout);

    /* ---------------- colour attachment: LINEAR + HOST_VISIBLE ---------------- */
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = FORMAT,
        .extent = {W, H, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image;
    CHECK(CreateImage(device, &img_info, NULL, &image), "vkCreateImage");
    VkMemoryRequirements img_mreq;
    GetImageMemoryRequirements(device, image, &img_mreq);
    uint32_t img_mt = find_memory_type(&mem_props, img_mreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (img_mt == UINT32_MAX) { printf("FAILED: no host-visible memory for image\n"); return 1; }
    VkMemoryAllocateInfo img_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = img_mreq.size, .memoryTypeIndex = img_mt,
    };
    VkDeviceMemory img_mem;
    CHECK(AllocateMemory(device, &img_alloc, NULL, &img_mem), "vkAllocateMemory(image)");
    CHECK(BindImageMemory(device, image, img_mem, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FORMAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView view;
    CHECK(CreateImageView(device, &view_info, NULL, &view), "vkCreateImageView");

    /* ---------------- shaders ---------------- */
    size_t vs_size = 0, fs_size = 0;
    char *vs_code = read_file("triangle.vert.spv", &vs_size);
    if (!vs_code) return 1;
    char *fs_code = read_file(cfg->fs_spv, &fs_size);
    if (!fs_code) return 1;
    printf("shaders: triangle.vert.spv (%zu B), %s (%zu B)\n",
           vs_size, cfg->fs_spv, fs_size);

    VkShaderModuleCreateInfo vs_mci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vs_size, .pCode = (uint32_t*)vs_code,
    };
    VkShaderModule vs_mod;
    CHECK(CreateShaderModule(device, &vs_mci, NULL, &vs_mod), "vkCreateShaderModule(vert)");
    VkShaderModuleCreateInfo fs_mci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fs_size, .pCode = (uint32_t*)fs_code,
    };
    VkShaderModule fs_mod;
    CHECK(CreateShaderModule(device, &fs_mci, NULL, &fs_mod), "vkCreateShaderModule(frag)");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs_mod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs_mod, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cb_att = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cb_att };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn_states };
    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &FORMAT };
    VkGraphicsPipelineCreateInfo pipe_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi_info, .pInputAssemblyState = &ia_info,
        .pViewportState = &vp_info, .pRasterizationState = &rs_info,
        .pMultisampleState = &ms_info, .pColorBlendState = &cb_info,
        .pDynamicState = &dyn_info, .layout = pipeline_layout,
    };
    VkPipeline pipeline;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline),
          "vkCreateGraphicsPipelines");

    /* ---------------- record ---------------- */
    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool pool;
    CHECK(CreateCommandPool(device, &pool_info, NULL, &pool), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo cb_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1 };
    VkCommandBuffer cmdbuf;
    CHECK(AllocateCommandBuffers(device, &cb_alloc, &cmdbuf), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(BeginCommandBuffer(cmdbuf, &begin_info), "vkBeginCommandBuffer");

    VkImageMemoryBarrier to_attachment = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    CmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       0, 0, NULL, 0, NULL, 1, &to_attachment);

    VkRenderingAttachmentInfo color_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = {0.0f, 0.0f, 0.0f, 1.0f} } },
    };
    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, {W,H} }, .layerCount = 1,
        .colorAttachmentCount = 1, .pColorAttachments = &color_att,
    };
    CmdBeginRendering(cmdbuf, &rendering);

    VkViewport viewport = { 0, 0, (float)W, (float)H, 0.0f, 1.0f };
    VkRect2D scissor = { {0,0}, {W,H} };
    CmdSetViewport(cmdbuf, 0, 1, &viewport);
    CmdSetScissor(cmdbuf, 0, 1, &scissor);
    CmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    /* Bind each used set at its own index. Non-contiguous indices are bound
     * separately rather than as one range, so the sparse config does not need
     * dummy sets allocated for the gaps. */
    for (uint32_t i = 0; i < cfg->used_count; i++) {
        CmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipeline_layout, cfg->used[i], 1, &desc_sets[i],
                              0, NULL);
        printf("bound descriptor set at index %u\n", cfg->used[i]);
    }
    fflush(stdout);

    CmdDraw(cmdbuf, 3, 1, 0, 0);
    CmdEndRendering(cmdbuf);
    CHECK(EndCommandBuffer(cmdbuf), "vkEndCommandBuffer");

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmdbuf };
    CHECK(QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    /* ---------------- readback ---------------- */
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout sl;
    GetImageSubresourceLayout(device, image, &sub, &sl);
    printf("rowPitch=%llu offset=%llu\n",
           (unsigned long long)sl.rowPitch, (unsigned long long)sl.offset);

    void *mapped = NULL;
    CHECK(MapMemory(device, img_mem, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(image)");
    uint8_t *base = (uint8_t*)mapped + sl.offset;

    uint32_t non_black = 0;
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *row = base + y * sl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *px = row + x * 4;
            if (px[0] || px[1] || px[2]) non_black++;
        }
    }
    uint8_t *centre = base + 32 * sl.rowPitch + 32 * 4;
    uint8_t *corner = base + 0 * sl.rowPitch + 0 * 4;

    printf("\n--- result: %s ---\n", cfg->name);
    printf("non-black pixels : %u / %u\n", non_black, W*H);
    printf("centre (32,32)   = %u,%u,%u,%u\n", centre[0], centre[1], centre[2], centre[3]);
    printf("corner (0,0)     = %u,%u,%u,%u\n", corner[0], corner[1], corner[2], corner[3]);
    printf("expected centre  = %u,%u,%u,%u\n",
           cfg->expect[0], cfg->expect[1], cfg->expect[2], cfg->expect[3]);

    int ok = 1;
    for (int c = 0; c < 4; c++) {
        int d = (int)centre[c] - (int)cfg->expect[c];
        if (d < -2 || d > 2) ok = 0;
    }
    /* Per-channel report card: name the set behind each wrong channel. */
    if (!ok) {
        printf("\nper-set diagnosis:\n");
        for (uint32_t i = 0; i < cfg->used_count; i++) {
            int ch = -1;
            for (int c = 0; c < 4; c++) if (cfg->ubo[i][c] != 0.0f) ch = c;
            if (ch < 0) continue;
            int d = (int)centre[ch] - (int)cfg->expect[ch];
            printf("  set %u -> channel %d: got %u expected %u  %s\n",
                   cfg->used[i], ch, centre[ch], cfg->expect[ch],
                   (d >= -2 && d <= 2) ? "OK" : "WRONG");
        }
    }
    /* Visual dump: RESTAB_PPM=<path> writes the framebuffer as binary PPM so the
     * result can be looked at as an image rather than only counted. */
    const char *ppm_path = getenv("RESTAB_PPM");
    if (ppm_path && ppm_path[0]) {
        FILE *pf = fopen(ppm_path, "wb");
        if (!pf) {
            printf("WARNING: cannot write %s\n", ppm_path);
        } else {
            fprintf(pf, "P6\n%u %u\n255\n", W, H);
            for (uint32_t y = 0; y < H; y++) {
                uint8_t *row = base + y * sl.rowPitch;
                for (uint32_t x = 0; x < W; x++) {
                    uint8_t *px = row + x * 4;
                    uint8_t rgb[3] = { px[0], px[1], px[2] };
                    fwrite(rgb, 1, 3, pf);
                }
            }
            fclose(pf);
            printf("VISUAL_DUMP: %s\n", ppm_path);
        }
    }

    UnmapMemory(device, img_mem);

    if (corner[0] || corner[1] || corner[2]) {
        printf("\nWARNING: corner is not clear, geometry may be wrong\n");
    }
    printf("\n%s\n", ok ? "SUCCESS: descriptor sets read correctly"
                        : "FAILED: centre pixel does not match");
    free(vs_code); free(fs_code);
    return ok ? 0 : 1;
}
