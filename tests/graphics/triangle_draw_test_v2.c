#include <stdbool.h>
/* triangle_draw_test_v2.c
 *
 * Sama seperti triangle_draw_test.c, TAPI skip vkCmdCopyImageToBuffer sama
 * sekali. Image dibikin VK_IMAGE_TILING_LINEAR + HOST_VISIBLE, langsung
 * di-map lewat vkGetImageSubresourceLayout + vkMapMemory. Ini ngisolasi
 * 1 variabel: apa CmdDraw v9 sendiri (Malloc Vertex Job + Fragment Job)
 * nulis sesuatu ke memory, tanpa lewat jalur meta-copy yang mungkin belum
 * lengkap di v9.
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
    fread(buf, 1, sz, f);
    fclose(f);
    *out_size = (size_t)sz;
    return buf;
}

/* The ICD is dlopen()ed directly rather than going through the Vulkan loader,
 * so VK_ICD_FILENAMES has no effect on this test. Point PANVK_ICD_SO at the
 * libvulkan_panfrost.so you actually want to exercise; the default below is
 * the path the committed evidence was produced against, so leaving it unset
 * reproduces that run exactly. */
#ifndef PANVK_DEFAULT_ICD_SO
#define PANVK_DEFAULT_ICD_SO \
    "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so"
#endif

int main(void) {
    const char *icd_path = getenv("PANVK_ICD_SO");
    if (!icd_path || !icd_path[0])
        icd_path = PANVK_DEFAULT_ICD_SO;

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
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
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

    if (!CmdBeginRendering || !CmdDraw || !CreateGraphicsPipelines ||
        !GetImageSubresourceLayout || !MapMemory || !UnmapMemory) {
        printf("FAILED: essential proc addr is NULL\n");
        return 1;
    }

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);

    const uint32_t W = 64, H = 64;
    VkFormat FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

    /* --- Color attachment image: LINEAR + HOST_VISIBLE, skip copy sama sekali --- */
    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = FORMAT,
        .extent = {W, H, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
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
    uint32_t img_mem_type = find_memory_type(&mem_props, img_mreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (img_mem_type == UINT32_MAX) {
        printf("FAILED: no host-visible+coherent memory type fits LINEAR color image\n");
        return 1;
    }
    VkMemoryAllocateInfo img_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = img_mreq.size,
        .memoryTypeIndex = img_mem_type,
    };
    VkDeviceMemory img_mem;
    CHECK(AllocateMemory(device, &img_alloc, NULL, &img_mem), "vkAllocateMemory(image)");
    CHECK(BindImageMemory(device, image, img_mem, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = FORMAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkImageView view;
    CHECK(CreateImageView(device, &view_info, NULL, &view), "vkCreateImageView");

    /* PATCH diagnostic: sentinel 0xAA sebelum submit, sama kayak v1 */
    {
        VkImageSubresource subres = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        VkSubresourceLayout layout;
        GetImageSubresourceLayout(device, image, &subres, &layout);
        void *sentinel_map;
        CHECK(MapMemory(device, img_mem, 0, img_mreq.size, 0, &sentinel_map), "vkMapMemory(sentinel-fill)");
        memset(sentinel_map, 0xAA, img_mreq.size);
        UnmapMemory(device, img_mem);
        printf("sentinel 0xAA ditulis ke image memory (row pitch=%llu) sebelum submit\n",
               (unsigned long long)layout.rowPitch);
        fflush(stdout);
    }

    /* --- Shaders --- */
    size_t vs_size, fs_size;
    char *vs_code = read_file("triangle.vert.spv", &vs_size);
    char *fs_code = read_file("triangle.frag.spv", &fs_size);
    if (!vs_code || !fs_code) return 1;

    VkShaderModuleCreateInfo vs_mod_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vs_size, .pCode = (uint32_t*)vs_code,
    };
    VkShaderModule vs_mod;
    CHECK(CreateShaderModule(device, &vs_mod_info, NULL, &vs_mod), "vkCreateShaderModule(vert)");

    VkShaderModuleCreateInfo fs_mod_info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fs_size, .pCode = (uint32_t*)fs_code,
    };
    VkShaderModule fs_mod;
    CHECK(CreateShaderModule(device, &fs_mod_info, NULL, &fs_mod), "vkCreateShaderModule(frag)");

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    };
    VkPipelineLayout pipeline_layout;
    CHECK(CreatePipelineLayout(device, &layout_info, NULL, &pipeline_layout), "vkCreatePipelineLayout");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs_mod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs_mod, .pName = "main" },
    };
    VkPipelineVertexInputStateCreateInfo vi_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    VkPipelineInputAssemblyStateCreateInfo ia_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    VkPipelineMultisampleStateCreateInfo ms_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineColorBlendAttachmentState cb_att = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    VkPipelineColorBlendStateCreateInfo cb_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cb_att,
    };
    VkDynamicState dyn_states[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn_states,
    };
    VkPipelineRenderingCreateInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &FORMAT,
    };
    VkGraphicsPipelineCreateInfo pipe_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = 2, .pStages = stages,
        .pVertexInputState = &vi_info,
        .pInputAssemblyState = &ia_info,
        .pViewportState = &vp_info,
        .pRasterizationState = &rs_info,
        .pMultisampleState = &ms_info,
        .pColorBlendState = &cb_info,
        .pDynamicState = &dyn_info,
        .layout = pipeline_layout,
    };
    VkPipeline pipeline;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline),
          "vkCreateGraphicsPipelines");

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VkCommandPool pool;
    CHECK(CreateCommandPool(device, &pool_info, NULL, &pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cb_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer cmdbuf;
    CHECK(AllocateCommandBuffers(device, &cb_alloc, &cmdbuf), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
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
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = {0.0f, 0.0f, 0.0f, 1.0f} } },
    };
    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, {W,H} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_att,
    };

    printf("calling vkCmdBeginRendering (with color attachment)...\n"); fflush(stdout);
    CmdBeginRendering(cmdbuf, &rendering);

    VkViewport viewport = { 0, 0, (float)W, (float)H, 0.0f, 1.0f };
    VkRect2D scissor = { {0,0}, {W,H} };
    CmdSetViewport(cmdbuf, 0, 1, &viewport);
    CmdSetScissor(cmdbuf, 0, 1, &scissor);

    CmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    printf("calling vkCmdDraw(3,1,0,0) -- ini yang exercise Malloc Vertex Job v9...\n");
    fflush(stdout);
    CmdDraw(cmdbuf, 3, 1, 0, 0);
    printf("vkCmdDraw returned (no crash)\n"); fflush(stdout);

    CmdEndRendering(cmdbuf);
    printf("vkCmdEndRendering returned (no crash)\n"); fflush(stdout);

    /* Barrier ke HOST read, GANTI TRANSFER - langsung tunggu host-visible */
    VkImageMemoryBarrier to_host = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    CmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT,
                       0, 0, NULL, 0, NULL, 1, &to_host);

    CHECK(EndCommandBuffer(cmdbuf), "vkEndCommandBuffer");

    VkSubmitInfo submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmdbuf,
    };
    printf("calling vkQueueSubmit...\n"); fflush(stdout);
    CHECK(QueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    printf("calling vkQueueWaitIdle...\n"); fflush(stdout);
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    VkImageSubresource subres = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
    VkSubresourceLayout layout;
    GetImageSubresourceLayout(device, image, &subres, &layout);

    void *mapped;
    CHECK(MapMemory(device, img_mem, 0, img_mreq.size, 0, &mapped), "vkMapMemory(image-direct)");
    uint8_t *base = (uint8_t*)mapped + layout.offset;

    printf("\n--- ringkasan pixel image %ux%u (rowPitch=%llu) ---\n", W, H,
           (unsigned long long)layout.rowPitch);
    uint32_t non_black = 0;
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *row = base + y * layout.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *p = &row[x * 4];
            if (!(p[0] == 0 && p[1] == 0 && p[2] == 0))
                non_black++;
        }
    }
    printf("total piksel non-hitam: %u dari %u\n", non_black, W * H);
    for (uint32_t sy = 0; sy < 4; sy++) {
        uint32_t y = (H / 4) * sy + (H / 8);
        uint8_t *row = base + y * layout.rowPitch;
        for (uint32_t sx = 0; sx < 4; sx++) {
            uint32_t x = (W / 4) * sx + (W / 8);
            uint8_t *p = &row[x * 4];
            printf("(%u,%u)=%3u,%3u,%3u,%3u  ", x, y, p[0], p[1], p[2], p[3]);
        }
        printf("\n");
    }

    uint8_t *corner = base;
    uint8_t *center =
        base + (H / 2) * layout.rowPitch + (W / 2) * 4;

    printf("\n--- targeted validation ---\n");
    printf("corner (0,0)   = %u,%u,%u,%u\n",
           corner[0], corner[1], corner[2], corner[3]);
    printf("center (32,32)= %u,%u,%u,%u\n",
           center[0], center[1], center[2], center[3]);

    bool corner_black =
        corner[0] == 0 &&
        corner[1] == 0 &&
        corner[2] == 0 &&
        corner[3] == 255;

    bool center_red =
        center[0] == 255 &&
        center[1] == 0 &&
        center[2] == 0 &&
        center[3] == 255;

    if (corner_black && center_red) {
        printf("\nSUCCESS: partial triangle rasterized -- center merah, corner tetap clear.\n");
    } else if (corner[0] == 170 && corner[1] == 170 &&
               center[0] == 170 && center[1] == 170) {
        printf("\nSENTINEL UTUH: GPU tidak menyentuh image.\n");
    } else if (corner_black &&
               center[0] == 0 &&
               center[1] == 0 &&
               center[2] == 0) {
        printf("\nCLEAR-ONLY: image clear berhasil, triangle tidak merender center.\n");
    } else {
        printf("\nUNEXPECTED: targeted validation gagal; cek pixel dump.\n");
    }

    FILE *ppm = fopen("panvk_triangle.ppm", "wb");
    if (!ppm) {
        perror("fopen panvk_triangle.ppm");
    } else {
        fprintf(ppm, "P6\n%u %u\n255\n", W, H);

        for (uint32_t y = 0; y < H; y++) {
            uint8_t *row = base + y * layout.rowPitch;

            for (uint32_t x = 0; x < W; x++) {
                uint8_t *px = &row[x * 4];
                fwrite(px, 1, 3, ppm);
            }
        }

        fclose(ppm);
        printf("VISUAL_DUMP: panvk_triangle.ppm\\n");
    }

    return 0;
}
