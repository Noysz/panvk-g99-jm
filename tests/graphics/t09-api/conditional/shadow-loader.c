#define main unused_visibility_main
#include "import_visibility.c"
#undef main
#include <stdbool.h>

#define VERTICES 160u
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
       (components != 1 && components != 2 && components != 4) ||
       length > 256 || stride > 64 || stride % 4) return 1;
   const char *path = getenv("PANVK_ICD_SO");
   if (!path) return 1;
   void *lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
   if (!lib) return 1;
   PFN_vkGetInstanceProcAddr gpa = dlsym(lib, "vkGetInstanceProcAddr");
   if (!gpa) return 1;
   PFN_vkCreateInstance CreateInstance = (PFN_vkCreateInstance)gpa(NULL, "vkCreateInstance");
   VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "vertex_fetch", .apiVersion = VK_API_VERSION_1_3};
   VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo = &app};
   VkInstance instance;
   VK_CHECK(CreateInstance(&ici, NULL, &instance));
   LOAD_I(EnumeratePhysicalDevices); LOAD_I(GetPhysicalDeviceFeatures2);
   LOAD_I(GetPhysicalDeviceMemoryProperties); LOAD_I(CreateDevice);
   LOAD_I(GetDeviceProcAddr); LOAD_I(DestroyInstance);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   VK_CHECK(EnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   VkPhysicalDeviceConditionalRenderingFeaturesEXT cr = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_CONDITIONAL_RENDERING_FEATURES_EXT};
   VkPhysicalDeviceRobustness2FeaturesEXT rb = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT, .pNext = &cr};
   VkPhysicalDeviceDynamicRenderingFeatures dr = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES, .pNext = &rb};
   VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &dr};
   GetPhysicalDeviceFeatures2(physical, &features);
   const bool local = getenv("T09_COND_LOCAL") != NULL;
   const bool bypass = getenv("T09_COND_BYPASS") != NULL;
   printf("CONDITIONAL_FEATURES inherited=%u conditional=%u robust2=%u null=%u dynamic=%u stores=%u robust1=%u local=%u bypass=%u\n",
          cr.inheritedConditionalRendering, cr.conditionalRendering, rb.robustBufferAccess2,
          rb.nullDescriptor, dr.dynamicRendering, features.features.vertexPipelineStoresAndAtomics,
          features.features.robustBufferAccess, local, bypass);
   if ((!local && !cr.inheritedConditionalRendering) || !cr.conditionalRendering || !rb.robustBufferAccess2 || !rb.nullDescriptor || !dr.dynamicRendering ||
       !features.features.vertexPipelineStoresAndAtomics || !features.features.robustBufferAccess) return 77;
   cr.inheritedConditionalRendering = !local;
   rb.robustImageAccess2 = VK_FALSE;
   rb.robustBufferAccess2 = getenv("T09_NULL_ONLY") == NULL;
   rb.nullDescriptor = getenv("T09_NO_NULL") == NULL;
   if (!length && !rb.nullDescriptor) return 1;
   VkPhysicalDeviceFeatures enable = {.robustBufferAccess = getenv("T09_NO_ROBUST") == NULL,
      .vertexPipelineStoresAndAtomics = VK_TRUE};
   if (getenv("T09_NO_ROBUST") && rb.robustBufferAccess2) return 1;
   float priority = 1;
   VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = 0, .queueCount = 1, .pQueuePriorities = &priority};
   const char *extensions[] = {VK_EXT_ROBUSTNESS_2_EXTENSION_NAME, VK_EXT_CONDITIONAL_RENDERING_EXTENSION_NAME};
   VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &dr, .pEnabledFeatures = &enable,
      .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci,
      .enabledExtensionCount = 2, .ppEnabledExtensionNames = extensions};
   VkDevice device;
   VK_CHECK(CreateDevice(physical, &dci, NULL, &device));
   printf("FEATURES robust1=%u robust2=%u null=%u\n", enable.robustBufferAccess,
          rb.robustBufferAccess2, rb.nullDescriptor);
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
   LOAD_D(CmdBindDescriptorSets); LOAD_D(CmdBindVertexBuffers); LOAD_D(CmdDraw);
   LOAD_D(CmdPipelineBarrier); LOAD_D(CreateFence); LOAD_D(DestroyFence);
   LOAD_D(CmdBeginConditionalRenderingEXT); LOAD_D(CmdEndConditionalRenderingEXT);
   LOAD_D(CmdDrawIndirect); LOAD_D(ResetFences); LOAD_D(CmdExecuteCommands);
   LOAD_D(QueueSubmit); LOAD_D(WaitForFences); LOAD_D(DestroyDevice);
   VkQueue queue;
   GetDeviceQueue(device, 0, 0, &queue);
   VkPhysicalDeviceMemoryProperties mp;
   GetPhysicalDeviceMemoryProperties(physical, &mp);
   VkBuffer buffers[3];
   VkDeviceMemory memory[3];
   uint32_t *maps[3];
   for (unsigned i = 0; i < 3; i++) {
      VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
         .size = i == 2 ? 64 : i ? OUT_WORDS * 4 : (length ? length : 4),
         .usage = i == 2 ? VK_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT : i ? VK_BUFFER_USAGE_STORAGE_BUFFER_BIT : VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
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
   for (unsigned i = 0; i < length / 4; i++) {
      if (kind == 'f') {
         float value = (float)(i + 10);
         memcpy(&maps[0][i], &value, 4);
      } else {
         maps[0][i] = i + 10;
      }
   }
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
   VkFormat formats[3][3] = {
      {VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
      {VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32A32_SINT},
      {VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32A32_UINT}};
   VkVertexInputBindingDescription vb = {0, stride, VK_VERTEX_INPUT_RATE_VERTEX};
   VkVertexInputAttributeDescription attr = {0, 0,
      formats[kind == 'f' ? 0 : kind == 'i' ? 1 : 2][components == 1 ? 0 : components == 2 ? 1 : 2], 0};
   VkPipelineVertexInputStateCreateInfo vi = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1, .pVertexBindingDescriptions = &vb,
      .vertexAttributeDescriptionCount = 1, .pVertexAttributeDescriptions = &attr};
   VkPipelineInputAssemblyStateCreateInfo ia = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST};
   VkPipelineRasterizationStateCreateInfo rs = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .rasterizerDiscardEnable = VK_TRUE, .lineWidth = 1};
   VkPipelineRenderingCreateInfo pri = {.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
   VkGraphicsPipelineCreateInfo gpci = {.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &pri, .stageCount = 1, .pStages = &stage, .pVertexInputState = &vi,
      .pInputAssemblyState = &ia, .pRasterizationState = &rs, .layout = layout};
   VkPipeline pipeline;
   VK_CHECK(CreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, NULL, &pipeline));
   VkCommandPoolCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
   VkCommandPool pool;
   VK_CHECK(CreateCommandPool(device, &cpci, NULL, &pool));
   VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   VK_CHECK(AllocateCommandBuffers(device, &cai, &cmd));
   VkCommandBuffer secondary[2];
   cai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
   cai.commandBufferCount = 2;
   VK_CHECK(AllocateCommandBuffers(device, &cai, secondary));
   VkCommandBufferInheritanceConditionalRenderingInfoEXT inherited_condition = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_CONDITIONAL_RENDERING_INFO_EXT,
      .conditionalRenderingEnable = !local};
   VkCommandBufferInheritanceRenderingInfo inherited_rendering = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_RENDERING_INFO,
      .pNext = &inherited_condition, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
   VkCommandBufferInheritanceInfo inheritance = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
      .pNext = &inherited_rendering};
   VkCommandBufferBeginInfo secondary_begin = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
      .pInheritanceInfo = &inheritance};
   const bool inverted = getenv("T09_COND_INVERTED") != NULL;
   const bool indirect = getenv("T09_COND_INDIRECT") != NULL;
   maps[2][8] = 40; maps[2][9] = 1; maps[2][10] = 0; maps[2][11] = 0;
   VkBuffer bound = length ? buffers[0] : VK_NULL_HANDLE;
   VkDeviceSize offset = 0;
   VkConditionalRenderingBeginInfoEXT condition = {
      .sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT,
      .buffer = buffers[2], .offset = 16,
      .flags = inverted ? VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT : 0};
   VkConditionalRenderingBeginInfoEXT own_condition = condition;
   own_condition.offset = 0;
   for (unsigned i = 0; i < 2; i++) {
      VK_CHECK(BeginCommandBuffer(secondary[i], &secondary_begin));
      CmdBindPipeline(secondary[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
      CmdBindDescriptorSets(secondary[i], VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, NULL);
      CmdBindVertexBuffers(secondary[i], 0, 1, &bound, &offset);
      if (i == 0) {
         CmdBeginConditionalRenderingEXT(secondary[i], &own_condition);
         if (indirect) CmdDrawIndirect(secondary[i], buffers[2], 32, 1, 16);
         else CmdDraw(secondary[i], 40, 1, 0, 0);
         CmdEndConditionalRenderingEXT(secondary[i]);
         if (local) CmdBeginConditionalRenderingEXT(secondary[i], &condition);
         CmdDraw(secondary[i], 40, 1, 40, 0);
         if (local) CmdEndConditionalRenderingEXT(secondary[i]);
      } else {
         if (local) CmdBeginConditionalRenderingEXT(secondary[i], &condition);
         CmdDraw(secondary[i], 80, 1, 80, 0);
         if (local) CmdEndConditionalRenderingEXT(secondary[i]);
      }
      VK_CHECK(EndCommandBuffer(secondary[i]));
   }
   VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VK_CHECK(BeginCommandBuffer(cmd, &begin));
   VkRenderingInfo rendering = {.sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT,
      .renderArea = {{0, 0}, {1, 1}}, .layerCount = 1};
   if (!local && !bypass) CmdBeginConditionalRenderingEXT(cmd, &condition);
   CmdBeginRendering(cmd, &rendering);
   CmdExecuteCommands(cmd, 1, &secondary[0]);
   CmdEndRendering(cmd);
   CmdBeginRendering(cmd, &rendering);
   CmdExecuteCommands(cmd, 1, &secondary[1]);
   CmdEndRendering(cmd);
   if (!local && !bypass) CmdEndConditionalRenderingEXT(cmd);
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
   unsigned bad = 0, guard_bad = 0;
   const uint32_t predicates[] = {1, 0, 0, 1, 0x80000000u, 0, 2, 0, 0xffffffffu, 0, 1, 1};
   for (unsigned iteration = 0; iteration < sizeof(predicates) / sizeof(predicates[0]); iteration++) {
   maps[2][4] = predicates[iteration];
   for (unsigned i = 0; i < OUT_WORDS; i++) maps[1][i] = GUARD;
   VK_CHECK(ResetFences(device, 1, &fence));
   VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
   VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, UINT64_C(10000000000)));
   for (unsigned v = 0; v < VERTICES; v++) {
      uint32_t expected[4] = {0, 0, 0, components < 4 ? (kind == 'f' ? 0x3f800000u : 1u) : 0};
      bool inbound = length && v * stride + components * 4 <= length;
      if (inbound) for (unsigned c = 0; c < components; c++) expected[c] = maps[0][v * stride / 4 + c];
      if (v < 40 ? inverted : ((predicates[iteration] != 0) == inverted))
         for (unsigned c = 0; c < 4; c++) expected[c] = GUARD;
      uint32_t *got = maps[1] + (v + 1) * 4;
      unsigned row_bad = 0;
      for (unsigned c = 0; c < 4; c++) row_bad += got[c] != expected[c];
      bad += row_bad;
      if (row_bad) printf("VERTEX index=%u inbound=%u got=%08x,%08x,%08x,%08x expected=%08x,%08x,%08x,%08x bad=%u\n",
         v, inbound, got[0], got[1], got[2], got[3], expected[0], expected[1], expected[2], expected[3], row_bad);
   }
   for (unsigned i = 0; i < OUT_WORDS; i++)
      if (i < 4 || i >= (VERTICES + 1) * 4) guard_bad += maps[1][i] != GUARD;
   for (unsigned i = 0; i < 16; i++)
      if (i != 4 && (i < 8 || i >= 12)) guard_bad += maps[2][i] != 0x7b7b7b7bu;
   printf("CONDITIONAL iteration=%u predicate=%u inverted=%u indirect=%u bad=%u guard_bad=%u\n",
          iteration, predicates[iteration], inverted, indirect, bad, guard_bad);
   if (bad || guard_bad) break;
   }
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
