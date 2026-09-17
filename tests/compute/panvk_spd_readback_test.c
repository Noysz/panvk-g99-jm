#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"

#define CHECK(x, msg) do { VkResult r = (x); if (r != VK_SUCCESS) { printf("FAILED %s: VkResult=%d\n", msg, r); return 1; } printf("%s ok\n", msg); } while (0)

static PFN_vkGetInstanceProcAddr icd_gpa;
static VkInstance instance;
static PFN_vkGetDeviceProcAddr GetDeviceProcAddr;

#define IPROC(name) (PFN_##name)icd_gpa(instance, #name)
#define DPROC(dev, name) (PFN_##name)GetDeviceProcAddr(dev, #name)

#define W 64
#define H 64

static uint32_t *read_spv(const char *path, size_t *out_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("open %s failed\n", path); return NULL; }
    static uint8_t buf[65536];
    ssize_t n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n <= 0) { printf("read %s failed\n", path); return NULL; }
    *out_size = (size_t)n;
    return (uint32_t *)buf;
}

int main(void) {
    void *lib = dlopen("/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    icd_gpa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");

    PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)icd_gpa(NULL, "vkCreateInstance");
    VkApplicationInfo app_info = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_2 };
    VkInstanceCreateInfo inst_info = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app_info };
    CHECK(CreateInstance(&inst_info, NULL, &instance), "vkCreateInstance");
    fflush(stdout);

    GetDeviceProcAddr = IPROC(vkGetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = IPROC(vkEnumeratePhysicalDevices);
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProperties = IPROC(vkGetPhysicalDeviceMemoryProperties);
    PFN_vkCreateDevice CreateDevice = IPROC(vkCreateDevice);

    uint32_t count = 0;
    CHECK(EnumeratePhysicalDevices(instance, &count, NULL), "EnumeratePhysicalDevices(count)");
    VkPhysicalDevice pdev;
    CHECK(EnumeratePhysicalDevices(instance, &count, &pdev), "EnumeratePhysicalDevices(fetch)");

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qinfo = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority };

    VkPhysicalDeviceDynamicRenderingFeaturesKHR dyn_feat = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR,
        .dynamicRendering = VK_TRUE,
    };
    const char *dev_ext = "VK_KHR_dynamic_rendering";
    VkDeviceCreateInfo dinfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &dyn_feat,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &qinfo,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = &dev_ext,
    };
    VkDevice device;
    CHECK(CreateDevice(pdev, &dinfo, NULL, &device), "vkCreateDevice");

    PFN_vkCreateImage CreateImage = DPROC(device, vkCreateImage);
    PFN_vkGetImageMemoryRequirements GetImageMemoryRequirements = DPROC(device, vkGetImageMemoryRequirements);
    PFN_vkAllocateMemory AllocateMemory = DPROC(device, vkAllocateMemory);
    PFN_vkBindImageMemory BindImageMemory = DPROC(device, vkBindImageMemory);
    PFN_vkCreateImageView CreateImageView = DPROC(device, vkCreateImageView);
    PFN_vkMapMemory MapMemory = DPROC(device, vkMapMemory);

    VkImageCreateInfo img_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = { W, H, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkImage image;
    CHECK(CreateImage(device, &img_info, NULL, &image), "vkCreateImage");

    VkMemoryRequirements img_mem_req;
    GetImageMemoryRequirements(device, image, &img_mem_req);

    VkPhysicalDeviceMemoryProperties mem_props;
    GetMemoryProperties(pdev, &mem_props);

    uint32_t host_visible_type = 0;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if (mem_props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            host_visible_type = i;
            break;
        }
    }

    VkMemoryAllocateInfo alloc_info = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = img_mem_req.size, .memoryTypeIndex = host_visible_type };
    VkDeviceMemory img_memory;
    CHECK(AllocateMemory(device, &alloc_info, NULL, &img_memory), "vkAllocateMemory(image)");
    CHECK(BindImageMemory(device, image, img_memory, 0), "vkBindImageMemory");

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
    };
    VkImageView view;
    CHECK(CreateImageView(device, &view_info, NULL, &view), "vkCreateImageView");

    /* --- SSBO debug buffer: 3 vec4 = 48 byte, buat baca posisi vertex --- */
    PFN_vkCreateBuffer CreateBuffer = DPROC(device, vkCreateBuffer);
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = DPROC(device, vkGetBufferMemoryRequirements);
    PFN_vkBindBufferMemory BindBufferMemory = DPROC(device, vkBindBufferMemory);

    VkBufferCreateInfo dbg_buf_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = 48,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkBuffer dbg_buf;
    CHECK(CreateBuffer(device, &dbg_buf_info, NULL, &dbg_buf), "vkCreateBuffer(dbg)");

    VkMemoryRequirements dbg_mem_req;
    GetBufferMemoryRequirements(device, dbg_buf, &dbg_mem_req);

    VkMemoryAllocateInfo dbg_alloc_info = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = dbg_mem_req.size, .memoryTypeIndex = host_visible_type };
    VkDeviceMemory dbg_memory;
    CHECK(AllocateMemory(device, &dbg_alloc_info, NULL, &dbg_memory), "vkAllocateMemory(dbg)");
    CHECK(BindBufferMemory(device, dbg_buf, dbg_memory, 0), "vkBindBufferMemory(dbg)");

    void *dbg_mapped;
    CHECK(MapMemory(device, dbg_memory, 0, 48, 0, &dbg_mapped), "vkMapMemory(dbg)");
    memset(dbg_mapped, 0xAA, 48);
    printf("SSBO debug diisi sentinel 0xAA sebelum submit\n");

    /* --- Descriptor set layout: 1 storage buffer, stage vertex --- */
    PFN_vkCreateDescriptorSetLayout CreateDescriptorSetLayout = DPROC(device, vkCreateDescriptorSetLayout);
    PFN_vkCreateDescriptorPool CreateDescriptorPool = DPROC(device, vkCreateDescriptorPool);
    PFN_vkAllocateDescriptorSets AllocateDescriptorSets = DPROC(device, vkAllocateDescriptorSets);
    PFN_vkUpdateDescriptorSets UpdateDescriptorSets = DPROC(device, vkUpdateDescriptorSets);

    VkDescriptorSetLayoutBinding dbg_binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
    };
    VkDescriptorSetLayoutCreateInfo dsl_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &dbg_binding,
    };
    VkDescriptorSetLayout dsl;
    CHECK(CreateDescriptorSetLayout(device, &dsl_info, NULL, &dsl), "vkCreateDescriptorSetLayout");

    VkDescriptorPoolSize pool_size = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo dp_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
    VkDescriptorPool dpool;
    CHECK(CreateDescriptorPool(device, &dp_info, NULL, &dpool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo ds_alloc = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = dpool, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VkDescriptorSet dset;
    CHECK(AllocateDescriptorSets(device, &ds_alloc, &dset), "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo dbg_buf_desc = { .buffer = dbg_buf, .offset = 0, .range = 48 };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = dset, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .pBufferInfo = &dbg_buf_desc,
    };
    UpdateDescriptorSets(device, 1, &write, 0, NULL);

    /* --- Shader modules, pipeline layout (SEKARANG pakai dsl), pipeline --- */
    PFN_vkCreateShaderModule CreateShaderModule = DPROC(device, vkCreateShaderModule);
    PFN_vkCreatePipelineLayout CreatePipelineLayout = DPROC(device, vkCreatePipelineLayout);
    PFN_vkCreateGraphicsPipelines CreateGraphicsPipelines = DPROC(device, vkCreateGraphicsPipelines);

    size_t vs_size, fs_size;
    uint32_t *vs_code = read_spv("/data/data/com.termux/files/home/triangle_debug_ssbo.vert.spv", &vs_size);
    if (!vs_code) return 1;
    VkShaderModuleCreateInfo vs_ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = vs_size, .pCode = vs_code };
    VkShaderModule vs_mod;
    CHECK(CreateShaderModule(device, &vs_ci, NULL, &vs_mod), "vkCreateShaderModule(vert)");

    uint32_t *fs_code = read_spv("/data/data/com.termux/files/home/triangle_frag.spv", &fs_size);
    if (!fs_code) return 1;
    VkShaderModuleCreateInfo fs_ci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = fs_size, .pCode = fs_code };
    VkShaderModule fs_mod;
    CHECK(CreateShaderModule(device, &fs_ci, NULL, &fs_mod), "vkCreateShaderModule(frag)");

    VkPipelineLayoutCreateInfo pl_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &dsl,
    };
    VkPipelineLayout playout;
    CHECK(CreatePipelineLayout(device, &pl_info, NULL, &playout), "vkCreatePipelineLayout");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs_mod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs_mod, .pName = "main" },
    };

    VkPipelineVertexInputStateCreateInfo vi_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };

    VkViewport viewport = { 0, 0, (float)W, (float)H, 0.0f, 1.0f };
    VkRect2D scissor = { {0, 0}, {W, H} };
    VkPipelineViewportStateCreateInfo vp_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &viewport, .scissorCount = 1, .pScissors = &scissor };

    VkPipelineRasterizationStateCreateInfo rs_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };

    VkPipelineColorBlendAttachmentState cb_att = { .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb_state = { .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &cb_att };

    VkFormat color_format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfoKHR rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &color_format,
    };

    VkGraphicsPipelineCreateInfo pipe_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &rendering_info,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vi_state,
        .pInputAssemblyState = &ia_state,
        .pViewportState = &vp_state,
        .pRasterizationState = &rs_state,
        .pMultisampleState = &ms_state,
        .pColorBlendState = &cb_state,
        .layout = playout,
    };
    VkPipeline pipeline;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_info, NULL, &pipeline), "vkCreateGraphicsPipelines");

    PFN_vkCreateCommandPool CreateCommandPool = DPROC(device, vkCreateCommandPool);
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = DPROC(device, vkAllocateCommandBuffers);
    PFN_vkBeginCommandBuffer BeginCommandBuffer = DPROC(device, vkBeginCommandBuffer);
    PFN_vkEndCommandBuffer EndCommandBuffer = DPROC(device, vkEndCommandBuffer);
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = DPROC(device, vkCmdPipelineBarrier);
    PFN_vkCmdBeginRendering CmdBeginRendering = (PFN_vkCmdBeginRendering)DPROC(device, vkCmdBeginRenderingKHR);
    PFN_vkCmdEndRendering CmdEndRendering = (PFN_vkCmdEndRendering)DPROC(device, vkCmdEndRenderingKHR);
    PFN_vkCmdBindPipeline CmdBindPipeline = DPROC(device, vkCmdBindPipeline);
    PFN_vkCmdBindDescriptorSets CmdBindDescriptorSets = DPROC(device, vkCmdBindDescriptorSets);
    PFN_vkCmdDraw CmdDraw = DPROC(device, vkCmdDraw);
    PFN_vkGetDeviceQueue GetDeviceQueue = DPROC(device, vkGetDeviceQueue);
    PFN_vkQueueSubmit QueueSubmit = DPROC(device, vkQueueSubmit);
    PFN_vkQueueWaitIdle QueueWaitIdle = DPROC(device, vkQueueWaitIdle);

    if (!CmdBeginRendering || !CmdEndRendering) {
        printf("FAILED: CmdBeginRenderingKHR/CmdEndRenderingKHR proc addr NULL\n");
        return 1;
    }

    VkCommandPoolCreateInfo cp_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHECK(CreateCommandPool(device, &cp_info, NULL, &cpool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cb_alloc = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmdbuf;
    CHECK(AllocateCommandBuffers(device, &cb_alloc, &cmdbuf), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(BeginCommandBuffer(cmdbuf, &begin_info), "vkBeginCommandBuffer");

    VkImageMemoryBarrier to_color = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    CmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1, &to_color);

    VkRenderingAttachmentInfoKHR color_att = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = {0.0f, 0.0f, 0.0f, 1.0f} } },
    };
    VkRenderingInfoKHR rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
        .renderArea = { {0,0}, {W, H} },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color_att,
    };

    printf("calling vkCmdBeginRendering...\n");
    CmdBeginRendering(cmdbuf, &rendering);

    CmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    CmdBindDescriptorSets(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1, &dset, 0, NULL);
    printf("calling vkCmdDraw(3,1,0,0)...\n");
    CmdDraw(cmdbuf, 3, 1, 0, 0);

    CmdEndRendering(cmdbuf);

    VkImageMemoryBarrier to_general = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
    };
    CmdPipelineBarrier(cmdbuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, NULL, 0, NULL, 1, &to_general);

    CHECK(EndCommandBuffer(cmdbuf), "vkEndCommandBuffer");

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    VkSubmitInfo submit_info = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cmdbuf };
    printf("calling vkQueueSubmit...\n");
    CHECK(QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");
    printf("calling vkQueueWaitIdle...\n");
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    /* --- Hasil paling penting: baca posisi clip-space langsung dari SSBO --- */
    printf("\n--- posisi vertex dari SSBO (skip tiler/rasterizer sepenuhnya) ---\n");
    float *p = (float *)dbg_mapped;
    for (int i = 0; i < 3; i++) {
        printf("vertex[%d] clip pos = (%.4f, %.4f, %.4f, %.4f)\n",
               i, p[i*4+0], p[i*4+1], p[i*4+2], p[i*4+3]);
    }
    printf("expected: vertex[0]=(0,-0.5,0,1) vertex[1]=(0.5,0.5,0,1) vertex[2]=(-0.5,0.5,0,1)\n");
    printf("(kalau masih 0xAAAAAAAA / NaN / nol semua = SPD tidak pernah menulis, itu akar masalahnya)\n");

    return 0;
}
