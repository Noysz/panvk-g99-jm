#define main unused_visibility_main
#include "import_visibility.c"
#undef main
#include <dirent.h>
#include <stdbool.h>

static unsigned fd_count(void)
{
   DIR *dir = opendir("/proc/self/fd");
   if (!dir) return UINT32_MAX;
   unsigned count = 0;
   struct dirent *entry;
   while ((entry = readdir(dir))) count += entry->d_name[0] != '.';
   closedir(dir);
   return count;
}

int main(int argc, char **argv)
{
   if (argc != 4) return 1;
   unsigned writes = strtoul(argv[2], NULL, 0);
   bool suspend = strtoul(argv[3], NULL, 0) != 0;
   if (!writes || writes > 32770 || (suspend && writes < 2)) return 1;
   unsigned long mask = strtoul(argv[1], NULL, 0);
   if (mask != 0 && mask != 1 && mask != 5 && mask != 15) return 1;
   unsigned views = mask ? (unsigned)__builtin_popcount((unsigned)mask) : 1;
   unsigned inside = views * writes;
   unsigned query_count = inside + 1 + (views > 2 ? views : 2);
   unsigned initial_fds = fd_count();
   const char *path = getenv("PANVK_ICD_SO");
   if (!path) return 1;
   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!lib) return 1;
   PFN_vkGetInstanceProcAddr gpa = dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!gpa) return 1;
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "timestamp_multiview", .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app};
   VkInstance instance;
   VK_CHECK(CreateInstance(&ici, NULL, &instance));
   LOAD_I(EnumeratePhysicalDevices); LOAD_I(GetPhysicalDeviceFeatures2);
   LOAD_I(GetPhysicalDeviceQueueFamilyProperties); LOAD_I(CreateDevice);
   LOAD_I(GetDeviceProcAddr); LOAD_I(DestroyInstance);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   VK_CHECK(EnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   VkPhysicalDeviceMultiviewFeatures mv = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
   VkPhysicalDeviceDynamicRenderingFeatures dr = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, .pNext = &mv};
   VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &dr};
   GetPhysicalDeviceFeatures2(physical, &features);
   VkQueueFamilyProperties qp;
   count = 1;
   GetPhysicalDeviceQueueFamilyProperties(physical, &count, &qp);
   int result = 77;
   printf("CAPS multiview=%u dynamic_rendering=%u timestamp_bits=%u\n",
      mv.multiview, dr.dynamicRendering, qp.timestampValidBits);
   if (!mv.multiview || !dr.dynamicRendering || !qp.timestampValidBits)
      goto instance_cleanup;
   mv.multiviewGeometryShader = mv.multiviewTessellationShader = VK_FALSE;
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &dr, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
   VkDevice device;
   VK_CHECK(CreateDevice(physical, &dci, NULL, &device));
   LOAD_D(GetDeviceQueue); LOAD_D(CreateQueryPool); LOAD_D(DestroyQueryPool);
   LOAD_D(GetQueryPoolResults); LOAD_D(CreateCommandPool); LOAD_D(DestroyCommandPool);
   LOAD_D(AllocateCommandBuffers); LOAD_D(BeginCommandBuffer); LOAD_D(EndCommandBuffer);
   LOAD_D(CmdResetQueryPool); LOAD_D(CmdWriteTimestamp); LOAD_D(CmdBeginRendering);
   LOAD_D(CmdEndRendering); LOAD_D(CreateFence); LOAD_D(DestroyFence);
   LOAD_D(QueueSubmit); LOAD_D(WaitForFences); LOAD_D(DestroyDevice);
   VkQueue queue;
   GetDeviceQueue(device, 0, 0, &queue);
   VkQueryPoolCreateInfo qpci = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = query_count};
   VkQueryPool queries;
   VK_CHECK(CreateQueryPool(device, &qpci, NULL, &queries));
   VkCommandPoolCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = 0};
   VkCommandPool pool;
   VK_CHECK(CreateCommandPool(device, &cpci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   VK_CHECK(AllocateCommandBuffers(device, &cai, &cmd));
   VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VK_CHECK(BeginCommandBuffer(cmd, &begin));
   CmdResetQueryPool(cmd, queries, 0, query_count);
   VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {1, 1}}, .layerCount = 1, .viewMask = (uint32_t)mask};
   rendering.flags = suspend ? VK_RENDERING_SUSPENDING_BIT : 0;
   CmdBeginRendering(cmd, &rendering);
   for (unsigned w = 0; w < writes; w++) {
      if (suspend && w == writes / 2) {
         CmdEndRendering(cmd);
         rendering.flags = VK_RENDERING_RESUMING_BIT;
         CmdBeginRendering(cmd, &rendering);
      }
      CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queries, w * views);
   }
   CmdEndRendering(cmd);
   CmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, queries, inside);
   VK_CHECK(EndCommandBuffer(cmd));
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VK_CHECK(CreateFence(device, &fci, NULL, &fence));
   VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &cmd};
   VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
   VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
   unsigned bad = 0;
   uint64_t first = 0, previous_first = 0;
   for (unsigned i = 0; i < query_count; i++) {
      uint64_t values[2] = {UINT64_C(0x1122334455667788), 99};
      VkResult got = GetQueryPoolResults(device, queries, i, 1, sizeof(values), values,
         sizeof(values), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
      bool expected_available = i <= inside;
      bad += got != (expected_available ? VK_SUCCESS : VK_NOT_READY);
      bad += values[1] != (uint64_t)expected_available;
      if (!i) {
         first = values[0];
         bad += !first;
      }
      if (i < inside && i % views == 0) {
         bad += !values[0] || values[0] < previous_first;
         previous_first = values[0];
      }
      if (i == inside) bad += !values[0] || values[0] < first || values[0] < previous_first;
      if (i > inside) bad += values[0] != UINT64_C(0x1122334455667788);
      if (query_count < 32 || i < 4 || i + 4 >= query_count)
         printf("QUERY index=%u result=%d value=%" PRIu64 " available=%" PRIu64 " expected=%u\n",
         i, got, values[0], values[1], expected_available);
   }
   printf("SLOTS checked=%u inside=%u writes=%u suspend=%u bad=%u\n", query_count, inside, writes, suspend, bad);
   DestroyFence(device, fence, NULL);
   DestroyCommandPool(device, pool, NULL);
   DestroyQueryPool(device, queries, NULL);
   DestroyDevice(device, NULL);
   printf("MULTIVIEW mask=%lu views=%u bad=%u verdict=%s\n", mask, views, bad, bad ? "FAIL" : "PASS");
   result = bad ? 2 : 0;
instance_cleanup:
   DestroyInstance(instance, NULL);
   dlclose(lib);
   unsigned final_fds = fd_count();
   printf("LIFECYCLE initial_fds=%u final_fds=%u\n", initial_fds, final_fds);
   if (initial_fds == UINT32_MAX || initial_fds != final_fds) return 3;
   return result;
}
