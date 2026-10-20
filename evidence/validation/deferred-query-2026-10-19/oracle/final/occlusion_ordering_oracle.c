#define _POSIX_C_SOURCE 200809L
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define INSTANCE_PROCS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetDeviceProcAddr) \
    X(GetPhysicalDeviceFeatures2) X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceQueueFamilyProperties) X(CreateDevice)
#define DEVICE_PROCS(X) \
    X(DestroyDevice) X(GetDeviceQueue) X(CreateImage) X(DestroyImage) \
    X(GetImageMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
    X(BindImageMemory) X(CreateImageView) X(DestroyImageView) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) \
    X(BindBufferMemory) X(MapMemory) X(UnmapMemory) \
    X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateGraphicsPipelines) X(DestroyPipeline) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(ResetCommandBuffer) X(ResetCommandPool) X(FreeCommandBuffers) \
    X(BeginCommandBuffer) X(EndCommandBuffer) X(CmdPipelineBarrier) \
    X(CmdExecuteCommands) \
    X(CmdBeginRendering) X(CmdEndRendering) X(CmdBindPipeline) \
    X(CmdSetViewport) X(CmdSetScissor) X(CmdDraw) X(CmdCopyImageToBuffer) \
    X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) \
    X(CmdBeginQuery) X(CmdEndQuery) X(CmdCopyQueryPoolResults) \
    X(GetQueryPoolResults) X(ResetQueryPool) X(CreateFence) X(DestroyFence) X(ResetFences) \
    X(QueueSubmit) X(WaitForFences)
#define DECLARE(name) static PFN_vk##name name;
INSTANCE_PROCS(DECLARE)
DEVICE_PROCS(DECLARE)
#undef DECLARE

#define CHECK(expr) do { \
    VkResult result_ = (expr); \
    if (result_ != VK_SUCCESS) { \
        fprintf(stderr, "%s: VkResult %d\n", #expr, result_); exit(1); \
    } \
} while (0)

static VkDevice device;
static VkPhysicalDeviceMemoryProperties memory;
static uint32_t scale = 1;
static unsigned pool_allocations, failed_allocations;
static int fail_pool_allocations, fail_at_query_end;

struct allocation_header { void *base; size_t size; };

static VKAPI_ATTR void *VKAPI_CALL pool_allocate(void *user, size_t size,
                                                size_t alignment, VkSystemAllocationScope scope)
{
    (void)user; (void)scope;
    if (fail_pool_allocations) { failed_allocations++; return NULL; }
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (size > SIZE_MAX - alignment - sizeof(struct allocation_header)) return NULL;
    void *base = malloc(size + alignment + sizeof(struct allocation_header));
    if (!base) return NULL;
    uintptr_t addr = ((uintptr_t)base + sizeof(struct allocation_header) + alignment - 1) & ~(alignment - 1);
    struct allocation_header header = {base, size};
    memcpy((void *)(addr - sizeof(header)), &header, sizeof(header));
    pool_allocations++;
    return (void *)addr;
}

static VKAPI_ATTR void VKAPI_CALL pool_free(void *user, void *ptr)
{
    (void)user;
    if (!ptr) return;
    struct allocation_header header;
    memcpy(&header, (unsigned char *)ptr - sizeof(header), sizeof(header));
    free(header.base);
    pool_allocations--;
}

static VKAPI_ATTR void *VKAPI_CALL pool_reallocate(void *user, void *original,
                                                  size_t size, size_t alignment,
                                                  VkSystemAllocationScope scope)
{
    if (!size) { pool_free(user, original); return NULL; }
    void *ptr = pool_allocate(user, size, alignment, scope);
    if (ptr && original) {
        struct allocation_header header;
        memcpy(&header, (unsigned char *)original - sizeof(header), sizeof(header));
        memcpy(ptr, original, size < header.size ? size : header.size);
        pool_free(user, original);
    }
    return ptr;
}

static VkDeviceMemory allocate(VkMemoryRequirements req, VkMemoryPropertyFlags flags)
{
    uint32_t type;
    for (type = 0; type < memory.memoryTypeCount; type++)
        if ((req.memoryTypeBits & (1u << type)) &&
            (memory.memoryTypes[type].propertyFlags & flags) == flags)
            break;
    if (type == memory.memoryTypeCount) {
        fprintf(stderr, "No compatible memory type\n"); exit(1);
    }
    VkMemoryAllocateInfo info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size, .memoryTypeIndex = type,
    };
    VkDeviceMemory allocation;
    CHECK(AllocateMemory(device, &info, NULL, &allocation));
    return allocation;
}

struct image { VkImage image; VkImageView view; VkDeviceMemory allocation; };
static struct image make_image(VkFormat format, VkImageUsageFlags usage,
                               VkImageAspectFlags aspect)
{
    struct image result;
    VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = format,
        .extent = {64 * scale, 64 * scale, 1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    CHECK(CreateImage(device, &info, NULL, &result.image));
    VkMemoryRequirements req;
    GetImageMemoryRequirements(device, result.image, &req);
    result.allocation = allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CHECK(BindImageMemory(device, result.image, result.allocation, 0));
    VkImageViewCreateInfo view = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = result.image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = format,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    CHECK(CreateImageView(device, &view, NULL, &result.view));
    return result;
}

struct buffer { VkBuffer buffer; VkDeviceMemory allocation; unsigned char *map; };
static struct buffer make_buffer(VkDeviceSize size)
{
    struct buffer result;
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    CHECK(CreateBuffer(device, &info, NULL, &result.buffer));
    VkMemoryRequirements req;
    GetBufferMemoryRequirements(device, result.buffer, &req);
    result.allocation = allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    CHECK(BindBufferMemory(device, result.buffer, result.allocation, 0));
    CHECK(MapMemory(device, result.allocation, 0, VK_WHOLE_SIZE, 0,
                    (void **)&result.map));
    return result;
}

static VkShaderModule shader(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END)) { perror(path); exit(1); }
    long size = ftell(file);
    if (size <= 0 || size % 4 || fseek(file, 0, SEEK_SET)) exit(1);
    uint32_t *code = malloc((size_t)size);
    if (!code || fread(code, 1, (size_t)size, file) != (size_t)size) exit(1);
    fclose(file);
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (size_t)size, .pCode = code,
    };
    VkShaderModule module;
    CHECK(CreateShaderModule(device, &info, NULL, &module));
    free(code);
    return module;
}

static void memory_barrier(VkCommandBuffer cmd, VkPipelineStageFlags dst_stage,
                           VkAccessFlags dst_access)
{
    VkMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = dst_access,
    };
    CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dst_stage,
                       0, 1, &barrier, 0, NULL, 0, NULL);
}

static uint64_t word64(const unsigned char *ptr)
{
    uint64_t result; memcpy(&result, ptr, sizeof(result)); return result;
}

static uint32_t word32(const unsigned char *ptr)
{
    uint32_t result; memcpy(&result, ptr, sizeof(result)); return result;
}

static void record_queries(VkCommandBuffer cmd, VkPipeline pipeline, VkQueryPool queries)
{
    VkViewport viewport = {0, 0, 64 * scale, 64 * scale, 0, 1};
    CmdSetViewport(cmd, 0, 1, &viewport);
    CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    for (unsigned q = 0; q < 5; q++) {
        VkRect2D scissor = q == 3 ? (VkRect2D){{40, 32}, {16, 8}} : (VkRect2D){{8, 8}, {32, 16}};
        scissor.offset.x *= scale; scissor.offset.y *= scale;
        scissor.extent.width *= scale; scissor.extent.height *= scale;
        CmdSetScissor(cmd, 0, 1, &scissor);
        CmdBeginQuery(cmd, queries, q, VK_QUERY_CONTROL_PRECISE_BIT);
        if (q != 1 && q != 4) CmdDraw(cmd, 3, 1, 0, 0);
        fail_pool_allocations = fail_at_query_end && q == 0;
        CmdEndQuery(cmd, queries, q);
        fail_pool_allocations = 0;
    }
}

int main(int argc, char **argv)
{
    int early_copy = 0, secondary = 0, lifetime = 0, poll_available = 0, simultaneous = 0, allocation_failure = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--early-copy-control")) early_copy = 1;
        else if (!strcmp(argv[i], "--secondary")) secondary = 1;
        else if (!strcmp(argv[i], "--lifetime")) lifetime = 1;
        else if (!strcmp(argv[i], "--poll-availability")) poll_available = 1;
        else if (!strcmp(argv[i], "--simultaneous")) simultaneous = 1;
        else if (!strcmp(argv[i], "--allocation-failure")) allocation_failure = 1;
        else {
            fprintf(stderr, "Usage: %s [--early-copy-control] [--secondary] [--lifetime] [--poll-availability] [--simultaneous] [--allocation-failure]\n", argv[0]); return 1;
        }
    }
    if (poll_available && simultaneous) {
        fprintf(stderr, "Polling requires one execution per cycle\n"); return 1;
    }
    if (poll_available) scale = 32;
    const char *icd = getenv("PANVK_ICD_SO");
    if (!icd) { fprintf(stderr, "Set PANVK_ICD_SO to the tested ICD\n"); return 1; }
    void *library = dlopen(icd, RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    PFN_vkGetInstanceProcAddr gpa = (PFN_vkGetInstanceProcAddr)
        dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!gpa) gpa = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!gpa) return 1;
    PFN_vkCreateInstance create_instance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion = VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
    VkInstance instance;
    CHECK(create_instance(&ici, NULL, &instance));
#define LOAD_INSTANCE(name) name = (PFN_vk##name)gpa(instance, "vk" #name); if (!name) { fprintf(stderr, "Missing instance function vk%s\n", #name); return 1; }
    INSTANCE_PROCS(LOAD_INSTANCE)
#undef LOAD_INSTANCE
    uint32_t count = 0;
    CHECK(EnumeratePhysicalDevices(instance, &count, NULL));
    if (!count) return 1;
    VkPhysicalDevice *devices = calloc(count, sizeof(*devices));
    if (!devices) return 1;
    CHECK(EnumeratePhysicalDevices(instance, &count, devices));
    VkPhysicalDevice physical = devices[0];
    free(devices);
    GetPhysicalDeviceMemoryProperties(physical, &memory);
    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceVulkan12Features f12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13,
    };
    VkPhysicalDeviceFeatures2 features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &f12,
    };
    GetPhysicalDeviceFeatures2(physical, &features);
    if (!features.features.occlusionQueryPrecise || !f13.dynamicRendering) {
        fprintf(stderr, "Required precise queries/dynamic rendering unsupported\n"); return 1;
    }
    if (poll_available && !f12.hostQueryReset) {
        fprintf(stderr, "Host query reset required for availability polling\n"); return 1;
    }
    f12 = (VkPhysicalDeviceVulkan12Features){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &f13,
        .hostQueryReset = poll_available,
    };
    features.features = (VkPhysicalDeviceFeatures){.occlusionQueryPrecise = VK_TRUE};
    f13 = (VkPhysicalDeviceVulkan13Features){
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    if (!families) return 1;
    GetPhysicalDeviceQueueFamilyProperties(physical, &count, families);
    uint32_t family;
    for (family = 0; family < count; family++)
        if (families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) break;
    free(families);
    if (family == count) return 1;
    float priority = 1;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &features,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
    };
    CHECK(CreateDevice(physical, &dci, NULL, &device));
#define LOAD_DEVICE(name) name = (PFN_vk##name)GetDeviceProcAddr(device, "vk" #name); if (!name) { fprintf(stderr, "Missing device function vk%s\n", #name); return 1; }
    DEVICE_PROCS(LOAD_DEVICE)
#undef LOAD_DEVICE
    VkQueue queue;
    GetDeviceQueue(device, family, 0, &queue);
    struct image color = make_image(VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    struct image depth = make_image(VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    size_t pixel_bytes = (size_t)64 * 64 * scale * scale * 4;
    struct buffer pixels = make_buffer(pixel_bytes), results = make_buffer(1024);
    VkShaderModule vs = shader("occlusion_oracle.vert.spv");
    VkShaderModule fs = shader("occlusion_oracle.frag.spv");
    VkPipelineLayoutCreateInfo lci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    VkPipelineLayout layout;
    CHECK(CreatePipelineLayout(device, &lci, NULL, &layout));
    VkPipelineShaderStageCreateInfo stages[] = {
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"},
    };
    VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1,
    };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1,
    };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    VkPipelineDepthStencilStateCreateInfo ds = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE, .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
    };
    VkPipelineColorBlendAttachmentState attachment = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo blend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &attachment,
    };
    VkDynamicState dynamic[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dynamic,
    };
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkPipelineRenderingCreateInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &format,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
    };
    VkGraphicsPipelineCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &rendering,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vi,
        .pInputAssemblyState = &ia, .pViewportState = &vp,
        .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pDepthStencilState = &ds, .pColorBlendState = &blend,
        .pDynamicState = &dyn, .layout = layout,
    };
    VkPipeline pipeline;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, NULL, &pipeline));
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = family,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };
    VkCommandPool pool;
    VkAllocationCallbacks pool_callbacks = {
        .pfnAllocation = pool_allocate, .pfnReallocation = pool_reallocate,
        .pfnFree = pool_free,
    };
    const VkAllocationCallbacks *pool_allocator = (lifetime || allocation_failure) ? &pool_callbacks : NULL;
    CHECK(CreateCommandPool(device, &cpci, pool_allocator, &pool));
    VkCommandBufferAllocateInfo cai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1,
    };
    VkCommandBuffer cmd;
    CHECK(AllocateCommandBuffers(device, &cai, &cmd));
    VkQueryPoolCreateInfo qpci = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION, .queryCount = 5,
    };
    VkQueryPool queries;
    CHECK(CreateQueryPool(device, &qpci, NULL, &queries));
    if (lifetime || allocation_failure) {
        for (unsigned action = 0; action < (allocation_failure ? 1u : 4u); action++) {
            VkCommandBufferBeginInfo abandoned_begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            CHECK(BeginCommandBuffer(cmd, &abandoned_begin));
            CmdResetQueryPool(cmd, queries, 0, 5);
            VkRenderingAttachmentInfo abandoned_color = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = color.view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            };
            VkRenderingAttachmentInfo abandoned_depth = abandoned_color;
            abandoned_depth.imageView = depth.view;
            abandoned_depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            abandoned_depth.clearValue.depthStencil.depth = 1;
            VkRenderingInfo abandoned_render = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {{0, 0}, {64 * scale, 64 * scale}}, .layerCount = 1,
                .colorAttachmentCount = 1, .pColorAttachments = &abandoned_color,
                .pDepthAttachment = &abandoned_depth,
            };
            CmdBeginRendering(cmd, &abandoned_render);
            fail_at_query_end = allocation_failure;
            record_queries(cmd, pipeline, queries);
            fail_at_query_end = 0;
            if (allocation_failure) {
                CmdEndRendering(cmd);
                VkResult end_status = EndCommandBuffer(cmd);
                if (end_status != VK_ERROR_OUT_OF_HOST_MEMORY || failed_allocations == 0) {
                    fprintf(stderr, "OQ_ALLOC expected OOM, got %d failures=%u\n", end_status, failed_allocations);
                    return 1;
                }
                printf("OQ_ALLOC injected=%u end_status=%d\n", failed_allocations, end_status);
                CHECK(ResetCommandBuffer(cmd, 0));
            } else if (action == 0) CHECK(ResetCommandBuffer(cmd, 0));
            else if (action == 1) CHECK(ResetCommandPool(device, pool, 0));
            else if (action == 2) {
                FreeCommandBuffers(device, pool, 1, &cmd);
                CHECK(AllocateCommandBuffers(device, &cai, &cmd));
            } else {
                DestroyCommandPool(device, pool, pool_allocator);
                CHECK(CreateCommandPool(device, &cpci, pool_allocator, &pool));
                cai.commandPool = pool;
                CHECK(AllocateCommandBuffers(device, &cai, &cmd));
            }
        }
        if (lifetime) printf("OQ_LIFETIME abandoned_recordings=4\n");
    }
    VkCommandBuffer secondary_cmd = VK_NULL_HANDLE;
    if (secondary) {
        cai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
        CHECK(AllocateCommandBuffers(device, &cai, &secondary_cmd));
        VkCommandBufferInheritanceRenderingInfo inheritance_rendering = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
            .colorAttachmentCount = 1, .pColorAttachmentFormats = &format,
            .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };
        VkCommandBufferInheritanceInfo inheritance = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
            .pNext = &inheritance_rendering,
        };
        VkCommandBufferBeginInfo secondary_begin = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT |
                     (simultaneous ? VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 0),
            .pInheritanceInfo = &inheritance,
        };
        CHECK(BeginCommandBuffer(secondary_cmd, &secondary_begin));
        record_queries(secondary_cmd, pipeline, queries);
        CHECK(EndCommandBuffer(secondary_cmd));
    }
    VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = simultaneous ? VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT : 0,
    };
    CHECK(BeginCommandBuffer(cmd, &begin));
    memory_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    CmdResetQueryPool(cmd, queries, 0, 5);
    memory_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    CmdCopyQueryPoolResults(cmd, queries, 0, 4, results.buffer, 0, 32,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    memory_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    VkImageMemoryBarrier bars[2];
    for (unsigned i = 0; i < 2; i++) bars[i] = (VkImageMemoryBarrier){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = i ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = i ? depth.image : color.image,
        .subresourceRange = {i ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        .dstAccessMask = i ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
    };
    CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT, 0, 0, NULL, 0, NULL, 2, bars);
    VkRenderingAttachmentInfo ca = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = color.view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.color.float32 = {0, 0, 0, 1},
    };
    VkRenderingAttachmentInfo da = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = depth.view, .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue.depthStencil = {1, 0},
    };
    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = {{0, 0}, {64 * scale, 64 * scale}},
        .flags = secondary ? VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT : 0,
        .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &ca,
        .pDepthAttachment = &da,
    };
    CmdBeginRendering(cmd, &ri);
    if (secondary) CmdExecuteCommands(cmd, 1, &secondary_cmd);
    else record_queries(cmd, pipeline, queries);
    CmdEndRendering(cmd);
    CmdCopyQueryPoolResults(cmd, queries, 0, 4, results.buffer, 256, 32,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    CmdCopyQueryPoolResults(cmd, queries, 0, 4, results.buffer, 384, 16,
        VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    CmdCopyQueryPoolResults(cmd, queries, 4, 1, results.buffer, 512, 16,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    memory_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    CmdResetQueryPool(cmd, queries, 4, 1);
    memory_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                   VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    CmdCopyQueryPoolResults(cmd, queries, 4, 1, results.buffer, 544, 16,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    bars[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    bars[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    bars[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    bars[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, bars);
    VkBufferImageCopy copy = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                             .imageExtent = {64 * scale, 64 * scale, 1}};
    CmdCopyImageToBuffer(cmd, color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        pixels.buffer, 1, &copy);
    memory_barrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    CHECK(EndCommandBuffer(cmd));
    VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    CHECK(CreateFence(device, &fci, NULL, &fence));
    unsigned failures = 0;
    const uint64_t expected[] = {512 * scale * scale, 0, 0, 128 * scale * scale};
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        memset(results.map, 0xa5, 1024);
        memset(pixels.map, 0xa5, pixel_bytes);
        if (poll_available) ResetQueryPool(device, queries, 0, 5);
        VkCommandBuffer executions[] = {cmd, cmd, cmd};
        VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                              .commandBufferCount = simultaneous ? 3 : 1,
                              .pCommandBuffers = executions};
        CHECK(QueueSubmit(queue, 1, &submit, fence));
        if (poll_available) {
            struct timespec start, now;
            if (clock_gettime(CLOCK_MONOTONIC, &start)) return 1;
            uint64_t observed[2] = {UINT64_MAX, 0};
            VkResult status;
            do {
                status = GetQueryPoolResults(device, queries, 0, 1, sizeof(observed),
                    observed, 16, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
                if (clock_gettime(CLOCK_MONOTONIC, &now)) return 1;
                if (now.tv_sec - start.tv_sec > 30) {
                    fprintf(stderr, "Availability polling timeout\n"); return 1;
                }
            } while (status == VK_NOT_READY);
            CHECK(status);
            unsigned bad = observed[0] != expected[0] || observed[1] == 0;
            failures += bad;
            printf("OQ_POLL cycle=%u first_available=%" PRIu64 " expected=%" PRIu64 " verdict=%s\n",
                   cycle, observed[0], expected[0], bad ? "FAIL" : "PASS");
        }
        CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(30000000000)));
        CHECK(ResetFences(device, 1, &fence));
        unsigned bad_pixels = 0;
        for (unsigned y = 0; y < 64 * scale; y++) for (unsigned x = 0; x < 64 * scale; x++) {
            unsigned red = (x >= 8 * scale && x < 40 * scale && y >= 8 * scale && y < 24 * scale) ||
                           (x >= 40 * scale && x < 56 * scale && y >= 32 * scale && y < 40 * scale);
            unsigned char *p = pixels.map + ((size_t)64 * scale * y + x) * 4;
            bad_pixels += p[0] != 255 * red || p[1] != 0 || p[2] != 0 || p[3] != 255;
        }
        failures += bad_pixels != 0;
        uint64_t host[4][2];
        CHECK(GetQueryPoolResults(device, queries, 0, 4, sizeof(host), host, 16,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
        for (unsigned q = 0; q < 4; q++) {
            unsigned char *gpu64 = results.map + (early_copy ? 0 : 256) + q * 32;
            unsigned char *gpu32 = results.map + 384 + q * 16;
            unsigned bad = host[q][0] != expected[q] || host[q][1] == 0 ||
                word64(gpu64) != expected[q] || word64(gpu64 + 8) == 0 ||
                word32(gpu32) != expected[q] || word32(gpu32 + 4) == 0 ||
                word64(results.map + q * 32 + 8) != 0;
            for (unsigned p = 16; p < 32; p++) bad |= gpu64[p] != 0xa5;
            for (unsigned p = 8; p < 16; p++) bad |= gpu32[p] != 0xa5;
            failures += !!bad;
            printf("cycle=%u query=%u expected=%" PRIu64 " host=%" PRIu64
                   " copy64=%" PRIu64 " available64=%" PRIu64 " copy32=%u verdict=%s\n",
                   cycle, q, expected[q], host[q][0], word64(gpu64), word64(gpu64 + 8),
                   word32(gpu32), bad ? "FAIL" : "PASS");
        }
        uint64_t unavailable[2] = {UINT64_MAX, UINT64_MAX};
        VkResult reset_status = GetQueryPoolResults(device, queries, 4, 1,
            sizeof(unavailable), unavailable, 16,
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        unsigned reset_bad = reset_status != VK_NOT_READY || unavailable[1] != 0 ||
            word64(results.map + 512) != 0 || word64(results.map + 520) == 0 ||
            word64(results.map + 552) != 0;
        failures += !!reset_bad;
        printf("cycle=%u bad_pixels=%u reset_status=%d reset_available=%" PRIu64
               " reset_verdict=%s\n", cycle, bad_pixels, reset_status,
               unavailable[1], reset_bad ? "FAIL" : "PASS");
    }
    DestroyFence(device, fence, NULL);
    DestroyCommandPool(device, pool, pool_allocator);
    if (pool_allocator) {
        printf("OQ_POOL outstanding_allocations=%u\n", pool_allocations);
        failures += pool_allocations != 0;
    }
    DestroyQueryPool(device, queries, NULL);
    DestroyPipeline(device, pipeline, NULL);
    DestroyPipelineLayout(device, layout, NULL);
    DestroyShaderModule(device, vs, NULL);
    DestroyShaderModule(device, fs, NULL);
    struct image images[] = {color, depth};
    struct buffer buffers[] = {pixels, results};
    for (unsigned i = 0; i < 2; i++) {
        DestroyImageView(device, images[i].view, NULL);
        DestroyImage(device, images[i].image, NULL);
        FreeMemory(device, images[i].allocation, NULL);
        UnmapMemory(device, buffers[i].allocation);
        DestroyBuffer(device, buffers[i].buffer, NULL);
        FreeMemory(device, buffers[i].allocation, NULL);
    }
    DestroyDevice(device, NULL);
    DestroyInstance(instance, NULL);
    dlclose(library);
    printf("OQ_ORACLE cycles=3 secondary=%d early_copy_control=%d lifetime=%d poll=%d simultaneous=%d allocation_failure=%d failures=%u verdict=%s\n",
           secondary, early_copy, lifetime, poll_available, simultaneous, allocation_failure, failures, failures ? "FAIL" : "PASS");
    return failures ? 2 : 0;
}
