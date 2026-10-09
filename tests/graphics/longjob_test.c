/* longjob_test.c -- GPU jobs long enough that kbase stops and resumes them.
 *
 * kbase (JM) soft-stops every job chain that has been on a job slot for
 * soft_stop_ticks scheduler ticks (default 1 tick of 100 ms, so after
 * 100-200 ms) and later resumes it from JS_TAIL; it hard-stops a chain
 * after 5 s (event 0x4 TERMINATED). Games at 10-20 fps have passes that long,
 * short CTS cases do not. This test checks that a stopped and resumed job
 * still renders every pixel right.
 *
 * Shaders: tests/shaders/longjob.vert/.frag (longjob_*_spv.h generated with
 *   glslangValidator -V --target-env vulkan1.1 --vn longjob_vert_spv \
 *     tests/shaders/longjob.vert -o tests/graphics/longjob_vert_spv.h, same
 *     for the .frag)
 * Build: clang -O2 -I<mesa>/include longjob_test.c -ldl
 * Run:   PANVK_ICD_SO=<libvulkan_panfrost.so> LONGJOB_MODE=frag ./a.out
 *
 * 512x512 RGBA8 target.
 *   LONGJOB_MODE=frag  one fullscreen draw; every pixel runs LONGJOB_ITERS
 *                      steps of a 32-bit LCG seeded by its position and
 *                      writes the 4 result bytes (one long fragment job).
 *   LONGJOB_MODE=vert  LONGJOB_GRID^2 draws (default 32x32 cells of 16x16
 *                      pixels); each vertex runs the LCG seeded by its cell
 *                      and the cell gets the result as a flat colour (one
 *                      long vertex/tiler chain of many MALLOC_VERTEX jobs).
 * The CPU computes every expected value (LCG jump-ahead). LONGJOB_REPS
 * submits (default 3), each one checked. A short run (small ITERS, well
 * under 100 ms) is the control: it is never stopped by kbase.
 * PASS (exit 0): every pixel of every rep matches.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vulkan/vulkan_core.h"
#include "longjob_vert_spv.h"
#include "longjob_frag_spv.h"

#define CHECK(e, m)                                                          \
   do {                                                                      \
      VkResult _r = (e);                                                     \
      if (_r != VK_SUCCESS) {                                                \
         printf("FAILED: %s (VkResult=%d)\n", m, _r);                        \
         return 2;                                                           \
      }                                                                      \
   } while (0)

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);
#define D 512

static uint32_t
mtype(VkPhysicalDeviceMemoryProperties *mp, uint32_t bits,
      VkMemoryPropertyFlags want)
{
   for (uint32_t i = 0; i < mp->memoryTypeCount; i++)
      if ((bits & (1u << i)) &&
          (mp->memoryTypes[i].propertyFlags & want) == want)
         return i;
   return UINT32_MAX;
}

/* h_n = A * h_0 + C for n steps of h = h * 1664525 + 1013904223. */
static void
lcg_jump(uint32_t n, uint32_t *A, uint32_t *C)
{
   uint32_t a = 1, c = 0;
   for (uint32_t i = 0; i < n; i++) {
      a *= 1664525u;
      c = c * 1664525u + 1013904223u;
   }
   *A = a;
   *C = c;
}

static double
now_ms(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int
main(void)
{
   const char *mode_s = getenv("LONGJOB_MODE");
   const int vert = mode_s && !strcmp(mode_s, "vert");
   const char *e;
   uint32_t iters = (e = getenv("LONGJOB_ITERS")) ? strtoul(e, NULL, 10)
                                                  : (vert ? 200000 : 4000);
   uint32_t reps = (e = getenv("LONGJOB_REPS")) ? strtoul(e, NULL, 10) : 3;
   uint32_t grid = (e = getenv("LONGJOB_GRID")) ? strtoul(e, NULL, 10) : 32;
   if (!grid || D % grid)
      grid = 32;

   const char *so = getenv("PANVK_ICD_SO");
   void *l = so ? dlopen(so, RTLD_NOW) : NULL;
   if (!l) {
      printf("FAILED: dlopen %s\n", so ? dlerror() : "(PANVK_ICD_SO unset)");
      return 2;
   }
   G gpa = (G)dlsym(l, "vk_icdGetInstanceProcAddr");
   VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &ai};
   VkInstance inst;
   CHECK(((PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance"))(&ii, NULL,
                                                               &inst),
         "instance");
#define IP(n) PFN_vk##n n = (PFN_vk##n)gpa(inst, "vk" #n)
   IP(GetDeviceProcAddr); IP(EnumeratePhysicalDevices); IP(CreateDevice);
   IP(GetPhysicalDeviceMemoryProperties);
   uint32_t n = 1;
   VkPhysicalDevice pd;
   CHECK(EnumeratePhysicalDevices(inst, &n, &pd), "enum");
   VkPhysicalDeviceMemoryProperties mp;
   GetPhysicalDeviceMemoryProperties(pd, &mp);
   VkPhysicalDeviceVulkan13Features f13 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .dynamicRendering = VK_TRUE};
   float pr = 1;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 1, .pQueuePriorities = &pr};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .pNext = &f13, .queueCreateInfoCount = 1,
                            .pQueueCreateInfos = &qi};
   VkDevice dev;
   CHECK(CreateDevice(pd, &di, NULL, &dev), "device");
#define DP(x) PFN_vk##x x = (PFN_vk##x)GetDeviceProcAddr(dev, "vk" #x)
   DP(GetDeviceQueue); DP(CreateCommandPool); DP(AllocateCommandBuffers);
   DP(BeginCommandBuffer); DP(EndCommandBuffer); DP(QueueSubmit);
   DP(QueueWaitIdle); DP(CreateImage); DP(GetImageMemoryRequirements);
   DP(GetImageSubresourceLayout); DP(AllocateMemory); DP(BindImageMemory);
   DP(CreateImageView); DP(MapMemory); DP(CreateShaderModule);
   DP(CreatePipelineLayout); DP(CreateGraphicsPipelines);
   DP(CmdBeginRendering); DP(CmdEndRendering); DP(CmdBindPipeline);
   DP(CmdSetViewport); DP(CmdSetScissor); DP(CmdDraw); DP(CmdPipelineBarrier);
   DP(CmdPushConstants); DP(ResetCommandBuffer);
   VkQueue q;
   GetDeviceQueue(dev, 0, 0, &q);

   const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
   VkImageCreateInfo ic = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D, .format = FMT, .extent = {D, D, 1},
      .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
   VkImage img;
   CHECK(CreateImage(dev, &ic, NULL, &img), "image");
   VkMemoryRequirements mr;
   GetImageMemoryRequirements(dev, img, &mr);
   VkMemoryAllocateInfo ma = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mtype(&mp, mr.memoryTypeBits,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
   VkDeviceMemory imem;
   CHECK(AllocateMemory(dev, &ma, NULL, &imem), "memory");
   BindImageMemory(dev, img, imem, 0);
   VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = img,
      .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FMT,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   VkImageView view;
   CHECK(CreateImageView(dev, &vi, NULL, &view), "view");

   VkShaderModuleCreateInfo smi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(longjob_vert_spv), .pCode = longjob_vert_spv};
   VkShaderModule vs, fs;
   CHECK(CreateShaderModule(dev, &smi, NULL, &vs), "vs");
   smi.codeSize = sizeof(longjob_frag_spv);
   smi.pCode = longjob_frag_spv;
   CHECK(CreateShaderModule(dev, &smi, NULL, &fs), "fs");
   VkPushConstantRange pcr = {
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
   VkPipelineLayout pl;
   CHECK(CreatePipelineLayout(dev, &pli, NULL, &pl), "layout");
   VkPipelineShaderStageCreateInfo st[2] = {
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = "main"},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fs, .pName = "main"}};
   VkPipelineVertexInputStateCreateInfo vin = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
   VkPipelineInputAssemblyStateCreateInfo ia = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
   VkPipelineViewportStateCreateInfo vpi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1, .scissorCount = 1};
   VkPipelineRasterizationStateCreateInfo rsi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL, .cullMode = VK_CULL_MODE_NONE,
      .lineWidth = 1};
   VkPipelineMultisampleStateCreateInfo msi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   VkPipelineColorBlendAttachmentState cba = {.colorWriteMask = 0xf};
   VkPipelineColorBlendStateCreateInfo cbs = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1, .pAttachments = &cba};
   VkDynamicState dy[2] = {VK_DYNAMIC_STATE_VIEWPORT,
                           VK_DYNAMIC_STATE_SCISSOR};
   VkPipelineDynamicStateCreateInfo dyi = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 2, .pDynamicStates = dy};
   VkPipelineRenderingCreateInfo pri = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1, .pColorAttachmentFormats = &FMT};
   VkGraphicsPipelineCreateInfo gpi = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &pri,
      .stageCount = 2, .pStages = st, .pVertexInputState = &vin,
      .pInputAssemblyState = &ia, .pViewportState = &vpi,
      .pRasterizationState = &rsi, .pMultisampleState = &msi,
      .pColorBlendState = &cbs, .pDynamicState = &dyi, .layout = pl};
   VkPipeline pipe;
   CHECK(CreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, NULL, &pipe),
         "pipeline");

   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT};
   VkCommandPool cp;
   CHECK(CreateCommandPool(dev, &cpi, NULL, &cp), "pool");
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   CHECK(AllocateCommandBuffers(dev, &cai, &cmd), "cmdbuf");

   VkImageSubresource sr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
   VkSubresourceLayout sl;
   GetImageSubresourceLayout(dev, img, &sr, &sl);
   uint8_t *m;
   CHECK(MapMemory(dev, imem, 0, VK_WHOLE_SIZE, 0, (void **)&m), "map");
   uint8_t *base = m + sl.offset;

   uint32_t A, C;
   lcg_jump(iters, &A, &C);
   const uint32_t cpx = D / grid;
   printf("LONGJOB mode=%s iters=%u reps=%u grid=%u draws=%u\n",
          vert ? "vert" : "frag", iters, reps, grid, vert ? grid * grid : 1);

   uint32_t bad_reps = 0;
   for (uint32_t r = 0; r < reps; r++) {
      memset(base, 0x40, (size_t)sl.rowPitch * D);
      ResetCommandBuffer(cmd, 0);
      VkCommandBufferBeginInfo bi = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
      CHECK(BeginCommandBuffer(cmd, &bi), "begin");
      VkImageMemoryBarrier b = {
         .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
         .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
         .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
         .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
         .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
         .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
      CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         NULL, 0, NULL, 1, &b);
      VkRenderingAttachmentInfo at = {
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = view,
         .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
         .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
         .clearValue = {.color = {.float32 = {0.25f, 0.25f, 0.25f, 0.25f}}}};
      VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                            .renderArea = {{0, 0}, {D, D}}, .layerCount = 1,
                            .colorAttachmentCount = 1,
                            .pColorAttachments = &at};
      CmdBeginRendering(cmd, &ri);
      VkViewport vp = {0, 0, D, D, 0, 1};
      VkRect2D sc = {{0, 0}, {D, D}};
      CmdSetViewport(cmd, 0, 1, &vp);
      CmdSetScissor(cmd, 0, 1, &sc);
      CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
      if (vert) {
         for (uint32_t c = 0; c < grid * grid; c++) {
            uint32_t p[4] = {iters, 1, c, grid};
            CmdPushConstants(cmd, pl,
                             VK_SHADER_STAGE_VERTEX_BIT |
                                VK_SHADER_STAGE_FRAGMENT_BIT,
                             0, 16, p);
            CmdDraw(cmd, 6, 1, 0, 0);
         }
      } else {
         uint32_t p[4] = {iters, 0, 0, 0};
         CmdPushConstants(cmd, pl,
                          VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                          0, 16, p);
         CmdDraw(cmd, 6, 1, 0, 0);
      }
      CmdEndRendering(cmd);
      VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .srcAccessMask =
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
      CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0,
                         NULL);
      CHECK(EndCommandBuffer(cmd), "end");

      VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                         .commandBufferCount = 1, .pCommandBuffers = &cmd};
      double t0 = now_ms();
      CHECK(QueueSubmit(q, 1, &si, VK_NULL_HANDLE), "submit");
      VkResult wr = QueueWaitIdle(q);
      double ms = now_ms() - t0;

      uint32_t bad = 0, bad_tiles = 0, fx = 0, fy = 0;
      uint8_t tile_bad[D / 16][D / 16];
      memset(tile_bad, 0, sizeof(tile_bad));
      for (uint32_t y = 0; y < D; y++) {
         for (uint32_t x = 0; x < D; x++) {
            uint32_t h0 = vert ? ((y / cpx) * grid + x / cpx) * 2654435761u +
                                    12345u
                               : (x * 73856093u) ^ (y * 19349663u);
            uint32_t h = A * h0 + C;
            const uint8_t *p = base + y * sl.rowPitch + x * 4;
            if (p[0] != (h & 255) || p[1] != ((h >> 8) & 255) ||
                p[2] != ((h >> 16) & 255) || p[3] != (h >> 24)) {
               if (!bad) {
                  fx = x;
                  fy = y;
               }
               bad++;
               if (!tile_bad[y / 16][x / 16]++)
                  bad_tiles++;
            }
         }
      }
      if (bad)
         bad_reps++;
      printf("LONGJOB rep=%u gpu_ms=%.1f wait=%d bad_pixels=%u bad_16x16_tiles=%u"
             " first_bad=(%u,%u)\n",
             r, ms, wr, bad, bad_tiles, bad ? fx : 0, bad ? fy : 0);
   }
   printf("LONGJOB verdict=%s\n", bad_reps ? "FAIL" : "PASS");
   return bad_reps ? 1 : 0;
}
