/* vk_map32_test.c -- 32-bit Windows program (wow64): does vkMapMemory of
 * host-visible memory work and give a usable pointer?
 *
 * 32-bit games under Wine wow64 need every Vulkan memory mapping below
 * 4 GB. Wine does that with VK_EXT_map_memory_placed (or
 * VK_EXT_external_memory_host); without it winevulkan prints "returned
 * mapping ... does not fit 32-bit pointer" and the map fails, and DXVK
 * cannot upload anything (Just Cause 2 / FNAF reports, 2026-10-08).
 *
 * Build: i686-w64-mingw32-gcc -O2 -I<mesa>/include vk_map32_test.c -o vk_map32_test.exe
 * Run:   wine vk_map32_test.exe   (inside the Winlator-like stack)
 *
 * PASS (exit 0): instance, device, 4 x 64 MiB host-visible allocations,
 * each mapped, written and read back through the mapping.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan_core.h"

#define N 4
#define SZ (64u << 20)

int
main(void)
{
   HMODULE m = LoadLibraryA("vulkan-1.dll");
   if (!m) {
      printf("VKMAP32 FAILED: LoadLibrary vulkan-1.dll\n");
      return 2;
   }
   PFN_vkGetInstanceProcAddr gipa =
      (PFN_vkGetInstanceProcAddr)GetProcAddress(m, "vkGetInstanceProcAddr");
   VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                           .apiVersion = VK_API_VERSION_1_1};
   VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                              .pApplicationInfo = &ai};
   VkInstance in;
   VkResult r = ((PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance"))(&ii, NULL, &in);
   printf("VKMAP32 vkCreateInstance=%d\n", r);
   fflush(stdout);
   if (r)
      return 2;
   uint32_t n = 1;
   VkPhysicalDevice pd = VK_NULL_HANDLE;
   r = ((PFN_vkEnumeratePhysicalDevices)gipa(in, "vkEnumeratePhysicalDevices"))(in, &n, &pd);
   if (r < 0 || !pd) {
      printf("VKMAP32 FAILED: enumerate %d n=%u\n", r, n);
      return 2;
   }
   VkPhysicalDeviceProperties pp;
   ((PFN_vkGetPhysicalDeviceProperties)gipa(in, "vkGetPhysicalDeviceProperties"))(pd, &pp);
   uint32_t ne = 0;
   PFN_vkEnumerateDeviceExtensionProperties ede =
      (PFN_vkEnumerateDeviceExtensionProperties)gipa(in, "vkEnumerateDeviceExtensionProperties");
   ede(pd, NULL, &ne, NULL);
   VkExtensionProperties *ep = calloc(ne, sizeof(*ep));
   ede(pd, NULL, &ne, ep);
   int placed = 0, exthost = 0;
   for (uint32_t i = 0; i < ne; i++) {
      placed |= !strcmp(ep[i].extensionName, "VK_EXT_map_memory_placed");
      exthost |= !strcmp(ep[i].extensionName, "VK_EXT_external_memory_host");
   }
   printf("VKMAP32 device %s, %u extensions, map_memory_placed=%d external_memory_host=%d\n",
          pp.deviceName, ne, placed, exthost);
   fflush(stdout);

   VkPhysicalDeviceMemoryProperties mp;
   ((PFN_vkGetPhysicalDeviceMemoryProperties)gipa(in, "vkGetPhysicalDeviceMemoryProperties"))(pd, &mp);
   int host = -1;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      VkMemoryPropertyFlags f = mp.memoryTypes[i].propertyFlags;
      if ((f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
          (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
         host = i;
         break;
      }
   }
   float pr = 1;
   VkDeviceQueueCreateInfo qi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                 .queueCount = 1, .pQueuePriorities = &pr};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .queueCreateInfoCount = 1, .pQueueCreateInfos = &qi};
   VkDevice dev;
   r = ((PFN_vkCreateDevice)gipa(in, "vkCreateDevice"))(pd, &di, NULL, &dev);
   if (r) {
      printf("VKMAP32 FAILED: vkCreateDevice %d\n", r);
      return 2;
   }
   PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr)gipa(in, "vkGetDeviceProcAddr");
   PFN_vkAllocateMemory am = (PFN_vkAllocateMemory)gdpa(dev, "vkAllocateMemory");
   PFN_vkMapMemory mm = (PFN_vkMapMemory)gdpa(dev, "vkMapMemory");
   int ok = 0;
   for (int i = 0; i < N; i++) {
      VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                                 .allocationSize = SZ, .memoryTypeIndex = host};
      VkDeviceMemory mem;
      r = am(dev, &ma, NULL, &mem);
      void *p = NULL;
      VkResult rm = r ? r : mm(dev, mem, 0, VK_WHOLE_SIZE, 0, &p);
      int good = 0;
      if (!rm && p) {
         volatile uint32_t *u = p;
         u[0] = 0x12345678u + i;
         u[SZ / 4 - 1] = 0x9abcdef0u;
         good = u[0] == 0x12345678u + (uint32_t)i && u[SZ / 4 - 1] == 0x9abcdef0u;
      }
      printf("VKMAP32 alloc %d: allocate=%d map=%d ptr=%p rw=%s\n", i, r, rm, p,
             good ? "ok" : "bad");
      fflush(stdout);
      ok += good;
   }
   printf("VKMAP32 mapped_ok=%d/%d verdict=%s\n", ok, N, ok == N ? "PASS" : "FAIL");
   return ok == N ? 0 : 1;
}
