/*
 * T4.5.1 -- vkCmdFillBuffer on Valhall v9 / Job Manager.
 *
 * Why this test exists
 * --------------------
 * Porting dispatch_precomp() to v9 was done to unblock the indirect draw helper.
 * But dispatch_precomp() is shared: before the port it was an empty stub, so
 * every precompiled-kernel path on v9 silently did nothing. After the port those
 * paths reach real code. vkCmdFillBuffer is one of them, via panlib_fill*.
 *
 * That means the port changed the behaviour of a path nothing in Phase 4 tested.
 * A wrong value in the new dispatch_precomp would have gone from "does nothing"
 * to "does something unverified", which is worse. This test closes that gap.
 *
 * What is checked
 * ---------------
 * cmd_meta.c picks one of four code paths depending on alignment and size:
 *
 *   addr and range both 16-byte aligned -> panlib_fill_uint4        (bulk)
 *                                      -> panlib_fill_uint4_scalar  (tail)
 *   otherwise                          -> panlib_fill              (bulk)
 *                                      -> panlib_fill_scalar        (tail)
 *
 * A workgroup covers 32 elements, so wg_bytes is 512 for the uint4 path and 128
 * for the scalar path. Cases below are sized to land on each combination
 * deliberately rather than by chance, and the case list says which.
 *
 * The buffer is preloaded with a per-byte pattern that is a function of the byte
 * offset, so an untouched byte is not merely "not the fill value", it is a known
 * value. That distinction matters: a test that only checks for the fill value
 * cannot tell "filled correctly" from "filled too much".
 *
 * Every case therefore verifies three regions:
 *   before the fill  -- must still hold the original pattern
 *   the fill range   -- must hold the fill value
 *   after the fill   -- must still hold the original pattern
 *
 * Overrun in either direction is caught, not just underrun.
 *
 * Negative control: FILL_CASE=nop issues vkCmdFillBuffer with size 0. cmd_meta.c
 * returns early without dispatching anything, so the whole buffer must come back
 * as the original pattern. If that case ever reports filled bytes, the harness
 * itself is wrong and no other result here can be trusted.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#define CHECK(e, m)                                                            \
   do {                                                                        \
      VkResult _r = (e);                                                       \
      if (_r != VK_SUCCESS) {                                                  \
         printf("FAIL %s -> %d\n", (m), _r);                                   \
         return 1;                                                             \
      }                                                                        \
   } while (0)

#define BUF_BYTES 8192u
#define PANVK_DEFAULT_ICD_SO                                                   \
   "/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/"       \
   "vulkan/libvulkan_panfrost.so"

/* Pattern is a function of offset so an untouched byte is identifiable. */
static inline uint8_t
pattern_at(uint32_t off)
{
   return (uint8_t)(0xA5u ^ (off * 31u));
}

struct fill_case {
   const char *name;
   uint32_t offset;    /* dstOffset passed to vkCmdFillBuffer */
   uint32_t size;      /* fillSize passed to vkCmdFillBuffer */
   uint32_t data;      /* the dword to write */
   const char *path;   /* which code path in cmd_meta.c this is aimed at */
};

static const struct fill_case cases[] = {
   /* addr 16-aligned, range a whole multiple of 512 -> uint4 bulk only */
   { "u4_bulk", 0, 1024, 0x12345678u, "panlib_fill_uint4, 2 workgroups, no tail" },
   /* 512 + 16: one uint4 workgroup then a 16-byte uint4 tail */
   { "u4_tail", 0, 528, 0xCAFEBABEu, "panlib_fill_uint4 + panlib_fill_uint4_scalar" },
   /* under one workgroup, still 16-aligned -> uint4 tail only */
   { "u4_small", 0, 32, 0x0BADF00Du, "panlib_fill_uint4_scalar only" },
   /* offset 4 breaks 16-byte alignment -> scalar path, wg is 128 bytes */
   { "sc_bulk", 4, 256, 0xDEADBEEFu, "panlib_fill, 2 workgroups, no tail" },
   /* 128 + 8 on an unaligned address -> scalar bulk then scalar tail */
   { "sc_tail", 4, 136, 0x5A5A1234u, "panlib_fill + panlib_fill_scalar" },
   /* unaligned and tiny -> scalar tail only */
   { "sc_small", 4, 12, 0x77778888u, "panlib_fill_scalar only" },
   /* fill in the middle: proves dstOffset is honoured at both edges */
   { "middle", 2048, 512, 0x11223344u, "uint4 bulk at a non-zero offset" },
   /* negative control: cmd_meta.c returns before dispatching anything */
   { "nop", 0, 0, 0xFFFFFFFFu, "no dispatch at all, buffer must be untouched" },
};

int
main(void)
{
   const char *want = getenv("FILL_CASE") ? getenv("FILL_CASE") : "u4_bulk";
   const struct fill_case *c = NULL;
   for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
      if (!strcmp(cases[i].name, want))
         c = &cases[i];
   if (!c) {
      printf("unknown FILL_CASE=%s\n", want);
      return 1;
   }

   const char *icd = getenv("PANVK_ICD") ? getenv("PANVK_ICD")
                                         : PANVK_DEFAULT_ICD_SO;
   void *lib = dlopen(icd, RTLD_NOW);
   if (!lib) {
      printf("dlopen: %s\n", dlerror());
      return 1;
   }
   PFN_vkGetInstanceProcAddr gpa =
      (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!gpa)
      gpa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
   if (!gpa) {
      printf("no vkGetInstanceProcAddr\n");
      return 1;
   }

   PFN_vkCreateInstance CI = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
   VkApplicationInfo ai = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                            .apiVersion = VK_API_VERSION_1_1 };
   VkInstanceCreateInfo ii = { .sType =
                                  VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                               .pApplicationInfo = &ai };
   VkInstance inst;
   CHECK(CI(&ii, NULL, &inst), "vkCreateInstance");
#define IP(n) (PFN_vk##n) gpa(inst, "vk" #n)
   PFN_vkEnumeratePhysicalDevices EPD = IP(EnumeratePhysicalDevices);
   PFN_vkGetDeviceProcAddr GDPA = IP(GetDeviceProcAddr);
   PFN_vkCreateDevice CD = IP(CreateDevice);
   PFN_vkGetPhysicalDeviceMemoryProperties GMP =
      IP(GetPhysicalDeviceMemoryProperties);

   uint32_t n = 1;
   VkPhysicalDevice pd;
   CHECK(EPD(inst, &n, &pd), "vkEnumeratePhysicalDevices");

   float prio = 1.0f;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0,
      .queueCount = 1,
      .pQueuePriorities = &prio
   };
   VkDeviceCreateInfo di = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                             .queueCreateInfoCount = 1,
                             .pQueueCreateInfos = &qi };
   VkDevice dev;
   CHECK(CD(pd, &di, NULL, &dev), "vkCreateDevice");
#define DP(x) (PFN_vk##x) GDPA(dev, "vk" #x)
   PFN_vkGetDeviceQueue GDQ = DP(GetDeviceQueue);
   PFN_vkCreateCommandPool CCP = DP(CreateCommandPool);
   PFN_vkAllocateCommandBuffers ACB = DP(AllocateCommandBuffers);
   PFN_vkBeginCommandBuffer BCB = DP(BeginCommandBuffer);
   PFN_vkEndCommandBuffer ECB = DP(EndCommandBuffer);
   PFN_vkQueueSubmit QS = DP(QueueSubmit);
   PFN_vkQueueWaitIdle QWI = DP(QueueWaitIdle);
   PFN_vkCreateBuffer CB = DP(CreateBuffer);
   PFN_vkGetBufferMemoryRequirements GBMR = DP(GetBufferMemoryRequirements);
   PFN_vkAllocateMemory AM = DP(AllocateMemory);
   PFN_vkBindBufferMemory BBM = DP(BindBufferMemory);
   PFN_vkMapMemory MM = DP(MapMemory);
   PFN_vkCmdFillBuffer CFB = DP(CmdFillBuffer);

   if (!CFB) {
      printf("FAIL vkCmdFillBuffer resolves NULL\n");
      return 1;
   }

   VkQueue q;
   GDQ(dev, 0, 0, &q);

   VkBufferCreateInfo bi = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                             .size = BUF_BYTES,
                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT };
   VkBuffer buf;
   CHECK(CB(dev, &bi, NULL, &buf), "vkCreateBuffer");
   VkMemoryRequirements mr;
   GBMR(dev, buf, &mr);
   VkPhysicalDeviceMemoryProperties mp;
   GMP(pd, &mp);
   uint32_t mt = UINT32_MAX;
   for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
      const VkMemoryPropertyFlags need =
         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if ((mr.memoryTypeBits & (1u << i)) &&
          (mp.memoryTypes[i].propertyFlags & need) == need) {
         mt = i;
         break;
      }
   }
   if (mt == UINT32_MAX) {
      printf("FAIL no host visible coherent memory type\n");
      return 1;
   }
   VkMemoryAllocateInfo ma = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                               .allocationSize = mr.size,
                               .memoryTypeIndex = mt };
   VkDeviceMemory mem;
   CHECK(AM(dev, &ma, NULL, &mem), "vkAllocateMemory");
   CHECK(BBM(dev, buf, mem, 0), "vkBindBufferMemory");
   uint8_t *map;
   CHECK(MM(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&map), "vkMapMemory");

   /* Preload the offset-dependent pattern. */
   for (uint32_t i = 0; i < BUF_BYTES; i++)
      map[i] = pattern_at(i);

   VkCommandPoolCreateInfo cpi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = 0
   };
   VkCommandPool pool;
   CHECK(CCP(dev, &cpi, NULL, &pool), "vkCreateCommandPool");
   VkCommandBufferAllocateInfo cbi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1
   };
   VkCommandBuffer cmd;
   CHECK(ACB(dev, &cbi, &cmd), "vkAllocateCommandBuffers");
   VkCommandBufferBeginInfo cbb = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
   };
   CHECK(BCB(cmd, &cbb), "vkBeginCommandBuffer");
   CFB(cmd, buf, c->offset, c->size, c->data);
   CHECK(ECB(cmd), "vkEndCommandBuffer");
   VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                       .commandBufferCount = 1,
                       .pCommandBuffers = &cmd };
   CHECK(QS(q, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
   CHECK(QWI(q), "vkQueueWaitIdle");

   /* cmd_meta.c rounds the range down to a multiple of 4, so that is the number
    * of bytes the driver is permitted to touch. Computing it here rather than
    * hardcoding keeps the expectation derived from the API inputs. */
   const uint32_t eff = c->size & ~3u;
   const uint32_t lo = c->offset;
   const uint32_t hi = lo + eff;

   uint32_t filled = 0, kept_before = 0, kept_after = 0;
   uint32_t bad_fill = 0, overrun_before = 0, overrun_after = 0;
   uint32_t first_bad = UINT32_MAX;

   for (uint32_t i = 0; i < BUF_BYTES; i++) {
      const uint8_t want_fill = (uint8_t)(c->data >> (8 * (i % 4)));
      const uint8_t orig = pattern_at(i);
      const uint8_t got = map[i];

      if (i >= lo && i < hi) {
         if (got == want_fill)
            filled++;
         else {
            bad_fill++;
            if (first_bad == UINT32_MAX)
               first_bad = i;
         }
      } else if (i < lo) {
         if (got == orig)
            kept_before++;
         else {
            overrun_before++;
            if (first_bad == UINT32_MAX)
               first_bad = i;
         }
      } else {
         if (got == orig)
            kept_after++;
         else {
            overrun_after++;
            if (first_bad == UINT32_MAX)
               first_bad = i;
         }
      }
   }

   const int pass = (bad_fill == 0) && (overrun_before == 0) &&
                    (overrun_after == 0) && (filled == eff);

   printf("case      : %s\n", c->name);
   printf("path      : %s\n", c->path);
   printf("request   : offset=%u size=%u data=0x%08x\n", c->offset, c->size,
          c->data);
   printf("effective : %u bytes (size rounded down to a multiple of 4)\n", eff);
   printf("filled    : %u / %u expected\n", filled, eff);
   printf("preserved : %u before + %u after = %u of %u outside bytes\n",
          kept_before, kept_after, kept_before + kept_after, BUF_BYTES - eff);
   printf("bad_fill  : %u\n", bad_fill);
   printf("overrun   : %u before, %u after\n", overrun_before, overrun_after);
   if (first_bad != UINT32_MAX)
      printf("first_bad : offset %u (got 0x%02x)\n", first_bad, map[first_bad]);
   printf("FILLFP %s eff=%u filled=%u bad=%u ovr=%u verdict=%s\n", c->name, eff,
          filled, bad_fill, overrun_before + overrun_after,
          pass ? "PASS" : "FAIL");
   return pass ? 0 : 2;
}
