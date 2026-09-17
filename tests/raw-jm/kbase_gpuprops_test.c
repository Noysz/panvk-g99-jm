#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

#define KBASE_IOCTL_TYPE 0x80

struct kbase_ioctl_version_check { uint16_t major; uint16_t minor; };
#define KBASE_IOCTL_VERSION_CHECK _IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)

struct kbase_ioctl_set_flags { uint32_t create_flags; };
#define KBASE_IOCTL_SET_FLAGS _IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags)

struct kbase_ioctl_get_gpuprops { uint64_t buffer; uint32_t size; uint32_t flags; };
#define KBASE_IOCTL_GET_GPUPROPS _IOW(KBASE_IOCTL_TYPE, 3, struct kbase_ioctl_get_gpuprops)

#define BASE_MEM_MAP_TRACKING_HANDLE (3ull << 12)

#define P_PRODUCT_ID      1
#define P_VERSION_STATUS  2
#define P_MINOR_REVISION  3
#define P_MAJOR_REVISION  4
#define P_RAW_GPU_ID      55
#define P_L2_LOG2_SIZE    14
#define P_L2_LOG2_LINESIZE 13
#define P_NUM_L2_SLICES   15
#define P_RAW_SHADER_PRESENT 25
#define P_RAW_TILER_PRESENT  26
#define P_RAW_L2_PRESENT     27
#define P_MAX_THREADS     18
#define P_MAX_WORKGROUP_SIZE 19

static uint64_t prop_get(const uint8_t *buf, size_t n, uint32_t key, uint64_t dflt) {
   size_t off = 0;
   while (off + 4 <= n) {
      uint32_t hdr;
      memcpy(&hdr, buf + off, 4);
      off += 4;
      uint32_t k = hdr >> 2;
      uint32_t sc = hdr & 3;
      uint32_t vs = 1u << sc;
      if (off + vs > n) break;
      if (k == key) {
         uint64_t v = 0;
         memcpy(&v, buf + off, vs);
         return v;
      }
      off += vs;
   }
   return dflt;
}

int main(void) {
   int fd = open("/dev/mali0", O_RDWR);
   if (fd < 0) { printf("open failed: %s\n", strerror(errno)); return 1; }

   struct kbase_ioctl_version_check ver = {0, 0};
   if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver)) {
      printf("VERSION_CHECK failed: %s\n", strerror(errno)); return 1;
   }
   printf("VERSION_CHECK: %u.%u\n", ver.major, ver.minor);

   struct kbase_ioctl_set_flags sf = { .create_flags = 0 };
   if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf)) {
      printf("SET_FLAGS failed: %s\n", strerror(errno)); return 1;
   }

   void *trk = mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, BASE_MEM_MAP_TRACKING_HANDLE);
   if (trk == MAP_FAILED) { printf("tracking mmap failed: %s\n", strerror(errno)); return 1; }

   struct kbase_ioctl_get_gpuprops req = { 0 };
   int ret = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &req);
   if (ret < 0) {
      printf("GET_GPUPROPS probe failed: %s\n", strerror(errno));
      return 1;
   }
   size_t size = (size_t)ret;
   uint8_t *buf = calloc(1, size);
   req.buffer = (uintptr_t)buf;
   req.size = (uint32_t)size;
   ret = ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &req);
   if (ret < 0) {
      printf("GET_GPUPROPS fill failed: %s\n", strerror(errno));
      return 1;
   }
   printf("blob: %zu bytes\n", size);

   uint64_t raw = prop_get(buf, size, P_RAW_GPU_ID, 0);
   uint64_t product = prop_get(buf, size, P_PRODUCT_ID, 0);
   uint64_t ver_status = prop_get(buf, size, P_VERSION_STATUS, 0);
   uint64_t major_rev = prop_get(buf, size, P_MAJOR_REVISION, 0);
   uint64_t minor_rev = prop_get(buf, size, P_MINOR_REVISION, 0);

   printf("\n== GPU ID ==\n");
   if (raw) {
      printf("RAW_GPU_ID       = 0x%04llx (mips %u, n. %u)\n",
             (unsigned long long)(raw & 0xffffffff), 0, 0);
      printf("  gpu_id completo  = 0x%08llx\n", (unsigned long long)raw);
      printf("  arch(bits[31:28])= %llu · prod(bits[31:16]) major = 0x%04llx\n",
             (unsigned long long)((raw >> 28) & 0xf),
             (unsigned long long)((raw >> 16) & 0xffff));
   } else {
      printf("RAW_GPU_ID ausente; reconstruindo dos campos:\n");
      printf("  product_id=0x%03llx major_rev=%llu minor_rev=%llu status=%llu\n",
             (unsigned long long)product, (unsigned long long)major_rev,
             (unsigned long long)minor_rev, (unsigned long long)ver_status);
   }
   printf("product_id        = 0x%03llx\n", (unsigned long long)product);

   unsigned arch_major = (unsigned)((raw >> 28) & 0xf);
   unsigned arch_minor = (unsigned)((raw >> 24) & 0xf);
   unsigned prod_major = (unsigned)((raw >> 16) & 0xf);
   unsigned pan_prod = (arch_major << 16) | (arch_minor << 8) | prod_major;
   printf("pan_prod_id (Mesa) = 0x%06x", pan_prod);
   if (pan_prod == 0x090001 || pan_prod == 0x090003)
      printf(" -> casa com o modelo G57!\n");
   else
      printf(" -> NÃO casa com G57 (0x090001/0x090003); cairá em 'unknown Valhall'\n");

   uint64_t shaders = prop_get(buf, size, P_RAW_SHADER_PRESENT, 0);
   uint64_t tiler = prop_get(buf, size, P_RAW_TILER_PRESENT, 0);
   uint64_t l2 = prop_get(buf, size, P_RAW_L2_PRESENT, 0);
   int cores = 0;
   for (int i = 0; i < 32; i++) if (shaders & (1ull << i)) cores++;
   printf("shader cores mask = 0x%llx (%d cores -> MC%d)\n",
          (unsigned long long)shaders, cores, cores);
   printf("tiler mask        = 0x%llx\n", (unsigned long long)tiler);
   printf("l2 mask           = 0x%llx\n", (unsigned long long)l2);
   printf("l2 size           = %llu KB/log2\n",
          (unsigned long long)prop_get(buf, size, P_L2_LOG2_SIZE, 0));
   printf("max threads       = %llu\n",
          (unsigned long long)prop_get(buf, size, P_MAX_THREADS, 0));
   printf("max workgroup     = %llu\n",
          (unsigned long long)prop_get(buf, size, P_MAX_WORKGROUP_SIZE, 0));

   printf("\n== blob (hex) ==\n");
   for (size_t i = 0; i < size; i += 16) {
      printf("%04zx: ", i);
      for (size_t j = 0; j < 16 && i + j < size; j++) printf("%02x ", buf[i + j]);
      printf("\n");
   }
   free(buf);
   close(fd);
   return 0;
}