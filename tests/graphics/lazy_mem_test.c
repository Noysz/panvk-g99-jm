/* lazy_mem_test.c -- device-local memory that gets its pages on first GPU
 * use (kbase LAZY_COMMIT, patch 0070).
 *
 * Allocates 256 MiB of a device-local, not host-visible memory type, then:
 *   step 1: GPU fills [0, 4 MiB) with 0x11111111
 *   step 2: GPU fills [200 MiB, 204 MiB) with 0x22222222
 *   step 3: GPU copies [0,1 MiB), [100 MiB,101 MiB) (never written) and
 *           [200 MiB,201 MiB) to a host-visible buffer.
 * After each step it waits, sleeps 2.1 s and submits an empty batch so
 * PANVK_MEM_PROF=1 prints the backed size of the lazy buffer.
 *
 * PASS (exit 0): every step completes (no GPU fault, no timeout) and the
 * copied data is 0x11111111 / 0x22222222 where it was written. The bytes
 * of the untouched region are only printed (any value is valid in Vulkan).
 * With PANVK_MEM_PROF=1 the log shows the backed size: about 4 MiB after
 * step 1 and about 204 MiB after step 2 (kbase backs a growable region
 * contiguously up to the highest page used). With
 * PANVK_KBASE_DEVMEM_EAGER=1 it is 256 MiB from the start.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "vulkan/vulkan_core.h"

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);

#define MiB (1024ull * 1024ull)
#define SIZE (256 * MiB)

static PFN_vkGetDeviceProcAddr gdpa;
static VkDevice dev;
static VkQueue q;
static VkCommandPool cp;

#define D(x) PFN_vk##x x = (PFN_vk##x)gdpa(dev, "vk" #x)

static int
run(void (*rec)(VkCommandBuffer, void *), void *data)
{
   D(AllocateCommandBuffers); D(BeginCommandBuffer); D(EndCommandBuffer);
   D(QueueSubmit); D(CreateFence); D(WaitForFences); D(FreeCommandBuffers);
   D(DestroyFence);
   VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp, .commandBufferCount = 1};
   VkCommandBuffer cb;
   AllocateCommandBuffers(dev, &ca, &cb);
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   BeginCommandBuffer(cb, &bi);
   if (rec)
      rec(cb, data);
   EndCommandBuffer(cb);
   VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence f;
   CreateFence(dev, &fi, NULL, &f);
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cb};
   VkResult r = QueueSubmit(q, 1, &si, f);
   if (r == VK_SUCCESS)
      r = WaitForFences(dev, 1, &f, VK_TRUE, 10000000000ull);
   DestroyFence(dev, f, NULL);
   FreeCommandBuffers(dev, cp, 1, &cb);
   return r;
}

struct bufs { VkBuffer lazy, host; };

static void
rec_fill1(VkCommandBuffer cb, void *d)
{
   D(CmdFillBuffer);
   CmdFillBuffer(cb, ((struct bufs *)d)->lazy, 0, 4 * MiB, 0x11111111);
}

static void
rec_fill2(VkCommandBuffer cb, void *d)
{
   D(CmdFillBuffer);
   CmdFillBuffer(cb, ((struct bufs *)d)->lazy, 200 * MiB, 4 * MiB, 0x22222222);
}

static void
rec_copy(VkCommandBuffer cb, void *d)
{
   D(CmdCopyBuffer);
   struct bufs *b = d;
   VkBufferCopy r[3] = {
      {.srcOffset = 0, .dstOffset = 0, .size = MiB},
      {.srcOffset = 100 * MiB, .dstOffset = MiB, .size = MiB},
      {.srcOffset = 200 * MiB, .dstOffset = 2 * MiB, .size = MiB},
   };
   CmdCopyBuffer(cb, b->lazy, b->host, 3, r);
}

static void
tick(const char *label)
{
   printf("LAZYMEM step %s done\n", label);
   fflush(stdout);
   usleep(2100000);
   run(NULL, NULL);
}

int
main(void)
{
   const char *so = getenv("PANVK_ICD_SO");
   void *l = dlopen(so, RTLD_NOW);
   if (!l) {
      printf("FAILED: dlopen %s\n", dlerror());
      return 2;
   }
   G gpa = (G)dlsym(l, "vk_icdGetInstanceProcAddr");
   VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &ai};
   VkInstance in;
   ((PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance"))(&ii, NULL, &in);
   uint32_t n = 1;
   VkPhysicalDevice pd;
   ((PFN_vkEnumeratePhysicalDevices)gpa(in, "vkEnumeratePhysicalDevices"))(
      in, &n, &pd);
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
   ((PFN_vkCreateDevice)gpa(in, "vkCreateDevice"))(pd, &di, NULL, &dev);
   gdpa = (PFN_vkGetDeviceProcAddr)gpa(in, "vkGetDeviceProcAddr");
   D(GetDeviceQueue); D(CreateCommandPool); D(CreateBuffer);
   D(GetBufferMemoryRequirements); D(AllocateMemory); D(BindBufferMemory);
   D(MapMemory); D(DeviceWaitIdle);
   GetDeviceQueue(dev, 0, 0, &q);
   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   CreateCommandPool(dev, &cpi, NULL, &cp);

   int dl_only = -1, host = -1;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
      if (dl_only < 0 && (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
          !(f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
         dl_only = i;
      if (host < 0 && (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))
         host = i;
   }
   if (dl_only < 0 || host < 0) {
      printf("FAILED: memory types dl_only=%d host=%d\n", dl_only, host);
      return 2;
   }

   struct bufs b;
   VkDeviceMemory m_lazy, m_host;
   VkBufferCreateInfo bc = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = SIZE,
      .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
   CreateBuffer(dev, &bc, NULL, &b.lazy);
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(dev, b.lazy, &mr);
   if (!(mr.memoryTypeBits & (1u << dl_only))) {
      printf("FAILED: buffer cannot use type %d\n", dl_only);
      return 2;
   }
   VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                              .allocationSize = mr.size,
                              .memoryTypeIndex = dl_only};
   if (AllocateMemory(dev, &ma, NULL, &m_lazy)) {
      printf("FAILED: allocate 256 MiB\n");
      return 2;
   }
   BindBufferMemory(dev, b.lazy, m_lazy, 0);

   bc.size = 3 * MiB;
   CreateBuffer(dev, &bc, NULL, &b.host);
   GetBufferMemoryRequirements(dev, b.host, &mr);
   ma.allocationSize = mr.size;
   ma.memoryTypeIndex = host;
   AllocateMemory(dev, &ma, NULL, &m_host);
   BindBufferMemory(dev, b.host, m_host, 0);
   uint32_t *p;
   MapMemory(dev, m_host, 0, 3 * MiB, 0, (void **)&p);
   memset(p, 0xee, 3 * MiB);

   tick("0 (allocated)");
   int r1 = run(rec_fill1, &b);
   tick("1 (filled 0-4 MiB)");
   int r2 = run(rec_fill2, &b);
   tick("2 (filled 200-204 MiB)");
   int r3 = run(rec_copy, &b);

   uint32_t bad1 = 0, bad3 = 0;
   for (uint32_t i = 0; i < MiB / 4; i++) {
      bad1 += p[i] != 0x11111111;
      bad3 += p[2 * MiB / 4 + i] != 0x22222222;
   }
   int pass = !r1 && !r2 && !r3 && !bad1 && !bad3;
   printf("LAZYMEM fill1=%d fill2=%d copy=%d bad_written_0=%u "
          "bad_written_200=%u untouched_100MiB_first_word=0x%08x "
          "verdict=%s\n", r1, r2, r3, bad1, bad3, p[MiB / 4],
          pass ? "PASS" : "FAIL");
   DeviceWaitIdle(dev);
   return pass ? 0 : 2;
}
