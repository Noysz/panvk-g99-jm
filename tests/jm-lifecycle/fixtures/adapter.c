#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef int VkResult;
typedef int mtx_t;
struct u_cnd_monotonic { int unused; };
struct panvk_batch { bool has_timestamp; unsigned jobs; };
struct panvk_device {
   int vk;
   struct { bool enabled; uint64_t gpu_busy_ns; } frame_prof;
};
struct kbase_ioctl_job_submit { uint64_t addr; uint32_t nr_atoms, stride; };
#define VK_SUCCESS 0
#define VK_ERROR_DEVICE_LOST -4
#define VK_ERROR_OUT_OF_HOST_MEMORY -1
#define KBASE_IOCTL_JOB_SUBMIT 42
#define PANVK_KBASE_SYNC_TARGET_COUNT 3
#define OS_TIMEOUT_INFINITE UINT64_MAX
#define thrd_timedout 2
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define util_dynarray_num_elements(a,b) (*(a))
#define mesa_loge(...) ((void)0)
#define vk_device_set_lost(...) ((void)0)
#define p_atomic_add(p,n) (*(p)+=(n))
static void mtx_lock(mtx_t *m) {}
static void mtx_unlock(mtx_t *m) {}
static void u_cnd_monotonic_broadcast(struct u_cnd_monotonic *c) {}
static int u_cnd_monotonic_timedwait(struct u_cnd_monotonic *c, mtx_t *m, struct timespec *t) { return thrd_timedout; }
static uint64_t os_time_get_nano(void) { return 100; }
static void timespec_from_nsec(struct timespec *t, uint64_t n) { t->tv_sec=n/1000000000; t->tv_nsec=n%1000000000; }
static int os_dupfd_cloexec(int fd) { return fd+1; }
static int mock_close(int fd) { return 0; }
#define close mock_close
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


struct panvk_kbase_event_v2 { uint32_t event_code; uint8_t atom_number; uint8_t pad[3]; uint64_t udata[2]; };
static struct panvk_kbase_jm *active;
static uint8_t captured[122][64];
static unsigned captured_count, ioctl_calls, injected_prefix;
static bool fail_submit, early, read_failed;
static struct panvk_kbase_event_v2 incoming[32];
static unsigned incoming_count;
static void panvk_kbase_handle_event(struct panvk_kbase_jm *, const struct panvk_kbase_event_v2 *);
static struct panvk_kbase_event_v2 event_for(unsigned i, uint32_t code)
{
   struct panvk_kbase_event_v2 ev = {.event_code=code, .atom_number=captured[i][40]};
   memcpy(ev.udata, captured[i]+8, sizeof(ev.udata));
   return ev;
}
static int mock_ioctl(int fd, int req, struct kbase_ioctl_job_submit *s)
{
   assert(req==KBASE_IOCTL_JOB_SUBMIT && s->stride==64 && s->nr_atoms<=122);
   ioctl_calls++;
   captured_count=s->nr_atoms;
   memcpy(captured,(void *)(uintptr_t)s->addr,s->nr_atoms*64);
   for (unsigned i=0;i<s->nr_atoms;i++) {
      uint32_t core;
      memcpy(&core,captured[i]+44,4);
      if ((core & ~BASE_JD_REQ_EVENT_ONLY_ON_FAILURE)==BASE_JD_REQ_SOFT_FENCE_WAIT ||
          core==BASE_JD_REQ_SOFT_FENCE_TRIGGER) {
         uint64_t addr;
         memcpy(&addr,captured[i],8);
         struct panvk_kbase_base_fence *f=(void *)(uintptr_t)addr;
         if (core==BASE_JD_REQ_SOFT_FENCE_TRIGGER) f->fd=70;
         memset(captured[i],0,8);
         memcpy(captured[i],f,sizeof(*f));
      }
   }
   if (early)
      for (unsigned i=0;i<injected_prefix;i++) {
         struct panvk_kbase_event_v2 ev=event_for(i,1);
         panvk_kbase_handle_event(active,&ev);
      }
   errno=EFAULT;
   return fail_submit ? -1 : 0;
}
static int mock_ppoll(struct pollfd *p, unsigned long n, const struct timespec *t, const void *s)
{
   p->revents=POLLIN;
   return 1;
}
static ssize_t mock_read(int fd, void *p, size_t n)
{
   assert(n==sizeof(incoming));
   memcpy(p,incoming,incoming_count*sizeof(incoming[0]));
   errno=EFAULT;
   return read_failed ? -1 : (ssize_t)(incoming_count*sizeof(incoming[0]));
}
static void panvk_kbase_report_failure(struct panvk_kbase_jm *k,uint8_t a,uint64_t s,uint32_t e) {}
static VkResult panvk_kbase_wait_seqno(struct panvk_kbase_jm *k,uint64_t s,uint64_t t) { return k->lost ? VK_ERROR_DEVICE_LOST : VK_SUCCESS; }
#define ioctl mock_ioctl
#define ppoll mock_ppoll
#define read mock_read
static void
panvk_kbase_atom(uint8_t *a, uint8_t num, uint64_t jc, uint32_t core_req,
                 uint8_t dep0, uint8_t type0, uint8_t dep1, uint8_t type1)
{
   memset(a, 0, PANVK_KBASE_ATOM_STRIDE);
   memcpy(a + 0, &jc, 8);
   a[36] = dep0;                /* pre_dep[0].atom_id */
   a[37] = dep0 ? type0 : 0;    /* pre_dep[0].dependency_type */
   a[38] = dep1;                /* pre_dep[1] */
   a[39] = dep1 ? type1 : 0;
   a[40] = num;                 /* atom_number */
   memcpy(a + 44, &core_req, 4);
}
static void
panvk_kbase_free_atoms(struct panvk_kbase_jm *k, unsigned n,
                       const uint8_t *nums)
{
   for (unsigned i = 0; i < n; i++) {
      assert(k->atom_use[nums[i]] != PANVK_KBASE_ATOM_FREE);
      k->atom_use[nums[i]] = PANVK_KBASE_ATOM_FREE;
   }
   k->nr_free += n;
}
static struct panvk_kbase_sub *
panvk_kbase_find_sub(struct panvk_kbase_jm *k, uint8_t atom)
{
   for (unsigned i = 0; i < k->count; i++) {
      struct panvk_kbase_sub *sub =
         &k->subs[(k->head + i) % PANVK_KBASE_MAX_SUBS];
      for (unsigned j = 0; j < sub->nr_atoms; j++) {
         if (sub->atoms[j] == atom)
            return sub;
      }
   }
   return NULL;
}
static void
panvk_kbase_handle_event(struct panvk_kbase_jm *k,
                         const struct panvk_kbase_event_v2 *ev)
{
   const uint8_t atom = ev->atom_number;

   if (k->atom_use[atom] == PANVK_KBASE_ATOM_AUX) {
      if (ev->event_code != BASE_JD_EVENT_DONE) {
         mesa_loge("panvk: kbase out-fence atom %u: event 0x%x", atom,
                   ev->event_code);
         k->lost = true;
         vk_device_set_lost(&k->dev->vk, "kbase out-fence failed: 0x%x",
                            ev->event_code);
      }
      if (!k->lost)
         panvk_kbase_free_atoms(k, 1, &atom);
      return;
   }

   struct panvk_kbase_sub *sub = panvk_kbase_find_sub(k, atom);
   if (!sub) {
      mesa_loge("panvk: kbase event 0x%x for unknown atom %u", ev->event_code,
                atom);
      return;
   }

   if (ev->event_code != BASE_JD_EVENT_DONE &&
       !(k->atom_info[atom].core_req == BASE_JD_REQ_SOFT_FENCE_WAIT &&
         ev->event_code == BASE_JD_EVENT_JOB_CANCELLED)) {
      panvk_kbase_report_failure(k, atom, sub->seqno, ev->event_code);
      k->lost = true;
      vk_device_set_lost(&k->dev->vk, "kbase atom %u failed: 0x%x", atom,
                         ev->event_code);
   }
   if (k->lost)
      return;

   if (atom == sub->last)
      sub->done = true;

   /* Submissions complete in order: retire the finished ones. */
   const uint64_t now = k->dev->frame_prof.enabled ? os_time_get_nano() : 0;
   while (k->count && k->subs[k->head].done) {
      struct panvk_kbase_sub *s = &k->subs[k->head];
      if (now) {
         /* GPU busy = union of [submit, completion seen] (FRAME_PROF). */
         const uint64_t start = MAX2(s->submit_ns, k->busy_end_ns);
         if (s->submit_ns && now > start)
            p_atomic_add(&k->dev->frame_prof.gpu_busy_ns, now - start);
         k->busy_end_ns = MAX2(k->busy_end_ns, now);
      }
      panvk_kbase_free_atoms(k, s->nr_atoms, s->atoms);
      if (s->fence_fd >= 0)
         close(s->fence_fd);
      k->completed_seqno = s->seqno;
      k->head = (k->head + 1) % PANVK_KBASE_MAX_SUBS;
      k->count--;
   }
}
static bool
panvk_kbase_progress_locked(struct panvk_kbase_jm *k, uint64_t abs_timeout_ns)
{
   if (k->reading) {
      if (!abs_timeout_ns)
         return false;
      struct timespec ts;
      timespec_from_nsec(&ts, abs_timeout_ns);
      return u_cnd_monotonic_timedwait(&k->cnd, &k->mtx, &ts) != thrd_timedout;
   }

   k->reading = true;
   mtx_unlock(&k->mtx);

   struct pollfd pfds[2] = {
      {.fd = k->fd, .events = POLLIN},
      {.fd = k->wake_fd, .events = POLLIN},
   };
   int ret;
   do {
      struct timespec rel, *timeout = NULL;
      if (abs_timeout_ns != OS_TIMEOUT_INFINITE) {
         uint64_t now = os_time_get_nano();
         timespec_from_nsec(&rel, abs_timeout_ns > now ? abs_timeout_ns - now : 0);
         timeout = &rel;
      }
      ret = ppoll(pfds, 2, timeout, NULL);
   } while (ret < 0 && errno == EINTR);

   /* poll() said there are events and nobody else reads: read() returns
    * every pending event that fits without blocking. */
   struct panvk_kbase_event_v2 ev[32];
   ssize_t len = 0;
   bool failed = ret < 0;
   if (ret > 0 && (pfds[0].revents & POLLIN)) {
      len = read(k->fd, ev, sizeof(ev));
      failed = len <= 0;
   } else if (ret > 0 && (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL))) {
      failed = true; /* POLLERR / POLLHUP / POLLNVAL */
   }
   if (failed)
      mesa_loge("panvk: kbase event poll/read failed: %s", strerror(errno));

   mtx_lock(&k->mtx);
   for (unsigned i = 0; len > 0 && i < len / sizeof(ev[0]); i++)
      panvk_kbase_handle_event(k, &ev[i]);
   if (failed)
      k->lost = true;
   k->reading = false;
   u_cnd_monotonic_broadcast(&k->cnd);
   return ret != 0;
}
static VkResult
panvk_kbase_alloc_atoms(struct panvk_kbase_jm *k, unsigned n, uint8_t *nums,
                        enum panvk_kbase_atom_use use)
{
   assert(n > 0 && n < 256);
   while (k->nr_free < n) {
      if (k->lost || k->closing)
         return VK_ERROR_DEVICE_LOST;
      panvk_kbase_progress_locked(k, OS_TIMEOUT_INFINITE);
   }

   if (k->lost || k->closing)
      return VK_ERROR_DEVICE_LOST;

   for (unsigned i = 0; i < n; i++) {
      while (k->atom_use[k->cursor] != PANVK_KBASE_ATOM_FREE)
         k->cursor = k->cursor % 255 + 1;
      nums[i] = k->cursor;
      k->atom_use[k->cursor] = use;
      k->cursor = k->cursor % 255 + 1;
   }
   k->nr_free -= n;
   return VK_SUCCESS;
}
static VkResult
panvk_kbase_submit_locked(struct panvk_kbase_jm *k,
                          const struct panvk_kbase_job *jobs, unsigned n,
                          int wait_fd, uint64_t *seqno)
{
   if (k->lost || k->closing)
      return VK_ERROR_DEVICE_LOST;

   if (!n && wait_fd < 0) {
      *seqno = k->last_seqno;
      return VK_SUCCESS;
   }

   const unsigned nr_atoms = n + (wait_fd >= 0) + (n == 0);
   uint8_t nums[PANVK_KBASE_MAX_SUB_ATOMS];
   VkResult result =
      panvk_kbase_alloc_atoms(k, nr_atoms, nums, PANVK_KBASE_ATOM_SUB);
   if (result != VK_SUCCESS)
      return result;

   /* The previous submission's last atom, while it is still in flight. */
   const uint8_t prev =
      k->count
         ? k->subs[(k->head + k->count - 1) % PANVK_KBASE_MAX_SUBS].last
         : 0;
   uint8_t atoms[PANVK_KBASE_MAX_SUB_ATOMS][PANVK_KBASE_ATOM_STRIDE];
   struct panvk_kbase_base_fence wait_fence = {.fd = wait_fd, .stream_fd = -1};
   uint8_t wait_atom = 0;
   unsigned a = 0;

   if (wait_fd >= 0) {
      k->stats.fence_wait_atoms++;
      wait_atom = nums[a];
      k->atom_info[nums[a]] = (struct panvk_kbase_atom_info){
         .core_req = BASE_JD_REQ_SOFT_FENCE_WAIT,
      };
      panvk_kbase_atom(atoms[a], nums[a], (uintptr_t)&wait_fence,
                       BASE_JD_REQ_SOFT_FENCE_WAIT |
                          BASE_JD_REQ_EVENT_ONLY_ON_FAILURE,
                       0, 0, 0, 0);
      a++;
   }

   /* ORDER on the previous submission and on the wait: a failure there does
    * not cancel this work (an errored fence was waited for like a signalled
    * one before 8.15, too). */
   for (unsigned i = 0; i < n; i++, a++) {
      uint32_t core_req = jobs[i].core_req;
      if (core_req == 0x16 && jobs[i].batch && jobs[i].batch->has_timestamp)
         core_req |= BASE_JD_REQ_PERMON;
      k->atom_info[nums[a]] = panvk_kbase_job_info(&jobs[i], k->fault_report);
      if (i + 1 < n)
         core_req |= BASE_JD_REQ_EVENT_ONLY_ON_FAILURE;
      if (i == 0)
         panvk_kbase_atom(atoms[a], nums[a], jobs[i].jc, core_req, prev,
                          BASE_JD_DEP_TYPE_ORDER,
                          k->ctrl_no_fence_wait_dep ? 0 : wait_atom,
                          BASE_JD_DEP_TYPE_ORDER);
      else if (jobs[i].overlap_second) {
         assert(i >= 2 && jobs[i].core_req == 0x16 &&
                jobs[i - 1].core_req == 0x01 &&
                jobs[i - 2].core_req == 0x16);
         panvk_kbase_atom(atoms[a], nums[a], jobs[i].jc, core_req, nums[a - 2],
                          BASE_JD_DEP_TYPE_DATA, 0, 0);
      } else if (jobs[i - 1].overlap_second) {
         assert(i >= 3 && jobs[i].core_req == 0x01);
         panvk_kbase_atom(atoms[a], nums[a], jobs[i].jc, core_req, nums[a - 1],
                          BASE_JD_DEP_TYPE_DATA, nums[a - 2],
                          BASE_JD_DEP_TYPE_DATA);
      } else
         panvk_kbase_atom(atoms[a], nums[a], jobs[i].jc, core_req, nums[a - 1],
                          BASE_JD_DEP_TYPE_DATA, 0, 0);
   }

   if (!n) {
      k->atom_info[nums[a]] = (struct panvk_kbase_atom_info){0};
      /* Only a wait: a dependency-only atom completes the submission. */
      panvk_kbase_atom(atoms[a], nums[a], 0, BASE_JD_REQ_DEP, prev,
                       BASE_JD_DEP_TYPE_ORDER, wait_atom,
                       BASE_JD_DEP_TYPE_ORDER);
      a++;
   }
   assert(a == nr_atoms);

   struct kbase_ioctl_job_submit submit = {
      .addr = (uint64_t)(uintptr_t)atoms,
      .nr_atoms = nr_atoms,
      .stride = PANVK_KBASE_ATOM_STRIDE,
   };
   if (ioctl(k->fd, KBASE_IOCTL_JOB_SUBMIT, &submit)) {
      /* Some atoms may be in the kernel: keep their numbers, give up. */
      mesa_loge("panvk: kbase JOB_SUBMIT of %u atoms failed: %s", nr_atoms,
                strerror(errno));
      k->lost = true;
      u_cnd_monotonic_broadcast(&k->cnd);
      return VK_ERROR_DEVICE_LOST;
   }

   assert(k->count < PANVK_KBASE_MAX_SUBS);
   struct panvk_kbase_sub *sub =
      &k->subs[(k->head + k->count) % PANVK_KBASE_MAX_SUBS];
   k->count++;
   *sub = (struct panvk_kbase_sub){
      .seqno = ++k->last_seqno,
      .last = nums[nr_atoms - 1],
      .fence_fd = -1,
      .nr_atoms = nr_atoms,
      .submit_ns = k->dev->frame_prof.enabled ? os_time_get_nano() : 0,
   };
   memcpy(sub->atoms, nums, nr_atoms);
   *seqno = sub->seqno;
   return VK_SUCCESS;
}
static VkResult
panvk_kbase_sync_export_active(
   void *data, const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
   int *sync_file)
{
   struct panvk_kbase_jm *k = data;
   const uint64_t seqno = targets[0];
   VkResult result = VK_SUCCESS;
   bool cpu_wait = k->cpu_fence_export;

   *sync_file = -1;
   if (k->ctrl_export_early)
      return VK_SUCCESS;

   mtx_lock(&k->mtx);
   if (k->lost || k->closing) {
      mtx_unlock(&k->mtx);
      return VK_ERROR_DEVICE_LOST;
   }
   while (!cpu_wait && k->completed_seqno < seqno) {
      struct panvk_kbase_sub *sub =
         &k->subs[(k->head + (seqno - k->subs[k->head].seqno)) %
                  PANVK_KBASE_MAX_SUBS];
      assert(k->count && sub->seqno == seqno);

      if (sub->fence_fd < 0) {
         uint8_t num;
         result = panvk_kbase_alloc_atoms(k, 1, &num, PANVK_KBASE_ATOM_AUX);
         if (result != VK_SUCCESS)
            break;
         /* The allocation may have waited: the submission may have completed,
          * or another export may have made its fence. Look again. */
         if (k->completed_seqno >= seqno || sub->fence_fd >= 0) {
            panvk_kbase_free_atoms(k, 1, &num);
            continue;
         }

         struct panvk_kbase_base_fence fence = {.fd = -1, .stream_fd = -1};
         uint8_t atom[PANVK_KBASE_ATOM_STRIDE];
         panvk_kbase_atom(atom, num, (uintptr_t)&fence,
                          BASE_JD_REQ_SOFT_FENCE_TRIGGER, sub->last,
                          BASE_JD_DEP_TYPE_DATA, 0, 0);
         struct kbase_ioctl_job_submit submit = {
            .addr = (uint64_t)(uintptr_t)atom,
            .nr_atoms = 1,
            .stride = PANVK_KBASE_ATOM_STRIDE,
         };
         if (ioctl(k->fd, KBASE_IOCTL_JOB_SUBMIT, &submit)) {
            mesa_loge("panvk: kbase FENCE_TRIGGER submit failed: %s",
                      strerror(errno));
            k->lost = true;
            u_cnd_monotonic_broadcast(&k->cnd);
            result = VK_ERROR_DEVICE_LOST;
            break;
         }
         if (fence.fd < 0) {
            /* The atom completes (and frees its number) with an error. */
            mesa_loge("panvk: kbase FENCE_TRIGGER gave no fd");
            cpu_wait = true;
            break;
         }
         sub->fence_fd = fence.fd;
         k->stats.export_new++;
      } else {
         k->stats.export_dup++;
      }

      *sync_file = os_dupfd_cloexec(sub->fence_fd);
      if (*sync_file < 0)
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
      break;
   }
   if (cpu_wait)
      k->stats.export_cpu++;
   else if (*sync_file < 0 && result == VK_SUCCESS)
      k->stats.export_done++;
   mtx_unlock(&k->mtx);

   if (cpu_wait && result == VK_SUCCESS)
      result = panvk_kbase_wait_seqno(k, seqno, OS_TIMEOUT_INFINITE);
   return result;
}

static unsigned scenarios;
static void init(struct panvk_kbase_jm *k,void *unused) {
 memset(k,0,sizeof(*k)); static struct panvk_device dev; k->dev=&dev;
 k->nr_free=255;k->cursor=1;k->atom_use[0]=PANVK_KBASE_ATOM_SUB;
 fail_submit=early=read_failed=false;ioctl_calls=0;incoming_count=0;active=k;
}
static void clear_owners(void) {}
static void clear_owners(void);

static void error_retirement(void)
{
   for (unsigned shadow = 0; shadow < 1; shadow++) {
      for (unsigned failure = 0; failure < 4; failure++) {
         struct panvk_kbase_jm k;
         struct panvk_kbase_job jobs[4] = {{0}};
         struct panvk_kbase_event_v2 first[4], second[3];
         uint64_t seq = 0;
         init(&k, NULL);
         assert(!panvk_kbase_submit_locked(&k, jobs, 4, -1, &seq));
         for (unsigned i = 0; i < 4; i++)
            first[i] = event_for(i, BASE_JD_EVENT_DONE);
         assert(!panvk_kbase_submit_locked(&k, jobs, 3, -1, &seq));
         for (unsigned i = 0; i < 3; i++)
            second[i] = event_for(i, BASE_JD_EVENT_DONE);
         struct panvk_kbase_event_v2 error = first[failure];
         error.event_code = 0x4003;
         panvk_kbase_handle_event(&k, &error);
         for (unsigned i = 3; i > 0; i--)
            panvk_kbase_handle_event(&k, &second[i - 1]);
         for (unsigned i = 4; i > 0; i--)
            panvk_kbase_handle_event(&k, &first[i - 1]);
         assert(k.lost && k.completed_seqno==0 && k.count==2);
         assert(k.nr_free == 248);
         for (unsigned i = 0; i < 4; i++)
            assert(k.atom_use[first[i].atom_number] == PANVK_KBASE_ATOM_SUB);
         for (unsigned i = 0; i < 3; i++)
            assert(k.atom_use[second[i].atom_number] == PANVK_KBASE_ATOM_SUB);
         unsigned calls = ioctl_calls;
         assert(panvk_kbase_submit_locked(&k, jobs, 1, -1, &seq) == VK_ERROR_DEVICE_LOST);
         assert(ioctl_calls == calls);
         clear_owners();
         scenarios++;
      }
      for (unsigned prefix = 0; prefix <= 3; prefix++) {
         struct panvk_kbase_jm k;
         struct panvk_kbase_job jobs[4] = {{0}};
         struct panvk_kbase_event_v2 prior[4];
         uint64_t seq = 0;
         init(&k, NULL);
         assert(!panvk_kbase_submit_locked(&k, jobs, 4, -1, &seq));
         for (unsigned i = 0; i < 4; i++)
            prior[i] = event_for(i, BASE_JD_EVENT_DONE);
         fail_submit = true;
         assert(panvk_kbase_submit_locked(&k, jobs, 3, -1, &seq) == VK_ERROR_DEVICE_LOST);
         for (unsigned i = 0; i < prefix; i++) {
            struct panvk_kbase_event_v2 ev = event_for(i, BASE_JD_EVENT_DONE);
            panvk_kbase_handle_event(&k, &ev);
         }
         for (unsigned i = 0; i < 4; i++)
            panvk_kbase_handle_event(&k, &prior[i]);
         assert(k.lost && k.completed_seqno==0 && k.count==1 && k.nr_free==248);
         clear_owners();
         scenarios++;
      }
      for (unsigned consumed = 0; consumed <= 32; consumed++) {
         struct panvk_kbase_jm k;
         struct panvk_kbase_job jobs[32] = {{0}};
         uint64_t seq = 0;
         init(&k, NULL);
         assert(!panvk_kbase_submit_locked(&k, jobs, 32, -1, &seq));
         for (unsigned i = 0; i < consumed; i++)
            incoming[i] = event_for(i, BASE_JD_EVENT_DONE);
         incoming_count = consumed;
         read_failed = true;
         assert(panvk_kbase_progress_locked(&k, 1));
         assert(k.lost && k.completed_seqno==0 && k.nr_free==223);
         for (unsigned i = consumed; i < 32; i++) {
            struct panvk_kbase_event_v2 ev = event_for(i, BASE_JD_EVENT_DONE);
            panvk_kbase_handle_event(&k, &ev);
         }
         assert(k.completed_seqno==0 && k.count==1 && k.nr_free==223);
         clear_owners();
         scenarios++;
      }
      for (unsigned failed_event = 0; failed_event < 2; failed_event++) {
         struct panvk_kbase_jm k;
         struct panvk_kbase_job job = {0};
         uint64_t seq = 0, targets[3] = {1};
         int fd = -1;
         init(&k, NULL);
         assert(!panvk_kbase_submit_locked(&k, &job, 1, -1, &seq));
         assert(!panvk_kbase_sync_export_active(&k, targets, &fd));
         struct panvk_kbase_event_v2 ev = event_for(0, failed_event ? 0x4003 : BASE_JD_EVENT_DONE);
         if (!failed_event)
            k.lost = true;
         panvk_kbase_handle_event(&k, &ev);
         assert(k.lost && k.completed_seqno==0 && k.nr_free==253);
         assert(k.atom_use[ev.atom_number] == PANVK_KBASE_ATOM_AUX);
         ev.event_code = BASE_JD_EVENT_DONE;
         panvk_kbase_handle_event(&k, &ev);
         assert(k.nr_free == 253);
         clear_owners();
         scenarios++;
      }
   }
}

static void normal_events(void) {
 for(unsigned count=1;count<=120;count++) {
  struct panvk_kbase_jm k;struct panvk_kbase_job jobs[120]={{0}};uint64_t seq;
  init(&k,NULL);assert(!panvk_kbase_submit_locked(&k,jobs,count,-1,&seq));
  assert(ioctl_calls==1 && captured_count==count && k.count==1);
  struct panvk_kbase_event_v2 tail=event_for(count-1,BASE_JD_EVENT_DONE);
  panvk_kbase_handle_event(&k,&tail);assert(!k.lost && !k.count && k.completed_seqno==1 && k.nr_free==255);
  panvk_kbase_handle_event(&k,&tail);assert(k.nr_free==255);scenarios++;
 }
}
int main(void) {normal_events();error_retirement();printf("ADAPTER PASS scenarios=%u grouped=1 partial=1 EFAULT_0_32=1 late_events=1 original_IDs=1 GPU_terminal_not_proven=1\n",scenarios);}
