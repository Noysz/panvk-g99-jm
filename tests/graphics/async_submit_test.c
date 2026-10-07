/* async_submit_test.c -- does vkQueueSubmit return before the GPU is done,
 * and is the result still correct?
 *
 * Submit 1: 16 fills of a 64 MiB buffer (value 1..16) + vkCmdSetEvent,
 *           signals semaphore S.
 * Submit 2: waits on S, one more fill (value 0x5a5a5a5a) of the first
 *           quarter, signals fence F.
 *
 * PASS (exit 0) needs all of:
 *   - F is not signaled right after the second vkQueueSubmit returns
 *     (the submit did not wait for the GPU),
 *   - vkWaitForFences(F) succeeds,
 *   - the buffer holds 0x5a5a5a5a in the first quarter and 16 in the rest
 *     (submit 2 ran after submit 1),
 *   - the event reads VK_EVENT_SET after the wait.
 * Exit 2 = FAIL. With PANVK_KBASE_SYNC_SUBMIT=1 (submission waits for the
 * GPU) the first check fails: that is the negative control.
 *
 * Driver: $PANVK_ICD_SO (default: build9). Build:
 *   clang -O1 -I<mesa>/include async_submit_test.c -o async_submit_test -ldl
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "vulkan/vulkan_core.h"

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);

static double
now(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec * 1e-9;
}

#define SIZE (64u << 20)
#define FILLS 16

int
main(void)
{
   const char *so = getenv("PANVK_ICD_SO");
   if (!so || !so[0])
      so = "/data/data/com.termux/files/home/panvk-g57/mesa/build9/src/"
           "panfrost/vulkan/libvulkan_panfrost.so";
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
   if (((PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance"))(&ii, NULL, &in)) {
      printf("FAILED: vkCreateInstance\n");
      return 2;
   }
   uint32_t n = 1;
   VkPhysicalDevice pd;
   ((PFN_vkEnumeratePhysicalDevices)gpa(in, "vkEnumeratePhysicalDevices"))(
      in, &n, &pd);
   float pr = 1;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 1,
      .pQueuePriorities = &pr};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .queueCreateInfoCount = 1,
                            .pQueueCreateInfos = &qi};
   VkDevice dev;
   if (((PFN_vkCreateDevice)gpa(in, "vkCreateDevice"))(pd, &di, NULL, &dev)) {
      printf("FAILED: vkCreateDevice\n");
      return 2;
   }
   PFN_vkGetDeviceProcAddr gdpa =
      (PFN_vkGetDeviceProcAddr)gpa(in, "vkGetDeviceProcAddr");
#define D(x) PFN_vk##x x = (PFN_vk##x)gdpa(dev, "vk" #x)
   D(GetDeviceQueue); D(CreateFence); D(QueueSubmit); D(WaitForFences);
   D(GetFenceStatus); D(CreateCommandPool); D(AllocateCommandBuffers);
   D(BeginCommandBuffer); D(EndCommandBuffer); D(QueueWaitIdle);
   D(CmdFillBuffer); D(CreateBuffer); D(GetBufferMemoryRequirements);
   D(AllocateMemory); D(BindBufferMemory); D(MapMemory);
   D(CmdPipelineBarrier); D(CreateSemaphore); D(CreateEvent);
   D(CmdSetEvent); D(GetEventStatus); D(DeviceWaitIdle);

   VkQueue q;
   GetDeviceQueue(dev, 0, 0, &q);

   VkBufferCreateInfo bc = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                            .size = SIZE,
                            .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT};
   VkBuffer b;
   CreateBuffer(dev, &bc, NULL, &b);
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(dev, b, &mr);
   /* Memory type 0: device local, host visible, host coherent. */
   VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                              .allocationSize = mr.size,
                              .memoryTypeIndex = 0};
   VkDeviceMemory m;
   if (AllocateMemory(dev, &ma, NULL, &m)) {
      printf("FAILED: vkAllocateMemory\n");
      return 2;
   }
   BindBufferMemory(dev, b, m, 0);
   uint32_t *ptr;
   MapMemory(dev, m, 0, SIZE, 0, (void **)&ptr);
   memset(ptr, 0, SIZE);

   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool cp;
   CreateCommandPool(dev, &cpi, NULL, &cp);
   VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp,
      .commandBufferCount = 2};
   VkCommandBuffer cb[2];
   AllocateCommandBuffers(dev, &ca, cb);

   VkEventCreateInfo eci = {.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
   VkEvent ev;
   CreateEvent(dev, &eci, NULL, &ev);

   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT};
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};

   BeginCommandBuffer(cb[0], &bi);
   for (uint32_t i = 1; i <= FILLS; i++) {
      CmdFillBuffer(cb[0], b, 0, SIZE, i);
      CmdPipelineBarrier(cb[0], VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0,
                         NULL);
   }
   CmdSetEvent(cb[0], ev, VK_PIPELINE_STAGE_TRANSFER_BIT);
   EndCommandBuffer(cb[0]);

   BeginCommandBuffer(cb[1], &bi);
   CmdFillBuffer(cb[1], b, 0, SIZE / 4, 0x5a5a5a5a);
   EndCommandBuffer(cb[1]);

   VkSemaphoreCreateInfo sci = {.sType =
                                   VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
   VkSemaphore s;
   CreateSemaphore(dev, &sci, NULL, &s);
   VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence f;
   CreateFence(dev, &fi, NULL, &f);

   VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
   VkSubmitInfo si[2] = {
      {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
       .commandBufferCount = 1,
       .pCommandBuffers = &cb[0],
       .signalSemaphoreCount = 1,
       .pSignalSemaphores = &s},
      {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
       .waitSemaphoreCount = 1,
       .pWaitSemaphores = &s,
       .pWaitDstStageMask = &ws,
       .commandBufferCount = 1,
       .pCommandBuffers = &cb[1]},
   };

   double t0 = now();
   VkResult r1 = QueueSubmit(q, 1, &si[0], VK_NULL_HANDLE);
   VkResult r2 = QueueSubmit(q, 1, &si[1], f);
   double t_submit = now() - t0;
   VkResult st = GetFenceStatus(dev, f);
   VkResult evst_early = GetEventStatus(dev, ev);

   t0 = now();
   VkResult rw = WaitForFences(dev, 1, &f, VK_TRUE, 5000000000ull);
   double t_wait = now() - t0;
   VkResult evst = GetEventStatus(dev, ev);

   uint32_t bad_q1 = 0, bad_rest = 0;
   for (uint32_t i = 0; i < SIZE / 4; i += 4099) {
      uint32_t want = i < SIZE / 16 ? 0x5a5a5a5au : FILLS;
      if (ptr[i] != want) {
         if (i < SIZE / 16)
            bad_q1++;
         else
            bad_rest++;
      }
   }
   uint32_t last = ptr[SIZE / 4 - 1];

   int async_ok = st == VK_NOT_READY;
   int pass = r1 == VK_SUCCESS && r2 == VK_SUCCESS && async_ok &&
              rw == VK_SUCCESS && bad_q1 == 0 && bad_rest == 0 &&
              last == FILLS && evst == VK_EVENT_SET;

   printf("ASYNCSUB submits=%.1f ms fence_after_submit=%s event_early=%s "
          "wait=%d after %.1f ms event=%s bad_q1=%u bad_rest=%u last=%u "
          "verdict=%s\n",
          t_submit * 1e3, st == VK_NOT_READY ? "NOT_READY" :
                          st == VK_SUCCESS   ? "SIGNALED" : "ERROR",
          evst_early == VK_EVENT_SET ? "SET" : "RESET", rw, t_wait * 1e3,
          evst == VK_EVENT_SET ? "SET" : "RESET", bad_q1, bad_rest, last,
          pass ? "PASS" : "FAIL");

   DeviceWaitIdle(dev);
   return pass ? 0 : 2;
}
