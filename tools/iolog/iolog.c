/*
 * iolog: LD_PRELOAD logger for the kbase job interface and MediaTek GED.
 *
 * Logs, with CLOCK_MONOTONIC timestamps, from any process that has it
 * preloaded:
 *   - KBASE_IOCTL_JOB_SUBMIT on /dev/mali*: every atom (number, core_req,
 *     pre-dependencies, jc), so the dependency graph a driver builds can be
 *     read back;
 *   - read() of kbase events on /dev/mali*: completion code and atom;
 *   - every ioctl on /proc/ged: bridge function id, input and output bytes;
 *   - other /dev/mali* ioctls: number and size only.
 *
 * IOLOG_FILE=<path> (default /tmp/iolog.<pid>.txt). Nothing is changed:
 * every call is passed to the real function first or right after reading
 * its input. Test tool only, not part of the driver.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <setjmp.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

enum { FD_UNKNOWN = 0, FD_OTHER, FD_MALI, FD_GED };
#define MAX_FD 4096

static unsigned char fd_kind[MAX_FD];
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static FILE *out;
static int (*real_ioctl)(int, int, ...);
static ssize_t (*real_read)(int, void *, size_t);
static int (*real_close)(int);
static long (*real_syscall)(long, ...);
static uint64_t last_flush;

/* Caller holds mtx. The game is usually killed at the end of a run. */
static void
maybe_flush(uint64_t t)
{
   if (out && t - last_flush > 100000000ull) {
      fflush(out);
      last_flush = t;
   }
}

static uint64_t
now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

static void
init(void)
{
   if (real_ioctl)
      return;
   real_ioctl = dlsym(RTLD_NEXT, "ioctl");
   real_read = dlsym(RTLD_NEXT, "read");
   real_close = dlsym(RTLD_NEXT, "close");
   real_syscall = dlsym(RTLD_NEXT, "syscall");
}

static FILE *
log_file(void)
{
   if (out)
      return out;
   const char *p = getenv("IOLOG_FILE");
   char buf[256];
   if (!p) {
      snprintf(buf, sizeof(buf), "/tmp/iolog.%d.txt", (int)getpid());
      p = buf;
   }
   out = fopen(p, "a");
   if (out) {
      setvbuf(out, NULL, _IOFBF, 1 << 16);
      fprintf(out, "# iolog pid %d\n", (int)getpid());
   }
   return out;
}

static int
kind_of(int fd)
{
   if (fd < 0 || fd >= MAX_FD)
      return FD_OTHER;
   if (fd_kind[fd])
      return fd_kind[fd];
   char path[64], target[128];
   snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
   ssize_t n = readlink(path, target, sizeof(target) - 1);
   int k = FD_OTHER;
   if (n > 0) {
      target[n] = 0;
      if (!strncmp(target, "/dev/mali", 9))
         k = FD_MALI;
      else if (!strcmp(target, "/proc/ged"))
         k = FD_GED;
   }
   fd_kind[fd] = k;
   return k;
}

static void
hexdump(FILE *f, const void *p, size_t n)
{
   const uint8_t *b = p;
   for (size_t i = 0; i < n; i++)
      fprintf(f, "%02x", b[i]);
}

/* struct kbase_ioctl_job_submit { u64 addr; u32 nr_atoms; u32 stride; } */
struct job_submit {
   uint64_t addr;
   uint32_t nr_atoms, stride;
};

/* struct GED_BRIDGE_PACKAGE (64-bit) */
struct ged_pkg {
   uint32_t function_id;
   int32_t size;
   void *in;
   int32_t in_size;
   void *out;
   int32_t out_size;
};

static void
log_submit(int fd, const struct job_submit *s, int ret, uint64_t t)
{
   FILE *f = log_file();
   if (!f)
      return;
   fprintf(f, "S %llu fd=%d tid=%d n=%u stride=%u ret=%d\n",
           (unsigned long long)t, fd, gettid(), s->nr_atoms, s->stride, ret);
   const uint8_t *a = (const uint8_t *)(uintptr_t)s->addr;
   for (uint32_t i = 0; i < s->nr_atoms && i < 64; i++, a += s->stride) {
      uint64_t jc;
      uint32_t req;
      memcpy(&jc, a, 8);
      memcpy(&req, a + 44, 4);
      fprintf(f, " A n=%u req=0x%x dep=%u:%u,%u:%u prio=%u slot=%u jc=0x%llx",
              a[40], req, a[36], a[37], a[38], a[39], a[41], a[43],
              (unsigned long long)jc);
      if (s->stride >= 64) {
         fprintf(f, " tail=");
         hexdump(f, a + 48, 16);
      }
      fputc('\n', f);
   }
}


/* ---- one-shot snapshot for offline decoding (IOLOG_SNAP=<file>) ----
 * Records: "JCHN" u32 n, n x {u64 jc, u32 core_req, u32 pad}; then
 * "MMAP" u32 0, u64 va, u64 size, data. Copied before the submit that
 * triggers it, so the job descriptors are as the CPU wrote them. */
#define SNAP_RING 8
static uint64_t snap_jc[SNAP_RING][64];
static uint32_t snap_req[SNAP_RING][64];
static unsigned snap_n[SNAP_RING], snap_pos;
static uint64_t first_submit_ns;
static int snap_done;
static __thread sigjmp_buf snap_jmp;
static __thread volatile int snap_in_copy;
static struct sigaction old_segv, old_bus;

static void
snap_handler(int sig, siginfo_t *si, void *uc)
{
   if (snap_in_copy)
      siglongjmp(snap_jmp, 1);
   struct sigaction *o = sig == SIGBUS ? &old_bus : &old_segv;
   if (o->sa_flags & SA_SIGINFO)
      o->sa_sigaction(sig, si, uc);
   else if (o->sa_handler != SIG_DFL && o->sa_handler != SIG_IGN)
      o->sa_handler(sig);
}

static void
snap_take(void)
{
   const char *path = getenv("IOLOG_SNAP");
   FILE *maps = fopen("/proc/self/maps", "r");
   FILE *f = path ? fopen(path, "w") : NULL;
   if (!maps || !f) {
      if (maps) fclose(maps);
      if (f) fclose(f);
      return;
   }
   uint32_t n = snap_n[(snap_pos + SNAP_RING - 1) % SNAP_RING];
   fwrite("JCHN", 4, 1, f);
   fwrite(&n, 4, 1, f);
   /* Only the submit being made: the GPU has not run it, so nothing in
    * its descriptors has been reused or written back yet. */
   for (unsigned k = SNAP_RING - 1; k < SNAP_RING; k++) {
      unsigned r = (snap_pos + k) % SNAP_RING;
      for (unsigned i = 0; i < snap_n[r]; i++) {
         uint32_t pad = 0;
         fwrite(&snap_jc[r][i], 8, 1, f);
         fwrite(&snap_req[r][i], 4, 1, f);
         fwrite(&pad, 4, 1, f);
      }
   }

   struct sigaction sa = {0};
   sa.sa_sigaction = snap_handler;
   sa.sa_flags = SA_SIGINFO;
   sigemptyset(&sa.sa_mask);
   sigaction(SIGSEGV, &sa, &old_segv);
   sigaction(SIGBUS, &sa, &old_bus);

   static uint8_t chunk[65536];
   char line[512];
   uint64_t total = 0, copied_maps = 0;
   const uint64_t max_map = 64ull << 20, max_total = 768ull << 20;
   while (fgets(line, sizeof(line), maps)) {
      unsigned long lo, hi, off = 0;
      char perm[8];
      if (sscanf(line, "%lx-%lx %7s %lx", &lo, &hi, perm, &off) != 4 ||
          !strstr(line, "/dev/mali") || perm[0] != 'r')
         continue;
      uint64_t size = hi - lo;
      if (size > max_map || total + size > max_total)
         continue;
      /* SAME_VA memory: GPU address = CPU address. Memory from the other
       * zones (shaders): kbase maps it at mmap offset = GPU address. */
      uint32_t zero = 0;
      fwrite("MMAP", 4, 1, f);
      fwrite(&zero, 4, 1, f);
      uint64_t va = off >= (1ull << 32) ? off : lo;
      fwrite(&va, 8, 1, f);
      fwrite(&size, 8, 1, f);
      for (uint64_t o = 0; o < size; o += sizeof(chunk)) {
         size_t len = size - o < sizeof(chunk) ? size - o : sizeof(chunk);
         snap_in_copy = 1;
         if (sigsetjmp(snap_jmp, 1) == 0)
            memcpy(chunk, (void *)(uintptr_t)(lo + o), len);
         else
            memset(chunk, 0, len); /* not readable: zeros */
         snap_in_copy = 0;
         fwrite(chunk, len, 1, f);
      }
      total += size;
      copied_maps++;
   }
   sigaction(SIGSEGV, &old_segv, NULL);
   sigaction(SIGBUS, &old_bus, NULL);
   fclose(maps);
   fclose(f);
   FILE *lf = log_file();
   if (lf)
      fprintf(lf, "# snapshot: %u chains, %llu maps, %llu MB\n", n,
              (unsigned long long)copied_maps,
              (unsigned long long)(total >> 20));
}

/* Caller holds mtx. */
static void
snap_note_submit(const struct job_submit *s, uint64_t t)
{
   if (!getenv("IOLOG_SNAP") || snap_done)
      return;
   if (!first_submit_ns)
      first_submit_ns = t;
   const uint8_t *a = (const uint8_t *)(uintptr_t)s->addr;
   unsigned r = snap_pos, n = 0;
   for (uint32_t i = 0; i < s->nr_atoms && n < 64; i++, a += s->stride) {
      uint64_t jc;
      uint32_t req;
      if (s->stride == 72) {
         memcpy(&jc, a + 8, 8);
         memcpy(&req, a + 52, 4);
      } else {
         memcpy(&jc, a, 8);
         memcpy(&req, a + 44, 4);
      }
      if (!jc || (req & 0x200))
         continue; /* soft jobs and dependency-only atoms */
      snap_jc[r][n] = jc;
      snap_req[r][n++] = req;
   }
   snap_n[r] = n;
   snap_pos = (snap_pos + 1) % SNAP_RING;
   const char *at = getenv("IOLOG_SNAP_AT");
   double at_s = at ? atof(at) : 120;
   /* A submit that carries real work: at least IOLOG_SNAP_MIN_JOBS chains
    * (default 6). */
   const char *mj = getenv("IOLOG_SNAP_MIN_JOBS");
   if ((t - first_submit_ns) / 1e9 >= at_s && n >= (mj ? atoi(mj) : 6)) {
      snap_done = 1;
      snap_take();
   }
}

int iolog_ioctl(int fd, int req, ...) __asm__("ioctl");

int
iolog_ioctl(int fd, int req, ...)
{
   va_list ap;
   va_start(ap, req);
   void *arg = va_arg(ap, void *);
   va_end(ap);

   init();
   const unsigned nr = _IOC_NR((unsigned)req), sz = _IOC_SIZE((unsigned)req);
   const unsigned type = _IOC_TYPE((unsigned)req);
   /* By request code: the fd path is not reliable in every thread. kbase
    * uses type 0x80, GED type 'g' with a 40-byte bridge package. */
   int k = type == 0x80 ? FD_MALI
           : (type == 'g' && sz == sizeof(struct ged_pkg)) ? FD_GED
           : kind_of(fd);
   if (k != FD_MALI && k != FD_GED)
      return real_ioctl(fd, req, arg);
   if (k == FD_MALI && fd >= 0 && fd < MAX_FD)
      fd_kind[fd] = FD_MALI;
   uint64_t t0 = now_ns();

   if (k == FD_GED && arg) {
      struct ged_pkg p;
      memcpy(&p, arg, sizeof(p));
      uint8_t in[96];
      int in_n = p.in && p.in_size > 0 ? (p.in_size < 96 ? p.in_size : 96) : 0;
      if (in_n)
         memcpy(in, p.in, in_n);
      int ret = real_ioctl(fd, req, arg);
      uint64_t t1 = now_ns();
      pthread_mutex_lock(&mtx);
      FILE *f = log_file();
      if (f) {
         fprintf(f, "G %llu tid=%d nr=%u fid=%u in=%d:", (unsigned long long)t0,
                 gettid(), nr, p.function_id, p.in_size);
         hexdump(f, in, in_n);
         int out_n = p.out && p.out_size > 0
                        ? (p.out_size < 96 ? p.out_size : 96) : 0;
         fprintf(f, " out=%d:", p.out_size);
         if (ret == 0 && out_n)
            hexdump(f, p.out, out_n);
         fprintf(f, " ret=%d us=%llu\n", ret,
                 (unsigned long long)(t1 - t0) / 1000);
      }
      maybe_flush(t1);
      pthread_mutex_unlock(&mtx);
      return ret;
   }

   /* kbase: JOB_SUBMIT = _IOW(0x80, 2, 16 bytes) */
   if (type == 0x80 && nr == 2 && sz == sizeof(struct job_submit) && arg) {
      struct job_submit s;
      memcpy(&s, arg, sizeof(s));
      pthread_mutex_lock(&mtx);
      snap_note_submit(&s, t0);
      pthread_mutex_unlock(&mtx);
      int ret = real_ioctl(fd, req, arg);
      pthread_mutex_lock(&mtx);
      log_submit(fd, &s, ret, t0);
      maybe_flush(t0);
      pthread_mutex_unlock(&mtx);
      return ret;
   }

   int ret = real_ioctl(fd, req, arg);
   pthread_mutex_lock(&mtx);
   FILE *f = log_file();
   if (f)
      fprintf(f, "I %llu fd=%d type=0x%x nr=%u size=%u ret=%d\n",
              (unsigned long long)t0, fd, type, nr, sz, ret);
   maybe_flush(t0);
   pthread_mutex_unlock(&mtx);
   return ret;
}

/* struct base_jd_event_v2 { u32 event_code; u8 atom_number; u8 pad[3];
 * u64 udata[2]; } = 24 bytes */
ssize_t
read(int fd, void *buf, size_t count)
{
   init();
   ssize_t n = real_read(fd, buf, count);
   if (n <= 0 || kind_of(fd) != FD_MALI || n % 24)
      return n;
   uint64_t t = now_ns();
   pthread_mutex_lock(&mtx);
   FILE *f = log_file();
   if (f) {
      const uint8_t *e = buf;
      for (ssize_t i = 0; i < n; i += 24) {
         uint32_t code;
         memcpy(&code, e + i, 4);
         fprintf(f, "E %llu code=0x%x atom=%u\n", (unsigned long long)t, code,
                 e[i + 4]);
      }
   }
   maybe_flush(t);
   pthread_mutex_unlock(&mtx);
   return n;
}

int
close(int fd)
{
   init();
   if (fd >= 0 && fd < MAX_FD)
      fd_kind[fd] = FD_UNKNOWN;
   return real_close(fd);
}


/* The vendor driver lives in another linker namespace, so LD_PRELOAD does
 * not reach it. Its jump slots for these functions are pointed here
 * instead (GOT patch), from a watcher thread that looks for newly loaded
 * Mali libraries. */
long
syscall(long nr, ...)
{
   va_list ap;
   va_start(ap, nr);
   long a[6];
   for (int i = 0; i < 6; i++)
      a[i] = va_arg(ap, long);
   va_end(ap);
   init();
   if (nr == __NR_ioctl)
      return iolog_ioctl((int)a[0], (int)a[1], (void *)a[2]);
   return real_syscall(nr, a[0], a[1], a[2], a[3], a[4], a[5]);
}

#define MAX_PATCHED 16
static uintptr_t patched[MAX_PATCHED];
static unsigned npatched;

static int
patch_cb(struct dl_phdr_info *info, size_t size, void *data)
{
   (void)size;
   (void)data;
   const char *name = info->dlpi_name ? info->dlpi_name : "";
   if (!(strstr(name, "mali") || strstr(name, "libged") || strstr(name, "libgpud") || strstr(name, "panfrost")) || strstr(name, "iolog"))
      return 0;
   for (unsigned i = 0; i < npatched; i++)
      if (patched[i] == info->dlpi_addr)
         return 0;

   const ElfW(Dyn) *dyn = NULL;
   for (int i = 0; i < info->dlpi_phnum; i++)
      if (info->dlpi_phdr[i].p_type == PT_DYNAMIC)
         dyn = (const ElfW(Dyn) *)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
   if (!dyn)
      return 0;

   uintptr_t base = info->dlpi_addr, symtab = 0, strtab = 0, jmprel = 0,
             rela = 0;
   size_t pltsz = 0, relasz = 0;
   for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
      uintptr_t v = d->d_un.d_ptr;
      switch (d->d_tag) {
      case DT_SYMTAB: symtab = v; break;
      case DT_STRTAB: strtab = v; break;
      case DT_JMPREL: jmprel = v; break;
      case DT_PLTRELSZ: pltsz = d->d_un.d_val; break;
      case DT_RELA: rela = v; break;
      case DT_RELASZ: relasz = d->d_un.d_val; break;
      }
   }
   /* bionic leaves d_ptr unrelocated */
   if (symtab && symtab < base) symtab += base;
   if (strtab && strtab < base) strtab += base;
   if (jmprel && jmprel < base) jmprel += base;
   if (rela && rela < base) rela += base;
   if (!symtab || !strtab)
      return 0;

   unsigned hooked = 0;
   for (int pass = 0; pass < 2; pass++) {
      const ElfW(Rela) *r = (const ElfW(Rela) *)(pass ? rela : jmprel);
      size_t n = (pass ? relasz : pltsz) / sizeof(ElfW(Rela));
      for (size_t i = 0; r && i < n; i++) {
         unsigned type = ELF64_R_TYPE(r[i].r_info);
         if (type != R_AARCH64_JUMP_SLOT && type != R_AARCH64_GLOB_DAT)
            continue;
         const ElfW(Sym) *sym =
            (const ElfW(Sym) *)symtab + ELF64_R_SYM(r[i].r_info);
         const char *sn = (const char *)strtab + sym->st_name;
         void *to = NULL;
         if (!strcmp(sn, "ioctl")) to = (void *)iolog_ioctl;
         else if (!strcmp(sn, "read")) to = (void *)read;
         else if (!strcmp(sn, "close")) to = (void *)close;
         else if (!strcmp(sn, "syscall")) to = (void *)syscall;
         if (!to)
            continue;
         void **slot = (void **)(base + r[i].r_offset);
         uintptr_t page = (uintptr_t)slot & ~(uintptr_t)4095;
         if (mprotect((void *)page, 4096, PROT_READ | PROT_WRITE))
            continue;
         __atomic_store_n(slot, to, __ATOMIC_RELEASE);
         mprotect((void *)page, 4096, PROT_READ);
         hooked++;
      }
   }
   if (npatched < MAX_PATCHED)
      patched[npatched++] = base;
   pthread_mutex_lock(&mtx);
   FILE *f = log_file();
   if (f) {
      fprintf(f, "# patched %u slots in %s\n", hooked, name);
      fflush(f);
   }
   pthread_mutex_unlock(&mtx);
   return 0;
}

static void *
watcher(void *arg)
{
   (void)arg;
   for (int i = 0; i < 6000; i++) { /* 10 minutes */
      dl_iterate_phdr(patch_cb, NULL);
      usleep(100000);
   }
   return NULL;
}

__attribute__((constructor)) static void
start(void)
{
   init();
   pthread_t t;
   if (!pthread_create(&t, NULL, watcher, NULL))
      pthread_detach(t);
}

__attribute__((destructor)) static void
fini(void)
{
   pthread_mutex_lock(&mtx);
   if (out)
      fflush(out);
   pthread_mutex_unlock(&mtx);
}
