/*
 * T4.5.10 -- attachment independence on Valhall v9 / Job Manager.
 *
 * The hole this closes
 * --------------------
 * mrt_shape_test.c asserts that RT0 and RT1 are not byte-identical after drawing
 * different colours to each. That catches full aliasing, where both views point
 * at one allocation, and nothing else. PARTIAL aliasing passes it: if the two
 * attachments overlapped in only part of their memory the buffers would still
 * differ somewhere, and the check would report PASS.
 *
 * So the claim that test supports is "not fully aliased", not "independent".
 *
 * The design here
 * ---------------
 * Establish a known prior state for RT1, draw to RT0, then require RT1 to be
 * unchanged byte for byte. Any overlap at any offset changes some byte.
 *
 * Two things make it strict:
 *
 * 1. RT1 is preloaded from the host with a PSEUDORANDOM pattern that is a
 *    function of the byte offset, not a flat colour. A flat clear would hide an
 *    overwrite that happened to write the same value; with a per-offset pattern
 *    every byte position has a distinct expected value. The images are LINEAR and
 *    host-visible, so this is written straight through the mapping.
 *
 * 2. RT1 is attached with loadOp LOAD, storeOp STORE, and
 *    colorWriteMask = 0. The zero write mask is what makes the assertion
 *    two-sided instead of one-sided. Vulkan leaves the contents of an attachment
 *    undefined when the fragment shader does not write its location, which is why
 *    the existing sq2only0 case could only assert "no blue appeared". A zero write
 *    mask is different: it guarantees no writes occur, so RT1 is REQUIRED to come
 *    back exactly as it went in, and "unchanged" becomes a real assertion.
 *
 * Cases, via ALIAS_CASE:
 *   mask     two attachments bound, RT1 write mask 0, shader writes both
 *            locations. Exercises the MRT path itself while guaranteeing RT1 is
 *            not legitimately written.
 *   solo     RT1 not attached at all in the pass, only RT0. Tests allocation
 *            level overlap rather than the MRT path.
 *   selfcheck  negative control. Deliberately writes one byte into RT1's mapping
 *            from the host after the snapshot is taken, so the comparison MUST
 *            report exactly one differing byte. If this case reports zero
 *            differences the comparison is not looking at the memory it thinks it
 *            is, and no other result in this file means anything.
 *
 * What is reported: the number of differing bytes, the first differing offset,
 * and the pixel coordinate it maps to. A single byte is enough to fail.
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

/* Pattern is a function of the byte offset so every position has a distinct
 * expected value. A flat fill would hide an overwrite that happened to write the
 * value already there. */
static inline uint8_t alias_pattern(size_t off)
{
    return (uint8_t)(0x5Au ^ (off * 37u) ^ (off >> 8));
}
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
    const char *cname = getenv("ALIAS_CASE");
    if (!cname || !cname[0]) cname = "mask";
    const int case_mask      = !strcmp(cname, "mask");
    const int case_solo      = !strcmp(cname, "solo");
    const int case_selfcheck = !strcmp(cname, "selfcheck");
    if (!case_mask && !case_solo && !case_selfcheck) {
        printf("FAILED: unknown ALIAS_CASE=%s (mask|solo|selfcheck)\n", cname);
        return 1;
    }
    /* selfcheck runs the same flow as mask; the difference is a deliberate host
     * side poke after the snapshot, applied further down. */
    const int attach_rt1 = case_mask || case_selfcheck;
    int is_circle = 0;                 /* the square covers a contiguous block */
    int only0    = 0;
    int rt_count = 2;                  /* both images always exist; only the
                                        * attachment list varies */

    printf("=== case: %s ===\n", cname);
    printf("shape          : %s\n", is_circle ? "circle (64-segment fan)" : "square (2 triangles)");
    printf("RT1 attached   : %s\n", attach_rt1 ? "yes, with colorWriteMask = 0" : "no");
    if (case_selfcheck)
        printf("mode           : NEGATIVE CONTROL, one byte poked into RT1 after the snapshot\n");

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
    VkDeviceSize rt1_alloc_size = 0;
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
        if (r == 1) rt1_alloc_size = mr.size;
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
    /* The load-bearing line of this test. A zero write mask is a spec guarantee
     * that no writes reach RT1, which is what turns "RT1 is unchanged" from an
     * undefined-value situation into a real assertion. */
    if (attach_rt1)
        cba[1].colorWriteMask = 0;
    const uint32_t attach_count = attach_rt1 ? 2 : 1;
    VkPipelineColorBlendStateCreateInfo cb = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = attach_count, .pAttachments = cba };
    VkDynamicState dyn[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2, .pDynamicStates = dyn };
    VkFormat fmts[2] = { FMT, FMT };
    VkPipelineRenderingCreateInfo pri = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = attach_count, .pColorAttachmentFormats = fmts };
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
    /* Preload RT1 with the per-offset pattern and keep a copy of exactly what
     * was written. The images are LINEAR and host visible, so the mapping is the
     * real backing store. The snapshot covers the whole allocation the driver
     * reported, not just the visible DIM*DIM region, so padding and row stride
     * slack are compared too. */
    VkImageSubresource sub1 = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout sl1;
    GetSubLayout(dev, img[1], &sub1, &sl1);
    const size_t rt1_bytes = (size_t)rt1_alloc_size;
    uint8_t *rt1_map = NULL;
    CHECK(MapMemory(dev, imem[1], 0, VK_WHOLE_SIZE, 0, (void**)&rt1_map), "map RT1");
    uint8_t *snapshot = malloc(rt1_bytes);
    if (!snapshot) { printf("FAILED: out of memory\n"); return 1; }
    for (size_t b = 0; b < rt1_bytes; b++)
        rt1_map[b] = alias_pattern(b);
    memcpy(snapshot, rt1_map, rt1_bytes);
    printf("RT1 preloaded  : %zu bytes of per-offset pattern (rowPitch=%llu)\n",
           rt1_bytes, (unsigned long long)sl1.rowPitch);

    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    CHECK(BeginCB(cmd, &bi), "beginCB");

    VkImageMemoryBarrier bars[2];
    for (uint32_t r = 0; r < attach_count; r++)
        bars[r] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            /* UNDEFINED permits the implementation to discard contents. RT1 is
             * carrying the pattern, so it must transition from GENERAL. */
            .oldLayout = r == 1 ? VK_IMAGE_LAYOUT_GENERAL
                                : VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = img[r],
            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT };
    CmdBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               0,0,NULL,0,NULL,attach_count,bars);

    VkRenderingAttachmentInfo att[2];
    for (uint32_t r = 0; r < attach_count; r++)
        att[r] = (VkRenderingAttachmentInfo){
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = iview[r],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            /* RT1 must be LOADed, not cleared: the pattern written from the host
             * is the thing being preserved. */
            .loadOp = r == 1 ? VK_ATTACHMENT_LOAD_OP_LOAD
                             : VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { .float32 = { clears[r][0], clears[r][1],
                                                    clears[r][2], clears[r][3] } } } };
    VkRenderingInfo ri = { .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { {0,0}, {DIM,DIM} }, .layerCount = 1,
        .colorAttachmentCount = attach_count, .pColorAttachments = att };
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

    /* ---- RT0: confirm the draw actually happened ----
     *
     * Needed because "RT1 unchanged" is trivially true if nothing was drawn at
     * all. The square covers a known 1024 pixels, derived arithmetically: NDC
     * +/-0.5 maps to pixel 16.0 and 48.0 in a 64-wide viewport, so covered pixel
     * centres run 16.5 to 47.5, which is 32 x 32.
     */
    VkImageSubresource sub0 = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
    VkSubresourceLayout sl0;
    GetSubLayout(dev, img[0], &sub0, &sl0);
    uint8_t *rt0_map = NULL;
    CHECK(MapMemory(dev, imem[0], 0, VK_WHOLE_SIZE, 0, (void**)&rt0_map), "map RT0");
    uint32_t rt0_drawn = 0;
    for (int y = 0; y < DIM; y++) {
        uint8_t *row = rt0_map + sl0.offset + (size_t)y * sl0.rowPitch;
        for (int x = 0; x < DIM; x++) {
            uint8_t *px = row + x*4;
            if (px[0] || px[1] || px[2]) rt0_drawn++;
        }
    }
    UnmapMemory(dev, imem[0]);

    /* ---- the negative control's deliberate corruption ---- */
    if (case_selfcheck) {
        const size_t poke = rt1_bytes / 3;
        rt1_map[poke] ^= 0xFF;
        printf("selfcheck      : flipped byte at offset %zu after the snapshot\n",
               poke);
    }

    /* ---- RT1: must be byte for byte what went in ---- */
    size_t diff_bytes = 0, first_diff = (size_t)-1;
    for (size_t b = 0; b < rt1_bytes; b++) {
        if (rt1_map[b] != snapshot[b]) {
            diff_bytes++;
            if (first_diff == (size_t)-1) first_diff = b;
        }
    }

    printf("\n--- attachment independence ---\n");
    printf("RT0 pixels drawn      : %u (square covers 1024 by construction)\n",
           rt0_drawn);
    printf("RT1 bytes compared    : %zu\n", rt1_bytes);
    printf("RT1 bytes differing   : %zu\n", diff_bytes);
    if (first_diff != (size_t)-1) {
        const size_t row = sl1.rowPitch ? (first_diff - sl1.offset) / sl1.rowPitch : 0;
        const size_t col = sl1.rowPitch ? ((first_diff - sl1.offset) % sl1.rowPitch) / 4 : 0;
        printf("first differing byte  : offset %zu -> approx pixel (%zu,%zu), "
               "expected 0x%02x got 0x%02x\n",
               first_diff, col, row, snapshot[first_diff], rt1_map[first_diff]);
    }

    if (rt0_drawn != 1024) {
        printf("MISMATCH: RT0 shows %u pixels, expected 1024. RT1 being unchanged "
               "would prove nothing if the draw did not happen.\n", rt0_drawn);
        all_ok = 0;
    }
    if (case_selfcheck) {
        /* Inverted expectation: this case MUST detect exactly one changed byte.
         * Zero would mean the comparison is not reading the memory it thinks. */
        if (diff_bytes != 1) {
            printf("MISMATCH: selfcheck expected exactly 1 differing byte, got "
                   "%zu. The comparison is not looking at RT1's real storage.\n",
                   diff_bytes);
            all_ok = 0;
        } else {
            printf("verdict               : PASS, the comparison detects a single "
                   "byte change\n");
        }
    } else {
        if (diff_bytes != 0) {
            printf("MISMATCH: RT1 changed while only RT0 was written. The "
                   "attachments share storage.\n");
            all_ok = 0;
        } else {
            printf("verdict               : PASS, RT1 identical across %zu bytes\n",
                   rt1_bytes);
        }
    }
    UnmapMemory(dev, imem[1]);

    printf("\nALIASFP %s rt0=%u cmp=%zu diff=%zu ok=%d verdict=%s\n",
           cname, rt0_drawn, rt1_bytes, diff_bytes, all_ok,
           all_ok ? "PASS" : "FAIL");
    printf("\n%s\n", all_ok ? "SUCCESS" : "FAILED");
    free(snapshot); free(vs); free(fs);
    return all_ok ? 0 : 1;
}
