#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sched.h>
#include <time.h>
#include "panvk_kbase_owner.h"

typedef pthread_mutex_t mtx_t;
struct u_cnd_monotonic { pthread_cond_t c; };
enum { thrd_success = 0, mtx_plain = 0 };
static int mtx_init(mtx_t *m, int f) { return pthread_mutex_init(m, NULL); }
static int mtx_lock(mtx_t *m) { return pthread_mutex_lock(m); }
static int mtx_unlock(mtx_t *m) { return pthread_mutex_unlock(m); }
static int mtx_destroy(mtx_t *m) { return pthread_mutex_destroy(m); }
static int u_cnd_monotonic_init(struct u_cnd_monotonic *c) { return pthread_cond_init(&c->c, NULL); }
static int u_cnd_monotonic_destroy(struct u_cnd_monotonic *c) { return pthread_cond_destroy(&c->c); }
static int u_cnd_monotonic_wait(struct u_cnd_monotonic *c, mtx_t *m) { return pthread_cond_wait(&c->c, m); }
static int u_cnd_monotonic_broadcast(struct u_cnd_monotonic *c) { return pthread_cond_broadcast(&c->c); }
#define p_atomic_inc(p) __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST)
#define p_atomic_dec_zero(p) (__atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST) == 0)
#define panvk_per_arch(n) test_##n
#define PAN_ARCH 9
#define HAVE_PAN_KMOD_KBASE 1
#define simple_mtx_destroy mtx_destroy
typedef struct { int unused; } VkAllocationCallbacks;
struct panvk_mem_prof { bool enabled; uint64_t last_ns; FILE *out; };
struct panvk_frame_prof { bool enabled; FILE *out; mtx_t mtx; };
struct panvk_device {
   struct { VkAllocationCallbacks alloc; void *mem_cache; } vk;
   struct panvk_mem_prof mem_prof;
   struct panvk_frame_prof frame_prof;
   struct { void *dev, *vm; } kmod;
   struct { int vk; } *depth_bounds_fs;
   struct { void *blackhole; } sparse_mem;
   struct { void *bo; int ctx; } printf;
   struct { void *handlers_bo; } tiler_oom;
   void *null_index_buffer, *poly_heap, *poly_heap_data, *indirect_varying_buffer, *tiler_heap;
   void *sample_positions, *dump_region_size;
   struct { mtx_t lock; } as;
   struct { void *decode_ctx; } debug;
};
struct vk_queue { int unused; };
static struct vk_queue test_queue;
#define vk_foreach_queue_safe(q, d) for (struct vk_queue *q = &test_queue; q; q = NULL)
static struct panvk_kbase_owner *owner;
static struct panvk_device *current_device;
static unsigned entered, proceed, cpu_closed, capture_closed, bo_puts, vm_closed, backend_closed, vk_closed, frees;
static void panvk_queue_destroy(struct vk_queue *q) {
   __atomic_store_n(&proceed, 1, __ATOMIC_RELEASE);
   panvk_kbase_owner_close(owner);
   assert(current_device->mem_prof.enabled && current_device->frame_prof.enabled);
   cpu_closed = 1;
}
static void panvk_mem_prof_tick(struct panvk_device *d) { assert(cpu_closed && !backend_closed); }
static void test_utrace_context_fini(struct panvk_device *d) {}
static void kbase_kmod_capture_close(void *d) { assert(cpu_closed && !bo_puts); capture_closed = 1; }
static void vk_shader_destroy(void *d, void *s, void *a) { assert(cpu_closed); }
static void panvk_precomp_cleanup(void *d) { assert(cpu_closed); }
static void panvk_meta_cleanup(void *d) { assert(cpu_closed); }
static void vk_pipeline_cache_destroy(void *d, void *a) { assert(cpu_closed); }
static void pan_kmod_bo_put(void *b) { assert(cpu_closed && !vm_closed && !backend_closed); bo_puts++; }
static void panvk_priv_bo_unref(void *b) { pan_kmod_bo_put(b); }
static void u_printf_destroy(void *c) { assert(cpu_closed); }
static void panvk_device_cleanup_mempools(void *d) { assert(!vm_closed && bo_puts); }
static void pan_kmod_vm_destroy(void *v) { assert(cpu_closed && bo_puts && !backend_closed); vm_closed = 1; }
static void cleanup_va_heaps(void *d) { assert(vm_closed); }
static void pandecode_destroy_context(void *d) { assert(vm_closed); }
static void pan_kmod_dev_destroy(void *d) { assert(vm_closed); backend_closed = 1; }
static void vk_device_finish(void *d) { assert(backend_closed); vk_closed = 1; }
static void vk_free(void *a, void *p) {
   if (p == current_device) {
      assert(vk_closed && backend_closed && cpu_closed);
      assert(!current_device->frame_prof.enabled && !current_device->mem_prof.enabled);
      frees++;
      free(p);
   } else {
      assert(!backend_closed);
   }
}
static void *callback(void *unused) {
   struct panvk_device *d = panvk_kbase_owner_enter(owner);
   assert(d == current_device);
   __atomic_store_n(&entered, 1, __ATOMIC_RELEASE);
   while (!__atomic_load_n(&proceed, __ATOMIC_ACQUIRE)) sched_yield();
   assert(d->frame_prof.enabled && d->mem_prof.enabled);
   assert(!mtx_lock(&d->frame_prof.mtx));
   assert(fputs("callback\n", d->frame_prof.out) >= 0);
   assert(!mtx_unlock(&d->frame_prof.mtx));
   panvk_kbase_owner_leave(owner);
   return NULL;
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
void
panvk_mem_prof_finish(struct panvk_device *dev)
{
   struct panvk_mem_prof *p = &dev->mem_prof;

   if (!p->enabled)
      return;

   p->last_ns = 0;
   panvk_mem_prof_tick(dev);
   if (p->out && p->out != stderr)
      fclose(p->out);
   p->enabled = false;
}
void
panvk_frame_prof_finish(struct panvk_device *dev)
{
   struct panvk_frame_prof *p = &dev->frame_prof;

   if (!p->enabled)
      return;
   p->enabled = false;
   if (p->out && p->out != stderr)
      fclose(p->out);
   mtx_destroy(&p->mtx);
}
void
panvk_per_arch(destroy_device)(struct panvk_device *device,
                               const VkAllocationCallbacks *pAllocator)
{
   if (!device)
      return;

   panvk_mem_prof_finish(device);
   panvk_frame_prof_finish(device);
   panvk_per_arch(utrace_context_fini)(device);

   vk_foreach_queue_safe(queue, &device->vk)
      panvk_queue_destroy(queue);

   if (device->depth_bounds_fs)
      vk_shader_destroy(&device->vk, &device->depth_bounds_fs->vk, NULL);
   panvk_precomp_cleanup(device);
#if PAN_ARCH >= 10 && PAN_ARCH < 14
   panvk_per_arch(device_draw_context_cleanup)(device);
#endif
   panvk_meta_cleanup(device);
   vk_pipeline_cache_destroy(device->vk.mem_cache, NULL);
   pan_kmod_bo_put(device->sparse_mem.blackhole);
   u_printf_destroy(&device->printf.ctx);
   panvk_priv_bo_unref(device->printf.bo);
   panvk_priv_bo_unref(device->tiler_oom.handlers_bo);
   panvk_priv_bo_unref(device->poly_heap);
   panvk_priv_bo_unref(device->poly_heap_data);
   panvk_priv_bo_unref(device->null_index_buffer);
   panvk_priv_bo_unref(device->indirect_varying_buffer);
   panvk_priv_bo_unref(device->tiler_heap);
   panvk_priv_bo_unref(device->sample_positions);
   panvk_device_cleanup_mempools(device);
   vk_free(&device->vk.alloc, device->dump_region_size);
   pan_kmod_vm_destroy(device->kmod.vm);
   cleanup_va_heaps(device);
   simple_mtx_destroy(&device->as.lock);

   if (device->debug.decode_ctx)
      pandecode_destroy_context(device->debug.decode_ctx);

   pan_kmod_dev_destroy(device->kmod.dev);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}
int main(void) {
   test_destroy_device(NULL, NULL);
   for (unsigned cycle = 0; cycle < 64; cycle++) {
      entered = proceed = cpu_closed = capture_closed = bo_puts = vm_closed = backend_closed = vk_closed = 0;
      current_device = calloc(1, sizeof(*current_device)); assert(current_device);
      current_device->frame_prof.enabled = current_device->mem_prof.enabled = true;
      current_device->frame_prof.out = tmpfile();
      current_device->mem_prof.out = tmpfile();
      assert(current_device->frame_prof.out && current_device->mem_prof.out);
      assert(!mtx_init(&current_device->frame_prof.mtx, 0));
      assert(!mtx_init(&current_device->as.lock, 0));
      owner = panvk_kbase_owner_create(current_device); assert(owner);
      pthread_t thread; assert(!pthread_create(&thread, NULL, callback, NULL));
      while (!__atomic_load_n(&entered, __ATOMIC_ACQUIRE)) sched_yield();
      test_destroy_device(current_device, NULL);
      assert(!pthread_join(thread, NULL));
      assert(!panvk_kbase_owner_enter(owner));
      panvk_kbase_owner_unref(owner);
   }
   assert(frees == 64);
   puts("DEVICE_TEARDOWN_PASS cycles=64 actual_destroy_device=1 actual_profiler_fini=1 actual_cpu_owner=1 lower_resource_stubs=1 gpu_terminal_not_modeled=1");
}
