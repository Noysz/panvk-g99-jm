// indirect_dispatch_test.c
//
// Validates vkCmdDispatchIndirect on the v9 native compute path
// (jm/panvk_vX_cmd_dispatch.c, PAN_ARCH >= 9 branch patched this session).
//
// The whole point of "indirect" is that the workgroup count is NOT known
// at command-buffer record time - it's read from a GPU buffer at dispatch
// time. So every test here fills the indirect-command buffer via CPU
// *before* submit (simulating "some earlier GPU pass decided this count"),
// and checks that:
//   1) exactly that many invocations ran (no more, no less)
//   2) elements beyond the count were NEVER touched (proves the GPU read
//      the real runtime count, not some compile-time/cached value)
//
//   A) NORMAL COUNT    - indirect {x=4,y=1,z=1}. Sanity baseline.
//   B) DIFFERENT COUNT - indirect {x=37,y=1,z=1}. Different from A on
//                        purpose, and not a "round" number, to rule out
//                        any hardcoded/cached count sneaking through.
//   C) ZERO-COUNT (riskiest edge case) - indirect {x=0,y=1,z=1}. This
//      is the exact branch (`is_no_op`) in indirect_dispatch.cl that
//      turns the job into MALI_JOB_TYPE_NULL instead of COMPUTE. Checks
//      vkQueueWaitIdle completes cleanly (no hang/crash) and NOTHING
//      got touched.
//
// Build: see build.sh in this directory.
// Run:   PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./indirect_dispatch_test

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CHECK(cond, msg) \
    do { if (!(cond)) { fprintf(stderr, "FATAL: %s\n", msg); exit(1); } } while (0)
#define VKCHECK(expr, msg) \
    do { VkResult _r = (expr); \
         if (_r != VK_SUCCESS) { fprintf(stderr, "FATAL: %s (VkResult=%d)\n", msg, _r); exit(1); } \
    } while (0)

#define BUF_CAPACITY 64          // buffer sized bigger than any count we test
#define SENTINEL     0xDEADBEEFu // untouched elements must keep this value

static VkInstance instance;
static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;
static VkCommandPool pool;

static VkDescriptorSetLayout dsl;
static VkPipelineLayout layout;
static VkPipeline pipe;
static VkShaderModule mod;

static VkBuffer out_buf;      // storage buffer the shader writes into
static VkDeviceMemory out_mem;
static void *out_mapped;

static VkBuffer ind_buf;      // VkDispatchIndirectCommand buffer
static VkDeviceMemory ind_mem;
static void *ind_mapped;

static VkDescriptorSet set;
static VkDescriptorPool dp;

static int g_pass = 0, g_fail = 0;
static void report(const char *name, int ok, const char *detail) {
    if (ok) { printf("[PASS] %-12s %s\n", name, detail); g_pass++; }
    else    { printf("[FAIL] %-12s %s\n", name, detail); g_fail++; }
}

static uint32_t find_mem_type(uint32_t bits, VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i++) {
        if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    }
    CHECK(0, "no matching memory type");
    return 0;
}

static void alloc_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer *buf, VkDeviceMemory *mem, void **mapped) {
    VkBufferCreateInfo bci = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size, .usage = usage, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    VKCHECK(vkCreateBuffer(dev, &bci, NULL, buf), "vkCreateBuffer");

    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev, *buf, &req);
    VkMemoryAllocateInfo mai = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = req.size,
        .memoryTypeIndex = find_mem_type(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) };
    VKCHECK(vkAllocateMemory(dev, &mai, NULL, mem), "vkAllocateMemory");
    VKCHECK(vkBindBufferMemory(dev, *buf, *mem, 0), "vkBindBufferMemory");
    VKCHECK(vkMapMemory(dev, *mem, 0, size, 0, mapped), "vkMapMemory");
}

static void setup_instance_device(void) {
    VkApplicationInfo app = { .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "indirect_dispatch_test", .apiVersion = VK_API_VERSION_1_1 };
    VkInstanceCreateInfo ici = { .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app };
    VKCHECK(vkCreateInstance(&ici, NULL, &instance), "vkCreateInstance");

    uint32_t pdCount = 1;
    VKCHECK(vkEnumeratePhysicalDevices(instance, &pdCount, &phys), "vkEnumeratePhysicalDevices");
    CHECK(pdCount >= 1, "no physical device (v9 not enumerated?)");

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(phys, &props);
    printf("device: %s\n", props.deviceName);

    uint32_t qCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qCount, NULL);
    VkQueueFamilyProperties qprops[16];
    CHECK(qCount <= 16, "too many queue families");
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &qCount, qprops);
    qfam = UINT32_MAX;
    for (uint32_t i = 0; i < qCount; i++)
        if (qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qfam = i; break; }
    CHECK(qfam != UINT32_MAX, "no compute queue family");

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = qfam, .queueCount = 1, .pQueuePriorities = &prio };
    VkDeviceCreateInfo dci = { .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci };
    VKCHECK(vkCreateDevice(phys, &dci, NULL, &dev), "vkCreateDevice");
    vkGetDeviceQueue(dev, qfam, 0, &queue);

    VkCommandPoolCreateInfo pci = { .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = qfam, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT };
    VKCHECK(vkCreateCommandPool(dev, &pci, NULL, &pool), "vkCreateCommandPool");
}

static VkShaderModule load_shader(const char *path) {
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL, path);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint32_t *code = malloc(sz);
    CHECK(fread(code, 1, sz, f) == (size_t)sz, "short read on spv");
    fclose(f);
    VkShaderModuleCreateInfo smci = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = (size_t)sz, .pCode = code };
    VkShaderModule m;
    VKCHECK(vkCreateShaderModule(dev, &smci, NULL, &m), "vkCreateShaderModule");
    free(code);
    return m;
}

static void setup_pipeline_and_resources(void) {
    const VkDeviceSize out_size = BUF_CAPACITY * sizeof(uint32_t);
    alloc_buffer(out_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &out_buf, &out_mem, &out_mapped);

    // VkDispatchIndirectCommand is exactly {uint32_t x,y,z;} per the spec
    alloc_buffer(sizeof(uint32_t) * 3,
                 VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 &ind_buf, &ind_mem, &ind_mapped);

    VkDescriptorSetLayoutBinding b = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo dslci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &b };
    VKCHECK(vkCreateDescriptorSetLayout(dev, &dslci, NULL, &dsl), "vkCreateDescriptorSetLayout");

    VkPipelineLayoutCreateInfo plci = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1, .pSetLayouts = &dsl };
    VKCHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout), "vkCreatePipelineLayout");

    mod = load_shader("shaders/write_id.spv");
    VkPipelineShaderStageCreateInfo stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = mod, .pName = "main" };
    VkComputePipelineCreateInfo cpci = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage, .layout = layout };
    VKCHECK(vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &cpci, NULL, &pipe), "vkCreateComputePipelines");

    VkDescriptorPoolSize ps = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1 };
    VkDescriptorPoolCreateInfo dpci = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &ps };
    VKCHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dp), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo dsai = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = dp, .descriptorSetCount = 1, .pSetLayouts = &dsl };
    VKCHECK(vkAllocateDescriptorSets(dev, &dsai, &set), "vkAllocateDescriptorSets");

    VkDescriptorBufferInfo dbi = { .buffer = out_buf, .offset = 0, .range = out_size };
    VkWriteDescriptorSet w = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &dbi };
    vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
}

// Runs one indirect-dispatch test case: writes {count,1,1} into the
// indirect buffer, resets the output buffer to SENTINEL, dispatches
// indirectly, then checks [0,count) == 0..count-1 and [count,BUF_CAPACITY)
// still == SENTINEL.
static void run_case(const char *name, uint32_t count) {
    uint32_t *ind = (uint32_t *)ind_mapped;
    ind[0] = count; ind[1] = 1; ind[2] = 1;

    uint32_t *out = (uint32_t *)out_mapped;
    for (uint32_t i = 0; i < BUF_CAPACITY; i++) out[i] = SENTINEL;

    VkCommandBufferAllocateInfo cbai = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool, .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = 1 };
    VkCommandBuffer cb;
    VKCHECK(vkAllocateCommandBuffers(dev, &cbai, &cb), "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo bi = { .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    VKCHECK(vkBeginCommandBuffer(cb, &bi), "vkBeginCommandBuffer");

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, NULL);
    vkCmdDispatchIndirect(cb, ind_buf, 0);

    VKCHECK(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
    VkSubmitInfo si = { .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb };
    VKCHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit");
    VKCHECK(vkQueueWaitIdle(queue), "vkQueueWaitIdle"); // if this hangs, that's the finding
    vkFreeCommandBuffers(dev, pool, 1, &cb);

    // check [0,count) got exactly their own index
    int ok = 1;
    uint32_t first_bad = 0xFFFFFFFFu, first_bad_val = 0;
    for (uint32_t i = 0; i < count && i < BUF_CAPACITY; i++) {
        if (out[i] != i) { ok = 0; first_bad = i; first_bad_val = out[i]; break; }
    }
    // check [count,BUF_CAPACITY) is untouched
    int spill = 0;
    uint32_t spill_idx = 0, spill_val = 0;
    for (uint32_t i = count; i < BUF_CAPACITY; i++) {
        if (out[i] != SENTINEL) { spill = 1; spill_idx = i; spill_val = out[i]; break; }
    }

    char detail[192];
    if (!ok) {
        snprintf(detail, sizeof(detail),
                 "count=%u: out[%u]=0x%08x (expected %u)",
                 count, first_bad, first_bad_val, first_bad);
    } else if (spill) {
        snprintf(detail, sizeof(detail),
                 "count=%u: [0,%u) correct BUT out[%u]=0x%08x got touched (expected untouched SENTINEL=0x%08x)",
                 count, count, spill_idx, spill_val, SENTINEL);
    } else {
        snprintf(detail, sizeof(detail),
                 "count=%u: [0,%u) all correct, rest of buffer still SENTINEL (untouched)",
                 count, count);
    }
    report(name, ok && !spill, detail);
}

static void teardown(void) {
    vkDestroyDescriptorPool(dev, dp, NULL);
    vkDestroyPipeline(dev, pipe, NULL);
    vkDestroyShaderModule(dev, mod, NULL);
    vkDestroyPipelineLayout(dev, layout, NULL);
    vkDestroyDescriptorSetLayout(dev, dsl, NULL);
    vkUnmapMemory(dev, out_mem); vkDestroyBuffer(dev, out_buf, NULL); vkFreeMemory(dev, out_mem, NULL);
    vkUnmapMemory(dev, ind_mem); vkDestroyBuffer(dev, ind_buf, NULL); vkFreeMemory(dev, ind_mem, NULL);
    vkDestroyCommandPool(dev, pool, NULL);
    vkDestroyDevice(dev, NULL);
    vkDestroyInstance(instance, NULL);
}

int main(void) {
    setup_instance_device();
    setup_pipeline_and_resources();

    printf("--- running 3 indirect-dispatch cases ---\n");
    run_case("A normal", 4);
    run_case("B diffcount", 37);
    run_case("C zerocount", 0);

    printf("--- %d passed, %d failed ---\n", g_pass, g_fail);
    teardown();
    return g_fail == 0 ? 0 : 1;
}
