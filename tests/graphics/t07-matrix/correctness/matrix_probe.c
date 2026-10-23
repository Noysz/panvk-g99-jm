#define main visibility_unused_main
#include "import_visibility.c"
#undef main
#include <time.h>

static double now(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return t.tv_sec + t.tv_nsec * 1e-9;
}

static uint32_t matrix_element(unsigned instance, unsigned kind, unsigned row,
                               unsigned col, unsigned round)
{
   return 1 + (instance * 23 + kind * 19 + row * 7 + col * 3 + round * 5) % 29;
}

static uint32_t initial_word(unsigned round, unsigned word, int row_major)
{
   if (word >= 384) return pattern(round, word);
   unsigned elem = word % 16;
   unsigned row = row_major ? elem / 4 : elem % 4;
   unsigned col = row_major ? elem % 4 : elem / 4;
   float value = matrix_element(word / 48, word / 16 % 3, row, col, round);
   uint32_t bits;
   memcpy(&bits, &value, sizeof(bits));
   return bits;
}

int main(int argc, char **argv)
{
   if (argc != 3) return 1;
   int row_major = !strcmp(argv[2], "row");
   if (!row_major && strcmp(argv[2], "column")) return 1;
   unsigned values = 96;
   unsigned words = values * 64 + 64 + 16;
   size_t bytes = words * sizeof(uint32_t);
   FILE *spv = fopen(argv[1], "rb");
   if (!spv) return 1;
   if (fseek(spv, 0, SEEK_END)) return 1;
   long spvsize = ftell(spv);
   if (spvsize <= 0 || spvsize % 4) return 1;
   rewind(spv);
   uint32_t *code = malloc(spvsize);
   if (!code || fread(code, 1, spvsize, spv) != (size_t)spvsize) return 1;
   fclose(spv);
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
   LOAD_I(GetPhysicalDeviceProperties); LOAD_I(CreateDevice);
   LOAD_I(GetDeviceProcAddr); LOAD_I(DestroyInstance);
   uint32_t count = 1;
   VkPhysicalDevice physical;
   VK_CHECK(EnumeratePhysicalDevices(instance, &count, &physical));
   if (!count) return 1;
   VkPhysicalDeviceMemoryProperties mp;
   VkPhysicalDeviceProperties props;
   GetPhysicalDeviceMemoryProperties(physical, &mp);
   GetPhysicalDeviceProperties(physical, &props);
   printf("DEVICE %s vendor=%x device=%x\n", props.deviceName, props.vendorID, props.deviceID);
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
   LOAD_D(CreateDescriptorSetLayout); LOAD_D(DestroyDescriptorSetLayout);
   LOAD_D(CreateDescriptorPool); LOAD_D(DestroyDescriptorPool);
   LOAD_D(AllocateDescriptorSets); LOAD_D(UpdateDescriptorSets);
   LOAD_D(CreateShaderModule); LOAD_D(DestroyShaderModule);
   LOAD_D(CreatePipelineLayout); LOAD_D(DestroyPipelineLayout);
   LOAD_D(CreateComputePipelines); LOAD_D(DestroyPipeline);
   LOAD_D(CreateCommandPool); LOAD_D(DestroyCommandPool); LOAD_D(AllocateCommandBuffers);
   LOAD_D(BeginCommandBuffer); LOAD_D(EndCommandBuffer);
   LOAD_D(CmdPipelineBarrier); LOAD_D(CmdBindPipeline);
   LOAD_D(CmdBindDescriptorSets); LOAD_D(CmdPushConstants); LOAD_D(CmdDispatch);
   LOAD_D(CreateFence); LOAD_D(DestroyFence); LOAD_D(ResetFences); LOAD_D(WaitForFences);
   LOAD_D(QueueSubmit); LOAD_D(DeviceWaitIdle); LOAD_D(DestroyDevice);
   VkQueue queue;
   GetDeviceQueue(device, 0, 0, &queue);
   VkBufferCreateInfo bci = {.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = bytes, .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
   VkBuffer buffer;
   VK_CHECK(CreateBuffer(device, &bci, NULL, &buffer));
   VkMemoryRequirements mr;
   GetBufferMemoryRequirements(device, buffer, &mr);
   VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .allocationSize = mr.size, .memoryTypeIndex = choose_type(&mp, mr.memoryTypeBits, 0, 0)};
   if (ai.memoryTypeIndex == UINT32_MAX) return 77;
   VkDeviceMemory memory;
   VK_CHECK(AllocateMemory(device, &ai, NULL, &memory));
   VK_CHECK(BindBufferMemory(device, buffer, memory, 0));
   void *mapping;
   VK_CHECK(MapMemory(device, memory, 0, VK_WHOLE_SIZE, 0, &mapping));
   volatile uint32_t *cpu = mapping;
   VkDescriptorSetLayoutBinding bindings[2] = {
      {.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
      {.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
       .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
   };
   VkDescriptorSetLayoutCreateInfo slci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .bindingCount = 2, .pBindings = bindings};
   VkDescriptorSetLayout setlayout;
   VK_CHECK(CreateDescriptorSetLayout(device, &slci, NULL, &setlayout));
   VkDescriptorPoolSize psizes[] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1},
   };
   VkDescriptorPoolCreateInfo dpci = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = psizes};
   VkDescriptorPool descriptorpool;
   VK_CHECK(CreateDescriptorPool(device, &dpci, NULL, &descriptorpool));
   VkDescriptorSetAllocateInfo dsai = {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = descriptorpool, .descriptorSetCount = 1, .pSetLayouts = &setlayout};
   VkDescriptorSet set;
   VK_CHECK(AllocateDescriptorSets(device, &dsai, &set));
   VkDescriptorBufferInfo bi = {.buffer = buffer, .range = bytes};
   VkWriteDescriptorSet wr = {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi};
   UpdateDescriptorSets(device, 1, &wr, 0, NULL);
   VkDescriptorBufferInfo ubi = {.buffer = buffer, .offset = 0, .range = 1536};
   wr.dstBinding = 1;
   wr.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   wr.pBufferInfo = &ubi;
   UpdateDescriptorSets(device, 1, &wr, 0, NULL);
   VkPushConstantRange pc = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
   VkPipelineLayoutCreateInfo plci = {.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1, .pSetLayouts = &setlayout,
      .pushConstantRangeCount = 1, .pPushConstantRanges = &pc};
   VkPipelineLayout layout;
   VK_CHECK(CreatePipelineLayout(device, &plci, NULL, &layout));
   VkShaderModuleCreateInfo smci = {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = spvsize, .pCode = code};
   VkShaderModule module;
   VK_CHECK(CreateShaderModule(device, &smci, NULL, &module));
   free(code);
   VkComputePipelineCreateInfo cpci = {.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main"},
      .layout = layout};
   VkPipeline pipeline;
   double start = now();
   VK_CHECK(CreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, NULL, &pipeline));
   printf("COMPILE values=%u seconds=%.6f\n", values, now() - start);
   fflush(stdout);
   if (getenv("MATRIX_COMPILE_ONLY")) {
      DestroyPipeline(device, pipeline, NULL);
      DestroyShaderModule(device, module, NULL);
      DestroyPipelineLayout(device, layout, NULL);
      DestroyDescriptorPool(device, descriptorpool, NULL);
      DestroyDescriptorSetLayout(device, setlayout, NULL);
      UnmapMemory(device, memory);
      DestroyBuffer(device, buffer, NULL);
      FreeMemory(device, memory, NULL);
      DestroyDevice(device, NULL);
      DestroyInstance(instance, NULL);
      dlclose(lib);
      printf("COMPILE_ONLY values=%u no_dispatch=1\n", values);
      return 0;
   }
   VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = 0};
   VkCommandPool pool;
   VK_CHECK(CreateCommandPool(device, &pci, NULL, &pool));
   VkCommandBufferAllocateInfo cba = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1};
   VkCommandBuffer cmd;
   VK_CHECK(AllocateCommandBuffers(device, &cba, &cmd));
   VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
   VK_CHECK(BeginCommandBuffer(cmd, &begin));
   VkMemoryBarrier barrier = {.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_UNIFORM_READ_BIT};
   CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                      0, 1, &barrier, 0, NULL, 0, NULL);
   CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
   CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
   uint32_t seed = 0x12345678;
   CmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &seed);
   CmdDispatch(cmd, 1, 1, 1);
   barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
   barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
   CmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                      0, 1, &barrier, 0, NULL, 0, NULL);
   VK_CHECK(EndCommandBuffer(cmd));
   VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
   VkFence fence;
   VK_CHECK(CreateFence(device, &fci, NULL, &fence));
   uint64_t bad_output = 0, bad_input = 0, bad_guard = 0;
   unsigned rounds = 9;
   int negative = getenv("MATRIX_WRONG_EXPECTED") != NULL;
   for (unsigned round = 0; round < rounds; round++) {
      for (unsigned j = 0; j < words; j++) cpu[j] = initial_word(round, j, row_major);
      VK_CHECK(ResetFences(device, 1, &fence));
      VkSubmitInfo submit = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1, .pCommandBuffers = &cmd};
      VK_CHECK(QueueSubmit(queue, 1, &submit, fence));
      VK_CHECK(WaitForFences(device, 1, &fence, VK_TRUE, 10000000000ull));
      for (unsigned x = 0; x < 64; x++) {
         uint32_t raw = pattern(round, 512 + x) ^ seed;
         unsigned instance = (raw ^ x) & 7;
         unsigned col = (raw >> 8) & 3;
         uint32_t acc = 0;
         for (unsigned row = 0; row < 4; row++) {
            uint32_t component = matrix_element(instance, 1, row, col, round) +
                                 matrix_element(instance, 2, row, col, round);
            for (unsigned c = 0; c < 4; c++)
               component += matrix_element(instance, 0, row, c, round) *
                            (1 + ((raw >> (c * 3)) & 7));
            acc += component * (2 * row + 1);
         }
         if (negative && x == 3) acc ^= 1;
         if (cpu[values * 64 + x] != acc) {
            if (bad_output < 8) printf("MISMATCH round=%u x=%u expected=%08x got=%08x\n",
                                       round, x, acc, cpu[values * 64 + x]);
            bad_output++;
         }
      }
      for (unsigned j = 0; j < values * 64; j++) bad_input += cpu[j] != initial_word(round, j, row_major);
      for (unsigned j = values * 64 + 64; j < words; j++) bad_guard += cpu[j] != pattern(round, j);
   }
   VK_CHECK(DeviceWaitIdle(device));
   DestroyFence(device, fence, NULL);
   DestroyCommandPool(device, pool, NULL);
   DestroyPipeline(device, pipeline, NULL);
   DestroyShaderModule(device, module, NULL);
   DestroyPipelineLayout(device, layout, NULL);
   DestroyDescriptorPool(device, descriptorpool, NULL);
   DestroyDescriptorSetLayout(device, setlayout, NULL);
   UnmapMemory(device, memory);
   DestroyBuffer(device, buffer, NULL);
   FreeMemory(device, memory, NULL);
   DestroyDevice(device, NULL);
   DestroyInstance(instance, NULL);
   dlclose(lib);
   uint64_t bad = bad_output + bad_input + bad_guard;
   printf("MATRIX values=%u rounds=%u output_bad=%" PRIu64 " input_bad=%" PRIu64
          " guard_bad=%" PRIu64 " verdict=%s\n", values, rounds, bad_output,
          bad_input, bad_guard, bad ? "FAIL" : "PASS");
   return bad ? 2 : 0;
}
