#define main unused_visibility_main
#include "import_visibility.c"
#undef main
#include <stdbool.h>

static unsigned option(const char *name, unsigned fallback)
{
   const char *text = getenv(name);
   return text ? strtoul(text, NULL, 10) : fallback;
}

#define VERTICES 80u
#define OUT_WORDS ((VERTICES + 4u) * 4u)
#define GUARD 0x6ac35719u

int main(int argc, char **argv)
{
   if (argc != 6) return 1;
   char kind = argv[1][0];
   unsigned components = strtoul(argv[2], NULL, 10);
   unsigned length = strtoul(argv[3], NULL, 10);
   unsigned stride = strtoul(argv[4], NULL, 10);
   if ((kind != 'f' && kind != 'i' && kind != 'u') ||
       (components < 1 || components > 4) ||
       length > 256 || stride > 64 || stride % 4) return 1;
   unsigned bits = option("T09_BITS", 32);
   unsigned mode = option("T09_MODE", 0);
   unsigned divisor = option("T09_DIVISOR", 1);
   unsigned bound_offset = option("T09_BOUND_OFFSET", 0);
   unsigned attr_offset = option("T09_ATTR_OFFSET", 0);
   bool dynamic = option("T09_DYNAMIC", 0);
   bool io16 = option("T09_IO16", 0);
   if ((bits != 8 && bits != 16 && bits != 32) || mode > 2 ||
       divisor > 7 || bound_offset > 32 || attr_offset > 32 ||
       bound_offset % (bits / 8) || attr_offset % (bits / 8)) return 1;
   const char *path = getenv("PANVK_ICD_SO");
   if (!path) return 1;
   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!lib) return 1;
   PFN_vkGetInstanceProcAddr gpa = dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (!gpa) return 1;
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vertex_fetch", .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app};
   VkInstance instance;
   VK_CHECK(CreateInstance(&ici, NULL, &instance));
   LOAD_I(EnumeratePhysicalDevices); LOAD_I(GetPhysicalDeviceFeatures2); LOAD_I(GetPhysicalDeviceFormatProperties);
   LOAD_I(GetPhysicalDeviceMemoryProperties); LOAD_I(CreateDevice);
   LOAD_I(GetDeviceProcAddr); LOAD_I(DestroyInstance);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   VK_CHECK(EnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT div_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT};
   VkPhysicalDeviceVertexInputDynamicStateFeaturesEXT dyn_features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_INPUT_DYNAMIC_STATE_FEATURES_EXT,
      .pNext = &div_features};
   VkPhysicalDeviceShaderFloat16Int8Features arithmetic16 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
      .pNext = &dyn_features};
   VkPhysicalDevice16BitStorageFeatures storage16 = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES,
      .pNext = &arithmetic16};
   VkPhysicalDeviceRobustness2FeaturesEXT rb = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .pNext = &storage16};
   VkPhysicalDeviceDynamicRenderingFeatures dr = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, .pNext = &rb};
   VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &dr};
   GetPhysicalDeviceFeatures2(physical, &features);
   if (!rb.robustBufferAccess2 || !rb.nullDescriptor || !dr.dynamicRendering ||
       !features.features.vertexPipelineStoresAndAtomics || !features.features.robustBufferAccess) return 77;
   if ((mode == 2 && (!div_features.vertexAttributeInstanceRateDivisor ||
       (!divisor && !div_features.vertexAttributeInstanceRateZeroDivisor))) ||
       (dynamic && !dyn_features.vertexInputDynamicState) ||
       (io16 && (!storage16.storageInputOutput16 ||
        (kind == 'f' ? !arithmetic16.shaderFloat16 : !features.features.shaderInt16)))) return 77;
   div_features.vertexAttributeInstanceRateDivisor = mode == 2;
   div_features.vertexAttributeInstanceRateZeroDivisor = mode == 2 && divisor == 0;
   dyn_features.vertexInputDynamicState = dynamic;
   storage16.storageBuffer16BitAccess = false;
   storage16.uniformAndStorageBuffer16BitAccess = false;
   storage16.storagePushConstant16 = false;
   storage16.storageInputOutput16 = io16;
   arithmetic16.shaderFloat16 = io16 && kind == 'f';
   arithmetic16.shaderInt8 = false;
   arithmetic16.pNext = dynamic ? (void *)&dyn_features :
                      mode == 2 ? (void *)&div_features : NULL;
   storage16.pNext = &arithmetic16;
   dyn_features.pNext = mode == 2 ? &div_features : NULL;
   rb.robustImageAccess2 = VK_FALSE;
   VkPhysicalDeviceFeatures enable = {.robustBufferAccess = VK_TRUE,
      .vertexPipelineStoresAndAtomics = VK_TRUE, .shaderInt16 = io16 && kind != 'f'};
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
   const char *extensions[3] = {VK_EXT_ROBUSTNESS_2_EXTENSION_NAME};
   unsigned extension_count = 1;
   if (mode == 2) extensions[extension_count++] = VK_EXT_VERTEX_ATTRIBUTE_DIVISOR_EXTENSION_NAME;
   if (dynamic) extensions[extension_count++] = VK_EXT_VERTEX_INPUT_DYNAMIC_STATE_EXTENSION_NAME;
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &dr, .pEnabledFeatures = &enable,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
      .enabledExtensionCount = extension_count, .ppEnabledExtensionNames = extensions};
   VkDevice device;
   VK_CHECK(CreateDevice(physical, &dci, NULL, &device));
   LOAD_D(GetDeviceQueue); LOAD_D(CreateBuffer); LOAD_D(DestroyBuffer);
   LOAD_D(GetBufferMemoryRequirements); LOAD_D(AllocateMemory); LOAD_D(FreeMemory);
   LOAD_D(BindBufferMemory); LOAD_D(MapMemory); LOAD_D(UnmapMemory);
   LOAD_D(CreateDescriptorSetLayout); LOAD_D(DestroyDescriptorSetLayout);
   LOAD_D(CreateDescriptorPool); LOAD_D(DestroyDescriptorPool);
   LOAD_D(AllocateDescriptorSets); LOAD_D(UpdateDescriptorSets);
   LOAD_D(CreatePipelineLayout); LOAD_D(DestroyPipelineLayout);
   LOAD_D(CreateShaderModule); LOAD_D(DestroyShaderModule);
   LOAD_D(CreateGraphicsPipelines); LOAD_D(DestroyPipeline);
   LOAD_D(CreateCommandPool); LOAD_D(DestroyCommandPool);
   LOAD_D(AllocateCommandBuffers); LOAD_D(BeginCommandBuffer); LOAD_D(EndCommandBuffer);
   LOAD_D(CmdBeginRendering); LOAD_D(CmdEndRendering); LOAD_D(CmdBindPipeline);
   LOAD_D(CmdBindDescriptorSets); LOAD_D(CmdDraw);
   LOAD_D(CmdPipelineBarrier); LOAD_D(CreateFence); LOAD_D(DestroyFence);
   LOAD_D(QueueSubmit); LOAD_D(WaitForFences); LOAD_D(DestroyDevice);
   LOAD_D(CmdBindVertexBuffers2); LOAD_D(CmdBindIndexBuffer); LOAD_D(CmdDrawIndexed);
   PFN_vkCmdSetVertexInputEXT CmdSetVertexInputEXT = dynamic ?
      (PFN_vkCmdSetVertexInputEXT)GetDeviceProcAddr(device, "vkCmdSetVertexInputEXT") : NULL;
   if (dynamic && !CmdSetVertexInputEXT) return 1;
   VkQueue queue;
   GetDeviceQueue(device, 0, 0, &queue);
   VkPhysicalDeviceMemoryProperties mp;
   GetPhysicalDeviceMemoryProperties(physical, &mp);
   VkBuffer buffers[3];
   VkDeviceMemory memory[3];
   uint32_t *maps[3];
   for (unsigned i = 0; i < 3; i++) {
      VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = i == 1 ? OUT_WORDS * 4 : i == 2 ? VERTICES * 4 : 512,
         .usage = i == 1 ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : i == 2 ? VK_BUFFER_USAGE_INDEX_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
      VK_CHECK(CreateBuffer(device, &bci, NULL, &buffers[i]));
      VkMemoryRequirements mr;
      GetBufferMemoryRequirements(device, buffers[i], &mr);
      uint32_t type = choose_type(&mp, mr.memoryTypeBits, 0, 0);
      if (type == UINT32_MAX) return 77;
      VkMemoryAllocateInfo mai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
         .allocationSize = mr.size, .memoryTypeIndex = type};
      VK_CHECK(AllocateMemory(device, &mai, NULL, &memory[i]));
      VK_CHECK(BindBufferMemory(device, buffers[i], memory[i], 0));
      void *mapping;
      VK_CHECK(MapMemory(device, memory[i], 0, VK_WHOLE_SIZE, 0, &mapping));
      maps[i] = mapping;
      memset(mapping, 0x7b, mr.size);
   }
   for (unsigned i = 0; i < 512 / (bits / 8); i++) {
      if (bits == 8) ((uint8_t *)maps[0])[i] = (i & 1) ? 255 : 0;
      else if (bits == 16) {
         const uint16_t halves[] = {0x3c00, 0x4000, 0x4200, 0x4400};
         ((uint16_t *)maps[0])[i] = kind == 'f' ? halves[i % 4] : 0xf000 + i;
      } else if (kind == 'f') {
         float value = (float)(i + 10);
         memcpy(&maps[0][i], &value, 4);
      } else maps[0][i] = kind == 'i' ? (uint32_t)(-10 - (int)i) : i + 10;
   }
   for (unsigned i = 0; i < VERTICES; i++) maps[2][i] = VERTICES - 1 - i + 5;
   for (unsigned i = 0; i < OUT_WORDS; i++) maps[1][i] = GUARD;
   VkDescriptorSetLayoutBinding binding = {.binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT};
   VkDescriptorSetLayoutCreateInfo slci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 1, .pBindings = &binding};
   VkDescriptorSetLayout set_layout;
   VK_CHECK(CreateDescriptorSetLayout(device, &slci, NULL, &set_layout));
   VkDescriptorPoolSize pool_size = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
   VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size};
   VkDescriptorPool descriptors;
   VK_CHECK(CreateDescriptorPool(device, &dpci, NULL, &descriptors));
   VkDescriptorSetAllocateInfo dsai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptors, .descriptorSetCount = 1, .pSetLayouts = &set_layout};
   VkDescriptorSet set;
   VK_CHECK(AllocateDescriptorSets(device, &dsai, &set));
   VkDescriptorBufferInfo dbi = {.buffer = buffers[1], .offset = 0, .range = OUT_WORDS * 4};
   VkWriteDescriptorSet write = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      .pBufferInfo = &dbi};
   UpdateDescriptorSets(device, 1, &write, 0, NULL);
   VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &set_layout};
   VkPipelineLayout layout;
   VK_CHECK(CreatePipelineLayout(device, &plci, NULL, &layout));
   FILE *file = fopen(argv[5], "rb");
   if (!file || fseek(file, 0, SEEK_END)) return 1;
   long bytes = ftell(file);
   if (bytes <= 0 || bytes % 4) return 1;
   rewind(file);
   uint32_t *code = malloc((size_t)bytes);
   if (!code || fread(code, 1, (size_t)bytes, file) != (size_t)bytes) return 1;
   fclose(file);
   VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = (size_t)bytes, .pCode = code};
   VkShaderModule shader;
   VK_CHECK(CreateShaderModule(device, &smci, NULL, &shader));
   free(code);
   VkPipelineShaderStageCreateInfo stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = shader, .pName = "main"};
   const VkFormat formats[3][3][4] = {
      {{VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM},
       {VK_FORMAT_R8_SINT, VK_FORMAT_R8G8_SINT, VK_FORMAT_R8G8B8_SINT, VK_FORMAT_R8G8B8A8_SINT},
       {VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT}},
      {{VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
       {VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT},
       {VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT}},
      {{VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
       {VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT},
       {VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT}}};
   VkFormat format = formats[bits == 8 ? 0 : bits == 16 ? 1 : 2][kind == 'f' ? 0 : kind == 'i' ? 1 : 2][components - 1];
   VkFormatProperties properties;
   GetPhysicalDeviceFormatProperties(physical, format, &properties);
   if (!(properties.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)) return 77;
   VkVertexInputBindingDescription vb = {0, stride, mode == 2 ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
   VkVertexInputAttributeDescription attr = {0, 0, format, attr_offset};
   VkVertexInputBindingDivisorDescriptionEXT div_binding = {0, divisor};
   VkPipelineVertexInputDivisorStateCreateInfoEXT div_state = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT,
      .vertexBindingDivisorCount = 1, .pVertexBindingDivisors = &div_binding};
   VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .pNext = mode == 2 ? &div_state : NULL,
      .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
      .vertexAttributeDescriptionCount = 1, .pVertexAttributeDescriptions = &attr};
   VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
   VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .rasterizerDiscardEnable = VK_TRUE, .lineWidth = 1};
   VkPipelineRenderingCreateInfo pri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
   VkDynamicState dyn_state = VK_DYNAMIC_STATE_VERTEX_INPUT_EXT;
   VkPipelineDynamicStateCreateInfo dyn_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = 1, .pDynamicStates = &dyn_state};
   VkGraphicsPipelineCreateInfo gpci = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pri, .stageCount = 1, .pStages = &stage, .pVertexInputState = &vi,
      .pInputAssemblyState = &ia, .pRasterizationState = &rs, .layout = layout, .pDynamicState = dynamic ? &dyn_info : NULL};
   VkPipeline pipeline;
   VK_CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));
   VkCommandPoolCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool pool;
   VK_CHECK(CreateCommandPool(device, &cpci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   VK_CHECK(AllocateCommandBuffers(device, &cai, &cmd));
   VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VK_CHECK(BeginCommandBuffer(cmd, &begin));
   VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea = {{0, 0}, {1, 1}}, .layerCount = 1};
   CmdBeginRendering(cmd, &rendering);
   CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
   CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
   VkBuffer bound = length ? buffers[0] : VK_NULL_HANDLE;
   VkDeviceSize offset = length ? bound_offset : 0;
   VkDeviceSize range = length ? length : VK_WHOLE_SIZE;
   if (dynamic) {
      VkVertexInputBindingDescription2EXT db = {
         .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT,
         .binding = 0, .stride = stride, .inputRate = vb.inputRate,
         .divisor = mode == 2 ? divisor : 1};
      VkVertexInputAttributeDescription2EXT da = {
         .sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT,
         .location = 0, .binding = 0, .format = format, .offset = attr_offset};
      CmdSetVertexInputEXT(cmd, 1, &db, 1, &da);
   }
   CmdBindVertexBuffers2(cmd, 0, 1, &bound, &offset, &range, NULL);
   if (mode == 1) {
      CmdBindIndexBuffer(cmd, buffers[2], 0, VK_INDEX_TYPE_UINT32);
      CmdDrawIndexed(cmd, VERTICES, 1, 0, -5, 0);
   } else if (mode == 2) CmdDraw(cmd, 1, VERTICES, 0, 0);
   else CmdDraw(cmd, VERTICES, 1, 0, 0);
   CmdEndRendering(cmd);
   VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
   CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      0, 1, &barrier, 0, NULL, 0, NULL);
   VK_CHECK(EndCommandBuffer(cmd));
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VK_CHECK(CreateFence(device, &fci, NULL, &fence));
   VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
      .commandBufferCount = 1, .pCommandBuffers = &cmd};
   VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
   VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
   unsigned bad = 0, guard_bad = 0;
   for (unsigned v = 0; v < VERTICES; v++) {
      uint32_t expected[4] = {0, 0, 0, components < 4 ? (kind == 'f' ? 0x3f800000u : 1u) : 0};
      unsigned element = mode == 2 ? (divisor ? v / divisor : 0) : v;
      unsigned location = attr_offset + element * stride;
      bool inbound = length && location + components * (bits / 8) <= length;
      if (inbound) for (unsigned c = 0; c < components; c++) {
         unsigned byte = bound_offset + location + c * (bits / 8);
         if (bits == 32) expected[c] = maps[0][byte / 4];
         else if (kind == 'f') {
            float val = bits == 8 ? (float)((uint8_t *)maps[0])[byte] / 255.0f :
               (float)((byte / 2) % 4 + 1);
            memcpy(&expected[c], &val, 4);
         } else if (bits == 8) expected[c] = kind == 'i' ?
            (uint32_t)(int32_t)((int8_t *)maps[0])[byte] : ((uint8_t *)maps[0])[byte];
         else expected[c] = kind == 'i' ?
            (uint32_t)(int32_t)((int16_t *)maps[0])[byte / 2] : ((uint16_t *)maps[0])[byte / 2];
      }
      uint32_t *got = maps[1] + (v + 1) * 4;
      unsigned row_bad = 0;
      for (unsigned c = 0; c < 4; c++) row_bad += got[c] != expected[c];
      bad += row_bad;
      if (row_bad || v < 2) printf("VERTEX index=%u inbound=%u got=%08x,%08x,%08x,%08x expected=%08x,%08x,%08x,%08x bad=%u\n",
         v, inbound, got[0], got[1], got[2], got[3], expected[0], expected[1], expected[2], expected[3], row_bad);
   }
   for (unsigned i = 0; i < OUT_WORDS; i++)
      if (i < 4 || i >= (VERTICES + 1) * 4) guard_bad += maps[1][i] != GUARD;
   DestroyFence(device, fence, NULL);
   DestroyCommandPool(device, pool, NULL);
   DestroyPipeline(device, pipeline, NULL);
   DestroyShaderModule(device, shader, NULL);
   DestroyPipelineLayout(device, layout, NULL);
   DestroyDescriptorPool(device, descriptors, NULL);
   DestroyDescriptorSetLayout(device, set_layout, NULL);
   for (unsigned i = 0; i < 3; i++) {
      UnmapMemory(device, memory[i]);
      DestroyBuffer(device, buffers[i], NULL);
      FreeMemory(device, memory[i], NULL);
   }
   DestroyDevice(device, NULL);
   DestroyInstance(instance, NULL);
   dlclose(lib);
   printf("FETCH kind=%c components=%u length=%u stride=%u bad=%u guard_bad=%u verdict=%s\n",
      kind, components, length, stride, bad, guard_bad, (bad || guard_bad) ? "FAIL" : "PASS");
   return (bad || guard_bad) ? 2 : 0;
}
