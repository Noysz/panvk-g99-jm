#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

#define KBASE_IOCTL_TYPE 0x80
struct kbase_ioctl_version_check { uint16_t major; uint16_t minor; };
#define KBASE_IOCTL_VERSION_CHECK _IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)
struct kbase_ioctl_set_flags { uint32_t create_flags; };
#define KBASE_IOCTL_SET_FLAGS _IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags)
struct kbase_ioctl_job_submit { uint64_t addr; uint32_t nr_atoms; uint32_t stride; };
#define KBASE_IOCTL_JOB_SUBMIT _IOW(KBASE_IOCTL_TYPE, 2, struct kbase_ioctl_job_submit)
#define BASE_MEM_PROT_CPU_RD (1ull << 0)
#define BASE_MEM_PROT_CPU_WR (1ull << 1)
#define BASE_MEM_PROT_GPU_RD (1ull << 2)
#define BASE_MEM_PROT_GPU_WR (1ull << 3)
#define BASE_MEM_PROT_GPU_EX (1ull << 4)
#define BASE_MEM_COHERENT_LOCAL (1ull << 11)
union kbase_ioctl_mem_alloc {
   struct { uint64_t va_pages; uint64_t commit_pages; uint64_t extension; uint64_t flags; } in;
   struct { uint64_t flags; uint64_t gpu_va; } out;
};
#define KBASE_IOCTL_MEM_ALLOC _IOWR(KBASE_IOCTL_TYPE, 5, union kbase_ioctl_mem_alloc)
#define BASE_MEM_MAP_TRACKING_HANDLE (3ull << 12)
struct base_dependency { uint8_t atom_id; uint8_t dependency_type; };
typedef uint32_t base_jd_core_req;

#define BASE_JD_REQ_DEP 0
#define BASE_JD_REQ_FS  (1U << 0)
#define BASE_JD_REQ_CS  (1U << 1)
#define BASE_JD_REQ_T   (1U << 2)
#define BASE_JD_REQ_CF  (1U << 3)
#define BASE_JD_REQ_V   (1U << 4)

#define BASE_JD_DEP_ORDER 0
struct base_jd_atom_v2 {
   uint64_t jc;
   uint64_t udata[2];
   uint64_t extres_list;
   uint16_t nr_extres;
   uint8_t jit_id[2];
   struct base_dependency pre_dep[2];
   uint8_t atom_number;
   uint8_t prio;
   uint8_t device_nr;
   uint8_t jobslot;
   base_jd_core_req core_req;
   uint8_t padding[8];
};
_Static_assert(sizeof(struct base_jd_atom_v2) == 56,
               "base_jd_atom_v2 must be 56 bytes");

struct base_jd_event_v2 {
   uint32_t event_code;
   uint8_t atom_number;
   uint8_t padding[3];
   uint64_t udata[2];
};

static void fill_job_header(uint32_t *h, unsigned type, unsigned index, unsigned dep1)
{
   memset(h, 0, 32);
   /* Job Header v9.xml: Type = bit 1-7, Index = bit 16-31. Bit 0 TIDAK
    * ADA field apapun (reserved) - versi lama salah nge-set bit ini. */
   h[4] = ((type & 0x7f) << 1) | ((index & 0xffff) << 16);
   h[5] = (dep1 & 0xffff);
   h[6] = 0; h[7] = 0;
}

static int alloc_bo(int fd, uint64_t va_pages, uint64_t flags, uint64_t *gpu_va)
{
   union kbase_ioctl_mem_alloc a = {0};
   a.in.va_pages = va_pages;
   a.in.commit_pages = va_pages;
   a.in.flags = flags;
   if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &a)) {
      printf("MEM_ALLOC failed: %s\n", strerror(errno));
      return -1;
   }
   *gpu_va = a.out.gpu_va;
   return 0;
}

int main(void) {
   int fd = open("/dev/mali0", O_RDWR);
   if (fd < 0) { printf("open failed: %s\n", strerror(errno)); return 1; }

   struct kbase_ioctl_version_check ver = {0,0};
   ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver);

   struct kbase_ioctl_set_flags sf = { .create_flags = 0 };
   ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf);

   void *tracking = mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, BASE_MEM_MAP_TRACKING_HANDLE);
   if (tracking == MAP_FAILED) {
      printf("mmap(tracking) failed: %s\n", strerror(errno));
   }

   uint64_t tgt_gpu;
   if (alloc_bo(fd, 1, BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                          BASE_MEM_PROT_GPU_WR | BASE_MEM_COHERENT_LOCAL,
                      &tgt_gpu)) return 1;
   void *tgt = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)tgt_gpu);
   if (tgt == MAP_FAILED) {
      printf("mmap(tgt) failed: %s\n", strerror(errno)); return 1;
   }
   *(uint32_t *)tgt = 0xDEADBEEF;

   uint64_t dsc_gpu;
   if (alloc_bo(fd, 1, BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                          BASE_MEM_PROT_GPU_RD,
                      &dsc_gpu)) return 1;
   void *dbuf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, (off_t)dsc_gpu);
   if (dbuf == MAP_FAILED) {
      printf("mmap(desc) failed: %s\n", strerror(errno)); return 1;
   }
   uint64_t desc_va = (uint64_t)(uintptr_t)dbuf;

   memset(dbuf, 0, 4096);
   uint64_t target_va = (uint64_t)(uintptr_t)tgt;
   printf("tgt_gpu=0x%llx cpu_va=0x%llx\n", (unsigned long long)tgt_gpu, (unsigned long long)target_va);
   uint32_t *d = dbuf;
   fill_job_header(d, 2, 1, 0);
   d[8]  = (uint32_t)(target_va & 0xffffffff);
   d[9]  = (uint32_t)(target_va >> 32);
   d[10] = 6;
   d[11] = 0;
   d[12] = 0x2A2A2A2A;
   d[13] = 0;

   struct base_jd_atom_v2 atom = {0};
   atom.jc = desc_va;
   atom.atom_number = 1;
   atom.core_req = BASE_JD_REQ_CS;  /* WRITE_VALUE usa core_req CS no kbase_wv_test.c original */

   struct base_jd_atom_v2 *ap = (struct base_jd_atom_v2 *)((uint32_t *)dbuf + 256);
   memcpy(ap, &atom, sizeof(atom));

   struct kbase_ioctl_job_submit submit = {
      .addr = (uint64_t)(uintptr_t)ap,
      .nr_atoms = 1,
      .stride = 64,
   };

   errno = 0;
   int submit_ret = ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit);
   if (submit_ret) {
      printf("JOB_SUBMIT failed: errno=%d (%s), stride=%u\n",
             errno, strerror(errno), submit.stride);
      return 1;
   }

   printf("WRITE_VALUE core_req=0x%x submitted, atom_size=%zu stride=%u\n",
          atom.core_req, sizeof(atom), submit.stride);

   struct pollfd pfd = { .fd = fd, .events = POLLIN };
   int pret = poll(&pfd, 1, 10000);
   unsigned event = 0xffffffff;
   if (pret > 0 && (pfd.revents & POLLIN)) {
      struct base_jd_event_v2 ev;
      read(fd, &ev, sizeof(ev));
      event = ev.event_code;
   }

   printf("event=0x%x, target=0x%08x\n", event, *(uint32_t *)tgt);
   close(fd);
   return 0;
}
