#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
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

union kbase_ioctl_mem_alloc {
   struct { uint64_t va_pages; uint64_t commit_pages; uint64_t extension; uint64_t flags; } in;
   struct { uint64_t flags; uint64_t gpu_va; } out;
};
#define KBASE_IOCTL_MEM_ALLOC _IOWR(KBASE_IOCTL_TYPE, 5, union kbase_ioctl_mem_alloc)

#define BASE_MEM_MAP_TRACKING_HANDLE (3ull << 12)

struct base_jd_udata { uint64_t blob[2]; };
struct base_dependency { uint8_t atom_id; uint8_t dependency_type; };
typedef uint32_t base_jd_core_req;
#define BASE_JD_REQ_DEP ((base_jd_core_req)0)

/* Layout UK 11.20+ / DDK r54p1 (validado por stride=64 aceito):
 * jc 0 · udata 8 · extres_list 24 · nr_extres 32 · jit_id 34 ·
 * pre_dep 36 · atom_number 40 · prio 41 · device_nr 42 · jobslot 43 ·
 * core_req 44 · renderpass_id 48 · padding 49..63.
 * FIXME: offsets a confirmar com um átomo real de computação. */
struct base_jd_atom_v2 {
   uint64_t jc;
   struct base_jd_udata udata;
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
_Static_assert(sizeof(struct base_jd_atom_v2) == 64, "atom v2 deve ter 64 bytes");
_Static_assert(__builtin_offsetof(struct base_jd_atom_v2, atom_number) == 40, "atom_number@40");
_Static_assert(__builtin_offsetof(struct base_jd_atom_v2, core_req) == 44, "core_req@44");
_Static_assert(__builtin_offsetof(struct base_jd_atom_v2, renderpass_id) == 48, "renderpass@48");

struct base_jd_event_v2 {
   uint32_t event_code;
   uint8_t atom_number;
   uint8_t padding[3];
   struct base_jd_udata udata;
};

int main(int argc, char **argv) {
   int use_stack = (argc > 1 && !strcmp(argv[1], "--stack"));

   int fd = open("/dev/mali0", O_RDWR);
   if (fd < 0) { printf("open failed: %s\n", strerror(errno)); return 1; }
   printf("open ok, fd=%d\n", fd);

   struct kbase_ioctl_version_check ver = {0, 0};
   if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver)) {
      printf("VERSION_CHECK failed: %s\n", strerror(errno)); return 1;
   }
   printf("VERSION_CHECK ok: %u.%u\n", ver.major, ver.minor);

   struct kbase_ioctl_set_flags set_flags = { .create_flags = 0 };
   if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &set_flags)) {
      printf("SET_FLAGS failed: %s\n", strerror(errno)); return 1;
   }
   printf("SET_FLAGS ok\n");

   void *tracking = mmap(NULL, 4096, PROT_NONE, MAP_SHARED, fd, BASE_MEM_MAP_TRACKING_HANDLE);
   if (tracking == MAP_FAILED) {
      printf("mmap(tracking) failed: %s\n", strerror(errno)); return 1;
   }
   printf("tracking_page ok\n");

   struct base_jd_atom_v2 *atom_page = NULL;
   uint64_t submit_addr = 0;

   if (!use_stack) {
      union kbase_ioctl_mem_alloc alloc = { 0 };
      alloc.in.va_pages = 1;
      alloc.in.commit_pages = 1;
      alloc.in.extension = 0;
      alloc.in.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                       BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR;
      if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &alloc)) {
         printf("MEM_ALLOC failed: %s\n", strerror(errno)); return 1;
      }
      printf("MEM_ALLOC ok: cookie=0x%llx flags=0x%llx%s\n",
             (unsigned long long)alloc.out.gpu_va,
             (unsigned long long)alloc.out.flags,
             (alloc.out.flags & (1ull << 13)) ? " (SAME_VA)" : "");
      void *cpu = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                       (off_t)alloc.out.gpu_va);
      if (cpu == MAP_FAILED) {
         printf("mmap(cookie) failed: %s\n", strerror(errno)); return 1;
      }
      printf("mmap ok: cpu=0x%p = VA de CPU e GPU (SAME_VA)\n", cpu);
      atom_page = cpu;
      submit_addr = (uint64_t)(uintptr_t)cpu;
   } else {
      printf("modo --stack: átomo/probe em memória de pilha (falha esperada no kernel novo)\n");
   }

   struct base_jd_atom_v2 atom;
   memset(&atom, 0, sizeof(atom));
   atom.jc = 0;
   atom.atom_number = 1;
   atom.core_req = BASE_JD_REQ_DEP;

   struct base_jd_atom_v2 *atom_ptr;
   if (use_stack) {
      atom_ptr = &atom;
   } else {
      memcpy(atom_page, &atom, sizeof(atom));
      atom_ptr = atom_page;
   }
   printf("atom em 0x%p · sizeof(base_jd_atom_v2)=%zu · stride a enviar=64\n",
          atom_ptr, sizeof(atom));

   struct kbase_ioctl_job_submit submit = {
      .addr = use_stack ? (uint64_t)(uintptr_t)atom_ptr : submit_addr,
      .nr_atoms = 1,
      .stride = 64, /* sizeof(struct base_jd_atom_v2) no DDK deste device */
   };

   errno = 0;
   if (ioctl(fd, KBASE_IOCTL_JOB_SUBMIT, &submit)) {
      printf("JOB_SUBMIT failed: %s\n", strerror(errno));
      return 1;
   }
   printf("JOB_SUBMIT ok (addr=0x%llx, nr_atoms=1, stride=64)\n",
          (unsigned long long)submit.addr);

   struct pollfd pfd = { .fd = fd, .events = POLLIN };
   int pret = poll(&pfd, 1, 5000);
   printf("poll: %d revents=0x%x\n", pret, pfd.revents);
   if (pret > 0 && (pfd.revents & POLLIN)) {
      struct base_jd_event_v2 ev;
      memset(&ev, 0, sizeof(ev));
      ssize_t n = read(fd, &ev, sizeof(ev));
      printf("event read %zd bytes: code=0x%x atom=%u\n", n, ev.event_code, ev.atom_number);
      if (ev.event_code == 0x1)
         printf(">>> BASE_JD_EVENT_DONE: round-trip submit/completion OK!\n");
      else
         printf(">>> evento != DONE (0x1); verificar\n");
   } else {
      printf("sem evento (timeout / sem POLLIN)\n");
   }

   close(fd);
   return 0;
}