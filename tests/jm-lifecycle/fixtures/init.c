#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>
#include "panvk_kbase_owner.h"
typedef int VkResult;
typedef pthread_mutex_t mtx_t;
struct u_cnd_monotonic { pthread_cond_t c; };
enum { thrd_success=0, thrd_timedout=ETIMEDOUT, mtx_plain=0 };
static int mtx_init(mtx_t *m,int f) { return pthread_mutex_init(m,NULL); }
static int mtx_lock(mtx_t *m) { return pthread_mutex_lock(m); }
static int mtx_unlock(mtx_t *m) { return pthread_mutex_unlock(m); }
static int mtx_destroy(mtx_t *m) { return pthread_mutex_destroy(m); }
static int u_cnd_monotonic_init(struct u_cnd_monotonic *c) {
 pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a,CLOCK_MONOTONIC);
 int r=pthread_cond_init(&c->c,&a); pthread_condattr_destroy(&a);return r;
}
static int u_cnd_monotonic_destroy(struct u_cnd_monotonic *c) { return pthread_cond_destroy(&c->c); }
static int u_cnd_monotonic_broadcast(struct u_cnd_monotonic *c) { return pthread_cond_broadcast(&c->c); }
static int u_cnd_monotonic_wait(struct u_cnd_monotonic *c,mtx_t *m) { return pthread_cond_wait(&c->c,m); }
static int u_cnd_monotonic_timedwait(struct u_cnd_monotonic *c,mtx_t *m,const struct timespec *t) {
 return pthread_cond_timedwait(&c->c,m,t);
}
#define p_atomic_inc(p) __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST)
#define p_atomic_dec_zero(p) (__atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST)==0)
#define p_atomic_add(p,v) __atomic_add_fetch(p,v,__ATOMIC_SEQ_CST)
#define VK_SUCCESS 0
#define VK_TIMEOUT 2
#define VK_ERROR_DEVICE_LOST -4
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
#define VK_ERROR_UNKNOWN -13
#define VK_ERROR_INVALID_EXTERNAL_HANDLE -1000072003
#define OS_TIMEOUT_INFINITE UINT64_MAX
#define PANVK_KBASE_SYNC_TARGET_COUNT 3
#define UNUSED __attribute__((unused))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#include <stddef.h>
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define DIV_ROUND_UP(a,b) (((a)+(b)-1)/(b))
#define mesa_loge(...) ((void)0)
#define vk_errorf(...) VK_ERROR_UNKNOWN
#define vk_device_set_lost(...) VK_ERROR_DEVICE_LOST
static uint64_t os_time_get_nano(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static uint64_t os_time_get_absolute_timeout(uint64_t n) { return os_time_get_nano()+n; }
static void timespec_from_nsec(struct timespec *t,uint64_t n) { t->tv_sec=n/1000000000;t->tv_nsec=n%1000000000; }
static int os_dupfd_cloexec(int fd) { return fcntl(fd,F_DUPFD_CLOEXEC,0); }
static int sync_wait(int fd,int ms) { struct pollfd p={fd,POLLIN,0};int r=poll(&p,1,ms);if(!r)errno=ETIME;return r>0?0:-1; }
struct vk_device { int alloc; };
enum vk_sync_wait_flags { VK_SYNC_WAIT_COMPLETE=0,VK_SYNC_WAIT_PENDING=1,VK_SYNC_WAIT_ANY=2 };
struct vk_sync;
struct vk_sync_wait { struct vk_sync *sync; uint64_t wait_value,stage_mask; };
struct vk_sync_type { VkResult (*wait_many)(struct vk_device *,uint32_t,const struct vk_sync_wait *,enum vk_sync_wait_flags,uint64_t); };
struct vk_sync { const struct vk_sync_type *type; };
typedef VkResult (*panvk_kbase_sync_wait_func)(void *,const uint64_t *,uint64_t);
typedef VkResult (*panvk_kbase_sync_export_func)(void *,const uint64_t *,int *);
enum panvk_kbase_sync_payload { PANVK_KBASE_SYNC_PAYLOAD_NONE,PANVK_KBASE_SYNC_PAYLOAD_GPU,PANVK_KBASE_SYNC_PAYLOAD_FILE,PANVK_KBASE_SYNC_PAYLOAD_OTHER };
struct panvk_batch { bool has_timestamp;unsigned jobs; };
#define util_dynarray_num_elements(a,b) (*(a))
struct panvk_device { int drm_fd;struct vk_device vk;struct { bool enabled;uint64_t gpu_busy_ns,wait_ns; } frame_prof; };
struct kbase_ioctl_job_submit { uint64_t addr;uint32_t nr_atoms,stride; };
#define KBASE_IOCTL_JOB_SUBMIT 42

#include <dirent.h>
#include <stdarg.h>
#include <sys/mman.h>
#define VK_SYSTEM_ALLOCATION_SCOPE_DEVICE 0
#define VK_ERROR_INITIALIZATION_FAILED -3
#define panvk_error(d,e) (e)
static int allocation_n,mutex_n,cond_n,fail_allocation,fail_mutex,fail_cond,fail_event;
static int live_allocations,live_mutexes,live_conds;
static void *tracked_calloc(size_t n,size_t s) {
 if(++allocation_n==fail_allocation)return NULL;
 void *p=calloc(n,s);if(p)live_allocations++;return p;
}
static void tracked_free(void *p) {if(p){live_allocations--;free(p);}}
static int tracked_mutex(mtx_t *p,int f) {if(++mutex_n==fail_mutex)return -1;int r=mtx_init(p,f);if(!r)live_mutexes++;return r;}
static int tracked_cond(struct u_cnd_monotonic *p) {if(++cond_n==fail_cond)return -1;int r=u_cnd_monotonic_init(p);if(!r)live_conds++;return r;}
static int tracked_mutex_destroy(mtx_t *p) {live_mutexes--;return mtx_destroy(p);}
static int tracked_cond_destroy(struct u_cnd_monotonic *p) {live_conds--;return u_cnd_monotonic_destroy(p);}
static int tracked_eventfd(unsigned n,int f) {if(fail_event){errno=EMFILE;return -1;}return eventfd(n,f);}
#define calloc tracked_calloc
#define free tracked_free
#define mtx_init tracked_mutex
#define mtx_destroy tracked_mutex_destroy
#define u_cnd_monotonic_init tracked_cond
#define u_cnd_monotonic_destroy tracked_cond_destroy
#define eventfd tracked_eventfd
static void *vk_zalloc(void *a,size_t s,size_t align,int scope) {return calloc(1,s);}
static void vk_free(void *a,void *p) {free(p);}
static const char *pan_env_flag(const char *s) {return NULL;}
static bool panvk_kbase_chain_enabled(void) {return true;}
static bool debug_get_bool_option(const char *s,bool d) {return d;}



struct panvk_kbase_owner {
   mtx_t mutex;
   struct u_cnd_monotonic cond;
   int refs;
   unsigned active;
   void *data;
};

struct panvk_kbase_owner *
panvk_kbase_owner_create(void *data)
{
   struct panvk_kbase_owner *owner = calloc(1, sizeof(*owner));
   if (!owner)
      return NULL;
   if (mtx_init(&owner->mutex, mtx_plain) != thrd_success)
      goto fail;
   if (u_cnd_monotonic_init(&owner->cond) != thrd_success) {
      mtx_destroy(&owner->mutex);
      goto fail;
   }
   owner->refs = 1;
   owner->data = data;
   return owner;
fail:
   free(owner);
   return NULL;
}

struct panvk_kbase_owner *
panvk_kbase_owner_ref(struct panvk_kbase_owner *owner)
{
   if (owner)
      p_atomic_inc(&owner->refs);
   return owner;
}

void
panvk_kbase_owner_unref(struct panvk_kbase_owner *owner)
{
   if (!owner || !p_atomic_dec_zero(&owner->refs))
      return;
   assert(!owner->active);
   u_cnd_monotonic_destroy(&owner->cond);
   mtx_destroy(&owner->mutex);
   free(owner);
}

void *
panvk_kbase_owner_enter(struct panvk_kbase_owner *owner)
{
   if (!owner)
      return NULL;
   mtx_lock(&owner->mutex);
   void *data = owner->data;
   if (data) {
      owner->active++;
      panvk_kbase_owner_ref(owner);
   }
   mtx_unlock(&owner->mutex);
   return data;
}

void
panvk_kbase_owner_leave(struct panvk_kbase_owner *owner)
{
   mtx_lock(&owner->mutex);
   assert(owner->active);
   if (!--owner->active)
      u_cnd_monotonic_broadcast(&owner->cond);
   mtx_unlock(&owner->mutex);
   panvk_kbase_owner_unref(owner);
}

void
panvk_kbase_owner_close(struct panvk_kbase_owner *owner)
{
   mtx_lock(&owner->mutex);
   owner->data = NULL;
   while (owner->active)
      u_cnd_monotonic_wait(&owner->cond, &owner->mutex);
   mtx_unlock(&owner->mutex);
}
#define PANVK_KBASE_MAX_CHAIN 120
#define PANVK_KBASE_ATOM_STRIDE 64 /* base_jd_atom_v2 + MTK BM_2 frame_nr */
/* The job atoms of one submission, plus a FENCE_WAIT and a DEP atom. */
#define PANVK_KBASE_MAX_SUB_ATOMS (PANVK_KBASE_MAX_CHAIN + 2)
/* Each submission in flight holds at least one of the 255 atom numbers. */
#define PANVK_KBASE_MAX_SUBS 255
/* From the kbase JM UAPI, mali_base_jm_kernel.h (r54p1). */
#define BASE_JD_DEP_TYPE_DATA (1U << 0)
#define BASE_JD_DEP_TYPE_ORDER (1U << 1)
#define BASE_JD_REQ_DEP 0U
#define BASE_JD_REQ_PERMON (1U << 7)
#define BASE_JD_REQ_SOFT_JOB (1U << 9)
#define BASE_JD_REQ_SOFT_FENCE_TRIGGER (BASE_JD_REQ_SOFT_JOB | 0x2)
#define BASE_JD_REQ_SOFT_FENCE_WAIT (BASE_JD_REQ_SOFT_JOB | 0x3)
#define BASE_JD_REQ_EVENT_ONLY_ON_FAILURE (1U << 12)
#define BASE_JD_EVENT_DONE 0x1
#define BASE_JD_EVENT_JOB_CANCELLED 0x4002

/* struct base_fence, mali_base_kernel.h */
struct panvk_kbase_base_fence {
   int fd;
   int stream_fd;
};

struct panvk_kbase_job {
   uint64_t jc;
   uint32_t core_req;
   bool overlap_second;
   /* PANVK_FAULT_REPORT: what the atom carries (0076). */
   struct panvk_cmd_buffer *cmdbuf;
   struct panvk_batch *batch;
   unsigned batch_idx;
};

/* PANVK_FAULT_REPORT: what each atom number carries while in flight. */
struct panvk_kbase_atom_info {
   uintptr_t cmdbuf_id;
   uint64_t jc;
   uint32_t core_req;
   unsigned batch_idx;
   unsigned nr_jobs;
};

static struct panvk_kbase_atom_info
panvk_kbase_job_info(const struct panvk_kbase_job *job, bool snapshot)
{
   return (struct panvk_kbase_atom_info){
      .cmdbuf_id = (uintptr_t)job->cmdbuf,
      .jc = job->jc,
      .core_req = job->core_req,
      .batch_idx = job->batch_idx,
      .nr_jobs = snapshot && job->batch
                    ? util_dynarray_num_elements(&job->batch->jobs, void *)
                    : 0,
   };
}

/* The job atoms of the vkQueueSubmit being built, not yet in the kernel. */
struct panvk_kbase_chain {
   struct panvk_kbase_job jobs[PANVK_KBASE_MAX_CHAIN];
   struct panvk_batch *batches[PANVK_KBASE_MAX_CHAIN];
   unsigned nr_jobs, nr_batches;
   /* Merged imported sync_files the next submission waits for, or -1. */
   int wait_fd;
};

struct panvk_kbase_sub {
   uint64_t seqno;
   uint8_t last; /* the atom whose event completes the submission */
   bool done;
   int fence_fd; /* out-fence made by an export, -1 until then */
   uint64_t submit_ns; /* PANVK_FRAME_PROF */
   uint16_t nr_atoms;
   uint8_t atoms[PANVK_KBASE_MAX_SUB_ATOMS];
};

enum panvk_kbase_atom_use {
   PANVK_KBASE_ATOM_FREE = 0,
   PANVK_KBASE_ATOM_SUB, /* in a submission, freed when it completes */
   PANVK_KBASE_ATOM_AUX, /* an export's FENCE_TRIGGER, freed by its event */
};

struct panvk_kbase_jm {
   int fd;
   int wake_fd;
   bool closing;
   struct panvk_kbase_owner *owner;
   /* Protects everything below, and is held across JOB_SUBMIT so that seqno
    * order is kernel submission order. The reader polls and reads without
    * it. */
   mtx_t mtx;
   struct u_cnd_monotonic cnd; /* completions processed, or reader done */
   uint64_t last_seqno;
   uint64_t completed_seqno;
   struct panvk_kbase_sub subs[PANVK_KBASE_MAX_SUBS]; /* ring, oldest first */
   unsigned head, count;
   uint8_t atom_use[256];
   unsigned nr_free, cursor;
   bool lost;
   bool reading; /* a thread is polling/reading the kbase fd */
   /* Only touched by the submitting thread (queue access is synchronised). */
   struct panvk_kbase_chain chain;
   bool wait_submit, cpu_fence_wait, cpu_fence_export;
   bool ctrl_no_fence_wait_dep, ctrl_export_early;
   /* Read once at creation (were read on every submit). */
   bool chain_enabled, no_signal;
   bool overlap_enabled;
   struct {
      struct panvk_priv_bo *heap, *tls, *wls;
   } overlap_banks[2];
   /* Counted always, printed at destroy with PANVK_KBASE_FENCE_TRACE=1:
    * waits by payload, FENCE_WAIT atoms, exports by outcome. */
   bool trace;
   struct {
      uint64_t wait_none, wait_own, wait_file, wait_cpu, fence_wait_atoms;
      uint64_t export_new, export_dup, export_done, export_cpu;
   } stats;

   /* FourFectVK diagnostics (0076/0077), all under mtx. */
   struct panvk_device *dev;
   uint64_t t0_ns, fail_print_ns;
   unsigned errors;
   bool fault_report, audit_all;
   unsigned fault_reports;
   unsigned audit_batches, audit_suspect_batches, audit_lines;
   struct panvk_kbase_atom_info atom_info[256];
   /* PANVK_FRAME_PROF: end of the GPU busy time counted so far. */
   uint64_t busy_end_ns;
};

static void
panvk_kbase_jm_close(struct panvk_kbase_jm *k)
{
   mtx_lock(&k->mtx);
   k->closing = true;
   const uint64_t wake = 1;
   ssize_t ret;
   do {
      ret = write(k->wake_fd, &wake, sizeof(wake));
   } while (ret < 0 && errno == EINTR);
   u_cnd_monotonic_broadcast(&k->cnd);
   mtx_unlock(&k->mtx);
   panvk_kbase_owner_close(k->owner);
}
static bool panvk_kbase_progress_locked(struct panvk_kbase_jm *k,uint64_t t) {assert(0);return false;}
static void panvk_kbase_log_stats(struct panvk_kbase_jm *k) {}
static void panvk_priv_bo_unref(void *p) {assert(!p);}
static VkResult
panvk_kbase_jm_create(struct panvk_device *device, struct panvk_kbase_jm **out)
{
   struct panvk_kbase_jm *k =
      vk_zalloc(&device->vk.alloc, sizeof(*k), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!k)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   k->fd = device->drm_fd;
   k->chain.wait_fd = -1;
   /* Atom number 0 means "no dependency": never hand it out. */
   k->atom_use[0] = PANVK_KBASE_ATOM_SUB;
   k->nr_free = 255;
   k->cursor = 1;
   k->wait_submit = pan_env_flag("PANVK_KBASE_WAIT_SUBMIT") != NULL;
   k->cpu_fence_wait = pan_env_flag("PANVK_KBASE_CPU_FENCE_WAIT") != NULL;
   k->cpu_fence_export = pan_env_flag("PANVK_KBASE_CPU_FENCE_EXPORT") != NULL;
   k->trace = pan_env_flag("PANVK_KBASE_FENCE_TRACE") != NULL;
   k->ctrl_no_fence_wait_dep =
      pan_env_flag("PANVK_KBASE_CTRL_NO_FENCE_WAIT_DEP") != NULL;
   k->ctrl_export_early = pan_env_flag("PANVK_KBASE_CTRL_EXPORT_EARLY") != NULL;
   k->dev = device;
   k->chain_enabled = panvk_kbase_chain_enabled();
#if PAN_ARCH == 9
   k->overlap_enabled = k->chain_enabled &&
      debug_get_bool_option("PANVK_JM_OVERLAP", false);
#endif
   k->no_signal = pan_env_flag("PANVK_KBASE_NO_SIGNAL") != NULL;
   k->t0_ns = os_time_get_nano();
   k->fault_report = pan_env_flag("PANVK_FAULT_REPORT") != NULL;
   k->audit_all = pan_env_flag("PANVK_FAULT_AUDIT_ALL") != NULL;

   if (mtx_init(&k->mtx, mtx_plain) != thrd_success)
      goto err_free;
   if (u_cnd_monotonic_init(&k->cnd) != thrd_success)
      goto err_mtx;

   k->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
   if (k->wake_fd < 0)
      goto err_cnd;
   k->owner = panvk_kbase_owner_create(k);
   if (!k->owner) {
      close(k->wake_fd);
      goto err_cnd;
   }

   *out = k;
   return VK_SUCCESS;

err_cnd:
   u_cnd_monotonic_destroy(&k->cnd);
err_mtx:
   mtx_destroy(&k->mtx);
err_free:
   vk_free(&device->vk.alloc, k);
   return panvk_error(device, VK_ERROR_INITIALIZATION_FAILED);
}
static void
panvk_kbase_jm_destroy(struct panvk_device *device, struct panvk_kbase_jm *k)
{
   if (!k)
      return;

   panvk_kbase_jm_close(k);
   uint64_t wake;
   while (read(k->wake_fd, &wake, sizeof(wake)) < 0 && errno == EINTR)
      ;
   mtx_lock(&k->mtx);
   assert(!k->reading);
   const uint64_t timeout = os_time_get_absolute_timeout(
      10ull * 1000 * 1000 * 1000);
   while (!k->lost && k->completed_seqno < k->last_seqno) {
      if (!panvk_kbase_progress_locked(k, timeout))
         break;
   }
   if (k->lost || k->completed_seqno < k->last_seqno)
      mesa_loge("panvk: kbase GPU retirement unproven at destroy");
   mtx_unlock(&k->mtx);

   panvk_kbase_log_stats(k);

   for (unsigned i = 0; i < k->count; i++) {
      struct panvk_kbase_sub *sub =
         &k->subs[(k->head + i) % PANVK_KBASE_MAX_SUBS];
      if (sub->fence_fd >= 0)
         close(sub->fence_fd);
   }
   if (k->chain.wait_fd >= 0)
      close(k->chain.wait_fd);
   for (unsigned i = 0; i < 2; i++) {
      panvk_priv_bo_unref(k->overlap_banks[i].heap);
      panvk_priv_bo_unref(k->overlap_banks[i].tls);
      panvk_priv_bo_unref(k->overlap_banks[i].wls);
   }
   close(k->wake_fd);
   panvk_kbase_owner_unref(k->owner);
   u_cnd_monotonic_destroy(&k->cnd);
   mtx_destroy(&k->mtx);
   vk_free(&device->vk.alloc, k);
}

struct kbase_kmod_mem_stats { uint64_t growable_commit_bytes; };
static void kbase_kmod_mem_stats(struct kbase_kmod_mem_stats *s) { s->growable_commit_bytes=0; }
static uint64_t fa_mem_available_mb(void) { return 0; }
static const char *panvk_jd_event_str(unsigned event) { return "test"; }
static void fault_log(const char *f,...) { char b[512];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a); }
#undef mesa_loge
#define mesa_loge(...) fault_log(__VA_ARGS__)
static void
panvk_kbase_fault_report(struct panvk_kbase_jm *k, uint8_t atom,
                         uint64_t seqno, uint32_t event_code)
{
   const struct panvk_kbase_atom_info *ai = &k->atom_info[atom];
   const bool frag = ai->core_req == 0x01;

   mesa_loge("panvk-fault: event 0x%x (%s) atom %u submission %" PRIu64
             " %s chain 0x%" PRIx64 ", cmdbuf-id 0x%" PRIxPTR " batch %u",
             event_code,
             panvk_jd_event_str(event_code), atom, seqno,
             !ai->jc ? "no job" : frag ? "fragment" : "vertex/tiler/compute",
             ai->jc, ai->cmdbuf_id, ai->batch_idx);
   mesa_loge("panvk-fault:   submit snapshot: %u batch jobs; "
             "live job status unavailable", ai->nr_jobs);

   struct kbase_kmod_mem_stats ms = {0};
   kbase_kmod_mem_stats(&ms);
   mesa_loge("panvk-fault:   t=%.1fs since device creation, MemAvailable %"
             PRIu64 " MB, growable BOs backed %" PRIu64 " MB",
             (os_time_get_nano() - k->t0_ns) / 1e9,
             fa_mem_available_mb(), ms.growable_commit_bytes >> 20);
}

static unsigned fd_count(void) { DIR *d=opendir("/proc/self/fd");assert(d);unsigned n=0;while(readdir(d))n++;closedir(d);return n; }
static void fault_snapshot(void) {
 size_t page=sysconf(_SC_PAGESIZE);void *p=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(p!=MAP_FAILED);
 struct panvk_batch *batch=p;batch->jobs=29;
 struct panvk_kbase_job job={.jc=0x1234,.core_req=0x16,.cmdbuf=p,.batch=p,.batch_idx=7};
 struct panvk_kbase_jm k={0};k.atom_info[1]=panvk_kbase_job_info(&job,true);
 assert(k.atom_info[1].nr_jobs==29 && k.atom_info[1].jc==0x1234 && k.atom_info[1].batch_idx==7);
 assert(!mprotect(p,page,PROT_NONE));panvk_kbase_fault_report(&k,1,42,0x4003);assert(!munmap(p,page));
}
int main(void) {
 unsigned initial_fds=fd_count();fault_snapshot();
 for(unsigned round=0;round<64;round++)for(unsigned which=0;which<8;which++) {
  allocation_n=mutex_n=cond_n=0;
  fail_allocation=which<2?which+1:0;fail_mutex=which>=2&&which<4?which-1:0;
  fail_cond=which>=4&&which<6?which-3:0;fail_event=which==6;
  struct panvk_device device={.drm_fd=-1};struct panvk_kbase_jm *k=NULL;
  VkResult r=panvk_kbase_jm_create(&device,&k);
  if(which<7) {assert(r!=VK_SUCCESS && !k);} else {assert(!r&&k);panvk_kbase_jm_destroy(&device,k);}
  assert(!live_allocations && !live_mutexes && !live_conds);
 }
 assert(fd_count()==initial_fds);
 puts("INIT PASS 512 cycles actual queue create/destroy + owner; allocation/mutex/condition/eventfd failures; zero live objects");
}
