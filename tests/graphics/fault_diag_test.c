/* fault_diag_test.c -- exercises the fault diagnostics knobs on purpose.
 *
 * Shader: tests/shaders/fault_uaf.comp (fault_uaf_spv.h is generated with
 *   glslangValidator -V --target-env vulkan1.1 --vn fault_uaf_spv \
 *     tests/shaders/fault_uaf.comp -o tests/graphics/fault_uaf_spv.h)
 * Build: clang -O2 -I<mesa>/include fault_diag_test.c -ldl
 * Run:   PANVK_ICD_SO=<libvulkan_panfrost.so> FAULTDIAG_MODE=uaf ./a.out
 *
 * A compute dispatch reads a 4 MiB source buffer in a long loop and writes
 * one sum per invocation. FAULTDIAG_MODE:
 *   ok  (default) wait for the fence, then free the source (valid usage);
 *   uaf free the source memory right after vkQueueSubmit, while the GPU is
 *       still reading it. This is invalid usage on purpose: it is what a
 *       use-after-free bug in a driver or a game looks like to the GPU.
 *
 * Expected on kbase:
 *   ok                                   -> ALL_CORRECT (exit 0)
 *   uaf                                  -> GPU page fault, kbase event 0x4,
 *                                           WRONG (exit 1)
 *   uaf + PANVK_DBG_FREE_DELAY_MS=2000   -> the freed BO stays mapped,
 *                                           ALL_CORRECT (exit 0)
 *   uaf + PANVK_FAULT_REPORT=1           -> "panvk-fault:" lines name the
 *                                           failed chain and its jobs.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vulkan/vulkan_core.h"
#include "fault_uaf_spv.h"

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);

static PFN_vkGetDeviceProcAddr gdpa;
static VkDevice dev;

#define D(x) PFN_vk##x x = (PFN_vk##x)gdpa(dev, "vk" #x)

#define WORDS (1u << 20) /* 4 MiB source */
#define GROUPS 64u
#define INVOCATIONS (GROUPS * 64u)

int
main(void)
{
   const char *mode = getenv("FAULTDIAG_MODE");
   const int uaf = mode && !strcmp(mode, "uaf");
   const char *it = getenv("FAULTDIAG_ITERS");
   const uint32_t iters = it ? (uint32_t)strtoul(it, NULL, 10) : 40000;

   const char *so = getenv("PANVK_ICD_SO");
   void *l = so ? dlopen(so, RTLD_NOW) : NULL;
   if (!l) {
      printf("FAILED: dlopen %s\n", so ? dlerror() : "(PANVK_ICD_SO unset)");
      return 2;
   }
   G gpa = (G)dlsym(l, "vk_icdGetInstanceProcAddr");
   VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &ai};
   VkInstance in;
   if (((PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance"))(&ii, NULL, &in)) {
      printf("FAILED: vkCreateInstance\n");
      return 2;
   }
   uint32_t n = 1;
   VkPhysicalDevice pd = VK_NULL_HANDLE;
   ((PFN_vkEnumeratePhysicalDevices)gpa(in, "vkEnumeratePhysicalDevices"))(
      in, &n, &pd);
   if (!pd) {
      printf("FAILED: no physical device\n");
      return 2;
   }
   VkPhysicalDeviceMemoryProperties mp;
   ((PFN_vkGetPhysicalDeviceMemoryProperties)gpa(
      in, "vkGetPhysicalDeviceMemoryProperties"))(pd, &mp);

   float pr = 1;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 1, .pQueuePriorities = &pr};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .queueCreateInfoCount = 1,
                            .pQueueCreateInfos = &qi};
   if (((PFN_vkCreateDevice)gpa(in, "vkCreateDevice"))(pd, &di, NULL, &dev)) {
      printf("FAILED: vkCreateDevice\n");
      return 2;
   }
   gdpa = (PFN_vkGetDeviceProcAddr)gpa(in, "vkGetDeviceProcAddr");
   D(GetDeviceQueue); D(CreateCommandPool); D(CreateBuffer); D(DestroyBuffer);
   D(GetBufferMemoryRequirements); D(AllocateMemory); D(FreeMemory);
   D(BindBufferMemory); D(MapMemory); D(CreateShaderModule);
   D(CreateDescriptorSetLayout); D(CreatePipelineLayout);
   D(CreateComputePipelines); D(CreateDescriptorPool);
   D(AllocateDescriptorSets); D(UpdateDescriptorSets);
   D(AllocateCommandBuffers); D(BeginCommandBuffer); D(EndCommandBuffer);
   D(CmdBindPipeline); D(CmdBindDescriptorSets); D(CmdPushConstants);
   D(CmdDispatch); D(CmdPipelineBarrier); D(QueueSubmit); D(CreateFence);
   D(WaitForFences); D(DeviceWaitIdle);
   VkQueue q;
   GetDeviceQueue(dev, 0, 0, &q);

   int host = -1;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
      if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
         host = i;
         break;
      }
   }
   if (host < 0) {
      printf("FAILED: no host-coherent memory type\n");
      return 2;
   }

   VkBuffer src, dst;
   VkDeviceMemory msrc, mdst;
   VkBufferCreateInfo bc = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = WORDS * 4,
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
   CreateBuffer(dev, &bc, NULL, &src);
   bc.size = INVOCATIONS * 4;
   CreateBuffer(dev, &bc, NULL, &dst);
   VkMemoryRequirements mr;
   VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                              .memoryTypeIndex = host};
   GetBufferMemoryRequirements(dev, src, &mr);
   ma.allocationSize = mr.size;
   AllocateMemory(dev, &ma, NULL, &msrc);
   BindBufferMemory(dev, src, msrc, 0);
   GetBufferMemoryRequirements(dev, dst, &mr);
   ma.allocationSize = mr.size;
   AllocateMemory(dev, &ma, NULL, &mdst);
   BindBufferMemory(dev, dst, mdst, 0);

   uint32_t *ps, *pdst;
   MapMemory(dev, msrc, 0, VK_WHOLE_SIZE, 0, (void **)&ps);
   MapMemory(dev, mdst, 0, VK_WHOLE_SIZE, 0, (void **)&pdst);
   for (uint32_t i = 0; i < WORDS; i++)
      ps[i] = 1;
   memset(pdst, 0xff, INVOCATIONS * 4);

   VkShaderModuleCreateInfo sm = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(fault_uaf_spv), .pCode = fault_uaf_spv};
   VkShaderModule mod;
   CreateShaderModule(dev, &sm, NULL, &mod);
   VkDescriptorSetLayoutBinding b[2] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
      {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT}};
   VkDescriptorSetLayoutCreateInfo dl = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2, .pBindings = b};
   VkDescriptorSetLayout dsl;
   CreateDescriptorSetLayout(dev, &dl, NULL, &dsl);
   VkPushConstantRange pcr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
   VkPipelineLayoutCreateInfo pl = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &dsl,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
   VkPipelineLayout lay;
   CreatePipelineLayout(dev, &pl, NULL, &lay);
   VkComputePipelineCreateInfo cp = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = mod,
                .pName = "main"},
      .layout = lay};
   VkPipeline pipe;
   if (CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cp, NULL, &pipe)) {
      printf("FAILED: pipeline\n");
      return 2;
   }

   VkDescriptorPoolSize ps2 = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
      .poolSizeCount = 1, .pPoolSizes = &ps2};
   VkDescriptorPool dp;
   CreateDescriptorPool(dev, &dpi, NULL, &dp);
   VkDescriptorSetAllocateInfo dsa = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl};
   VkDescriptorSet ds;
   AllocateDescriptorSets(dev, &dsa, &ds);
   VkDescriptorBufferInfo bi[2] = {{src, 0, VK_WHOLE_SIZE},
                                   {dst, 0, VK_WHOLE_SIZE}};
   VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .dstSet = ds, .dstBinding = 0,
                             .descriptorCount = 2,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .pBufferInfo = bi};
   UpdateDescriptorSets(dev, 1, &w, 0, NULL);

   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool pool;
   CreateCommandPool(dev, &cpi, NULL, &pool);
   VkCommandBufferAllocateInfo cba = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
   VkCommandBuffer cb;
   AllocateCommandBuffers(dev, &cba, &cb);
   VkCommandBufferBeginInfo bgi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   BeginCommandBuffer(cb, &bgi);
   CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, lay, 0, 1, &ds, 0,
                         NULL);
   uint32_t pcv[2] = {WORDS, iters};
   CmdPushConstants(cb, lay, VK_SHADER_STAGE_COMPUTE_BIT, 0, 8, pcv);
   CmdDispatch(cb, GROUPS, 1, 1);
   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
   EndCommandBuffer(cb);

   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CreateFence(dev, &fci, NULL, &fence);
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cb};
   QueueSubmit(q, 1, &si, fence);

   if (uaf) {
      /* Invalid usage on purpose: the GPU is still reading src. */
      DestroyBuffer(dev, src, NULL);
      FreeMemory(dev, msrc, NULL);
   }
   VkResult wr = WaitForFences(dev, 1, &fence, VK_TRUE, 20000000000ull);
   if (!uaf) {
      DestroyBuffer(dev, src, NULL);
      FreeMemory(dev, msrc, NULL);
   }

   unsigned good = 0;
   for (uint32_t i = 0; i < INVOCATIONS; i++)
      good += pdst[i] == iters;
   printf("FAULTDIAG mode=%s iters=%u fence=%d correct=%u/%u dst[0]=%u\n",
          uaf ? "uaf" : "ok", iters, wr, good, INVOCATIONS, pdst[0]);
   printf("FAULTDIAG verdict=%s\n",
          good == INVOCATIONS ? "ALL_CORRECT" : "WRONG");
   DeviceWaitIdle(dev);
   return good == INVOCATIONS ? 0 : 1;
}
