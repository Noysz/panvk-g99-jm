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
struct kbase_ioctl_mem_exec_init { uint64_t va_pages; };
#define KBASE_IOCTL_MEM_EXEC_INIT _IOW(KBASE_IOCTL_TYPE, 38, struct kbase_ioctl_mem_exec_init)
#define BASE_MEM_MAP_TRACKING_HANDLE (3ull << 12)
struct base_dependency { uint8_t atom_id; uint8_t dependency_type; };
typedef uint32_t base_jd_core_req;
#define BASE_JD_REQ_CS ((base_jd_core_req)(1u << 1))
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
   uint8_t renderpass_id;
   uint8_t padding[15];
};
struct base_jd_event_v2 {
   uint32_t event_code;
   uint8_t atom_number;
   uint8_t padding[3];
   uint64_t udata[2];
};
#define MALI_JOB_TYPE_COMPUTE 4
#define MALI_SHADER_WORDS 32

static const uint32_t shader_A[MALI_SHADER_WORDS] = {
   0x2a2a2ac0, 0x0110c02a, 0x18000080, 0x78614002,
};

static void fill_job_header(uint32_t *h, unsigned type, unsigned index, unsigned dep1)
{
   memset(h, 0, 32);
   h[4] = (1u << 0) | ((type & 0x7f) << 1) | ((index & 0xffff) << 16);
   h[5] = (dep1 & 0xffff);
   h[6] = 0; h[7] = 0;
}

static void init_compute_desc(uint32_t *d, uint64_t shader_va, uint64_t fau_va, unsigned fau_count)
{
   fill_job_header(d, MALI_JOB_TYPE_COMPUTE, 1, 0);
   d[0] = (0 << 0) | (0 << 10) | (0 << 20);
   d[1] = (1 << 0);
   d[2] = 1; d[3] = 1; d[4] = 1;
   d[5] = 0; d[6] = 0; d[7] = 0;
   d[8] = (fau_count << 8);
   d[9] = 0;
   d[10] = 0; d[11] = 0;
   d[12] = (uint32_t)(shader_va & 0xffffffff);
   d[13] = (uint32_t)(shader_va >> 32);
   d[14] = 0; d[15] = 0;
   d[16] = (uint32_t)(fau_va & 0xffffffff);
   d[17] = (uint32_t)(fau_va >> 32);
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
   if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver)) {
      printf("VERSION_CHECK failed: %s\n", strerror(errno)); return 1;
   }

   struct kbase_ioctl_set_flags sf = { .create_flags = 0 };
   if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf)) {
      printf("SET_FLAGS failed: %s\n", strerror(errno)); return 1;
   }

   void *tracking = mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, BASE_MEM_MAP_TRACKING_HANDLE);
   if (tracking == MAP_FAILED) {
      printf("mmap(tracking) failed: %s\n", strerror(errno)); return 1;
   }

   struct kbase_ioctl_mem_exec_init ex = { .va_pages = 0x100000 };
   if (ioctl(fd, KBASE_IOCTL_MEM_EXEC_INIT, &ex)) {
      printf("MEM_EXEC_INIT failed: %s\n", strerror(errno)); return 1;
   }

   uint64_t exec_gpu;
   if (alloc_bo(fd, 1, BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                          BASE_MEM_PROT_GPU_RD | BASE_MEM_COHERENT_LOCAL |
                          BASE_MEM_PROT_GPU_EX,
                      &exec_gpu)) return 1;
   void *shader_cpu = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                            (off_t)exec_gpu);
   if (shader_cpu == MAP_FAILED) {
      printf("mmap(exec) failed: %s\n", strerror(errno)); return 1;
   }
   memcpy(shader_cpu, shader_A, sizeof(shader_A));
   __sync_synchronize();
   uint64_t sh_va = exec_gpu;
   printf("shader: gpu_va=0x%llx\n", (unsigned long long)sh_va);

   uint64_t fau_gpu;
   if (alloc_bo(fd, 1, BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                          BASE_MEM_PROT_GPU_RD | BASE_MEM_COHERENT_LOCAL,
                      &fau_gpu)) return 1;
   void *fau = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                    (off_t)fau_gpu);
   if (fau == MAP_FAILED) {
      printf("mmap(fau) failed: %s\n", strerror(errno)); return 1;
   }
   uint64_t fau_va = (uint64_t)(uintptr_t)fau;

   /* TESTE: FAU plane aponta para o proprio shader (auto-write) */
   for (int i = 0; i < 8; i += 2) {
      ((uint32_t *)fau)[i]   = (uint32_t)(sh_va & 0xffffffff);
      ((uint32_t *)fau)[i+1] = (uint32_t)(sh_va >> 32);
   }
   __sync_synchronize();

   uint64_t dsc_gpu;
   if (alloc_bo(fd, 1, BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                          BASE_MEM_PROT_GPU_RD,
                      &dsc_gpu)) return 1;
   void *dbuf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                     (off_t)dsc_gpu);
   if (dbuf == MAP_FAILED) {
      printf("mmap(desc) failed: %s\n", strerror(errno)); return 1;
   }
   uint64_t desc_va = (uint64_t)(uintptr_t)dbuf;

   memset(dbuf, 0, 4096);
   uint32_t *d = dbuf;
   init_compute_desc(d, sh_va, fau_va, 2);

   struct base_jd_atom_v2 atom = {0};
   atom.jc = desc_va;
   atom.atom_number = 1;
   atom.core_req = BASE_JD_REQ_CS;

   struct base_jd_atom_v2 *ap = (struct base_jd_atom_v2 *)((uint32_t *)dbuf + 256);
   memcpy(ap, &atom, sizeof(atom));

   struct kbase_ioctl_job_submit submit = {
      .addr = (uint64_t)(uintptr_t)ap,
      .nr_atoms = 1,
      .stride = 64,
   };

   errno = 0;
   if (ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit)) {
      printf("JOB_SUBMIT failed: %s\n", strerror(errno)); return 1;
   }

   struct pollfd pfd = { .fd = fd, .events = POLLIN };
   int pret = poll(&pfd, 1, 10000);
   unsigned event = 0xffffffff;
   if (pret > 0 && (pfd.revents & POLLIN)) {
      struct base_jd_event_v2 ev;
      memset(&ev, 0, sizeof(ev));
      read(fd, &ev, sizeof(ev));
      event = ev.event_code;
   }

   sleep(1);
   printf("shader[0..3] antes: %08x %08x %08x %08x\n",
          ((uint32_t *)shader_cpu)[0], ((uint32_t *)shader_cpu)[1],
          ((uint32_t *)shader_cpu)[2], ((uint32_t *)shader_cpu)[3]);
   printf("event=0x%x\n", event);
   if (((uint32_t *)shader_cpu)[0] == 0x2A2A2A2A)
      printf(">>> SUCESSO: shader auto-modificou-se via FAU\n");
   else
      printf(">>> shader intacto\n");

   close(fd);
   return 0;
}
