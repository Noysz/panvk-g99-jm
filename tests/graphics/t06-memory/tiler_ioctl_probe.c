#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include "drm-uapi/mali_kbase_ioctl.h"

static unsigned hits;
static unsigned allocs;

int ioctl(int fd, int request, ...)
{
   static int (*real_ioctl)(int, int, ...);
   if (!real_ioctl) real_ioctl = dlsym(RTLD_NEXT, "ioctl");
   va_list args;
   va_start(args, request);
   void *arg = va_arg(args, void *);
   va_end(args);
   if ((unsigned)request != KBASE_IOCTL_MEM_ALLOC)
      return real_ioctl(fd, request, arg);
   union kbase_ioctl_mem_alloc *a = arg;
   const uint64_t align = UINT64_C(1) << 20;
   const char *failure = getenv("TILERTEST_INJECT");
   if (a->in.va_pages == 32768 && a->in.extension == 512) {
      allocs++;
      fprintf(stderr, "HEAP_ALLOC pages=%" PRIu64 " commit=%" PRIu64
              " extension=%" PRIu64 " flags=0x%" PRIx64 "\n",
              (uint64_t)a->in.va_pages, (uint64_t)a->in.commit_pages,
              (uint64_t)a->in.extension, (uint64_t)a->in.flags);
   }
   if ((a->in.flags & align) && failure && !hits) {
      hits++;
      fprintf(stderr, "TILER_INJECT %s\n", failure);
      if (!strcmp(failure, "strip")) a->in.flags &= ~align;
      else {
         memset(a, 0xa5, sizeof(*a));
         errno = !strcmp(failure, "EINVAL") ? EINVAL : ENOMEM;
         return -1;
      }
   }
   int result = real_ioctl(fd, request, arg);
   return result;
}

__attribute__((destructor)) static void finish(void)
{
   fprintf(stderr, "TILER_PROBE injections=%u heap_alloc_requests=%u\n", hits, allocs);
}
