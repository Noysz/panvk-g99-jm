/* begin_end_rendering_test.c
 *
 * Test PALING minimal buat CmdBeginRendering/CmdEndRendering v9 -- TANPA
 * attachment sama sekali (colorAttachmentCount=0, no depth/stencil).
 * Tujuannya cuma satu: pastiin cmd_init_render_state / cmd_alloc_fb_desc /
 * cmd_meta_resolve_attachments (yang barusan dipatch dari body Bifrost asli)
 * nggak crash begitu dipanggil lewat jalur v9. Belum ada draw call apapun --
 * itu langkah SETELAH ini.
 *
 * Pola dlopen/entrypoint sama persis kayak compute_test.c yang sudah
 * kebukti jalan, biar konsisten dan gampang dibandingin kalau ada masalah.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vulkan/vulkan_core.h"

#define CHECK(expr, msg) do { \
    VkResult _r = (expr); \
    if (_r != VK_SUCCESS) { \
        printf("FAILED: %s (VkResult=%d)\n", msg, _r); \
        return 1; \
    } \
    printf("%s ok\n", msg); \
} while (0)

typedef PFN_vkVoidFunction (*PFN_icdGetInstanceProcAddr)(VkInstance, const char*);

int main(void) {
    void *lib = dlopen("/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/libvulkan_panfrost.so", RTLD_NOW);
    if (!lib) {
        printf("dlopen failed: %s\n", dlerror());
        return 1;
    }

    PFN_icdGetInstanceProcAddr icd_gpa =
        (PFN_icdGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
    if (!icd_gpa) {
        printf("dlsym vk_icdGetInstanceProcAddr failed: %s\n", dlerror());
        return 1;
    }

    PFN_vkCreateInstance CreateInstance =
        (PFN_vkCreateInstance)icd_gpa(NULL, "vkCreateInstance");

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
    };
    VkInstance instance;
    CHECK(CreateInstance(&inst_info, NULL, &instance), "vkCreateInstance");
    fflush(stdout);

    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    #define IPROC(name) (PFN_vk##name)icd_gpa(instance, "vk" #name)

    GetDeviceProcAddr = IPROC(GetDeviceProcAddr);
    PFN_vkEnumeratePhysicalDevices EnumeratePhysicalDevices = IPROC(EnumeratePhysicalDevices);
    PFN_vkCreateDevice CreateDevice = IPROC(CreateDevice);

    printf("proc addrs: GetDeviceProcAddr=%p EnumeratePhysicalDevices=%p CreateDevice=%p\n",
           (void*)GetDeviceProcAddr, (void*)EnumeratePhysicalDevices, (void*)CreateDevice);
    if (!GetDeviceProcAddr || !EnumeratePhysicalDevices || !CreateDevice) {
        printf("FAILED: one or more instance proc addrs is NULL, stop here\n");
        return 1;
    }
    fflush(stdout);

    uint32_t count = 0;
    VkResult r_enum1 = EnumeratePhysicalDevices(instance, &count, NULL);
    printf("EnumeratePhysicalDevices(count query): VkResult=%d count=%u\n", r_enum1, count);
    if (r_enum1 != VK_SUCCESS || count == 0) {
        printf("FAILED: no physical device enumerated, stop here\n");
        return 1;
    }

    VkPhysicalDevice pdev = (VkPhysicalDevice)0;
    VkResult r_enum2 = EnumeratePhysicalDevices(instance, &count, &pdev);
    printf("EnumeratePhysicalDevices(fetch): VkResult=%d count=%u pdev=%p\n", r_enum2, count, (void*)pdev);
    if (r_enum2 != VK_SUCCESS || pdev == (VkPhysicalDevice)0) {
        printf("FAILED: pdev invalid, stop here\n");
        return 1;
    }
    fflush(stdout);

    /* Aktifkan fitur dynamicRendering -- ini jalur render yang dipakai
     * PanVK (VkRenderingInfo langsung, bukan VkRenderPass/VkFramebuffer
     * tradisional). Tanpa ini, vkCmdBeginRendering mungkin ditolak validasi
     * (kalau ada), atau di driver eksperimental begini mungkin tetap
     * jalan tanpa dicek ketat -- kita aktifkan eksplisit biar benar. */
    VkPhysicalDeviceVulkan13Features features13 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .dynamicRendering = VK_TRUE,
    };

    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = 0,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    VkDeviceCreateInfo dev_info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &features13,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_info,
    };
    VkDevice device;
    CHECK(CreateDevice(pdev, &dev_info, NULL, &device), "vkCreateDevice");
    fflush(stdout);

    #define DPROC(name) (PFN_vk##name)GetDeviceProcAddr(device, "vk" #name)
    PFN_vkGetDeviceQueue GetDeviceQueue = DPROC(GetDeviceQueue);
    PFN_vkCreateCommandPool CreateCommandPool = DPROC(CreateCommandPool);
    PFN_vkAllocateCommandBuffers AllocateCommandBuffers = DPROC(AllocateCommandBuffers);
    PFN_vkBeginCommandBuffer BeginCommandBuffer = DPROC(BeginCommandBuffer);
    PFN_vkCmdBeginRendering CmdBeginRendering = DPROC(CmdBeginRendering);
    PFN_vkCmdEndRendering CmdEndRendering = DPROC(CmdEndRendering);
    PFN_vkEndCommandBuffer EndCommandBuffer = DPROC(EndCommandBuffer);
    PFN_vkQueueSubmit QueueSubmit = DPROC(QueueSubmit);
    PFN_vkQueueWaitIdle QueueWaitIdle = DPROC(QueueWaitIdle);

    printf("device proc addrs: CmdBeginRendering=%p CmdEndRendering=%p\n",
           (void*)CmdBeginRendering, (void*)CmdEndRendering);
    if (!CmdBeginRendering || !CmdEndRendering) {
        printf("FAILED: CmdBeginRendering/CmdEndRendering is NULL\n");
        return 1;
    }
    fflush(stdout);

    VkQueue queue;
    GetDeviceQueue(device, 0, 0, &queue);
    printf("vkGetDeviceQueue ok\n");
    fflush(stdout);

    VkCommandPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = 0,
    };
    VkCommandPool pool;
    CHECK(CreateCommandPool(device, &pool_info, NULL, &pool), "vkCreateCommandPool");

    VkCommandBufferAllocateInfo cb_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer cmdbuf;
    CHECK(AllocateCommandBuffers(device, &cb_alloc, &cmdbuf), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };
    CHECK(BeginCommandBuffer(cmdbuf, &begin_info), "vkBeginCommandBuffer");

    /* Rendering info TANPA attachment sama sekali -- paling minimal yang
     * bisa dikasih ke vkCmdBeginRendering, biar risiko crash sekecil
     * mungkin di percobaan pertama ini. */
    VkRenderingInfo rendering_info = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = {0, 0}, .extent = {1, 1} },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pColorAttachments = NULL,
        .pDepthAttachment = NULL,
        .pStencilAttachment = NULL,
    };

    printf("calling vkCmdBeginRendering (no attachments)...\n");
    fflush(stdout);
    CmdBeginRendering(cmdbuf, &rendering_info);
    printf("vkCmdBeginRendering returned (no crash)\n");
    fflush(stdout);

    printf("calling vkCmdEndRendering...\n");
    fflush(stdout);
    CmdEndRendering(cmdbuf);
    printf("vkCmdEndRendering returned (no crash)\n");
    fflush(stdout);

    CHECK(EndCommandBuffer(cmdbuf), "vkEndCommandBuffer");
    printf("command buffer recorded\n");
    fflush(stdout);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdbuf,
    };
    printf("calling vkQueueSubmit...\n");
    fflush(stdout);
    CHECK(QueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE), "vkQueueSubmit");

    printf("calling vkQueueWaitIdle...\n");
    fflush(stdout);
    CHECK(QueueWaitIdle(queue), "vkQueueWaitIdle");

    printf("SUCCESS: BeginRendering/EndRendering (no attachments) completed without crash\n");
    return 0;
}
