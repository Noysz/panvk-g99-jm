/* mrt_shape_test.c
 *
 * Phase 4.3 — multiple render targets on Mali-G57 MC2 (v9/JM), validated with
 * a square and a circle rather than the triangle used everywhere else.
 *
 * HOW CORRECTNESS IS DECIDED
 *
 * No expected pixel count is hand-written anywhere in this file. Two independent
 * checks are used instead:
 *
 *   1. An arithmetic prediction, for the square only. NDC +/-0.5 maps to pixel
 *      16.0 and 48.0 in a 64x64 viewport, so pixel centres 16.5..47.5 are inside
 *      and the count must be exactly 32*32 = 1024. No pixel centre lands on an
 *      edge, so there is no tie to resolve.
 *
 *   2. A CPU reference rasterizer, for both shapes. It walks the same triangle
 *      list the vertex shader builds, applies the same viewport transform, and
 *      applies Vulkan's own single-sample coverage rule: a pixel is covered iff
 *      its centre lies inside the primitive. GPU output is then compared to it
 *      pixel by pixel and the exact mismatch count is printed.
 *
 * Because a CPU/GPU comparison could be corrupted by pixel centres sitting
 * almost exactly on an edge, the test also MEASURES that risk instead of
 * assuming it away: for every pixel it computes a signed margin in pixel units
 * (positive inside, negative outside) and reports the smallest |margin| seen.
 * If that number is large compared to float precision, no pixel was close to a
 * tie and the comparison is unambiguous. Pixels below the epsilon are reported
 * separately as ambiguous rather than being scored as failures.
 *
 * MRT is checked with two attachments that differ in BOTH the drawn colour and
 * the clear colour, so aliasing between attachments is detectable in the covered
 * region and in the untouched region.
 *
 * Env: MRT_CASE=sq1|sq2|ci1|ci2, MRT_PPM_PREFIX=<path>, PANVK_ICD_SO.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <dlfcn.h>
#include "vulkan/vulkan_core.h"

#define CHECK(expr, msg) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { printf("FAILED: %s (VkResult=%d)\n", msg, _r); return 1; } \
} while (0)

typedef PFN_vkVoidFunction (*PFN_icdGetInstanceProcAddr)(VkInstance, const char*);

#define DIM 64
#define MAX_TRIS 64
#define NSEG 64
#define EPS_PX 1e-3f     /* margin below this counts as a tie, in pixel units */

static uint32_t find_memory_type(VkPhysicalDeviceMemoryProperties *mp,
                                  uint32_t bits, VkMemoryPropertyFlags want)
{
    for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
        if ((bits & (1u<<i)) && (mp->memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

static char *read_file(const char *p, size_t *sz) {
    FILE *f = fopen(p, "rb");
    if (!f) { printf("FAILED: cannot open %s\n", p); return NULL; }
    fseek(f,0,SEEK_END); long n = ftell(f); fseek(f,0,SEEK_SET);
    char *b = malloc(n);
    if (fread(b,1,n,f) != (size_t)n) { printf("FAILED: short read %s\n", p); fclose(f); free(b); return NULL; }
    fclose(f); *sz = (size_t)n; return b;
}

#ifndef PANVK_DEFAULT_ICD_SO
#define PANVK_DEFAULT_ICD_SO \
    "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so"
#endif

/* ---------------- CPU reference rasterizer ---------------- */

struct tri { float x[3], y[3]; };   /* pixel-space coordinates */

/* Same transform the GPU applies for viewport (0,0,DIM,DIM). */
static void ndc_to_px(float nx, float ny, float *px, float *py) {
    *px = (nx * 0.5f + 0.5f) * (float)DIM;
    *py = (ny * 0.5f + 0.5f) * (float)DIM;
}

static float edge_fn(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

/* Signed margin in pixel units: positive when the point is inside the triangle,
 * magnitude = perpendicular distance to the closest edge. Winding-agnostic,
 * matching cullMode = NONE. */
static float tri_margin(const struct tri *t, float px, float py) {
    float e[3], len[3];
    for (int i = 0; i < 3; i++) {
        int j = (i+1) % 3;
        e[i]   = edge_fn(t->x[i], t->y[i], t->x[j], t->y[j], px, py);
        float dx = t->x[j] - t->x[i], dy = t->y[j] - t->y[i];
        len[i] = sqrtf(dx*dx + dy*dy);
        if (len[i] == 0.0f) len[i] = 1e-20f;
    }
    int all_pos = (e[0] >= 0 && e[1] >= 0 && e[2] >= 0);
    int all_neg = (e[0] <= 0 && e[1] <= 0 && e[2] <= 0);
    float dmin = INFINITY;
    for (int i = 0; i < 3; i++) {
        float d = fabsf(e[i]) / len[i];
        if (d < dmin) dmin = d;
    }
    return (all_pos || all_neg) ? dmin : -dmin;
}

static int build_square(struct tri *out) {
    const float nx[6] = {-0.5f, 0.5f, 0.5f, -0.5f,  0.5f, -0.5f};
    const float ny[6] = {-0.5f,-0.5f, 0.5f, -0.5f,  0.5f,  0.5f};
    for (int t = 0; t < 2; t++)
        for (int v = 0; v < 3; v++)
            ndc_to_px(nx[t*3+v], ny[t*3+v], &out[t].x[v], &out[t].y[v]);
    return 2;
}

static int build_circle(struct tri *out) {
    /* Mirrors circle.vert exactly, including doing the trig in float. */
    for (int t = 0; t < NSEG; t++) {
        for (int v = 0; v < 3; v++) {
            float nx, ny;
            if (v == 0) { nx = 0.0f; ny = 0.0f; }
            else {
                int k = t + (v - 1);
                float a = (float)(6.28318530717958647692 * (double)k / (double)NSEG);
                nx = cosf(a) * 0.7f;
                ny = sinf(a) * 0.7f;
            }
            ndc_to_px(nx, ny, &out[t].x[v], &out[t].y[v]);
        }
    }
    return NSEG;
}

int main(void) {
    const char *cname = getenv("MRT_CASE");
    if (!cname || !cname[0]) cname = "sq1";
    int is_circle = (cname[0] == 'c');
    /* "only0" attaches two targets but binds a fragment shader that writes just
     * location 0. Nothing can legitimately produce RT1's blue in that setup, so
     * finding blue in RT1 would mean the two locations are not routed
     * independently. Unwritten attachment contents are undefined per spec, so
     * the assertion is deliberately one-sided: no blue, rather than all clear. */
    int only0    = (strstr(cname, "only0") != NULL);
    int rt_count = (strstr(cname, "2") != NULL) ? 2 : 1;

    printf("=== case: %s ===\n", cname);
    printf("shape          : %s\n", is_circle ? "circle (64-segment fan)" : "square (2 triangles)");
    printf("render targets : %d\n", rt_count);
    if (only0)
        printf("mode           : NEGATIVE CONTROL, fragment shader writes location 0 only\n");

    /* ---- CPU reference ---- */
    static struct tri tris[MAX_TRIS];
    int ntris = is_circle ? build_circle(tris) : build_square(tris);
    /* Per-pixel classification. A pixel is only scored when its margin is
     * comfortably away from zero. Pixel centres sitting exactly on the internal
     * edge shared by two triangles are genuinely undecidable from one triangle
     * at a time - Vulkan guarantees the pair fills them watertight, but not which
     * triangle wins. Those are counted and reported, never scored. */
    static int8_t cls[DIM*DIM];      /* +1 inside, -1 outside, 0 ambiguous */
    uint32_t scored_in = 0, scored_out = 0, ambiguous = 0;
    float min_abs_margin = INFINITY, min_nonzero_margin = INFINITY;
    for (int y = 0; y < DIM; y++) {
        for (int x = 0; x < DIM; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float best = -INFINITY;
            for (int t = 0; t < ntris; t++) {
                float m = tri_margin(&tris[t], px, py);
                if (m > best) best = m;
            }
            float a = fabsf(best);
            if (a < min_abs_margin) min_abs_margin = a;
            if (a >= EPS_PX && a < min_nonzero_margin) min_nonzero_margin = a;
            if (a < EPS_PX)      { cls[y*DIM+x] =  0; ambiguous++;  }
            else if (best > 0)   { cls[y*DIM+x] =  1; scored_in++;  }
            else                 { cls[y*DIM+x] = -1; scored_out++; }
        }
    }
    printf("cpu reference  : %d triangles\n", ntris);
    printf("  definitely inside  : %u\n", scored_in);
    printf("  definitely outside : %u\n", scored_out);
    printf("  ambiguous (on edge): %u   <- reported, never scored\n", ambiguous);
    printf("  min |margin|       : %.6f px\n", min_abs_margin);
    printf("  min |margin| >= eps: %.6f px   (eps %.6f)\n", min_nonzero_margin, EPS_PX);
    if (!is_circle) {
        printf("arithmetic pred: total drawn must be exactly 1024 (32x32).\n");
        printf("  NDC +/-0.5 maps to pixel 16.0 and 48.0, so pixel centres\n");
        printf("  16.5..47.5 are covered. Vulkan fills the shared diagonal\n");
        printf("  watertight, so the union is the full 1024 either way.\n");
        printf("  bound check: %u <= 1024 <= %u\n", scored_in, scored_in + ambiguous);
    } else {
        printf("no closed form for a circle, so the total is bound-checked:\n");
        printf("  %u <= drawn <= %u\n", scored_in, scored_in + ambiguous);
    }
    fflush(stdout);

    /* ---- Vulkan ---- */
    const char *icd = getenv("PANVK_ICD_SO");
    if (!icd || !icd[0]) icd = PANVK_DEFAULT_ICD_SO;
    void *lib = dlopen(icd, RTLD_NOW);
    if (!lib) { printf("dlopen failed: %s\n", dlerror()); return 1; }
    PFN_icdGetInstanceProcAddr gpa =
        (PFN_icdGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!gpa) { printf("dlsym failed\n"); return 1; }

    PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
    VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                             .apiVersion = VK_API_VERSION_1_3 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                 .pApplicationInfo = &ai };
    VkInstance inst;
    CHECK(CreateInstance(&ici, NULL, &inst), "vkCreateInstance");

    #define IPROC(n) (PFN_vk##n)gpa(inst, "vk" #n)
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr = IPROC(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EnumPD = IPROC(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CreateDevice = IPROC(CreateDevice);
    PFN_vkGetPhysicalDeviceMemoryProperties GetMemProps = IPROC(GetPhysicalDeviceMemoryProperties);
    PFN_vkGetPhysicalDeviceProperties GetProps = IPROC(GetPhysicalDeviceProperties);

    uint32_t n = 0;
    CHECK(EnumPD(inst, &n, NULL), "enum count");
    if (!n) { printf("FAILED: no device\n"); return 1; }
    VkPhysicalDevice pdev;
    CHECK(EnumPD(inst, &n, &pdev), "enum fetch");

    VkPhysicalDeviceProperties props;
    GetProps(pdev, &props);
    printf("device         : %s\n", props.deviceName);
    printf("maxColorAttach : %u   (queried, not assumed)\n",
           props.limits.maxColorAttachments);
    if ((uint32_t)rt_count > props.limits.maxColorAttachments) {
        printf("FAILED: case needs %d attachments, device reports %u\n",
               rt_count, props.limits.maxColorAttachments);
        return 1;
    }
    VkPhysicalDeviceMemoryProperties mp;
    GetMemProps(pdev, &mp);

    VkPhysicalDeviceVulkan13Features f13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE };
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &f13, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VkDevice dev;
    CHECK(CreateDevice(pdev, &dci, NULL, &dev), "vkCreateDevice");

    #define DP(x) (PFN_vk##x)GetDeviceProcAddr(dev, "vk" #x)
    PFN_vkGetDeviceQueue GetDeviceQueue = DP(GetDeviceQueue);
    PFN_vkCreateCommandPool CreateCommandPool = DP(CreateCommandPool);
    PFN_vkAllocateCommandBuffers AllocCB = DP(AllocateCommandBuffers);
    PFN_vkBeginCommandBuffer BeginCB = DP(BeginCommandBuffer);
    PFN_vkEndCommandBuffer EndCB = DP(EndCommandBuffer);
    PFN_vkQueueSubmit QueueSubmit = DP(QueueSubmit);
    PFN_vkQueueWaitIdle QueueWaitIdle = DP(QueueWaitIdle);
    PFN_vkCreateImage CreateImage = DP(CreateImage);
    PFN_vkGetImageMemoryRequirements GetImgReq = DP(GetImageMemoryRequirements);
    PFN_vkGetImageSubresourceLayout GetSubLayout = DP(GetImageSubresourceLayout);
    PFN_vkAllocateMemory AllocMem = DP(AllocateMemory);
    PFN_vkBindImageMemory BindImg = DP(BindImageMemory);
    PFN_vkCreateImageView CreateImageView = DP(CreateImageView);
    PFN_vkMapMemory MapMemory = DP(MapMemory);
    PFN_vkUnmapMemory UnmapMemory = DP(UnmapMemory);
    PFN_vkCreateShaderModule CreateShader = DP(CreateShaderModule);
    PFN_vkCreatePipelineLayout CreatePL = DP(CreatePipelineLayout);
    PFN_vkCreateGraphicsPipelines CreateGP = DP(CreateGraphicsPipelines);
    PFN_vkCmdBeginRendering CmdBeginRendering = DP(CmdBeginRendering);
    PFN_vkCmdEndRendering CmdEndRendering = DP(CmdEndRendering);
    PFN_vkCmdBindPipeline CmdBindPipeline = DP(CmdBindPipeline);
    PFN_vkCmdSetViewport CmdSetViewport = DP(CmdSetViewport);
    PFN_vkCmdSetScissor CmdSetScissor = DP(CmdSetScissor);
    PFN_vkCmdDraw CmdDraw = DP(CmdDraw);
    PFN_vkCmdPipelineBarrier CmdBarrier = DP(CmdPipelineBarrier);

    VkQueue queue;
    GetDeviceQueue(dev, 0, 0, &queue);
    VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;

    /* Distinct clear colours so aliasing shows up in the untouched region too. */
    const float clears[2][4] = {
        {0.0f, 0.0f, 0.0f, 1.0f},        /* RT0 -> 0,0,0     */
        {0.0f, 0.25f, 0.0f, 1.0f},       /* RT1 -> 0,64,0    */
    };
    const uint8_t clear_u8[2][3] = { {0,0,0}, {0,64,0} };
    const uint8_t draw_u8[2][3]  = { {255,0,0}, {0,0,255} };

    VkImage img[2]; VkDeviceMemory imem[2]; VkImageView iview[2];
    for (int r = 0; r < rt_count; r++) {
        VkImageCreateInfo c = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = FMT,
            .extent = {DIM,DIM,1}, .mipLevels = 1, .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_LINEAR,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED };
        CHECK(CreateImage(dev, &c, NULL, &img[r]), "vkCreateImage");
        VkMemoryRequirements mr;
        GetImgReq(dev, img[r], &mr);
        uint32_t mt = find_memory_type(&mp, mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (mt == UINT32_MAX) { printf("FAILED: no host-visible mem\n"); return 1; }
        VkMemoryAllocateInfo ma = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = mr.size, .memoryTypeIndex = mt };
        CHECK(AllocMem(dev, &ma, NULL, &imem[r]), "vkAllocateMemory");
        CHECK(BindImg(dev, img[r], imem[r], 0), "vkBindImageMemory");
        VkImageViewCreateInfo vi = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = img[r], .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FMT,
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1} };
        CHECK(CreateImageView(dev, &vi, NULL, &iview[r]), "vkCreateImageView");
    }
    printf("attachments    : %d created, distinct clear colours\n", rt_count);

    size_t vs_sz=0, fs_sz=0;
    char *vs = read_file(is_circle ? "circle.vert.spv" : "square.vert.spv", &vs_sz);
    if (!vs) return 1;
    char *fs = read_file((rt_count == 2 && !only0) ? "mrt2.frag.spv" : "mrt1.frag.spv", &fs_sz);
    if (!fs) return 1;
    VkShaderModuleCreateInfo vm = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vs_sz, .pCode = (uint32_t*)vs };
    VkShaderModule vmod;
    CHECK(CreateShader(dev, &vm, NULL, &vmod), "shader(vert)");
    VkShaderModuleCreateInfo fm = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fs_sz, .pCode = (uint32_t*)fs };
    VkShaderModule fmod;
    CHECK(CreateShader(dev, &fm, NULL, &fmod), "shader(frag)");

    VkPipelineLayoutCreateInfo plc = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    VkPipelineLayout pl;
    CHECK(CreatePL(dev, &plc, NULL, &pl), "pipelineLayout");

    VkPipelineShaderStageCreateInfo st[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vmod, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
          .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fmod, .pName = "main" } };
    VkPipelineVertexInputStateCreateInfo vin = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo iasm = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST };
    VkPipelineViewportStateCreateInfo vps = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1, .scissorCount = 1 };
    VkPipelineRasterizationStateCreateInfo rs = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE, .lineWidth = 1.0f };
    VkPipelineMultisampleStateCreateInfo ms = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT };
    VkPipelineColorBlendAttachmentState cba[2];
    for (int r = 0; r < rt_count; r++) {
        cba[r] = (VkPipelineColorBlendAttachmentState){
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                              VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT };
    }
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = rt_count, .pAttachments = cba };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn };
    VkFormat fmts[2] = { FMT, FMT };
    VkPipelineRenderingCreateInfo pri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = rt_count, .pColorAttachmentFormats = fmts };
    VkGraphicsPipelineCreateInfo gp = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
        .stageCount = 2, .pStages = st, .pVertexInputState = &vin,
        .pInputAssemblyState = &iasm, .pViewportState = &vps,
        .pRasterizationState = &rs, .pMultisampleState = &ms,
        .pColorBlendState = &cb, .pDynamicState = &dy, .layout = pl };
    VkPipeline pipe;
    CHECK(CreateGP(dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipe), "vkCreateGraphicsPipelines");

    VkCommandPoolCreateInfo cpc = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0 };
    VkCommandPool cp;
    CHECK(CreateCommandPool(dev, &cpc, NULL, &cp), "commandPool");
    VkCommandBufferAllocateInfo cba2 = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cp, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cmd;
    CHECK(AllocCB(dev, &cba2, &cmd), "allocCB");
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(BeginCB(cmd, &bi), "beginCB");

    VkImageMemoryBarrier bars[2];
    for (int r = 0; r < rt_count; r++)
        bars[r] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = img[r],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
    CmdBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               0,0,NULL,0,NULL,rt_count,bars);

    VkRenderingAttachmentInfo att[2];
    for (int r = 0; r < rt_count; r++)
        att[r] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = iview[r],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { .float32 = { clears[r][0], clears[r][1],
                                                    clears[r][2], clears[r][3] } } } };
    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, {DIM,DIM} }, .layerCount = 1,
        .colorAttachmentCount = rt_count, .pColorAttachments = att };
    CmdBeginRendering(cmd, &ri);

    VkViewport vp = { 0,0,(float)DIM,(float)DIM,0.0f,1.0f };
    VkRect2D sc = { {0,0}, {DIM,DIM} };
    CmdSetViewport(cmd, 0, 1, &vp);
    CmdSetScissor(cmd, 0, 1, &sc);
    CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    uint32_t vcount = is_circle ? (NSEG*3) : 6;
    printf("vertexCount    : %u\n", vcount);
    CmdDraw(cmd, vcount, 1, 0, 0);
    CmdEndRendering(cmd);
    CHECK(EndCB(cmd), "endCB");

    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1, .pCommandBuffers = &cmd };
    CHECK(QueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    /* ---- compare each attachment against the CPU reference ---- */
    const char *prefix = getenv("MRT_PPM_PREFIX");
    int all_ok = 1;
    static uint8_t rtcopy[2][DIM*DIM*3];

    for (int r = 0; r < rt_count; r++) {
        VkImageSubresource sub = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
        VkSubresourceLayout sl;
        GetSubLayout(dev, img[r], &sub, &sl);
        void *m = NULL;
        CHECK(MapMemory(dev, imem[r], 0, VK_WHOLE_SIZE, 0, &m), "map");
        uint8_t *base = (uint8_t*)m + sl.offset;

        uint32_t drawn_total = 0, mism_in = 0, mism_out = 0, wrong_colour = 0;
        uint32_t amb_drawn = 0, amb_clear = 0, amb_other = 0;
        for (int y = 0; y < DIM; y++) {
            uint8_t *row = base + y*sl.rowPitch;
            for (int x = 0; x < DIM; x++) {
                uint8_t *p = row + x*4;
                memcpy(&rtcopy[r][(y*DIM+x)*3], p, 3);

                int is_draw  = (p[0]==draw_u8[r][0] && p[1]==draw_u8[r][1] && p[2]==draw_u8[r][2]);
                int is_clear = (p[0]==clear_u8[r][0]&& p[1]==clear_u8[r][1]&& p[2]==clear_u8[r][2]);
                if (is_draw) drawn_total++;
                if (!is_draw && !is_clear) wrong_colour++;

                switch (cls[y*DIM+x]) {
                case  1: if (!is_draw)  mism_in++;  break;
                case -1: if (!is_clear) mism_out++; break;
                default:
                    if (is_draw)       amb_drawn++;
                    else if (is_clear) amb_clear++;
                    else               amb_other++;
                    break;
                }
            }
        }
        printf("\n--- RT%d ---\n", r);
        printf("expect drawn %u,%u,%u   clear %u,%u,%u\n",
               draw_u8[r][0],draw_u8[r][1],draw_u8[r][2],
               clear_u8[r][0],clear_u8[r][1],clear_u8[r][2]);
        printf("total drawn-colour pixels : %u\n", drawn_total);
        printf("mismatch, definitely in   : %u   (must be 0)\n", mism_in);
        printf("mismatch, definitely out  : %u   (must be 0)\n", mism_out);
        printf("pixels of neither colour  : %u   (must be 0)\n", wrong_colour);
        printf("on-edge pixels: drawn %u, clear %u, other %u\n",
               amb_drawn, amb_clear, amb_other);

        int rt_ok;
        if (only0 && r == 1) {
            /* One-sided claim: RT1 must contain no pixel of the blue colour that
             * only the two-output shader can emit. Contents are otherwise
             * undefined and are reported without being scored. */
            printf("negative control: blue pixels in RT1 = %u   (must be 0)\n",
                   drawn_total);
            rt_ok = (drawn_total == 0);
            printf("RT%d verdict               : %s\n", r,
                   rt_ok ? "NO LEAK from location 0" : "LEAK DETECTED");
            if (!rt_ok) all_ok = 0;
            if (prefix && prefix[0]) {
                char path[512];
                snprintf(path, sizeof(path), "%s_rt%d.ppm", prefix, r);
                FILE *pf = fopen(path, "wb");
                if (pf) {
                    fprintf(pf, "P6\n%d %d\n255\n", DIM, DIM);
                    fwrite(rtcopy[r], 1, DIM*DIM*3, pf);
                    fclose(pf);
                    printf("VISUAL_DUMP: %s\n", path);
                }
            }
            UnmapMemory(dev, imem[r]);
            continue;
        }
        rt_ok = (mism_in == 0 && mism_out == 0 && wrong_colour == 0);
        /* Total must lie inside the bound the reference establishes. */
        int in_bounds = (drawn_total >= scored_in &&
                         drawn_total <= scored_in + ambiguous);
        printf("bound %u <= %u <= %u        : %s\n",
               scored_in, drawn_total, scored_in + ambiguous,
               in_bounds ? "inside" : "OUTSIDE");
        if (!in_bounds) rt_ok = 0;
        if (!is_circle) {
            printf("square arithmetic: %u == 1024   : %s\n",
                   drawn_total, drawn_total == 1024 ? "EXACT" : "MISMATCH");
            if (drawn_total != 1024) rt_ok = 0;
        }
        printf("RT%d verdict               : %s\n", r,
               rt_ok ? "MATCHES REFERENCE" : "MISMATCH");
        if (!rt_ok) all_ok = 0;

        if (prefix && prefix[0]) {
            char path[512];
            snprintf(path, sizeof(path), "%s_rt%d.ppm", prefix, r);
            FILE *pf = fopen(path, "wb");
            if (pf) {
                fprintf(pf, "P6\n%d %d\n255\n", DIM, DIM);
                fwrite(rtcopy[r], 1, DIM*DIM*3, pf);
                fclose(pf);
                printf("VISUAL_DUMP: %s\n", path);
            }
        }
        UnmapMemory(dev, imem[r]);
    }

    if (rt_count == 2 && !only0) {
        int same = (memcmp(rtcopy[0], rtcopy[1], DIM*DIM*3) == 0);
        printf("\n--- aliasing check ---\n");
        printf("RT0 and RT1 byte-identical : %s\n", same ? "YES" : "NO");
        printf("verdict                    : %s\n",
               same ? "FAIL, attachments alias the same memory"
                    : "PASS, attachments are independent");
        if (same) all_ok = 0;
    }

    printf("\nFINGERPRINT %s in=%u out=%u amb=%u minmargin=%.6f ok=%d\n",
           cname, scored_in, scored_out, ambiguous, min_abs_margin, all_ok);
    printf("\n%s\n", all_ok ? "SUCCESS" : "FAILED");
    free(vs); free(fs);
    return all_ok ? 0 : 1;
}
