/* pool_cache_test.c -- command pool BO cache cap (patch 0073).
 *
 * DXVK keeps one command pool per command list and resets the pools with
 * vkResetCommandPool(0). panvk returns the BOs of a reset command buffer to
 * the pool's BO cache, and before 0073 they stayed there until the pool was
 * destroyed, so the cache grew to the sum of the peaks of every pool.
 *
 * The test records POOLTEST_DISPATCHES (default 40000) compute dispatches in
 * one command buffer per pool, for 8 pools, submits, waits and resets every
 * pool, two rounds. Each dispatch adds 1 to a counter. Then it sleeps 2.1 s
 * and submits an empty command buffer so PANVK_MEM_PROF prints the cache
 * size after all resets.
 *
 * Build: clang -O2 -I<mesa>/include pool_cache_test.c -ldl
 * Run:   PANVK_ICD_SO=<libvulkan_panfrost.so> ./a.out
 *        (sets PANVK_MEM_PROF=./pool_cache_test.prof unless it is set to a
 *        path already)
 *
 * PASS (exit 0): both counters are exact and the command pool cache after
 * the resets is at most POOLTEST_MAX_MB (default 56 = the 48 MB default cap
 * plus slack for one cached BO per BO pool). Control:
 * PANVK_POOL_CACHE_MAX_MB=0 (no cap) is expected to FAIL on the cache size.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vulkan/vulkan_core.h"
#include "pool_count_spv.h"

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);

#define NPOOLS 8
#define ROUNDS 2

static PFN_vkGetDeviceProcAddr gdpa;
static VkDevice dev;

#define D(x) PFN_vk##x x = (PFN_vk##x)gdpa(dev, "vk" #x)

static double
rss_mb(void)
{
   char buf[4096];
   FILE *f = fopen("/proc/self/status", "r");
   if (!f)
      return 0;
   size_t n = fread(buf, 1, sizeof(buf) - 1, f);
   fclose(f);
   buf[n] = 0;
   const char *s = strstr(buf, "VmRSS:");
   return s ? strtod(s + 6, NULL) / 1024.0 : 0;
}

/* Last "cmd pool cache X MB" value in the profile file, -1 if none. */
static double
last_pool_cache(const char *path, double *max_out)
{
   FILE *f = fopen(path, "r");
   char line[1024];
   double last = -1, max = -1;
   if (!f)
      return -1;
   while (fgets(line, sizeof(line), f)) {
      const char *s = strstr(line, "cmd pool cache ");
      if (s) {
         last = strtod(s + 15, NULL);
         if (last > max)
            max = last;
      }
   }
   fclose(f);
   *max_out = max;
   return last;
}

int
main(void)
{
   const char *prof = getenv("PANVK_MEM_PROF");
   if (!prof || !strchr(prof, '/')) {
      prof = "./pool_cache_test.prof";
      setenv("PANVK_MEM_PROF", prof, 1);
   }
   unlink(prof);
   const int ndisp = getenv("POOLTEST_DISPATCHES")
                        ? atoi(getenv("POOLTEST_DISPATCHES")) : 40000;
   const double max_mb = getenv("POOLTEST_MAX_MB")
                            ? atof(getenv("POOLTEST_MAX_MB")) : 56.0;

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
   D(GetDeviceQueue); D(CreateCommandPool); D(ResetCommandPool);
   D(AllocateCommandBuffers); D(BeginCommandBuffer); D(EndCommandBuffer);
   D(CreateBuffer); D(GetBufferMemoryRequirements); D(AllocateMemory);
   D(BindBufferMemory); D(MapMemory); D(CreateShaderModule);
   D(CreateDescriptorSetLayout); D(CreatePipelineLayout);
   D(CreateComputePipelines); D(CreateDescriptorPool);
   D(AllocateDescriptorSets); D(UpdateDescriptorSets); D(CmdBindPipeline);
   D(CmdBindDescriptorSets); D(CmdPushConstants); D(CmdDispatch);
   D(CmdPipelineBarrier); D(QueueSubmit); D(CreateFence); D(WaitForFences);
   D(ResetFences); D(DeviceWaitIdle); D(DestroyDevice);
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
                            .size = 256,
                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT};
   CreateBuffer(dev, &bc, NULL, &buf);
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(dev, buf, &mr);
   VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                              .allocationSize = mr.size,
                              .memoryTypeIndex = host};
   AllocateMemory(dev, &ma, NULL, &mem);
   BindBufferMemory(dev, buf, mem, 0);
   volatile uint32_t *counter;
   MapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&counter);
   *counter = 0;

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
      .codeSize = sizeof(pool_count_spv), .pCode = pool_count_spv};
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

   VkCommandPool pools[NPOOLS + 1];
   VkCommandBuffer cbs[NPOOLS + 1];
   for (int p = 0; p <= NPOOLS; p++) {
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

   const double rss0 = rss_mb();
   int bad_count = 0;
   for (int r = 0; r < ROUNDS; r++) {
      for (int p = 0; p < NPOOLS; p++) {
         VkCommandBufferBeginInfo bi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
         BeginCommandBuffer(cbs[p], &bi);
         CmdBindPipeline(cbs[p], VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
         CmdBindDescriptorSets(cbs[p], VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0,
                               1, &ds, 0, NULL);
         const uint32_t one = 1;
         for (int i = 0; i < ndisp; i++) {
            CmdPushConstants(cbs[p], pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4,
                             &one);
            CmdDispatch(cbs[p], 1, 1, 1);
         }
         VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                               .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                               .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
         CmdPipelineBarrier(cbs[p], VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0,
                            NULL);
         EndCommandBuffer(cbs[p]);
         VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                            .commandBufferCount = 1,
                            .pCommandBuffers = &cbs[p]};
         ResetFences(dev, 1, &fence);
         VkResult res = QueueSubmit(q, 1, &si, fence);
         if (!res)
            res = WaitForFences(dev, 1, &fence, VK_TRUE, 30000000000ull);
         if (res) {
            printf("FAILED: submit/wait round %d pool %d: %d\n", r, p, res);
            return 2;
         }
         ResetCommandPool(dev, pools[p], 0);
      }
      const uint32_t want = (uint32_t)((r + 1) * NPOOLS * ndisp);
      printf("POOLCACHE round %d counter=%u want=%u rss=%.1f MB\n", r,
             *counter, want, rss_mb());
      bad_count += *counter != want;
   }

   /* Let the 2 s profile interval pass, then tick it with an empty submit
    * from the spare pool. */
   usleep(2100000);
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   BeginCommandBuffer(cbs[NPOOLS], &bi);
   EndCommandBuffer(cbs[NPOOLS]);
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cbs[NPOOLS]};
   ResetFences(dev, 1, &fence);
   QueueSubmit(q, 1, &si, fence);
   WaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull);
   DeviceWaitIdle(dev);
   const double rss1 = rss_mb();

   double max_cache;
   double cache = last_pool_cache(prof, &max_cache);
   int pass = !bad_count && cache >= 0 && cache <= max_mb;
   printf("POOLCACHE pools=%d dispatches=%d cache_after_resets=%.1f MB "
          "max_seen=%.1f MB limit=%.1f MB rss_before=%.1f rss_after=%.1f "
          "bad_counters=%d verdict=%s\n", NPOOLS, ndisp, cache, max_cache,
          max_mb, rss0, rss1, bad_count, pass ? "PASS" : "FAIL");
   DestroyDevice(dev, NULL);
   return pass ? 0 : 1;
}
