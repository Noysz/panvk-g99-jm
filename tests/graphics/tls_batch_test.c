/* tls_batch_test.c -- thread-local storage (scratch) memory of a command
 * buffer with many batches (patch 0074).
 *
 * On JM every vkCmdDispatch and every render pass is its own batch, and
 * before 0074 every batch that ran a shader with scratch got its own TLS
 * buffer: next_pow2(per-thread size) x threads per core x cores, about
 * 1-3 MB each on a G57 MC2. A DX9 game with a few hundred passes per frame
 * then held over 2 GB in one command buffer (NFS Most Wanted, PANVK_MEM_PROF
 * user log, 2026-10-08).
 *
 * The test records POOLTEST_DISPATCHES (default 200) dispatches of
 * tests/shaders/tls_spill.comp (private array with dynamic indexing, so the
 * shader needs scratch) in one command buffer, submits and waits, and then,
 * with the command buffer not reset yet, lets PANVK_MEM_PROF print the
 * kbase committed size. The baseline is a profile line printed before the
 * recording.
 *
 * Build: clang -O2 -I<mesa>/include tls_batch_test.c -ldl
 * Run:   PANVK_ICD_SO=<libvulkan_panfrost.so> ./a.out
 *
 * PASS (exit 0): the output buffer is exact and the committed size grew by
 * at most TLSTEST_MAX_MB (default 16) while the command buffer was alive.
 * Control: PANVK_TLS_PER_BATCH=1 (one TLS buffer per batch, the old code)
 * is expected to FAIL on the size.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vulkan/vulkan_core.h"
#include "tls_spill_spv.h"

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);

#define NINV 64

static PFN_vkGetDeviceProcAddr gdpa;
static VkDevice dev;

#define D(x) PFN_vk##x x = (PFN_vk##x)gdpa(dev, "vk" #x)

/* "committed at alloc" and BO count of the first and last profile lines. */
static int
read_prof(const char *path, double *c0, double *c1, int *b0, int *b1)
{
   FILE *f = fopen(path, "r");
   char line[1024];
   int n = 0;
   if (!f)
      return 0;
   while (fgets(line, sizeof(line), f)) {
      const char *s = strstr(line, "kbase BOs ");
      const char *c = strstr(line, "MB VA, ");
      if (!s || !c)
         continue;
      int bos = atoi(s + 10);
      double com = strtod(c + 7, NULL);
      if (!n++) {
         *c0 = com;
         *b0 = bos;
      }
      *c1 = com;
      *b1 = bos;
   }
   fclose(f);
   return n;
}

int
main(void)
{
   const char *prof = getenv("PANVK_MEM_PROF");
   if (!prof || !strchr(prof, '/')) {
      prof = "./tls_batch_test.prof";
      setenv("PANVK_MEM_PROF", prof, 1);
   }
   unlink(prof);
   const int ndisp = getenv("POOLTEST_DISPATCHES")
                        ? atoi(getenv("POOLTEST_DISPATCHES")) : 200;
   const double max_mb = getenv("TLSTEST_MAX_MB")
                            ? atof(getenv("TLSTEST_MAX_MB")) : 16.0;

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
   D(GetDeviceQueue); D(CreateCommandPool); D(AllocateCommandBuffers);
   D(BeginCommandBuffer); D(EndCommandBuffer); D(CreateBuffer);
   D(GetBufferMemoryRequirements); D(AllocateMemory); D(BindBufferMemory);
   D(MapMemory); D(CreateShaderModule); D(CreateDescriptorSetLayout);
   D(CreatePipelineLayout); D(CreateComputePipelines);
   D(CreateDescriptorPool); D(AllocateDescriptorSets);
   D(UpdateDescriptorSets); D(CmdBindPipeline); D(CmdBindDescriptorSets);
   D(CmdPushConstants); D(CmdDispatch); D(CmdPipelineBarrier);
   D(QueueSubmit); D(CreateFence); D(WaitForFences); D(ResetFences);
   D(DeviceWaitIdle); D(DestroyDevice);
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
   VkBuffer buf;
   VkDeviceMemory mem;
   VkBufferCreateInfo bc = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = NINV * 4,
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
   CreateBuffer(dev, &bc, NULL, &buf);
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(dev, buf, &mr);
   VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                              .allocationSize = mr.size,
                              .memoryTypeIndex = host};
   AllocateMemory(dev, &ma, NULL, &mem);
   BindBufferMemory(dev, buf, mem, 0);
   uint32_t *v;
   MapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&v);
   memset(v, 0, NINV * 4);

   VkDescriptorSetLayoutBinding lb = {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                                      VK_SHADER_STAGE_COMPUTE_BIT};
   VkDescriptorSetLayoutCreateInfo li = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1, .pBindings = &lb};
   VkDescriptorSetLayout dsl;
   CreateDescriptorSetLayout(dev, &li, NULL, &dsl);
   VkPushConstantRange pcr = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &dsl,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
   VkPipelineLayout pl;
   CreatePipelineLayout(dev, &pli, NULL, &pl);
   VkShaderModuleCreateInfo smi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(tls_spill_spv), .pCode = tls_spill_spv};
   VkShaderModule sm;
   CreateShaderModule(dev, &smi, NULL, &sm);
   VkComputePipelineCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = sm,
                .pName = "main"},
      .layout = pl};
   VkPipeline pipe;
   if (CreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpi, NULL, &pipe)) {
      printf("FAILED: compute pipeline\n");
      return 2;
   }
   VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
      .poolSizeCount = 1, .pPoolSizes = &ps};
   VkDescriptorPool dp;
   CreateDescriptorPool(dev, &dpi, NULL, &dp);
   VkDescriptorSetAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl};
   VkDescriptorSet ds;
   AllocateDescriptorSets(dev, &dai, &ds);
   VkDescriptorBufferInfo dbi = {buf, 0, VK_WHOLE_SIZE};
   VkWriteDescriptorSet w = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                             .dstSet = ds, .dstBinding = 0,
                             .descriptorCount = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                             .pBufferInfo = &dbi};
   UpdateDescriptorSets(dev, 1, &w, 0, NULL);

   VkCommandPool pools[2];
   VkCommandBuffer cbs[2];
   for (int p = 0; p < 2; p++) {
      VkCommandPoolCreateInfo cpci = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
      CreateCommandPool(dev, &cpci, NULL, &pools[p]);
      VkCommandBufferAllocateInfo ca = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
         .commandPool = pools[p], .commandBufferCount = 1};
      AllocateCommandBuffers(dev, &ca, &cbs[p]);
   }
   VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CreateFence(dev, &fi, NULL, &fence);
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT};
   BeginCommandBuffer(cbs[1], &bi);
   EndCommandBuffer(cbs[1]);
   VkSubmitInfo tick = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                        .commandBufferCount = 1, .pCommandBuffers = &cbs[1]};

   /* Baseline profile line. */
   QueueSubmit(q, 1, &tick, fence);
   WaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull);

   bi.flags = 0;
   BeginCommandBuffer(cbs[0], &bi);
   CmdBindPipeline(cbs[0], VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   CmdBindDescriptorSets(cbs[0], VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds,
                         0, NULL);
   VkMemoryBarrier cb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
                                          VK_ACCESS_SHADER_WRITE_BIT};
   for (int i = 0; i < ndisp; i++) {
      uint32_t k = (uint32_t)i + 1;
      CmdPushConstants(cbs[0], pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &k);
      CmdDispatch(cbs[0], 1, 1, 1);
      CmdPipelineBarrier(cbs[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &cb, 0,
                         NULL, 0, NULL);
   }
   VkMemoryBarrier hb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   CmdPipelineBarrier(cbs[0], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, NULL, 0, NULL);
   EndCommandBuffer(cbs[0]);
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cbs[0]};
   ResetFences(dev, 1, &fence);
   VkResult r = QueueSubmit(q, 1, &si, fence);
   if (!r)
      r = WaitForFences(dev, 1, &fence, VK_TRUE, 30000000000ull);
   if (r) {
      printf("FAILED: submit/wait %d\n", r);
      return 2;
   }

   /* The command buffer is not reset: its BOs are still alive. */
   usleep(2100000);
   ResetFences(dev, 1, &fence);
   QueueSubmit(q, 1, &tick, fence);
   WaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull);
   DeviceWaitIdle(dev);

   unsigned bad = 0;
   for (uint32_t x = 0; x < NINV; x++) {
      uint32_t want = 0;
      for (int i = 0; i < ndisp; i++) {
         uint32_t k = (uint32_t)i + 1;
         uint32_t idx = (x * 7u + k) % 96u;
         want += idx * k + x;
      }
      bad += v[x] != want;
   }

   double c0 = 0, c1 = 0;
   int b0 = 0, b1 = 0;
   int lines = read_prof(prof, &c0, &c1, &b0, &b1);
   double grew = c1 - c0;
   int pass = !bad && lines >= 2 && grew <= max_mb;
   printf("TLSBATCH dispatches=%d prof_lines=%d committed %.1f -> %.1f MB "
          "(+%.1f, limit %.1f) BOs %d -> %d bad_values=%u verdict=%s\n",
          ndisp, lines, c0, c1, grew, max_mb, b0, b1, bad,
          pass ? "PASS" : "FAIL");
   DestroyDevice(dev, NULL);
   return pass ? 0 : 1;
}
