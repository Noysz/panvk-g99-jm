/* fault_frag_test.c -- the fragment-job side of the fault diagnostics.
 *
 * Shaders: tests/shaders/longjob.vert (mode 0, fullscreen quad) and
 *   tests/shaders/fault_tex.frag (fault_tex_frag_spv.h generated with
 *   glslangValidator -V --target-env vulkan1.1 --vn fault_tex_frag_spv \
 *     tests/shaders/fault_tex.frag -o tests/graphics/fault_tex_frag_spv.h)
 * Build: clang -O2 -I<mesa>/include fault_frag_test.c -ldl
 * Run:   PANVK_ICD_SO=<libvulkan_panfrost.so> FAULTFRAG_MODE=uaf ./a.out
 *
 * One 512x512 render pass with one fullscreen draw; every pixel does
 * FAULTFRAG_ITERS texelFetch()es of a 256x256 RGBA8 texture filled with
 * 0x40 (combined image sampler, linear host-visible image) and writes the
 * average, so every output byte must be 0x40.
 *   FAULTFRAG_MODE=ok   wait for the GPU, then free the texture memory;
 *   FAULTFRAG_MODE=uaf  free the texture memory right after vkQueueSubmit
 *                       while the fragment job still reads it (invalid
 *                       usage on purpose).
 * Expected on kbase: ok -> ALL_CORRECT; uaf -> MMU fault, kbase event 0x4 on
 * the fragment chain, WRONG; uaf + PANVK_DBG_FREE_DELAY_MS=2000 ->
 * ALL_CORRECT; uaf + PANVK_FAULT_REPORT=1 -> the audit lists the texture
 * plane as the only address that is in no BO.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vulkan/vulkan_core.h"
#include "longjob_vert_spv.h"
#include "fault_tex_frag_spv.h"

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
#define T 256

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
   const char *mode = getenv("FAULTFRAG_MODE");
   const int uaf = mode && !strcmp(mode, "uaf");
   const char *e = getenv("FAULTFRAG_ITERS");
   const uint32_t iters = e ? strtoul(e, NULL, 10) : 3000;
   const VkMemoryPropertyFlags HV = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

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
   DP(QueueWaitIdle); DP(CreateImage); DP(DestroyImage);
   DP(GetImageMemoryRequirements); DP(GetImageSubresourceLayout);
   DP(AllocateMemory); DP(FreeMemory); DP(BindImageMemory);
   DP(CreateImageView); DP(DestroyImageView); DP(MapMemory);
   DP(CreateShaderModule); DP(CreatePipelineLayout);
   DP(CreateGraphicsPipelines); DP(CmdBeginRendering); DP(CmdEndRendering);
   DP(CmdBindPipeline); DP(CmdSetViewport); DP(CmdSetScissor); DP(CmdDraw);
   DP(CmdPipelineBarrier); DP(CmdPushConstants); DP(CreateSampler);
   DP(CreateDescriptorSetLayout); DP(CreateDescriptorPool);
   DP(AllocateDescriptorSets); DP(UpdateDescriptorSets);
   DP(CmdBindDescriptorSets);
   VkQueue q;
   GetDeviceQueue(dev, 0, 0, &q);

   /* Render target. */
   const VkFormat FMT = VK_FORMAT_R8G8B8A8_UNORM;
   VkImageCreateInfo ic = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D, .format = FMT, .extent = {D, D, 1},
      .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_LINEAR,
      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
   VkImage rt;
   CHECK(CreateImage(dev, &ic, NULL, &rt), "rt");
   VkMemoryRequirements mr;
   GetImageMemoryRequirements(dev, rt, &mr);
   VkMemoryAllocateInfo ma = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size,
      .memoryTypeIndex = mtype(&mp, mr.memoryTypeBits, HV)};
   VkDeviceMemory rtmem;
   CHECK(AllocateMemory(dev, &ma, NULL, &rtmem), "rt mem");
   BindImageMemory(dev, rt, rtmem, 0);
   VkImageViewCreateInfo vi = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = rt,
      .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = FMT,
      .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
   VkImageView rtview;
   CHECK(CreateImageView(dev, &vi, NULL, &rtview), "rt view");

   /* Texture: linear, host-visible, filled with 0x40 by the CPU. */
   ic.extent = (VkExtent3D){T, T, 1};
   ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
   ic.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
   VkImage tex;
   CHECK(CreateImage(dev, &ic, NULL, &tex), "tex");
   GetImageMemoryRequirements(dev, tex, &mr);
   ma.allocationSize = mr.size;
   ma.memoryTypeIndex = mtype(&mp, mr.memoryTypeBits, HV);
   VkDeviceMemory texmem;
   CHECK(AllocateMemory(dev, &ma, NULL, &texmem), "tex mem");
   BindImageMemory(dev, tex, texmem, 0);
   uint8_t *tp;
   CHECK(MapMemory(dev, texmem, 0, VK_WHOLE_SIZE, 0, (void **)&tp), "map tex");
   memset(tp, 0x40, mr.size);
   vi.image = tex;
   VkImageView texview;
   CHECK(CreateImageView(dev, &vi, NULL, &texview), "tex view");
   VkSamplerCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
   VkSampler smp;
   CHECK(CreateSampler(dev, &sci, NULL, &smp), "sampler");

   VkDescriptorSetLayoutBinding bnd = {
      0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
      VK_SHADER_STAGE_FRAGMENT_BIT};
   VkDescriptorSetLayoutCreateInfo dl = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1, .pBindings = &bnd};
   VkDescriptorSetLayout dsl;
   CHECK(CreateDescriptorSetLayout(dev, &dl, NULL, &dsl), "dsl");
   VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
      .poolSizeCount = 1, .pPoolSizes = &ps};
   VkDescriptorPool dp;
   CHECK(CreateDescriptorPool(dev, &dpi, NULL, &dp), "dpool");
   VkDescriptorSetAllocateInfo dsa = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl};
   VkDescriptorSet ds;
   CHECK(AllocateDescriptorSets(dev, &dsa, &ds), "dset");
   VkDescriptorImageInfo dii = {smp, texview,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
   VkWriteDescriptorSet w = {
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .pImageInfo = &dii};
   UpdateDescriptorSets(dev, 1, &w, 0, NULL);

   VkShaderModuleCreateInfo smi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(longjob_vert_spv), .pCode = longjob_vert_spv};
   VkShaderModule vs, fs;
   CHECK(CreateShaderModule(dev, &smi, NULL, &vs), "vs");
   smi.codeSize = sizeof(fault_tex_frag_spv);
   smi.pCode = fault_tex_frag_spv;
   CHECK(CreateShaderModule(dev, &smi, NULL, &fs), "fs");
   VkPushConstantRange pcr = {
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &dsl,
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
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool cp;
   CHECK(CreateCommandPool(dev, &cpi, NULL, &cp), "pool");
   VkCommandBufferAllocateInfo cai = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   CHECK(AllocateCommandBuffers(dev, &cai, &cmd), "cmdbuf");
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   CHECK(BeginCommandBuffer(cmd, &bi), "begin");
   VkImageMemoryBarrier b[2] = {
      {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
       .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = rt,
       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
       .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
      {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
       .oldLayout = VK_IMAGE_LAYOUT_PREINITIALIZED,
       .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
       .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
       .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = tex,
       .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
       .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
       .dstAccessMask = VK_ACCESS_SHADER_READ_BIT}};
   CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      0, 0, NULL, 0, NULL, 2, b);
   VkRenderingAttachmentInfo at = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = rtview,
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
   VkRenderingInfo ri = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                         .renderArea = {{0, 0}, {D, D}}, .layerCount = 1,
                         .colorAttachmentCount = 1, .pColorAttachments = &at};
   CmdBeginRendering(cmd, &ri);
   VkViewport vp = {0, 0, D, D, 0, 1};
   VkRect2D sc = {{0, 0}, {D, D}};
   CmdSetViewport(cmd, 0, 1, &vp);
   CmdSetScissor(cmd, 0, 1, &sc);
   CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
   CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &ds,
                         0, NULL);
   uint32_t p[4] = {iters, 0, 0, 0};
   CmdPushConstants(cmd, pl,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, 16, p);
   CmdDraw(cmd, 6, 1, 0, 0);
   CmdEndRendering(cmd);
   VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
   CHECK(EndCommandBuffer(cmd), "end");

   VkImageSubresource sr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
   VkSubresourceLayout sl;
   GetImageSubresourceLayout(dev, rt, &sr, &sl);
   uint8_t *m;
   CHECK(MapMemory(dev, rtmem, 0, VK_WHOLE_SIZE, 0, (void **)&m), "map rt");
   memset(m + sl.offset, 0xee, (size_t)sl.rowPitch * D);

   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cmd};
   double t0 = now_ms();
   CHECK(QueueSubmit(q, 1, &si, VK_NULL_HANDLE), "submit");
   if (uaf) {
      /* Invalid usage on purpose: the fragment job still reads tex. */
      DestroyImageView(dev, texview, NULL);
      DestroyImage(dev, tex, NULL);
      FreeMemory(dev, texmem, NULL);
   }
   VkResult wr = QueueWaitIdle(q);
   double ms = now_ms() - t0;
   if (!uaf) {
      DestroyImageView(dev, texview, NULL);
      DestroyImage(dev, tex, NULL);
      FreeMemory(dev, texmem, NULL);
   }

   uint32_t bad = 0;
   const uint8_t *base = m + sl.offset;
   for (uint32_t y = 0; y < D; y++)
      for (uint32_t x = 0; x < D; x++)
         for (uint32_t c = 0; c < 4; c++) {
            int v = base[y * sl.rowPitch + x * 4 + c];
            if (v < 0x3f || v > 0x41) {
               bad++;
               c = 4;
            }
         }
   printf("FAULTFRAG mode=%s iters=%u gpu_ms=%.1f wait=%d bad_pixels=%u "
          "pixel0=%02x%02x%02x%02x\n", uaf ? "uaf" : "ok", iters, ms, wr, bad,
          base[0], base[1], base[2], base[3]);
   printf("FAULTFRAG verdict=%s\n", bad ? "WRONG" : "ALL_CORRECT");
   return bad ? 1 : 0;
}
