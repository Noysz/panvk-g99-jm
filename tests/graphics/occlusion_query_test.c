/*
 * T4.5.3 -- occlusion queries on Valhall v9 / Job Manager.
 *
 * Why this test exists
 * --------------------
 * Two precompiled-kernel paths were silently dead on v9 until dispatch_precomp()
 * was ported, and neither has been executed since:
 *
 *   vkCmdResetQueryPool       -> panlib_clear_query_result
 *   vkCmdCopyQueryPoolResults -> panlib_copy_query_result
 *
 * The stub they depended on was installed by this project's own patch 0011 so
 * the shared object would link, so restoring them is this project's problem to
 * verify. vkCmdFillBuffer, the third such path, is covered by
 * fill_buffer_test.c.
 *
 * Why occlusion queries are the right vehicle
 * -------------------------------------------
 * A query test normally has to trust the query to check the query. Here it does
 * not. The vertex shader is the same triangle6.vert used throughout Phase 4, and
 * the number of pixels each of its two triangles covers is already established
 * independently and four separate ways: 512 for triangle A and 253 for triangle
 * B, from vkCmdDraw, vkCmdDrawIndexed, vkCmdDrawIndirect and
 * vkCmdDrawIndexedIndirect, all landing on framebuffer sha256 57ddb7b669f1 and
 * 010c54cd0cff respectively.
 *
 * With no depth or stencil attachment and one sample per pixel, a precise
 * occlusion query counts exactly the samples that pass, which is the covered
 * pixel count. So the expected query result is a number this project already
 * knows from somewhere other than the query itself.
 *
 * VK_QUERY_CONTROL_PRECISE_BIT is required for that. Without it the driver uses
 * MALI_OCCLUSION_MODE_PREDICATE (panvk_vX_cmd_query.c) and any non-zero value
 * would be conformant, which would make the comparison meaningless. The device
 * advertises occlusionQueryPrecise = true (panvk_vX_physical_device.c:334).
 *
 * Three independent numbers per case
 * ----------------------------------
 *   1. pixels counted in the framebuffer readback   -- no query involved
 *   2. vkGetQueryPoolResults                        -- host reads report memory
 *                                                      directly, no kernel
 *   3. vkCmdCopyQueryPoolResults                    -- via panlib_copy_query_result
 *
 * (2) versus (3) isolates the copy kernel, since only (3) runs it. (1) versus
 * both anchors the pair to something outside the query machinery entirely. All
 * three agreeing is a much stronger statement than a query returning a
 * plausible number.
 *
 * Cases, via OQ_CASE:
 *   tri_a    draw vertices 0..2, expect 512 covered, query 512
 *   tri_b    draw vertices 3..5, expect 253 covered, query 253
 *   both     two queries in one pass, expect 512 and 253 in separate slots
 *   zero     vertexCount 0. Negative control: query must report exactly 0.
 *            The spec guarantees a begun-and-ended query starts at zero, so 0
 *            is a real assertion here, not an absence of evidence.
 *   noreset  begin/end without vkCmdResetQueryPool first. Availability is
 *            undefined, so nothing is asserted about the value; this exists to
 *            confirm the reset path is what makes the reported value defined.
 *
 * PANVK_PRECOMP_STUB=1 reproduces the patch-0011 stub and is the A/B: with it,
 * the copy kernel does nothing, so (3) should not match (1) and (2).
 *
 * Env: OQ_CASE, PANVK_ICD_SO.
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

struct oq_case {
    const char *name;
    uint32_t    nq;            /* how many queries to use */
    uint32_t    first_vertex[2];
    uint32_t    vertex_count[2];
    int         do_reset;      /* issue vkCmdResetQueryPool first */
    int         assert_value;  /* whether the query value is asserted at all */
    int         reset_after;   /* reset again after the pass, then copy a 2nd time */
    const char *expect;
};

/* Expected sample counts are NOT written here. They are measured from the
 * framebuffer in the same run and the query is compared against that. The only
 * hardcoded number in the whole comparison is 0 for the zero-vertex control,
 * which the Vulkan spec guarantees ("when an occlusion query begins, the count
 * of passing samples always starts at zero"). */
int main(void) {
    const char *case_name = getenv("OQ_CASE");
    if (!case_name || !case_name[0]) case_name = "tri_a";

    struct oq_case cases[] = {
        { "tri_a",   1, {0,0}, {3,0}, 1, 1, 0, "triangle A, query must equal measured coverage" },
        { "tri_b",   1, {3,0}, {3,0}, 1, 1, 0, "triangle B, smaller, query must equal measured coverage" },
        { "both",    2, {0,3}, {3,3}, 1, 1, 0, "two queries in one pass, A then B, must differ" },
        { "zero",    1, {0,0}, {0,0}, 1, 1, 0, "negative control, query must be exactly 0" },
        { "noreset", 1, {0,0}, {3,0}, 0, 0, 0, "no reset first, value undefined, not asserted" },
        /* This is the case that actually exercises panlib_clear_query_result.
         * The others do not: CmdBeginQuery zeroes the counter itself with
         * WRITE_VALUE jobs (panvk_vX_cmd_query.c, per the spec requirement that
         * a query starts at zero), so a broken clear kernel would still give the
         * right sample count. What vkCmdResetQueryPool owns is availability. So:
         * run a query and copy it (available, value known), then reset, then
         * copy again without WAIT. The second availability word must be 0. If
         * the clear kernel does nothing, it stays 1. */
        { "reset_clears", 1, {0,0}, {3,0}, 1, 1, 1, "availability must go 1 then 0 across a reset" },
    };
    struct oq_case *tc = NULL;
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); i++)
        if (!strcmp(case_name, cases[i].name)) tc = &cases[i];
    if (!tc) { printf("FAILED: unknown OQ_CASE=%s\n", case_name); return 1; }

    printf("=== case: %s ===\n", tc->name);
    printf("queries        : %u\n", tc->nq);
    printf("draw 0         : firstVertex=%u vertexCount=%u\n",
           tc->first_vertex[0], tc->vertex_count[0]);
    if (tc->nq > 1)
        printf("draw 1         : firstVertex=%u vertexCount=%u\n",
               tc->first_vertex[1], tc->vertex_count[1]);
    printf("reset first    : %s\n", tc->do_reset ? "yes" : "no");
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
    PFN_vkCmdDraw CmdDraw = DPROC(CmdDraw);
    PFN_vkCreateQueryPool CreateQueryPool = DPROC(CreateQueryPool);
    PFN_vkCmdResetQueryPool CmdResetQueryPool = DPROC(CmdResetQueryPool);
    PFN_vkCmdBeginQuery CmdBeginQuery = DPROC(CmdBeginQuery);
    PFN_vkCmdEndQuery CmdEndQuery = DPROC(CmdEndQuery);
    PFN_vkCmdCopyQueryPoolResults CmdCopyQueryPoolResults = DPROC(CmdCopyQueryPoolResults);
    PFN_vkGetQueryPoolResults GetQueryPoolResults = DPROC(GetQueryPoolResults);
    PFN_vkCreateBuffer CreateBufferFn = DPROC(CreateBuffer);
    PFN_vkGetBufferMemoryRequirements GetBufMemReq = DPROC(GetBufferMemoryRequirements);
    PFN_vkBindBufferMemory BindBufMem = DPROC(BindBufferMemory);

    const char *missing = NULL;
    if (!CmdDraw)                 missing = "vkCmdDraw";
    else if (!CreateQueryPool)    missing = "vkCreateQueryPool";
    else if (!CmdResetQueryPool)  missing = "vkCmdResetQueryPool";
    else if (!CmdBeginQuery)      missing = "vkCmdBeginQuery";
    else if (!CmdEndQuery)        missing = "vkCmdEndQuery";
    else if (!CmdCopyQueryPoolResults) missing = "vkCmdCopyQueryPoolResults";
    else if (!GetQueryPoolResults)     missing = "vkGetQueryPoolResults";
    if (missing) { printf("FAILED: %s resolves NULL\n", missing); return 1; }
    printf("entrypoints    : all present\n");
    PFN_vkCmdBindIndexBuffer CmdBindIndexBuffer = DPROC(CmdBindIndexBuffer);
    PFN_vkCmdBindIndexBuffer2 CmdBindIndexBuffer2 = DPROC(CmdBindIndexBuffer2);

    printf("vkCmdBindIndexBuffer  : %s\n", CmdBindIndexBuffer ? "present" : "NULL");
    printf("vkCmdBindIndexBuffer2 : %s\n", CmdBindIndexBuffer2 ? "present" : "NULL");
    if (!CmdBindIndexBuffer && !CmdBindIndexBuffer2) {
        printf("FAILED: no index-buffer bind entry point -- NOT IMPLEMENTED\n");
        return 1;
    }
    fflush(stdout);

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    const uint32_t W = 64, H = 64;
    VkFormat FORMAT = VK_FORMAT_R8G8B8A8_UNORM;

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
    /* Occlusion query pool. PRECISE is requested at BeginQuery time; without it
     * the driver selects MALI_OCCLUSION_MODE_PREDICATE and any non-zero value
     * would be legal, which would make the comparison against measured coverage
     * meaningless. */
    VkQueryPoolCreateInfo qpi = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_OCCLUSION,
        .queryCount = tc->nq };
    VkQueryPool qpool;
    CHECK(CreateQueryPool(device, &qpi, NULL, &qpool), "vkCreateQueryPool");

    /* Destination for vkCmdCopyQueryPoolResults. 64-bit results plus the
     * availability word per query. */
    const VkDeviceSize qstride = 16;
    VkBufferCreateInfo qbi = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = qstride * tc->nq * 2,   /* second half for the post-reset copy */
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
    VkBuffer qbuf;
    CHECK(CreateBufferFn(device, &qbi, NULL, &qbuf), "vkCreateBuffer(query)");
    VkMemoryRequirements qmr; GetBufMemReq(device, qbuf, &qmr);
    uint32_t qmt = find_memory_type(&mem_props, qmr.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (qmt == UINT32_MAX) { printf("FAILED: no host visible memory for query buffer\n"); return 1; }
    VkMemoryAllocateInfo qma = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = qmr.size, .memoryTypeIndex = qmt };
    VkDeviceMemory qmem;
    CHECK(AllocateMemory(device, &qma, NULL, &qmem), "vkAllocateMemory(query)");
    CHECK(BindBufMem(device, qbuf, qmem, 0), "vkBindBufferMemory(query)");
    uint64_t *qmap = NULL;
    CHECK(MapMemory(device, qmem, 0, VK_WHOLE_SIZE, 0, (void**)&qmap), "map query buffer");
    /* Poison the destination so "the copy did nothing" is distinguishable from
     * "the copy wrote zero". Without this the stub A/B would be ambiguous for
     * the zero-vertex case. */
    for (unsigned i = 0; i < (qstride * tc->nq * 2) / 8; i++) qmap[i] = 0xDEADBEEFDEADBEEFull;

    CHECK(BeginCommandBuffer(cmd, &cbbi), "beginCmdBuf");
    if (tc->do_reset)
        CmdResetQueryPool(cmd, qpool, 0, tc->nq);

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

    for (uint32_t qi = 0; qi < tc->nq; qi++) {
        CmdBeginQuery(cmd, qpool, qi, VK_QUERY_CONTROL_PRECISE_BIT);
        CmdDraw(cmd, tc->vertex_count[qi], 1, tc->first_vertex[qi], 0);
        CmdEndQuery(cmd, qpool, qi);
    }
    CmdEndRendering(cmd);

    /* This is the call under test. WAIT makes the result defined without a
     * separate host sync, and WITH_AVAILABILITY has the kernel write the
     * availability word so a copy that ran can be told apart from one that did
     * not. */
    CmdCopyQueryPoolResults(cmd, qpool, 0, tc->nq, qbuf, 0, qstride,
                            VK_QUERY_RESULT_64_BIT |
                            VK_QUERY_RESULT_WAIT_BIT |
                            VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (tc->reset_after) {
        /* Reset, then copy again into the second half. No WAIT_BIT here: waiting
         * on a query that was just made unavailable would never return. */
        CmdResetQueryPool(cmd, qpool, 0, tc->nq);
        CmdCopyQueryPoolResults(cmd, qpool, 0, tc->nq, qbuf,
                                qstride * tc->nq, qstride,
                                VK_QUERY_RESULT_64_BIT |
                                VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    }
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
    /* ---- the three independent numbers ----
     *
     * For the two-query case the framebuffer cannot separate the triangles by
     * total coverage alone, so per-quadrant counts are used: triangle A covers
     * the centre and straddles all four quadrants, triangle B sits entirely in
     * the top-left. Rather than attribute pixels, the two-query case asserts
     * that the two queries differ and that their sum equals total coverage,
     * which is checkable without knowing which is which.
     */
    uint64_t host[2] = {0,0};
    VkResult hres = GetQueryPoolResults(device, qpool, 0, tc->nq,
                                        sizeof(host), host, 8,
                                        VK_QUERY_RESULT_64_BIT |
                                        VK_QUERY_RESULT_WAIT_BIT);
    uint64_t devv[2] = {0,0}, avail[2] = {0,0};
    for (uint32_t qi = 0; qi < tc->nq; qi++) {
        devv[qi]  = qmap[(qstride/8)*qi + 0];
        avail[qi] = qmap[(qstride/8)*qi + 1];
    }

    printf("\n--- three independent numbers ---\n");
    printf("pixels drawn (framebuffer)   : %u\n", non_black);
    printf("vkGetQueryPoolResults        : %llu", (unsigned long long)host[0]);
    if (tc->nq > 1) printf(", %llu", (unsigned long long)host[1]);
    printf("   (VkResult=%d)\n", hres);
    printf("vkCmdCopyQueryPoolResults    : %llu", (unsigned long long)devv[0]);
    if (tc->nq > 1) printf(", %llu", (unsigned long long)devv[1]);
    printf("\n");
    printf("availability word            : 0x%llx", (unsigned long long)avail[0]);
    if (tc->nq > 1) printf(", 0x%llx", (unsigned long long)avail[1]);
    printf("\n");

    int pass = 1;
    const uint64_t POISON = 0xDEADBEEFDEADBEEFull;

    /* The copy must have run at all. Poison surviving means the kernel never
     * wrote, which is exactly the pre-patch stub behaviour. */
    for (uint32_t qi = 0; qi < tc->nq; qi++) {
        if (devv[qi] == POISON) {
            printf("MISMATCH: query %u destination still holds the poison value, "
                   "the copy kernel did not write\n", qi);
            pass = 0;
        }
        if (avail[qi] == POISON) {
            printf("MISMATCH: query %u availability word untouched\n", qi);
            pass = 0;
        }
    }

    if (tc->assert_value) {
        if (tc->nq == 1) {
            /* Sole hardcoded expectation in the file, and only for the control:
             * the spec guarantees a query starts at zero, so a zero-vertex draw
             * must report zero passing samples. */
            const uint64_t want = tc->vertex_count[0] == 0 ? 0 : (uint64_t)non_black;
            /* For reset_after the host read happens after the command buffer has
             * already executed the reset, so the query is legitimately
             * unavailable and zeroed by then. Asserting the host value there
             * would be testing the test. The device copy recorded before the
             * reset is still checked, as is the availability transition. */
            if (!tc->reset_after && host[0] != want) {
                printf("MISMATCH: host query %llu != expected %llu\n",
                       (unsigned long long)host[0], (unsigned long long)want);
                pass = 0;
            }
            if (devv[0] != want) {
                printf("MISMATCH: device copy %llu != expected %llu\n",
                       (unsigned long long)devv[0], (unsigned long long)want);
                pass = 0;
            }
        } else {
            if (host[0] == host[1]) {
                printf("MISMATCH: both queries report %llu, the two draws should "
                       "differ\n", (unsigned long long)host[0]);
                pass = 0;
            }
            if (host[0] + host[1] != (uint64_t)non_black) {
                printf("MISMATCH: %llu + %llu != %u pixels drawn\n",
                       (unsigned long long)host[0], (unsigned long long)host[1],
                       non_black);
                pass = 0;
            }
            for (uint32_t qi = 0; qi < tc->nq; qi++)
                if (devv[qi] != host[qi]) {
                    printf("MISMATCH: query %u device %llu != host %llu\n", qi,
                           (unsigned long long)devv[qi],
                           (unsigned long long)host[qi]);
                    pass = 0;
                }
        }
    } else {
        printf("note: value not asserted for this case by design\n");
    }

    if (tc->reset_after) {
        const uint64_t after_v = qmap[(qstride/8)*tc->nq + 0];
        const uint64_t after_a = qmap[(qstride/8)*tc->nq + 1];
        printf("after reset: value=%llu availability=0x%llx\n",
               (unsigned long long)after_v, (unsigned long long)after_a);
        if (after_a == POISON) {
            printf("MISMATCH: post-reset copy never wrote, cannot judge the "
                   "clear kernel\n");
            pass = 0;
        } else if (avail[0] != 1) {
            printf("MISMATCH: availability was not 1 before the reset, so the "
                   "transition proves nothing\n");
            pass = 0;
        } else if (after_a != 0) {
            printf("MISMATCH: availability still 0x%llx after "
                   "vkCmdResetQueryPool, the clear kernel did not run\n",
                   (unsigned long long)after_a);
            pass = 0;
        } else {
            printf("availability transitioned 1 -> 0 across the reset\n");
        }
    }

    printf("OQFP %s px=%u host=%llu dev=%llu avail=0x%llx nq=%u verdict=%s\n",
           tc->name, non_black, (unsigned long long)host[0],
           (unsigned long long)devv[0], (unsigned long long)avail[0], tc->nq,
           pass ? "PASS" : "FAIL");

    UnmapMemory(device, img_mem);
    free(vs_code); free(fs_code);
    printf("DONE\n");
    return pass ? 0 : 2;
}
