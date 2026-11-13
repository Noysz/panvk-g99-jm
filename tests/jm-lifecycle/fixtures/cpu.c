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
struct vk_device { int unused; };
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
struct panvk_device { struct vk_device vk;struct { bool enabled;uint64_t gpu_busy_ns,wait_ns; } frame_prof; };
struct kbase_ioctl_job_submit { uint64_t addr;uint32_t nr_atoms,stride; };
#define KBASE_IOCTL_JOB_SUBMIT 42
static int early_fd=-1;
static int mock_ioctl(int fd,int req,struct kbase_ioctl_job_submit *s)
{
 if(early_fd>=0){
  for(unsigned i=s->nr_atoms;i>0;i--){
   const uint8_t *atom=(const uint8_t *)(uintptr_t)s->addr+(i-1)*s->stride;
   uint8_t ev[24]={1};ev[4]=atom[40];memcpy(ev+8,atom+8,16);
   assert(write(early_fd,ev,sizeof(ev))==sizeof(ev));
  }
  return 0;
 }
 errno=EFAULT;return -1;
}
#define ioctl mock_ioctl

static VkResult vk_sync_wait(struct vk_device *d, struct vk_sync *s, uint64_t value,
                             enum vk_sync_wait_flags flags, uint64_t timeout)
{
 struct vk_sync_wait w={.sync=s,.wait_value=value};
 return s->type->wait_many(d,1,&w,flags,timeout);
}



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
enum kbase_cpu_sync_state {
   KBASE_CPU_SYNC_RESET,
   KBASE_CPU_SYNC_PENDING,
   KBASE_CPU_SYNC_WAITING,
   KBASE_CPU_SYNC_SIGNALED,
   KBASE_CPU_SYNC_FAILED,
};

struct kbase_cpu_sync {
   struct vk_sync sync;
   mtx_t mutex;
   struct u_cnd_monotonic cond;
   enum kbase_cpu_sync_state state;
   VkResult result;
   void *pending_data;
   struct panvk_kbase_owner *pending_owner;
   uint64_t payload_serial;
   panvk_kbase_sync_wait_func pending_wait;
   /* Makes a sync_file for the pending GPU work, NULL if it cannot. */
   panvk_kbase_sync_export_func pending_export;
   uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT];
   /* Imported sync_file payload (SYNC_FD semaphores/fences), -1 if none. */
   int sync_file;
};

/* Drop an imported sync_file payload. Caller holds ks->mutex. */
static void
kbase_cpu_sync_drop_file(struct kbase_cpu_sync *ks)
{
   if (ks->sync_file >= 0)
      close(ks->sync_file);
   ks->sync_file = -1;
}

static void
kbase_cpu_sync_drop_pending(struct kbase_cpu_sync *ks)
{
   panvk_kbase_owner_unref(ks->pending_owner);
   ks->pending_owner = NULL;
   ks->pending_data = NULL;
   ks->pending_wait = NULL;
   ks->pending_export = NULL;
   ks->payload_serial++;
}

static VkResult
kbase_cpu_sync_init(struct vk_device *device, struct vk_sync *sync,
                    uint64_t initial_value)
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);
   int ret = mtx_init(&ks->mutex, mtx_plain);
   if (ret != thrd_success)
      return vk_errorf(device, VK_ERROR_UNKNOWN, "mtx_init failed");

   ret = u_cnd_monotonic_init(&ks->cond);
   if (ret != thrd_success) {
      mtx_destroy(&ks->mutex);
      return vk_errorf(device, VK_ERROR_UNKNOWN, "cnd_init failed");
   }

   ks->state = initial_value ? KBASE_CPU_SYNC_SIGNALED : KBASE_CPU_SYNC_RESET;
   ks->result = VK_SUCCESS;
   ks->pending_data = NULL;
   ks->pending_wait = NULL;
   ks->pending_export = NULL;
   ks->pending_owner = NULL;
   ks->payload_serial = 0;
   memset(ks->targets, 0, sizeof(ks->targets));
   ks->sync_file = -1;
   return VK_SUCCESS;
}

static void
kbase_cpu_sync_finish(UNUSED struct vk_device *device, struct vk_sync *sync)
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);
   kbase_cpu_sync_drop_file(ks);
   kbase_cpu_sync_drop_pending(ks);
   u_cnd_monotonic_destroy(&ks->cond);
   mtx_destroy(&ks->mutex);
}

static VkResult
kbase_cpu_sync_signal(UNUSED struct vk_device *device, struct vk_sync *sync,
                      UNUSED uint64_t value)
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);
   mtx_lock(&ks->mutex);
   kbase_cpu_sync_drop_file(ks);
   kbase_cpu_sync_drop_pending(ks);
   ks->state = KBASE_CPU_SYNC_SIGNALED;
   ks->result = VK_SUCCESS;
   ks->pending_data = NULL;
   ks->pending_wait = NULL;
   ks->pending_export = NULL;
   u_cnd_monotonic_broadcast(&ks->cond);
   mtx_unlock(&ks->mutex);
   return VK_SUCCESS;
}

static VkResult
kbase_cpu_sync_reset(UNUSED struct vk_device *device, struct vk_sync *sync)
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);
   mtx_lock(&ks->mutex);
   assert(ks->state != KBASE_CPU_SYNC_WAITING);
   kbase_cpu_sync_drop_file(ks);
   kbase_cpu_sync_drop_pending(ks);
   ks->state = KBASE_CPU_SYNC_RESET;
   ks->result = VK_SUCCESS;
   ks->pending_data = NULL;
   ks->pending_wait = NULL;
   ks->pending_export = NULL;
   memset(ks->targets, 0, sizeof(ks->targets));
   mtx_unlock(&ks->mutex);
   return VK_SUCCESS;
}

static VkResult
kbase_cpu_sync_wait_many(struct vk_device *device, uint32_t wait_count,
                         const struct vk_sync_wait *waits,
                         enum vk_sync_wait_flags wait_flags,
                         uint64_t abs_timeout_ns);

static void
kbase_cpu_sync_set_pending(
   struct vk_sync *sync, void *data, struct panvk_kbase_owner *owner,
   panvk_kbase_sync_wait_func wait,
   panvk_kbase_sync_export_func export_fn,
   const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT])
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);

   assert(sync->type->wait_many == kbase_cpu_sync_wait_many);
   mtx_lock(&ks->mutex);
   /* Binary payloads follow DRM-syncobj semantics: a new signal replaces
    * whatever fence the sync currently holds, so re-arming from SIGNALED
    * (e.g. a reused WSI semaphore that was CPU-waited) is legal.  Only an
    * in-progress wait on the old payload would be a genuine bug. */
   assert(ks->state != KBASE_CPU_SYNC_WAITING);
   kbase_cpu_sync_drop_file(ks);
   kbase_cpu_sync_drop_pending(ks);
   ks->pending_owner = panvk_kbase_owner_ref(owner);
   ks->pending_data = data;
   ks->pending_wait = wait;
   ks->pending_export = export_fn;
   memcpy(ks->targets, targets, sizeof(ks->targets));
   ks->result = VK_SUCCESS;
   ks->state = KBASE_CPU_SYNC_PENDING;
   u_cnd_monotonic_broadcast(&ks->cond);
   mtx_unlock(&ks->mutex);
}

void
panvk_kbase_sync_set_pending(
   struct vk_sync *sync, void *data, panvk_kbase_sync_wait_func wait,
   panvk_kbase_sync_export_func export_fn,
   const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT])
{
   kbase_cpu_sync_set_pending(sync, data, NULL, wait, export_fn, targets);
}

void
panvk_kbase_sync_set_pending_owned(
   struct vk_sync *sync, struct panvk_kbase_owner *owner,
   panvk_kbase_sync_wait_func wait, panvk_kbase_sync_export_func export_fn,
   const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT])
{
   kbase_cpu_sync_set_pending(sync, owner, owner, wait, export_fn, targets);
}

enum panvk_kbase_sync_payload
panvk_kbase_sync_get_payload(struct vk_sync *sync, void **data,
                             uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
                             int *sync_file)
{
   if (sync->type->wait_many != kbase_cpu_sync_wait_many)
      return PANVK_KBASE_SYNC_PAYLOAD_OTHER;

   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);
   enum panvk_kbase_sync_payload payload = PANVK_KBASE_SYNC_PAYLOAD_OTHER;

   mtx_lock(&ks->mutex);
   if (ks->state == KBASE_CPU_SYNC_SIGNALED) {
      payload = PANVK_KBASE_SYNC_PAYLOAD_NONE;
   } else if (ks->state == KBASE_CPU_SYNC_PENDING && ks->sync_file >= 0) {
      *sync_file = os_dupfd_cloexec(ks->sync_file);
      if (*sync_file >= 0)
         payload = PANVK_KBASE_SYNC_PAYLOAD_FILE;
   } else if (ks->state == KBASE_CPU_SYNC_PENDING && ks->pending_export) {
      *data = ks->pending_data;
      memcpy(targets, ks->targets, sizeof(ks->targets));
      payload = PANVK_KBASE_SYNC_PAYLOAD_GPU;
   }
   mtx_unlock(&ks->mutex);
   return payload;
}

static VkResult
kbase_cpu_sync_wait_one(struct vk_device *device, struct kbase_cpu_sync *ks,
                        enum vk_sync_wait_flags wait_flags,
                        uint64_t abs_timeout_ns)
{
   struct timespec abs_timeout_ts;
   timespec_from_nsec(&abs_timeout_ts, abs_timeout_ns);

   mtx_lock(&ks->mutex);
   while (true) {
      switch (ks->state) {
      case KBASE_CPU_SYNC_SIGNALED:
         mtx_unlock(&ks->mutex);
         return VK_SUCCESS;
      case KBASE_CPU_SYNC_FAILED: {
         VkResult result = ks->result;
         mtx_unlock(&ks->mutex);
         return result;
      }
      case KBASE_CPU_SYNC_PENDING: {
         if (wait_flags & VK_SYNC_WAIT_PENDING) {
            mtx_unlock(&ks->mutex);
            return VK_SUCCESS;
         }

         panvk_kbase_sync_wait_func wait = ks->pending_wait;
         void *data = ks->pending_data;
         struct panvk_kbase_owner *owner =
            panvk_kbase_owner_ref(ks->pending_owner);
         uint64_t serial = ks->payload_serial;
         int file = ks->sync_file >= 0 ? os_dupfd_cloexec(ks->sync_file) : -1;
         if (ks->sync_file >= 0 && file < 0) {
            panvk_kbase_owner_unref(owner);
            mtx_unlock(&ks->mutex);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }
         uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT];
         memcpy(targets, ks->targets, sizeof(targets));
         if (file >= 0)
            targets[0] = file;
         ks->state = KBASE_CPU_SYNC_WAITING;
         mtx_unlock(&ks->mutex);

         VkResult result = wait(data, targets, abs_timeout_ns);
         if (file >= 0)
            close(file);
         panvk_kbase_owner_unref(owner);

         mtx_lock(&ks->mutex);
         if (ks->payload_serial != serial) {
            mtx_unlock(&ks->mutex);
            return result;
         }
         if (result == VK_SUCCESS) {
            kbase_cpu_sync_drop_pending(ks);
            ks->state = KBASE_CPU_SYNC_SIGNALED;
            ks->pending_data = NULL;
            ks->pending_wait = NULL;
         } else if (result == VK_TIMEOUT) {
            ks->state = KBASE_CPU_SYNC_PENDING;
         } else {
            ks->state = KBASE_CPU_SYNC_FAILED;
            ks->result = result;
         }
         u_cnd_monotonic_broadcast(&ks->cond);
         mtx_unlock(&ks->mutex);
         return result;
      }
      case KBASE_CPU_SYNC_RESET:
      case KBASE_CPU_SYNC_WAITING: {
         if (abs_timeout_ns == 0) {
            mtx_unlock(&ks->mutex);
            return VK_TIMEOUT;
         }

         int ret = u_cnd_monotonic_timedwait(&ks->cond, &ks->mutex,
                                              &abs_timeout_ts);
         if (ret == thrd_timedout) {
            mtx_unlock(&ks->mutex);
            return VK_TIMEOUT;
         }
         if (ret != thrd_success) {
            mtx_unlock(&ks->mutex);
            return vk_errorf(device, VK_ERROR_UNKNOWN,
                             "kbase sync condition wait failed");
         }
         break;
      }
      }
   }
}

static VkResult
kbase_cpu_sync_wait_many(struct vk_device *device,
                         uint32_t wait_count, const struct vk_sync_wait *waits,
                         enum vk_sync_wait_flags wait_flags,
                         uint64_t abs_timeout_ns)
{
   bool wait_any = !!(wait_flags & VK_SYNC_WAIT_ANY);

   if (!wait_any) {
      for (uint32_t i = 0; i < wait_count; i++) {
         struct kbase_cpu_sync *ks =
            container_of(waits[i].sync, struct kbase_cpu_sync, sync);
         VkResult result = kbase_cpu_sync_wait_one(
            device, ks, wait_flags, abs_timeout_ns);
         if (result != VK_SUCCESS)
            return result;
      }
      return VK_SUCCESS;
   }

   while (true) {
      for (uint32_t i = 0; i < wait_count; i++) {
         struct kbase_cpu_sync *ks =
            container_of(waits[i].sync, struct kbase_cpu_sync, sync);
         VkResult result = kbase_cpu_sync_wait_one(device, ks, wait_flags, 0);
         if (result == VK_SUCCESS)
            return VK_SUCCESS;
         if (result != VK_TIMEOUT)
            return result;
      }
      if (abs_timeout_ns == 0 || os_time_get_nano() >= abs_timeout_ns)
         return VK_TIMEOUT;
      sched_yield();
   }
}

static VkResult
kbase_cpu_sync_move(UNUSED struct vk_device *device, struct vk_sync *dst,
                    struct vk_sync *src)
{
   if (dst == src)
      return VK_SUCCESS;

   struct kbase_cpu_sync *ks_dst =
      container_of(dst, struct kbase_cpu_sync, sync);
   struct kbase_cpu_sync *ks_src =
      container_of(src, struct kbase_cpu_sync, sync);

   struct kbase_cpu_sync *first =
      (uintptr_t)ks_dst < (uintptr_t)ks_src ? ks_dst : ks_src;
   struct kbase_cpu_sync *second = first == ks_dst ? ks_src : ks_dst;
   mtx_lock(&first->mutex);
   mtx_lock(&second->mutex);
   assert(ks_src->state != KBASE_CPU_SYNC_WAITING);
   assert(ks_dst->state != KBASE_CPU_SYNC_WAITING);

   kbase_cpu_sync_drop_pending(ks_dst);
   ks_dst->pending_owner = ks_src->pending_owner;
   ks_src->pending_owner = NULL;
   ks_src->payload_serial++;
   ks_dst->state = ks_src->state;
   ks_dst->result = ks_src->result;
   ks_dst->pending_data = ks_src->pending_data;
   ks_dst->pending_wait = ks_src->pending_wait;
   ks_dst->pending_export = ks_src->pending_export;
   memcpy(ks_dst->targets, ks_src->targets, sizeof(ks_dst->targets));
   kbase_cpu_sync_drop_file(ks_dst);
   ks_dst->sync_file = ks_src->sync_file;
   ks_src->sync_file = -1;

   ks_src->state = KBASE_CPU_SYNC_RESET;
   ks_src->result = VK_SUCCESS;
   ks_src->pending_data = NULL;
   ks_src->pending_wait = NULL;
   ks_src->pending_export = NULL;
   memset(ks_src->targets, 0, sizeof(ks_src->targets));
   u_cnd_monotonic_broadcast(&ks_dst->cond);
   u_cnd_monotonic_broadcast(&ks_src->cond);
   mtx_unlock(&second->mutex);
   mtx_unlock(&first->mutex);
   return VK_SUCCESS;
}

/* Wait for an imported sync_file. targets[0] holds the fd (copied under the
 * lock by kbase_cpu_sync_wait_one, the fd stays open until the payload is
 * replaced, reset or destroyed). */
static VkResult
kbase_cpu_sync_file_wait(UNUSED void *data,
                         const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
                         uint64_t abs_timeout_ns)
{
   int fd = (int)targets[0];
   int timeout_ms;
   if (abs_timeout_ns == OS_TIMEOUT_INFINITE || abs_timeout_ns > INT64_MAX) {
      timeout_ms = -1;
   } else {
      uint64_t now = os_time_get_nano();
      uint64_t rel = abs_timeout_ns > now ? abs_timeout_ns - now : 0;
      uint64_t ms = DIV_ROUND_UP(rel, 1000000);
      timeout_ms = ms > INT32_MAX ? INT32_MAX : (int)ms;
   }

   if (sync_wait(fd, timeout_ms) == 0)
      return VK_SUCCESS;
   if (errno == ETIME || errno == ETIMEDOUT)
      return VK_TIMEOUT;
   return VK_ERROR_DEVICE_LOST;
}

/* SYNC_FD import: the payload becomes "signaled when the sync_file
 * signals". The runtime closes the caller's fd on success, so keep a dup. */
static VkResult
kbase_cpu_sync_import_sync_file(struct vk_device *device, struct vk_sync *sync,
                                int sync_file)
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);
   if (sync_file < 0)
      return kbase_cpu_sync_signal(device, sync, 0);

   int fd = os_dupfd_cloexec(sync_file);
   if (fd < 0)
      return vk_errorf(device, VK_ERROR_INVALID_EXTERNAL_HANDLE,
                       "dup of sync_file failed");

   mtx_lock(&ks->mutex);
   assert(ks->state != KBASE_CPU_SYNC_WAITING);
   kbase_cpu_sync_drop_file(ks);
   kbase_cpu_sync_drop_pending(ks);
   ks->sync_file = fd;
   memset(ks->targets, 0, sizeof(ks->targets));
   ks->targets[0] = (uint64_t)fd;
   ks->pending_data = ks;
   ks->pending_wait = kbase_cpu_sync_file_wait;
   ks->pending_export = NULL;
   ks->result = VK_SUCCESS;
   ks->state = KBASE_CPU_SYNC_PENDING;
   u_cnd_monotonic_broadcast(&ks->cond);
   mtx_unlock(&ks->mutex);
   return VK_SUCCESS;
}

/* SYNC_FD export. An imported sync_file is handed back as a dup. Pending
 * work from our own queue becomes a kernel fence through pending_export (a
 * kbase FENCE_TRIGGER atom, Phase 8.15). Without one, wait for the work on
 * the CPU and export -1 ("already signaled", allowed by the spec for
 * SYNC_FD): correct, but it serializes the caller with the GPU. */
static VkResult
kbase_cpu_sync_export_sync_file(struct vk_device *device, struct vk_sync *sync,
                                int *sync_file)
{
   struct kbase_cpu_sync *ks = container_of(sync, struct kbase_cpu_sync, sync);

   mtx_lock(&ks->mutex);
   if (ks->state == KBASE_CPU_SYNC_PENDING && ks->sync_file >= 0) {
      int fd = os_dupfd_cloexec(ks->sync_file);
      mtx_unlock(&ks->mutex);
      if (fd < 0)
         return vk_errorf(device, VK_ERROR_OUT_OF_HOST_MEMORY,
                          "dup of sync_file failed");
      *sync_file = fd;
      return VK_SUCCESS;
   }
   if (ks->state == KBASE_CPU_SYNC_PENDING && ks->pending_export) {
      panvk_kbase_sync_export_func export_fn = ks->pending_export;
      void *data = ks->pending_data;
      struct panvk_kbase_owner *owner =
         panvk_kbase_owner_ref(ks->pending_owner);
      uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT];
      memcpy(targets, ks->targets, sizeof(targets));
      mtx_unlock(&ks->mutex);
      VkResult result = export_fn(data, targets, sync_file);
      panvk_kbase_owner_unref(owner);
      return result;
   }
   mtx_unlock(&ks->mutex);

   const struct vk_sync_wait w = { .sync = sync, .stage_mask = ~0ull };
   VkResult result = kbase_cpu_sync_wait_many(device, 1, &w, 0,
                                              OS_TIMEOUT_INFINITE);
   if (result != VK_SUCCESS)
      return result;
   *sync_file = -1;
   return VK_SUCCESS;
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


struct panvk_kbase_event_v2 { uint32_t event_code; uint8_t atom_number; uint8_t pad[3]; uint64_t udata[2]; };
static void panvk_kbase_report_failure(struct panvk_kbase_jm *k,uint8_t a,uint64_t s,uint32_t e) {}
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
panvk_kbase_wait_seqno(struct panvk_kbase_jm *k, uint64_t seqno,
                       uint64_t abs_timeout_ns)
{
   VkResult result = VK_SUCCESS;
   const uint64_t t0 = k->dev->frame_prof.enabled ? os_time_get_nano() : 0;
   bool waited = false;

   mtx_lock(&k->mtx);
   while (k->completed_seqno < seqno) {
      waited = true;
      if (k->lost || k->closing) {
         result = VK_ERROR_DEVICE_LOST;
         break;
      }
      if (!panvk_kbase_progress_locked(k, abs_timeout_ns) &&
          k->completed_seqno < seqno) {
         result = VK_TIMEOUT;
         break;
      }
   }
   if (k->lost || k->closing)
      result = VK_ERROR_DEVICE_LOST;
   mtx_unlock(&k->mtx);
   if (t0 && waited)
      p_atomic_add(&k->dev->frame_prof.wait_ns, os_time_get_nano() - t0);
   return result;
}
static VkResult
panvk_kbase_sync_wait_cb(void *data,
                         const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
                         uint64_t abs_timeout_ns)
{
   struct panvk_kbase_owner *owner = data;
   struct panvk_kbase_jm *k = panvk_kbase_owner_enter(owner);
   if (!k)
      return VK_ERROR_DEVICE_LOST;
   VkResult result = panvk_kbase_wait_seqno(k, targets[0], abs_timeout_ns);
   panvk_kbase_owner_leave(owner);
   return result;
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
static VkResult
panvk_kbase_sync_export_cb(
   void *data, const uint64_t targets[PANVK_KBASE_SYNC_TARGET_COUNT],
   int *sync_file)
{
   struct panvk_kbase_owner *owner = data;
   struct panvk_kbase_jm *k = panvk_kbase_owner_enter(owner);
   if (!k)
      return VK_ERROR_DEVICE_LOST;
   VkResult result = panvk_kbase_sync_export_active(k, targets, sync_file);
   panvk_kbase_owner_leave(owner);
   return result;
}
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
static VkResult
panvk_kbase_wait_external(struct panvk_kbase_jm *k, struct vk_sync *sync,
                          uint64_t value)
{
   VkResult result;
   do {
      mtx_lock(&k->mtx);
      const bool stopped = k->closing || k->lost;
      mtx_unlock(&k->mtx);
      if (stopped)
         return VK_ERROR_DEVICE_LOST;
      result = vk_sync_wait(&k->dev->vk, sync, value, VK_SYNC_WAIT_COMPLETE,
                            os_time_get_absolute_timeout(50000000));
   } while (result == VK_TIMEOUT);
   return result;
}
static const struct vk_sync_type type={.wait_many=kbase_cpu_sync_wait_many};
static unsigned cases;
static void init_queue(struct panvk_kbase_jm *k,struct panvk_device *d,int fd,bool shadow)
{
 memset(k,0,sizeof(*k));k->dev=d;k->fd=fd;k->wake_fd=eventfd(0,EFD_NONBLOCK|EFD_CLOEXEC);
 assert(k->wake_fd>=0);assert(!mtx_init(&k->mtx,0));assert(!u_cnd_monotonic_init(&k->cnd));
 k->owner=panvk_kbase_owner_create(k);assert(k->owner);k->nr_free=255;k->cursor=1;
}
static void fini_queue(struct panvk_kbase_jm *k)
{
 panvk_kbase_jm_close(k);assert(!k->reading);
 panvk_kbase_owner_unref(k->owner);close(k->wake_fd);
 u_cnd_monotonic_destroy(&k->cnd);mtx_destroy(&k->mtx);
}
static void init_sync(struct kbase_cpu_sync *s)
{
 memset(s,0,sizeof(*s));s->sync.type=&type;assert(!kbase_cpu_sync_init(NULL,&s->sync,0));
}
static void arm(struct kbase_cpu_sync *s,struct panvk_kbase_jm *k,uint64_t n)
{
 uint64_t targets[3]={n};
 panvk_kbase_sync_set_pending_owned(&s->sync,k->owner,panvk_kbase_sync_wait_cb,panvk_kbase_sync_export_cb,targets);
}
struct wait_arg { struct kbase_cpu_sync *s;VkResult result; };
static void *wait_thread(void *ptr)
{
 struct wait_arg *a=ptr;
 a->result=kbase_cpu_sync_wait_one(NULL,a->s,VK_SYNC_WAIT_COMPLETE,UINT64_MAX);
 return NULL;
}
static void wait_reading(struct panvk_kbase_jm *k)
{
 uint64_t deadline=os_time_get_nano()+2000000000;
 for(;;){mtx_lock(&k->mtx);bool r=k->reading;mtx_unlock(&k->mtx);if(r)return;assert(os_time_get_nano()<deadline);sched_yield();}
}
static void close_race(bool shadow,unsigned n)
{
 int fds[2];assert(!pipe(fds));struct panvk_device d={0};
 struct panvk_kbase_jm *k=calloc(1,sizeof(*k));init_queue(k,&d,fds[0],shadow);
 struct kbase_cpu_sync s[8];struct wait_arg args[8];pthread_t threads[8];
 for(unsigned i=0;i<n;i++){init_sync(&s[i]);arm(&s[i],k,1);args[i]=(struct wait_arg){&s[i],99};assert(!pthread_create(&threads[i],NULL,wait_thread,&args[i]));}
 wait_reading(k);uint64_t t=os_time_get_nano();
 panvk_kbase_jm_close(k);assert(os_time_get_nano()-t<2000000000);
 for(unsigned i=0;i<n;i++){assert(!pthread_join(threads[i],NULL));assert(args[i].result==VK_ERROR_DEVICE_LOST);}
 struct kbase_cpu_sync late;init_sync(&late);arm(&late,k,2);
 fini_queue(k);memset(k,0xa5,sizeof(*k));free(k);
 assert(kbase_cpu_sync_wait_one(NULL,&late,0,0)==VK_ERROR_DEVICE_LOST);
 for(unsigned i=0;i<n;i++)kbase_cpu_sync_finish(NULL,&s[i].sync);
 kbase_cpu_sync_finish(NULL,&late.sync);close(fds[0]);close(fds[1]);cases++;
}
static void payloads(void)
{
 struct panvk_device d={0};struct panvk_kbase_jm k;init_queue(&k,&d,-1,false);
 struct kbase_cpu_sync a,b;init_sync(&a);init_sync(&b);
 for(unsigned i=0;i<500;i++){
  arm(&a,&k,0);assert(!kbase_cpu_sync_move(NULL,&b.sync,&a.sync));
  assert(k.owner->refs==2);
  int fd=-99;assert(!kbase_cpu_sync_export_sync_file(NULL,&b.sync,&fd)&&fd==-1);
  assert(!kbase_cpu_sync_wait_one(NULL,&b,0,0));assert(k.owner->refs==1);
  arm(&a,&k,0);assert(!kbase_cpu_sync_reset(NULL,&a.sync));assert(k.owner->refs==1);
  arm(&a,&k,0);assert(!kbase_cpu_sync_signal(NULL,&a.sync,0));assert(k.owner->refs==1);
 }
 arm(&a,&k,1);panvk_kbase_jm_close(&k);int fd=-99;
 assert(kbase_cpu_sync_export_sync_file(NULL,&a.sync,&fd)==VK_ERROR_DEVICE_LOST);
 kbase_cpu_sync_finish(NULL,&a.sync);kbase_cpu_sync_finish(NULL,&b.sync);fini_queue(&k);cases++;
}
static void imported_replacement(void)
{
 struct kbase_cpu_sync s;init_sync(&s);int f[2];assert(!pipe(f));
 assert(!kbase_cpu_sync_import_sync_file(NULL,&s.sync,f[0]));
 struct wait_arg a={&s,99};pthread_t t;assert(!pthread_create(&t,NULL,wait_thread,&a));
 uint64_t end=os_time_get_nano()+2000000000;
 for(;;){mtx_lock(&s.mutex);bool r=s.state==KBASE_CPU_SYNC_WAITING;mtx_unlock(&s.mutex);if(r)break;assert(os_time_get_nano()<end);sched_yield();}
 assert(!kbase_cpu_sync_signal(NULL,&s.sync,0));
 assert(write(f[1],"x",1)==1);assert(!pthread_join(t,NULL));assert(a.result==VK_SUCCESS);
 assert(s.state==KBASE_CPU_SYNC_SIGNALED);kbase_cpu_sync_finish(NULL,&s.sync);close(f[0]);close(f[1]);cases++;
}

struct external_arg { struct panvk_kbase_jm *k;struct kbase_cpu_sync *s;VkResult result; };
static void *external_thread(void *ptr)
{
 struct external_arg *a=ptr;assert(panvk_kbase_owner_enter(a->k->owner));
 a->result=panvk_kbase_wait_external(a->k,&a->s->sync,0);
 panvk_kbase_owner_leave(a->k->owner);return NULL;
}
static void external_shutdown(void)
{
 struct panvk_device d={0};struct panvk_kbase_jm k;init_queue(&k,&d,-1,true);
 struct kbase_cpu_sync s;init_sync(&s);int f[2];assert(!pipe(f));
 assert(!kbase_cpu_sync_import_sync_file(NULL,&s.sync,f[0]));
 struct external_arg a={&k,&s,99};pthread_t thread;assert(!pthread_create(&thread,NULL,external_thread,&a));
 uint64_t end=os_time_get_nano()+2000000000;
 for(;;){mtx_lock(&s.mutex);bool r=s.state==KBASE_CPU_SYNC_WAITING;mtx_unlock(&s.mutex);if(r)break;assert(os_time_get_nano()<end);sched_yield();}
 uint64_t t=os_time_get_nano();panvk_kbase_jm_close(&k);assert(os_time_get_nano()-t<1000000000);
 assert(!pthread_join(thread,NULL));assert(a.result==VK_ERROR_DEVICE_LOST);
 kbase_cpu_sync_finish(NULL,&s.sync);fini_queue(&k);close(f[0]);close(f[1]);cases++;
}

struct export_arg { struct kbase_cpu_sync *s;VkResult result;int fd; };
static void *export_thread(void *ptr)
{
 struct export_arg *a=ptr;a->result=kbase_cpu_sync_export_sync_file(NULL,&a->s->sync,&a->fd);return NULL;
}
static void export_shutdown(void)
{
 int f[2];assert(!pipe(f));struct panvk_device d={0};struct panvk_kbase_jm k;init_queue(&k,&d,f[0],true);
 k.count=1;k.last_seqno=1;k.subs[0]=(struct panvk_kbase_sub){.seqno=1,.last=1,.fence_fd=-1};k.nr_free=0;
 struct kbase_cpu_sync s;init_sync(&s);arm(&s,&k,1);
 struct export_arg a={&s,99,-99};pthread_t thread;assert(!pthread_create(&thread,NULL,export_thread,&a));
 wait_reading(&k);assert(!kbase_cpu_sync_reset(NULL,&s.sync));panvk_kbase_jm_close(&k);
 assert(!pthread_join(thread,NULL));assert(a.result==VK_ERROR_DEVICE_LOST);
 assert(k.owner->refs==1);kbase_cpu_sync_finish(NULL,&s.sync);fini_queue(&k);close(f[0]);close(f[1]);cases++;
}

static void early_concurrent_events(void)
{
 int f[2];assert(!pipe(f));struct panvk_device d={0};struct panvk_kbase_jm k;init_queue(&k,&d,f[0],true);
 struct kbase_cpu_sync s;init_sync(&s);arm(&s,&k,1);
 struct wait_arg a={&s,99};pthread_t thread;assert(!pthread_create(&thread,NULL,wait_thread,&a));wait_reading(&k);
 struct panvk_kbase_job jobs[2]={{.jc=0x1000,.core_req=0x16},{.jc=0x2000,.core_req=1}};uint64_t seq;
 mtx_lock(&k.mtx);early_fd=f[1];assert(!panvk_kbase_submit_locked(&k,jobs,2,-1,&seq));early_fd=-1;mtx_unlock(&k.mtx);
 assert(!pthread_join(thread,NULL));assert(a.result==VK_SUCCESS && k.completed_seqno==1 && !k.count);
 kbase_cpu_sync_finish(NULL,&s.sync);fini_queue(&k);close(f[0]);close(f[1]);cases++;
}

static VkResult unowned_wait(void *data, const uint64_t *targets, uint64_t timeout)
{
 assert(*(int *)data==123 && targets[0]==456);return VK_SUCCESS;
}
static void legacy_payload(void)
{
 struct kbase_cpu_sync s;init_sync(&s);int data=123;uint64_t targets[3]={456};
 panvk_kbase_sync_set_pending(&s.sync,&data,unowned_wait,NULL,targets);
 assert(!s.pending_owner && s.pending_data==&data);
 assert(!kbase_cpu_sync_wait_one(NULL,&s,0,0));kbase_cpu_sync_finish(NULL,&s.sync);cases++;
}
int main(void)
{
 for(unsigned i=0;i<64;i++){close_race(i&1,1+i%8);}
 for(unsigned i=0;i<64;i++)early_concurrent_events();
 legacy_payload();payloads();imported_replacement();external_shutdown();export_shutdown();
 printf("PASS actual owner/sync/queue callbacks, reader close races=%u; payload lifecycle=500 cycles\n",cases);
 return 0;
}
