#define main unused_visibility_main
#include "import_visibility.c"
#undef main
#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>

#define DATA_BYTES 65536u
#define TOTAL_BYTES (DATA_BYTES + 128u)
#define SENTINEL 0xa571c38du

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

static uint64_t cpu_time(void)
{
   struct timespec now;
   if (clock_gettime(CLOCK_MONOTONIC_RAW, &now)) abort();
   return (uint64_t)now.tv_sec * 1000000000ull + now.tv_nsec;
}

int main(int argc, char **argv)
{
   if (argc != 2) return 1;
   bool caps_only = !strcmp(argv[1], "caps");
   bool secondary = !strcmp(argv[1], "secondary");
   if (!caps_only && !secondary && strcmp(argv[1], "primary")) return 1;
   unsigned initial_fds = fd_count();
   uint64_t frequency = 0;
#if defined(__aarch64__)
   __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
#endif
   const char *path = getenv("PANVK_ICD_SO");
   if (!path) return 1;
   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!lib) return 1;
   PFN_vkGetInstanceProcAddr gpa = dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!gpa) return 1;
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .pApplicationName = "timestamp_api", .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
   VkInstance instance;
   VK_CHECK(CreateInstance(&ici, NULL, &instance));
   LOAD_I(EnumeratePhysicalDevices); LOAD_I(GetPhysicalDeviceProperties);
   LOAD_I(GetPhysicalDeviceQueueFamilyProperties); LOAD_I(GetPhysicalDeviceMemoryProperties);
   LOAD_I(CreateDevice); LOAD_I(GetDeviceProcAddr); LOAD_I(DestroyInstance);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   VK_CHECK(EnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   VkPhysicalDeviceProperties props;
   GetPhysicalDeviceProperties(physical, &props);
   VkQueueFamilyProperties queue_props;
   count = 1;
   GetPhysicalDeviceQueueFamilyProperties(physical, &count, &queue_props);
   printf("CAPS bits=%u period=%.9f compute_graphics=%u frequency=%" PRIu64 "\n",
          queue_props.timestampValidBits, props.limits.timestampPeriod,
          props.limits.timestampComputeAndGraphics, frequency);
   int result = 0;
   if (caps_only) goto instance_cleanup;
   if (!queue_props.timestampValidBits) { result = 77; goto instance_cleanup; }
   double expected_period = frequency ? 1e9 / frequency : 0;
   if (getenv("T09_WRONG_PERIOD")) expected_period *= 2;
   if (queue_props.timestampValidBits != 64 || !props.limits.timestampComputeAndGraphics ||
       !expected_period || fabs(props.limits.timestampPeriod - expected_period) > expected_period * 1e-6) {
      puts("PERIOD_CHECK FAIL");
      result = 2;
      goto instance_cleanup;
   }
   puts("PERIOD_CHECK PASS");
   VkPhysicalDeviceMemoryProperties mp;
   GetPhysicalDeviceMemoryProperties(physical, &mp);
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
   VkDevice device;
   VK_CHECK(CreateDevice(physical, &dci, NULL, &device));
   LOAD_D(GetDeviceQueue); LOAD_D(CreateBuffer); LOAD_D(DestroyBuffer);
   LOAD_D(GetBufferMemoryRequirements); LOAD_D(AllocateMemory); LOAD_D(FreeMemory);
   LOAD_D(BindBufferMemory); LOAD_D(MapMemory); LOAD_D(UnmapMemory);
   LOAD_D(CreateQueryPool); LOAD_D(DestroyQueryPool); LOAD_D(GetQueryPoolResults);
   LOAD_D(CreateCommandPool); LOAD_D(DestroyCommandPool); LOAD_D(AllocateCommandBuffers);
   LOAD_D(BeginCommandBuffer); LOAD_D(EndCommandBuffer); LOAD_D(CmdExecuteCommands);
   LOAD_D(CmdResetQueryPool); LOAD_D(CmdWriteTimestamp); LOAD_D(CmdFillBuffer);
   LOAD_D(CmdCopyQueryPoolResults); LOAD_D(CmdPipelineBarrier);
   LOAD_D(CreateFence); LOAD_D(DestroyFence); LOAD_D(ResetFences);
   LOAD_D(QueueSubmit); LOAD_D(WaitForFences); LOAD_D(DestroyDevice);
   VkQueue queue;
   GetDeviceQueue(device, 0, 0, &queue);
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = TOTAL_BYTES, .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   VkBuffer buffer;
   VK_CHECK(CreateBuffer(device, &bci, NULL, &buffer));
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(device, buffer, &mr);
   uint32_t type = choose_type(&mp, mr.memoryTypeBits, 0, 0);
   if (type == UINT32_MAX) return 77;
   VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size, .memoryTypeIndex = type};
   VkDeviceMemory memory;
   VK_CHECK(AllocateMemory(device, &mai, NULL, &memory));
   VK_CHECK(BindBufferMemory(device, buffer, memory, 0));
   void *mapping;
   VK_CHECK(MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapping));
   volatile uint32_t *cpu = mapping;
   VkQueryPoolCreateInfo qpci = {.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
      .queryType = VK_QUERY_TYPE_TIMESTAMP, .queryCount = 2};
   VkQueryPool queries;
   VK_CHECK(CreateQueryPool(device, &qpci, NULL, &queries));
   VkCommandPoolCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                  .queueFamilyIndex = 0};
   VkCommandPool pool;
   VK_CHECK(CreateCommandPool(device, &cpci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 2};
   VkCommandBuffer primary[2], body;
   VK_CHECK(AllocateCommandBuffers(device, &cai, primary));
   body = primary[0];
   VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VkCommandBufferInheritanceInfo inheritance = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
   if (secondary) {
      cai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
      cai.commandBufferCount = 1;
      VK_CHECK(AllocateCommandBuffers(device, &cai, &body));
      begin.pInheritanceInfo = &inheritance;
   }
   VK_CHECK(BeginCommandBuffer(body, &begin));
   CmdResetQueryPool(body, queries, 0, 2);
   CmdWriteTimestamp(body, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0);
   CmdFillBuffer(body, buffer, 0, DATA_BYTES, 0x13579bdfu);
   CmdWriteTimestamp(body, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 1);
   CmdCopyQueryPoolResults(body, queries, 0, 2, buffer, DATA_BYTES, 32,
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
   CmdCopyQueryPoolResults(body, queries, 0, 2, buffer, DATA_BYTES + 64, 16,
      VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
   VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   CmdPipelineBarrier(body, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                      0, 1, &barrier, 0, NULL, 0, NULL);
   VK_CHECK(EndCommandBuffer(body));
   begin.pInheritanceInfo = NULL;
   if (secondary) {
      VK_CHECK(BeginCommandBuffer(primary[0], &begin));
      CmdExecuteCommands(primary[0], 1, &body);
      VK_CHECK(EndCommandBuffer(primary[0]));
   }
   VK_CHECK(BeginCommandBuffer(primary[1], &begin));
   CmdResetQueryPool(primary[1], queries, 0, 2);
   VK_CHECK(EndCommandBuffer(primary[1]));
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VK_CHECK(CreateFence(device, &fci, NULL, &fence));
   unsigned bad = 0;
   uint64_t previous = 0, previous_begin = 0, previous_pre = 0, previous_post = 0;
   for (unsigned round = 0; round < 3; round++) {
      for (unsigned i = 0; i < TOTAL_BYTES / 4; i++) cpu[i] = SENTINEL;
      VK_CHECK(ResetFences(device, 1, &fence));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                            .commandBufferCount = 1, .pCommandBuffers = &primary[0]};
      uint64_t pre = cpu_time();
      VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
      VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
      uint64_t post = cpu_time();
      uint64_t host64[8];
      uint32_t host32[8];
      memset(host64, 0xa5, sizeof(host64));
      memset(host32, 0xa5, sizeof(host32));
      VK_CHECK(GetQueryPoolResults(device, queries, 0, 2, sizeof(host64), host64, 32,
         VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
      VK_CHECK(GetQueryPoolResults(device, queries, 0, 2, sizeof(host32), host32, 16,
         VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
      bad += !host64[0] || host64[0] < previous || host64[4] <= host64[0];
      if (round) {
         double delta_ns = (double)(host64[0] - previous_begin) * props.limits.timestampPeriod;
         uint64_t lower = pre - previous_post, upper = post - previous_pre;
         bool time_ok = host64[0] > previous_begin &&
                        delta_ns + 10000 >= lower && delta_ns <= upper + 10000;
         bad += !time_ok;
         printf("CLOCK round=%u delta_ns=%.3f lower=%" PRIu64 " upper=%" PRIu64
                " verdict=%s\n", round, delta_ns, lower, upper, time_ok ? "PASS" : "FAIL");
      }
      previous_begin = host64[0];
      previous_pre = pre;
      previous_post = post;
      previous = host64[4];
      for (unsigned query = 0; query < 2; query++) {
         uint64_t copied[2];
         memcpy(copied, (const char *)mapping + DATA_BYTES + query * 32, sizeof(copied));
         bad += copied[0] != host64[query * 4] || copied[1] != 1;
         bad += host64[query * 4 + 1] != 1 || host32[query * 4 + 1] != 1;
         bad += host32[query * 4] != (uint32_t)host64[query * 4];
         bad += cpu[DATA_BYTES / 4 + 16 + query * 4] != host32[query * 4];
         bad += cpu[DATA_BYTES / 4 + 17 + query * 4] != 1;
         for (unsigned i = 2; i < 4; i++) {
            bad += host64[query * 4 + i] != UINT64_C(0xa5a5a5a5a5a5a5a5);
            bad += host32[query * 4 + i] != 0xa5a5a5a5u;
         }
      }
      uint32_t expected_fill = getenv("T09_WRONG_FILL") ? 0x13579bdeu : 0x13579bdfu;
      for (unsigned i = 0; i < DATA_BYTES / 4; i++) bad += cpu[i] != expected_fill;
      for (unsigned i = 0; i < 32; i++) {
         bool written = i < 4 || (i >= 8 && i < 12) ||
                        (i >= 16 && i < 18) || (i >= 20 && i < 22);
         if (!written) bad += cpu[DATA_BYTES / 4 + i] != SENTINEL;
      }
      printf("ROUND index=%u begin=%" PRIu64 " end=%" PRIu64 " cumulative_bad=%u\n",
             round, host64[0], host64[4], bad);
      if (round < 2) {
         struct timespec delay = {.tv_nsec = 50000000};
         while (nanosleep(&delay, &delay) && errno == EINTR) {}
      }
   }
   VK_CHECK(ResetFences(device, 1, &fence));
   VkSubmitInfo reset_submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &primary[1]};
   VK_CHECK(QueueSubmit(queue, 1, &reset_submit, fence));
   VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
   uint64_t reset_data[4] = {11, 22, 33, 44};
   VkResult reset_result = GetQueryPoolResults(device, queries, 0, 2, sizeof(reset_data),
      reset_data, 16, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
   bad += reset_result != VK_NOT_READY || reset_data[0] != 11 || reset_data[1] != 0 ||
          reset_data[2] != 33 || reset_data[3] != 0;
   printf("RESET result=%d values=%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64 "\n",
          reset_result, reset_data[0], reset_data[1], reset_data[2], reset_data[3]);
   DestroyFence(device, fence, NULL);
   DestroyCommandPool(device, pool, NULL);
   DestroyQueryPool(device, queries, NULL);
   UnmapMemory(device, memory);
   DestroyBuffer(device, buffer, NULL);
   FreeMemory(device, memory, NULL);
   DestroyDevice(device, NULL);
   printf("API mode=%s bad=%u verdict=%s\n", argv[1], bad, bad ? "FAIL" : "PASS");
   result = bad ? 2 : 0;

instance_cleanup:
   DestroyInstance(instance, NULL);
   dlclose(lib);
   unsigned final_fds = fd_count();
   printf("LIFECYCLE initial_fds=%u final_fds=%u\n", initial_fds, final_fds);
   if (initial_fds == UINT32_MAX || initial_fds != final_fds) return 3;
   return result;
}
