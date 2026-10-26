#include <vulkan/vulkan_core.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/dma-heap.h>

#define VK_CHECK(e) do { VkResult r_ = (e); if (r_ != VK_SUCCESS) { \
   fprintf(stderr, "%s: %d at %d\n", #e, r_, __LINE__); exit(1); } } while (0)
#define LOAD_I(n) PFN_vk##n n = (PFN_vk##n)gpa(instance, "vk" #n)
#define LOAD_D(n) PFN_vk##n n = (PFN_vk##n)GetDeviceProcAddr(device, "vk" #n)
#define SIZE (64 * 1024)
#define WORDS (SIZE / 4)

static uint32_t choose_type(const VkPhysicalDeviceMemoryProperties *p,
                            uint32_t bits, int cached, int gpu_only)
{
   uint32_t want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  (cached ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT :
                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
   for (uint32_t i = 0; i < p->memoryTypeCount; i++) {
      uint32_t f = p->memoryTypes[i].propertyFlags;
      if (gpu_only) {
         if ((bits & (1u << i)) && !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
            return i;
         continue;
      }
      if ((bits & (1u << i)) && (f & want) == want &&
          (!cached || !(f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))
         return i;
   }
   return UINT32_MAX;
}

static uint32_t pattern(uint32_t round, uint32_t word)
{
   return (word * 0x9e3779b9u) ^ (round * 0x45d9f3bu) ^ 0xa571c38du;
}

int main(int argc, char **argv)
{
   if (argc != 3) return 1;
   const char *mode = argv[1];
   int native = !strcmp(mode, "native");
   int exported = !strcmp(mode, "export");
   int imported = !strcmp(mode, "import");
   int raw = !strcmp(mode, "system") || !strcmp(mode, "system-uncached");
   int cached = !strcmp(argv[2], "cached");
   int gpu_only = !strcmp(argv[2], "device");
   if ((!native && !exported && !imported && !raw) ||
       (!cached && !gpu_only && strcmp(argv[2], "coherent"))) return 1;
   int negative = getenv("VISIBILITY_WRONG_EXPECTED") != NULL;
   const char *libpath = getenv("PANVK_ICD_SO");
   if (!libpath) return 1;
   void *lib = dlopen(libpath, RTLD_NOW | RTLD_LOCAL);
   if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
   PFN_vkGetInstanceProcAddr gpa = dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!gpa) return 1;
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &app};
   VkInstance instance;
   VK_CHECK(CreateInstance(&ici, NULL, &instance));
   LOAD_I(EnumeratePhysicalDevices); LOAD_I(GetPhysicalDeviceMemoryProperties);
   LOAD_I(GetPhysicalDeviceExternalBufferProperties); LOAD_I(CreateDevice);
   LOAD_I(GetDeviceProcAddr); LOAD_I(DestroyInstance);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   VK_CHECK(EnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   VkPhysicalDeviceMemoryProperties mp;
   GetPhysicalDeviceMemoryProperties(physical, &mp);
   VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
   VkPhysicalDeviceExternalBufferInfo ebi = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
      .usage = usage, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   };
   VkExternalBufferProperties ebp = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
   GetPhysicalDeviceExternalBufferProperties(physical, &ebi, &ebp);
   printf("external features=0x%x compatible=0x%x\n",
          ebp.externalMemoryProperties.externalMemoryFeatures,
          ebp.externalMemoryProperties.compatibleHandleTypes);
   if (!native && !(ebp.externalMemoryProperties.externalMemoryFeatures &
                     VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) return 77;
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
   const char *extensions[] = {VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                              VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
      .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
   VkDevice device;
   VK_CHECK(CreateDevice(physical, &dci, NULL, &device));
   LOAD_D(GetDeviceQueue); LOAD_D(CreateBuffer); LOAD_D(DestroyBuffer);
   LOAD_D(GetBufferMemoryRequirements); LOAD_D(AllocateMemory); LOAD_D(FreeMemory);
   LOAD_D(BindBufferMemory); LOAD_D(MapMemory); LOAD_D(UnmapMemory);
   LOAD_D(GetMemoryFdKHR); LOAD_D(GetMemoryFdPropertiesKHR);
   LOAD_D(FlushMappedMemoryRanges); LOAD_D(InvalidateMappedMemoryRanges);
   LOAD_D(CreateCommandPool); LOAD_D(DestroyCommandPool); LOAD_D(AllocateCommandBuffers);
   LOAD_D(BeginCommandBuffer); LOAD_D(EndCommandBuffer); LOAD_D(ResetCommandBuffer);
   LOAD_D(CmdPipelineBarrier); LOAD_D(CmdCopyBuffer); LOAD_D(CmdFillBuffer);
   LOAD_D(CreateFence); LOAD_D(DestroyFence); LOAD_D(ResetFences); LOAD_D(WaitForFences);
   LOAD_D(QueueSubmit); LOAD_D(DeviceWaitIdle); LOAD_D(DestroyDevice);
   VkQueue queue;
   GetDeviceQueue(device, 0, 0, &queue);
   VkExternalMemoryBufferCreateInfo ext = {
      .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = native ? NULL : &ext, .size = SIZE, .usage = usage};
   VkBuffer buffer, readback;
   VK_CHECK(CreateBuffer(device, &bci, NULL, &buffer));
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(device, buffer, &mr);
   uint32_t allowed = mr.memoryTypeBits;
   int fd = -1;
   if (raw) {
      char path[128];
      snprintf(path, sizeof(path), "/dev/dma_heap/%s", mode);
      int heap = open(path, O_RDONLY | O_CLOEXEC);
      if (heap < 0) { perror(path); return 77; }
      struct dma_heap_allocation_data a = {.len = mr.size, .fd_flags = O_RDWR | O_CLOEXEC};
      if (ioctl(heap, DMA_HEAP_IOCTL_ALLOC, &a)) { perror("heap alloc"); return 1; }
      close(heap);
      fd = a.fd;
      VkMemoryFdPropertiesKHR prop = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
      VK_CHECK(GetMemoryFdPropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &prop));
      allowed &= prop.memoryTypeBits;
      printf("fd memoryTypeBits=0x%x\n", prop.memoryTypeBits);
   }
   uint32_t type = choose_type(&mp, allowed, cached, gpu_only);
   if (type == UINT32_MAX) {
      printf("UNSUPPORTED memory mode=%s type=%s allowed=0x%x\n", mode, argv[2], allowed);
      return 77;
   }
   printf("mode=%s memory_type=%u flags=0x%x bytes=%u rounds=32\n",
          mode, type, mp.memoryTypes[type].propertyFlags, SIZE);
   VkExportMemoryAllocateInfo exp = {.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
      .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
   VkImportMemoryFdInfoKHR imp = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, .fd = fd};
   VkMemoryDedicatedAllocateInfo dedicated = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .buffer = buffer,
      .pNext = raw ? (void *)&imp : exported || imported ? (void *)&exp : NULL};
   VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext = &dedicated, .allocationSize = mr.size, .memoryTypeIndex = type};
   VkDeviceMemory memory;
   VK_CHECK(AllocateMemory(device, &ai, NULL, &memory));
   VK_CHECK(BindBufferMemory(device, buffer, memory, 0));
   if (exported || imported) {
      VkMemoryGetFdInfoKHR getfd = {.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
         .memory = memory, .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
      VK_CHECK(GetMemoryFdKHR(device, &getfd, &fd));
      printf("EXPORTED_FD verified\n");
      if (exported) close(fd);
   }
   if (imported) {
      DestroyBuffer(device, buffer, NULL);
      FreeMemory(device, memory, NULL);
      VK_CHECK(CreateBuffer(device, &bci, NULL, &buffer));
      VkMemoryFdPropertiesKHR prop = {.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR};
      VK_CHECK(GetMemoryFdPropertiesKHR(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fd, &prop));
      if (!(prop.memoryTypeBits & (1u << type))) {
         printf("UNSUPPORTED reimport type=%u allowed=0x%x\n", type, prop.memoryTypeBits);
         return 77;
      }
      imp.fd = fd;
      dedicated.buffer = buffer;
      dedicated.pNext = &imp;
      VK_CHECK(AllocateMemory(device, &ai, NULL, &memory));
      VK_CHECK(BindBufferMemory(device, buffer, memory, 0));
   }
   bci.pNext = NULL;
   VK_CHECK(CreateBuffer(device, &bci, NULL, &readback));
   GetBufferMemoryRequirements(device, readback, &mr);
   ai.pNext = NULL;
   ai.allocationSize = mr.size;
   ai.memoryTypeIndex = choose_type(&mp, mr.memoryTypeBits, 0, 0);
   if (ai.memoryTypeIndex == UINT32_MAX) return 77;
   VkDeviceMemory readmem;
   VK_CHECK(AllocateMemory(device, &ai, NULL, &readmem));
   VK_CHECK(BindBufferMemory(device, readback, readmem, 0));
   void *mapping = NULL, *readmapping;
   if (!gpu_only) VK_CHECK(MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapping));
   VK_CHECK(MapMemory(device, readmem, 0, VK_WHOLE_SIZE, 0, &readmapping));
   volatile uint32_t *cpu = mapping, *result = readmapping;
   VkMappedMemoryRange range = {.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      .memory = memory, .offset = 0, .size = VK_WHOLE_SIZE};
   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = 0};
   VkCommandPool pool;
   VK_CHECK(CreateCommandPool(device, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cba = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   VK_CHECK(AllocateCommandBuffers(device, &cba, &cmd));
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VK_CHECK(CreateFence(device, &fci, NULL, &fence));
   uint64_t bad_copy = 0, bad_fill = 0, bad_tail = 0;
   for (uint32_t round = 1; round <= 32; round++) {
      for (uint32_t j = 0; j < WORDS; j++) {
         if (!gpu_only) cpu[j] = pattern(round, j);
         result[j] = ~pattern(round, j);
      }
      range.size = VK_WHOLE_SIZE;
      if (cached) VK_CHECK(FlushMappedMemoryRanges(device, 1, &range));
      VK_CHECK(ResetCommandBuffer(cmd, 0));
      VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
      VK_CHECK(BeginCommandBuffer(cmd, &begin));
      VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
         .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT, .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
      CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
      if (gpu_only) {
         CmdFillBuffer(cmd, buffer, 0, SIZE, pattern(round, 0));
         barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
         barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
         CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
      }
      VkBufferCopy copy = {.size = SIZE};
      CmdCopyBuffer(cmd, buffer, readback, 1, &copy);
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
      barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
      uint32_t fill = 0x31415926u ^ (round * 0x1020304u);
      CmdFillBuffer(cmd, buffer, 0, SIZE / 2, fill);
      barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
      barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
      CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
      VK_CHECK(EndCommandBuffer(cmd));
      VK_CHECK(ResetFences(device, 1, &fence));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1, .pCommandBuffers = &cmd};
      VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
      VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, 5000000000ull));
      if (cached) {
         for (uint32_t j = WORDS / 2; j < WORDS; j++)
            cpu[j] = pattern(round + 100, j);
         range.size = SIZE / 2;
         VK_CHECK(InvalidateMappedMemoryRanges(device, 1, &range));
      }
      for (uint32_t j = 0; j < WORDS; j++) {
         uint32_t expected = pattern(round, gpu_only ? 0 : j) ^ (negative && j == 3 ? 1u : 0u);
         bad_copy += result[j] != expected;
         if (!gpu_only) {
            if (j < WORDS / 2) bad_fill += cpu[j] != fill;
            else bad_tail += cpu[j] != pattern(round + (cached ? 100 : 0), j);
         }
      }
   }
   VK_CHECK(DeviceWaitIdle(device));
   DestroyFence(device, fence, NULL);
   DestroyCommandPool(device, pool, NULL);
   if (!gpu_only) UnmapMemory(device, memory);
   UnmapMemory(device, readmem);
   DestroyBuffer(device, buffer, NULL); DestroyBuffer(device, readback, NULL);
   FreeMemory(device, memory, NULL); FreeMemory(device, readmem, NULL);
   DestroyDevice(device, NULL); DestroyInstance(instance, NULL);
   uint64_t bad = bad_copy + bad_fill + bad_tail;
   printf("VISIBILITY mode=%s type=%s copy_bad=%" PRIu64 " fill_bad=%" PRIu64
          " tail_bad=%" PRIu64 " verdict=%s\n", mode, argv[2], bad_copy, bad_fill, bad_tail, bad ? "FAIL" : "PASS");
   return bad ? 2 : 0;
}
