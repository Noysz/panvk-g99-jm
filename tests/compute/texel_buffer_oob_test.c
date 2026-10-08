/* texel_buffer_oob_test.c -- texel buffer reads, stores and atomics out of
 * the view range with robustBufferAccess2 (VK_EXT_robustness2).
 *
 * Shader: tests/shaders/texel_oob.comp (texel_oob_spv.h is generated with
 *   glslangValidator -V --target-env vulkan1.1 --vn texel_oob_spv \
 *     tests/shaders/texel_oob.comp -o tests/compute/texel_oob_spv.h)
 * Build: clang -O2 -I<mesa>/include texel_buffer_oob_test.c -ldl
 * Run:   PANVK_ICD_SO=<libvulkan_panfrost.so> ./a.out
 *
 * One buffer, four views of 4 texels each, placed in memory that is filled
 * with a non-zero pattern beyond the view, so a read without a bounds check
 * returns the pattern:
 *   uniform texel buffer R32_UINT, storage texel buffer R32_UINT,
 *   uniform texel buffer R8G8B8A8_UNORM, uniform texel buffer R32G32_SFLOAT.
 * Indices 0..7 are read (4..7 out of bounds), then 0..7 are stored, then an
 * atomic add at index 1 (in range) and 6 (out of range).
 *
 * PASS (exit 0) when, as the Vulkan spec requires for robustBufferAccess2:
 *   - in-range reads return the data (missing G, B, A as 0, 0, 1);
 *   - out-of-range reads return 0 for the format's components and 0, 0, 1
 *     for missing G, B, A: R32_UINT and R32G32_SFLOAT (0,0,0,1),
 *     R8G8B8A8_UNORM (0,0,0,0);
 *   - out-of-range stores and atomics leave the memory after the view alone,
 *     in-range ones land.
 * The device is created with robustBufferAccess2 if the driver exposes it;
 * if not, the test says so and exits 3.
 */
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vulkan/vulkan_core.h"
#include "texel_oob_spv.h"

typedef PFN_vkVoidFunction (*G)(VkInstance, const char *);

static PFN_vkGetDeviceProcAddr gdpa;
static VkDevice dev;

#define D(x) PFN_vk##x x = (PFN_vk##x)gdpa(dev, "vk" #x)

#define VIEW_BYTES 256 /* distance between views, >= any texel alignment */
#define PATTERN(off) (0xA5000000u | (uint32_t)(off))

static unsigned bad;

static void
check(const char *what, int idx, const uint32_t *got, const uint32_t *want)
{
   if (memcmp(got, want, 16) == 0)
      return;
   bad++;
   printf("  BAD %-14s idx %d got %08x %08x %08x %08x want %08x %08x %08x "
          "%08x\n", what, idx, got[0], got[1], got[2], got[3], want[0],
          want[1], want[2], want[3]);
}

static uint32_t
f2u(float f)
{
   uint32_t u;
   memcpy(&u, &f, 4);
   return u;
}

int
main(void)
{
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

   VkPhysicalDeviceRobustness2FeaturesEXT r2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
   VkPhysicalDeviceFeatures2 f2 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &r2};
   ((PFN_vkGetPhysicalDeviceFeatures2)gpa(in, "vkGetPhysicalDeviceFeatures2"))(
      pd, &f2);
   VkPhysicalDeviceProperties props;
   ((PFN_vkGetPhysicalDeviceProperties)gpa(in, "vkGetPhysicalDeviceProperties"))(
      pd, &props);
   printf("TEXELOOB device %s robustBufferAccess2=%u minTexelBufferOffsetAlignment=%u\n",
          props.deviceName, r2.robustBufferAccess2,
          (unsigned)props.limits.minTexelBufferOffsetAlignment);
   if (!r2.robustBufferAccess2) {
      printf("TEXELOOB verdict=NOT_SUPPORTED (robustBufferAccess2 not exposed)\n");
      return 3;
   }
   if (props.limits.minTexelBufferOffsetAlignment > VIEW_BYTES) {
      printf("FAILED: texel buffer alignment %u > %u\n",
             (unsigned)props.limits.minTexelBufferOffsetAlignment, VIEW_BYTES);
      return 2;
   }

   VkPhysicalDeviceMemoryProperties mp;
   ((PFN_vkGetPhysicalDeviceMemoryProperties)gpa(
      in, "vkGetPhysicalDeviceMemoryProperties"))(pd, &mp);

   /* Only robustBufferAccess2 from robustness2, nothing else. */
   VkPhysicalDeviceRobustness2FeaturesEXT r2e = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
      .robustBufferAccess2 = VK_TRUE};
   VkPhysicalDeviceFeatures2 fe = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &r2e,
      .features = {.robustBufferAccess = VK_TRUE}};
   const char *ext = VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
   float pr = 1;
   VkDeviceQueueCreateInfo qi = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueCount = 1, .pQueuePriorities = &pr};
   VkDeviceCreateInfo di = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                            .pNext = &fe,
                            .queueCreateInfoCount = 1,
                            .pQueueCreateInfos = &qi,
                            .enabledExtensionCount = 1,
                            .ppEnabledExtensionNames = &ext};
   VkResult r = ((PFN_vkCreateDevice)gpa(in, "vkCreateDevice"))(pd, &di, NULL,
                                                                &dev);
   if (r) {
      printf("FAILED: vkCreateDevice %d\n", r);
      return 2;
   }
   gdpa = (PFN_vkGetDeviceProcAddr)gpa(in, "vkGetDeviceProcAddr");
   D(GetDeviceQueue); D(CreateCommandPool); D(CreateBuffer);
   D(GetBufferMemoryRequirements); D(AllocateMemory); D(BindBufferMemory);
   D(MapMemory); D(CreateBufferView); D(CreateShaderModule);
   D(CreateDescriptorSetLayout); D(CreatePipelineLayout);
   D(CreateComputePipelines); D(CreateDescriptorPool);
   D(AllocateDescriptorSets); D(UpdateDescriptorSets);
   D(AllocateCommandBuffers); D(BeginCommandBuffer); D(EndCommandBuffer);
   D(CmdBindPipeline); D(CmdBindDescriptorSets); D(CmdDispatch);
   D(CmdPipelineBarrier); D(QueueSubmit); D(CreateFence); D(WaitForFences);
   D(DeviceWaitIdle);
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

   /* src: 4 views of VIEW_BYTES each; out: 130 words. */
   VkBuffer src, out;
   VkDeviceMemory msrc, mout;
   VkBufferCreateInfo bc = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = 4 * VIEW_BYTES,
      .usage = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
               VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT};
   CreateBuffer(dev, &bc, NULL, &src);
   bc.size = 4096;
   bc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   CreateBuffer(dev, &bc, NULL, &out);
   VkMemoryRequirements mr;
   VkMemoryAllocateInfo ma = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                              .memoryTypeIndex = host};
   GetBufferMemoryRequirements(dev, src, &mr);
   ma.allocationSize = mr.size;
   AllocateMemory(dev, &ma, NULL, &msrc);
   BindBufferMemory(dev, src, msrc, 0);
   GetBufferMemoryRequirements(dev, out, &mr);
   ma.allocationSize = mr.size;
   AllocateMemory(dev, &ma, NULL, &mout);
   BindBufferMemory(dev, out, mout, 0);
   uint32_t *ps, *po;
   MapMemory(dev, msrc, 0, VK_WHOLE_SIZE, 0, (void **)&ps);
   MapMemory(dev, mout, 0, VK_WHOLE_SIZE, 0, (void **)&po);
   for (uint32_t i = 0; i < 4 * VIEW_BYTES / 4; i++)
      ps[i] = PATTERN(i * 4);
   /* RG32F view: in-range texels are small floats. */
   float *pf = (float *)(ps + 3 * VIEW_BYTES / 4);
   for (int i = 0; i < 8; i++)
      pf[i] = 1.0f + (float)i;
   memset(po, 0xcc, 4096);

   const VkFormat fmt[4] = {VK_FORMAT_R32_UINT, VK_FORMAT_R32_UINT,
                            VK_FORMAT_R8G8B8A8_UNORM,
                            VK_FORMAT_R32G32_SFLOAT};
   const uint32_t texel[4] = {4, 4, 4, 8};
   VkBufferView views[4];
   for (int v = 0; v < 4; v++) {
      VkBufferViewCreateInfo vi = {
         .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, .buffer = src,
         .format = fmt[v], .offset = v * VIEW_BYTES, .range = 4 * texel[v]};
      if (CreateBufferView(dev, &vi, NULL, &views[v])) {
         printf("FAILED: buffer view %d\n", v);
         return 2;
      }
   }

   VkDescriptorSetLayoutBinding lb[5] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
      {1, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
      {3, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
      {4, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT},
   };
   VkDescriptorSetLayoutCreateInfo li = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 5, .pBindings = lb};
   VkDescriptorSetLayout dsl;
   CreateDescriptorSetLayout(dev, &li, NULL, &dsl);
   VkPipelineLayoutCreateInfo pli = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &dsl};
   VkPipelineLayout pl;
   CreatePipelineLayout(dev, &pli, NULL, &pl);
   VkShaderModuleCreateInfo smi = {
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = sizeof(texel_oob_spv), .pCode = texel_oob_spv};
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

   VkDescriptorPoolSize ps2[3] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 3},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1}};
   VkDescriptorPoolCreateInfo dpi = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 1,
      .poolSizeCount = 3, .pPoolSizes = ps2};
   VkDescriptorPool dp;
   CreateDescriptorPool(dev, &dpi, NULL, &dp);
   VkDescriptorSetAllocateInfo dai = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl};
   VkDescriptorSet ds;
   AllocateDescriptorSets(dev, &dai, &ds);
   VkDescriptorBufferInfo obi = {out, 0, VK_WHOLE_SIZE};
   VkWriteDescriptorSet w[5] = {
      {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
       .dstBinding = 0, .descriptorCount = 1,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &obi},
   };
   for (int v = 0; v < 4; v++)
      w[1 + v] = (VkWriteDescriptorSet){
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = ds,
         .dstBinding = 1 + v, .descriptorCount = 1,
         .descriptorType = v == 1 ? VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER
                                  : VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
         .pTexelBufferView = &views[v]};
   UpdateDescriptorSets(dev, 5, w, 0, NULL);

   VkCommandPoolCreateInfo cpci = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool cp;
   CreateCommandPool(dev, &cpci, NULL, &cp);
   VkCommandBufferAllocateInfo ca = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = cp, .commandBufferCount = 1};
   VkCommandBuffer cb;
   AllocateCommandBuffers(dev, &ca, &cb);
   VkCommandBufferBeginInfo bi = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   BeginCommandBuffer(cb, &bi);
   CmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
   CmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0,
                         NULL);
   CmdDispatch(cb, 1, 1, 1);
   VkMemoryBarrier mb = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                         .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                         .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   CmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0, NULL);
   EndCommandBuffer(cb);
   VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   CreateFence(dev, &fi, NULL, &fence);
   VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                      .commandBufferCount = 1, .pCommandBuffers = &cb};
   r = QueueSubmit(q, 1, &si, fence);
   if (!r)
      r = WaitForFences(dev, 1, &fence, VK_TRUE, 10000000000ull);
   if (r) {
      printf("FAILED: submit/wait %d\n", r);
      return 2;
   }

   const uint32_t one_f = f2u(1.0f);
   unsigned bad_in = 0, bad_oob = 0, bad_store = 0;
   for (int i = 0; i < 8; i++) {
      int oob = i >= 4;
      unsigned before = bad;
      uint32_t want[4];

      /* R32_UINT uniform and storage: (x, 0, 0, 1). */
      uint32_t x = PATTERN(i * 4);
      uint32_t w0[4] = {oob ? 0 : x, 0, 0, 1};
      check("u_r32ui", i, &po[0 * 32 + i * 4], w0);
      uint32_t xs = PATTERN(VIEW_BYTES + i * 4);
      uint32_t w1[4] = {oob ? 0 : xs, 0, 0, 1};
      check("s_r32ui", i, &po[1 * 32 + i * 4], w1);

      /* R8G8B8A8_UNORM: bytes of the pattern / 255, OOB all 0. */
      uint32_t p = PATTERN(2 * VIEW_BYTES + i * 4);
      for (int k = 0; k < 4; k++)
         want[k] = oob ? 0 : f2u((float)((p >> (8 * k)) & 0xff) / 255.0f);
      check("u_rgba8", i, &po[2 * 32 + i * 4], want);

      /* R32G32_SFLOAT: (2i+1, 2i+2, 0, 1), OOB (0, 0, 0, 1). */
      uint32_t w3[4] = {oob ? 0 : f2u(1.0f + 2 * i), oob ? 0 : f2u(2.0f + 2 * i),
                        0, one_f};
      check("u_rg32f", i, &po[3 * 32 + i * 4], w3);

      if (bad != before) {
         if (oob)
            bad_oob += bad - before;
         else
            bad_in += bad - before;
      }
   }

   /* Stores 0..7, then +0x10000 at 1 and at 6 (6 is out of range). */
   uint32_t *sv = ps + VIEW_BYTES / 4;
   for (int i = 0; i < 8; i++) {
      uint32_t want = i < 4 ? 0x5500u + i : PATTERN(VIEW_BYTES + i * 4);
      if (i == 1)
         want += 0x10000;
      if (sv[i] != want) {
         printf("  BAD store         idx %d memory %08x want %08x\n", i, sv[i],
                want);
         bad_store++;
      }
   }
   printf("  atomic returns: in-range idx 1 = %08x (want 00005501), "
          "out-of-range idx 6 = %08x (value not checked)\n", po[128], po[129]);
   if (po[128] != 0x5501)
      bad_store++;

   int pass = !bad_in && !bad_oob && !bad_store;
   printf("TEXELOOB bad_in_range=%u bad_out_of_range=%u bad_store_atomic=%u "
          "verdict=%s\n", bad_in, bad_oob, bad_store, pass ? "PASS" : "FAIL");
   DeviceWaitIdle(dev);
   return pass ? 0 : 1;
}
