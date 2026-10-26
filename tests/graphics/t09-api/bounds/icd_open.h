/* icd_open.h -- get vkGetInstanceProcAddr from a driver .so either through
 * the Linux ICD entry (vk_icdGetInstanceProcAddr) or, for an Android HAL
 * build (-Dplatforms=android, what Winlator/adrenotools loads), through the
 * hwvulkan module HMI -> open("vk0") -> hwvulkan_device_t::GetInstanceProcAddr.
 * This calls the HAL directly, without the Android Vulkan loader. */
#ifndef ICD_OPEN_H
#define ICD_OPEN_H
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "vulkan/vulkan_core.h"

struct icdo_hw_module_methods { int (*open)(const void *module, const char *id, void **device); };
struct icdo_hw_module { uint32_t tag; uint16_t module_api_version, hal_api_version; const char *id, *name, *author;
                        struct icdo_hw_module_methods *methods; void *dso; uint64_t reserved[32 - 7]; };
struct icdo_hw_device { uint32_t tag, version; void *module; uint64_t reserved[12]; int (*close)(void *); };
struct icdo_hwvulkan_device { struct icdo_hw_device common; PFN_vkEnumerateInstanceExtensionProperties eie;
                              PFN_vkCreateInstance create_instance; PFN_vkGetInstanceProcAddr gipa; };

static PFN_vkGetInstanceProcAddr icd_open_gipa(void *lib, const char **how)
{
   if (getenv("T10_LOADER")) {
      *how = "Vulkan loader";
      return (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
   }
   PFN_vkGetInstanceProcAddr g = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vk_icdGetInstanceProcAddr");
   if (g) { *how = "ICD (vk_icdGetInstanceProcAddr)"; return g; }
   struct icdo_hw_module *hmi = (struct icdo_hw_module *)dlsym(lib, "HMI");
   if (!hmi || !hmi->methods || !hmi->methods->open) return NULL;
   struct icdo_hwvulkan_device *dev = NULL;
   if (hmi->methods->open(hmi, "vk0", (void **)&dev) != 0 || !dev) return NULL;
   *how = "Android HAL (HMI, hwvulkan vk0)";
   return dev->gipa;
}
#endif
