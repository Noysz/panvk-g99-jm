/* indexed_draw_test.c
 *
 * Phase 4.1 — validates vkCmdDrawIndexed on Mali-G57 MC2 (v9/JM).
 *
 * The v9 CmdDrawIndexed code exists and looks complete
 * (jm/panvk_vX_cmd_draw.c:2998) but has never been run. This exercises it.
 *
 * Uses triangle6.vert, which holds two distinct triangles:
 *   vertices 0,1,2 -> triangle A, the validated baseline geometry, covers centre
 *   vertices 3,4,5 -> triangle B, small corner triangle, does NOT cover centre
 *
 * That distinction is the point. A test that only asks "did a triangle appear"
 * cannot tell a working index buffer from an ignored one, because the
 * non-indexed path would draw A regardless.
 *
 * Cases, via IDX_CASE:
 *   a16      indices {0,1,2} UINT16   -> triangle A, must match the 512/4096 oracle
 *   a32      indices {0,1,2} UINT32   -> identical to a16
 *   b16      indices {3,4,5} UINT16   -> triangle B, must DIFFER from A, non-zero
 *   voffset  indices {0,1,2}, vertexOffset=3 -> must equal b16 exactly
 *   degen    indices {0,0,0}          -> degenerate, expect 0 non-black
 *   zero     indexCount = 0           -> nothing drawn, no hang
 *
 * 'voffset' is the strongest single case: two different API inputs that must
 * land on the same image. 'degen' is the negative control.
 *
 * Env: IDX_CASE, IDX_PPM=<path>, PANVK_ICD_SO, PANVK_DEBUG_RESTAB.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
#include "vulkan/vulkan_core.h"

#define CHECK(expr, msg) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { \
        printf("FAILED: %s (VkResult=%d)\n", msg, _r); \
        return 1; \
    } \
} while (0)

typedef PFN_vkVoidFunction (*PFN_icdGetInstanceProcAddr)(VkInstance, const char*);

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *mp,
                                  uint32_t type_bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((type_bits & (1u << i)) &&
            (mp->memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("FAILED: cannot open %s\n", path); return NULL; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc(sz);
    if (fread(buf, 1, sz, f) != (size_t)sz) {
        printf("FAILED: short read %s\n", path); fclose(f); free(buf); return NULL;
    }
    fclose(f); *out_size = (size_t)sz; return buf;
}

#ifndef PANVK_DEFAULT_ICD_SO
#define PANVK_DEFAULT_ICD_SO \
    "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so"
#endif

struct idx_case {
    const char *name;
    uint32_t    idx[6];
    uint32_t    nidx;          /* how many indices to write into the buffer */
    uint32_t    index_count;   /* indexCount passed to the draw */
    uint32_t    first_index;   /* firstIndex passed to the draw */
    int32_t     vertex_offset;
    int         use_uint32;
    const char *expect;
};

int main(void) {
    const char *case_name = getenv("IDX_CASE");
    if (!case_name || !case_name[0]) case_name = "a16";

    struct idx_case cases[] = {
        { "a16",     {0,1,2},       3, 3, 0, 0, 0, "triangle A, 512/4096, centre red" },
        { "a32",     {0,1,2},       3, 3, 0, 0, 1, "identical to a16" },
        { "b16",     {3,4,5},       3, 3, 0, 0, 0, "triangle B, differs from A, centre black" },
        { "voffset", {0,1,2},       3, 3, 0, 3, 0, "must equal b16 exactly" },
        { "degen",   {0,0,0},       3, 3, 0, 0, 0, "degenerate, expect 0 non-black" },
        { "zero",    {0,1,2},       3, 0, 0, 0, 0, "nothing drawn, no hang" },
        /* firstIndex probe: six indices in the buffer, draw the last three.
         * 253 means firstIndex was honoured, 512 means it was ignored. */
        { "firstidx",{0,1,2,3,4,5}, 6, 3, 3, 0, 0, "triangle B if firstIndex works" },
    };
    struct idx_case *tc = NULL;
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
        if (!strcmp(case_name, cases[i].name)) tc = &cases[i];
    if (!tc) { printf("FAILED: unknown IDX_CASE=%s\n", case_name); return 1; }

    printf("=== case: %s ===\n", tc->name);
    printf("indices        : {%u,%u,%u}\n", tc->idx[0], tc->idx[1], tc->idx[2]);
    printf("indexCount     : %u\n", tc->index_count);
    printf("firstIndex     : %u\n", tc->first_index);
    printf("indices in buf : %u\n", tc->nidx);
    printf("vertexOffset   : %d\n", tc->vertex_offset);
    printf("indexType      : %s\n", tc->use_uint32 ? "UINT32" : "UINT16");
    printf("expectation    : %s\n", tc->expect);
    fflush(stdout);

    const char *icd_path = getenv("PANVK_ICD_SO");
    if (!icd_path || !icd_path[0]) icd_path = PANVK_DEFAULT_ICD_SO;
    void *lib = dlopen(icd_path, RTLD_NOW);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    PFN_icdGetInstanceProcAddr icd_gpa =
        (PFN_icdGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!icd_gpa) { printf("dlsym failed: %s\n", dlerror()); return 1; }

    PFN_vkCreateInstance CreateInstance =
        (PFN_vkCreateInstance)icd_gpa(NULL, "vkCreateInstance");
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info };
    VkInstance instance;
    CHECK(CreateInstance(&inst_info, NULL, &instance), "vkCreateInstance");

    #define IPROC(name) (PFN_vk##name)icd_gpa(instance, "vk" #name)
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = IPROC(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = IPROC(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CreateDevice = IPROC(CreateDevice);
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemoryProperties = IPROC(GetPhysicalDeviceMemoryProperties);

    uint32_t count = 0;
    CHECK(EnumeratePhysicalDevices(instance, &count, NULL), "enum count");
    if (!count) { printf("FAILED: no physical device\n"); return 1; }
    VkPhysicalDevice pdev;
    CHECK(EnumeratePhysicalDevices(instance, &count, &pdev), "enum fetch");
    VkPhysicalDeviceMemoryProperties mem_props;
    GetMemoryProperties(pdev, &mem_props);

    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VkDevice device;
    CHECK(CreateDevice(pdev, &dci, NULL, &device), "vkCreateDevice");

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
    PFN_vkCmdPipelineBarrier CmdPipelineBarrier = DPROC(CmdPipelineBarrier);
    PFN_vkCreateBuffer CreateBuffer = DPROC(CreateBuffer);
    PFN_vkGetBufferMemoryRequirements GetBufferMemoryRequirements = DPROC(GetBufferMemoryRequirements);
    PFN_vkBindBufferMemory BindBufferMemory = DPROC(BindBufferMemory);
    /* the two under test */
    PFN_vkCmdDrawIndexed CmdDrawIndexed = DPROC(CmdDrawIndexed);
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = DPROC(CmdBindIndexBuffer);
    PFN_vkCmdBindIndexBuffer2 CmdBindIndexBuffer2 = DPROC(CmdBindIndexBuffer2);

    printf("vkCmdDrawIndexed      : %s\n", CmdDrawIndexed ? "present" : "NULL");
    printf("vkCmdBindIndexBuffer  : %s\n", CmdBindIndexBuffer ? "present" : "NULL");
    printf("vkCmdBindIndexBuffer2 : %s\n", CmdBindIndexBuffer2 ? "present" : "NULL");
    if (!CmdDrawIndexed) {
        printf("FAILED: vkCmdDrawIndexed is NULL -- NOT IMPLEMENTED\n"); return 1;
    }
    if (!CmdBindIndexBuffer && !CmdBindIndexBuffer2) {
        printf("FAILED: no index-buffer bind entry point -- NOT IMPLEMENTED\n");
        return 1;
    }
    fflush(stdout);

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    const uint32_t W = 64, H = 64;
    VkFormat FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

    /* ---- index buffer ---- */
    size_t idx_stride = tc->use_uint32 ? 4 : 2;
    size_t idx_bytes  = idx_stride * tc->nidx;
    VkBufferCreateInfo ibci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = idx_bytes, .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VkBuffer ibuf;
    CHECK(CreateBuffer(device, &ibci, NULL, &ibuf), "vkCreateBuffer(index)");
    VkMemoryRequirements imreq;
    GetBufferMemoryRequirements(device, ibuf, &imreq);
    uint32_t imt = find_memory_type(&mem_props, imreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (imt == UINT32_MAX) { printf("FAILED: no host-visible mem for index buffer\n"); return 1; }
    VkMemoryAllocateInfo imai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = imreq.size, .memoryTypeIndex = imt };
    VkDeviceMemory imem;
    CHECK(AllocateMemory(device, &imai, NULL, &imem), "vkAllocateMemory(index)");
    CHECK(BindBufferMemory(device, ibuf, imem, 0), "vkBindBufferMemory(index)");
    void *ip = NULL;
    CHECK(MapMemory(device, imem, 0, idx_bytes, 0, &ip), "vkMapMemory(index)");
    if (tc->use_uint32) {
        uint32_t *u = ip;
        for (uint32_t i = 0; i < tc->nidx; i++) u[i] = tc->idx[i];
    } else {
        uint16_t *u = ip;
        for (uint32_t i = 0; i < tc->nidx; i++) u[i] = (uint16_t)tc->idx[i];
    }
    UnmapMemory(device, imem);
    printf("index buffer written: %zu bytes\n", idx_bytes);

    /* ---- colour attachment ---- */
    VkImageCreateInfo ici = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D, .format = FORMAT,
        .extent = {W,H,1}, .mipLevels = 1, .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
    VkImage image;
    CHECK(CreateImage(device, &ici, NULL, &image), "vkCreateImage");
    VkMemoryRequirements mreq;
    GetImageMemoryRequirements(device, image, &mreq);
    uint32_t mt = find_memory_type(&mem_props, mreq.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) { printf("FAILED: no host-visible mem for image\n"); return 1; }
    VkMemoryAllocateInfo mai = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mreq.size, .memoryTypeIndex = mt };
    VkDeviceMemory img_mem;
    CHECK(AllocateMemory(device, &mai, NULL, &img_mem), "vkAllocateMemory(image)");
    CHECK(BindImageMemory(device, image, img_mem, 0), "vkBindImageMemory");
    VkImageViewCreateInfo vci = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FORMAT,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1} };
    VkImageView view;
    CHECK(CreateImageView(device, &vci, NULL, &view), "vkCreateImageView");

    /* ---- pipeline ---- */
    size_t vs_size=0, fs_size=0;
    char *vs_code = read_file("triangle6.vert.spv", &vs_size);
    if (!vs_code) return 1;
    char *fs_code = read_file("triangle.frag.spv", &fs_size);
    if (!fs_code) return 1;
    VkShaderModuleCreateInfo vmci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vs_size, .pCode = (uint32_t*)vs_code };
    VkShaderModule vs_mod;
    CHECK(CreateShaderModule(device, &vmci, NULL, &vs_mod), "shader(vert)");
    VkShaderModuleCreateInfo fmci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fs_size, .pCode = (uint32_t*)fs_code };
    VkShaderModule fs_mod;
    CHECK(CreateShaderModule(device, &fmci, NULL, &fs_mod), "shader(frag)");

    VkPipelineLayoutCreateInfo plci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout pl;
    CHECK(CreatePipelineLayout(device, &plci, NULL, &pl), "pipelineLayout");

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs_mod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs_mod, .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo vi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vp = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                          VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT };
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1, .pAttachments = &cba };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dsi = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn };
    VkPipelineRenderingCreateInfo pri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1, .pColorAttachmentFormats = &FORMAT };
    VkGraphicsPipelineCreateInfo gpci = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
        .stageCount = 2, .pStages = stages, .pVertexInputState = &vi,
        .pInputAssemblyState = &ia, .pViewportState = &vp,
        .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &cb, .pDynamicState = &dsi, .layout = pl };
    VkPipeline pipeline;
    CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline),
          "vkCreateGraphicsPipelines");

    /* ---- record ---- */
    VkCommandPoolCreateInfo cpci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = 0 };
    VkCommandPool cpool;
    CHECK(CreateCommandPool(device, &cpci, NULL, &cpool), "commandPool");
    VkCommandBufferAllocateInfo cbai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cpool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    CHECK(AllocateCommandBuffers(device, &cbai, &cmd), "allocCmdBuf");
    VkCommandBufferBeginInfo cbbi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(BeginCommandBuffer(cmd, &cbbi), "beginCmdBuf");

    VkImageMemoryBarrier bar = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
    CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                       0,0,NULL,0,NULL,1,&bar);

    VkRenderingAttachmentInfo cai = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = { .color = { .float32 = {0,0,0,1} } } };
    VkRenderingInfo ri = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, {W,H} }, .layerCount = 1,
        .colorAttachmentCount = 1, .pColorAttachments = &cai };
    CmdBeginRendering(cmd, &ri);

    VkViewport vpo = { 0,0,(float)W,(float)H,0.0f,1.0f };
    VkRect2D sci = { {0,0}, {W,H} };
    CmdSetViewport(cmd, 0, 1, &vpo);
    CmdSetScissor(cmd, 0, 1, &sci);
    CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    VkIndexType itype = tc->use_uint32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    if (CmdBindIndexBuffer2) {
        CmdBindIndexBuffer2(cmd, ibuf, 0, idx_bytes, itype);
        printf("bound via vkCmdBindIndexBuffer2\n");
    } else {
        CmdBindIndexBuffer(cmd, ibuf, 0, itype);
        printf("bound via vkCmdBindIndexBuffer\n");
    }
    fflush(stdout);

    CmdDrawIndexed(cmd, tc->index_count, 1, tc->first_index, tc->vertex_offset, 0);
    CmdEndRendering(cmd);
    CHECK(EndCommandBuffer(cmd), "endCmdBuf");

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &cmd };
    CHECK(QueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    /* ---- readback ---- */
    VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout sl;
    GetImageSubresourceLayout(device, image, &sub, &sl);
    void *mapped = NULL;
    CHECK(MapMemory(device, img_mem, 0, VK_WHOLE_SIZE, 0, &mapped), "map image");
    uint8_t *base = (uint8_t*)mapped + sl.offset;

    uint32_t non_black = 0;
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *row = base + y * sl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *px = row + x*4;
            if (px[0] || px[1] || px[2]) non_black++;
        }
    }
    uint8_t *centre = base + 32*sl.rowPitch + 32*4;
    uint8_t *corner = base + 0*sl.rowPitch + 0*4;

    /* Cheap shape fingerprint: per-quadrant coverage. Two triangles with the
     * same total area would still differ here, so equality claims between cases
     * rest on more than a single number. */
    uint32_t quad[4] = {0,0,0,0};
    for (uint32_t y = 0; y < H; y++) {
        uint8_t *row = base + y*sl.rowPitch;
        for (uint32_t x = 0; x < W; x++) {
            uint8_t *px = row + x*4;
            if (px[0] || px[1] || px[2])
                quad[(y < H/2 ? 0 : 2) + (x < W/2 ? 0 : 1)]++;
        }
    }

    printf("\n--- result: %s ---\n", tc->name);
    printf("non-black        : %u / %u\n", non_black, W*H);
    printf("centre (32,32)   = %u,%u,%u,%u\n", centre[0],centre[1],centre[2],centre[3]);
    printf("corner (0,0)     = %u,%u,%u,%u\n", corner[0],corner[1],corner[2],corner[3]);
    printf("quadrants TL,TR,BL,BR = %u,%u,%u,%u\n", quad[0],quad[1],quad[2],quad[3]);
    printf("FINGERPRINT %s n=%u c=%u,%u,%u q=%u,%u,%u,%u\n",
           tc->name, non_black, centre[0],centre[1],centre[2],
           quad[0],quad[1],quad[2],quad[3]);

    const char *ppm = getenv("IDX_PPM");
    if (ppm && ppm[0]) {
        FILE *pf = fopen(ppm, "wb");
        if (pf) {
            fprintf(pf, "P6\n%u %u\n255\n", W, H);
            for (uint32_t y = 0; y < H; y++) {
                uint8_t *row = base + y*sl.rowPitch;
                for (uint32_t x = 0; x < W; x++) {
                    uint8_t *px = row + x*4;
                    uint8_t rgb[3] = { px[0], px[1], px[2] };
                    fwrite(rgb,1,3,pf);
                }
            }
            fclose(pf);
            printf("VISUAL_DUMP: %s\n", ppm);
        }
    }
    UnmapMemory(device, img_mem);
    free(vs_code); free(fs_code);
    printf("DONE\n");
    return 0;
}
