#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include "drm-uapi/mali_kbase_ioctl.h"

static int (*real_ioctl)(int, int, ...);
static pthread_once_t once = PTHREAD_ONCE_INIT;

static void resolve(void)
{
   real_ioctl = dlsym(RTLD_NEXT, "ioctl");
   if (!real_ioctl) abort();
}

int ioctl(int fd, int request, ...)
{
   pthread_once(&once, resolve);
   va_list args;
   va_start(args, request);
   void *arg = va_arg(args, void *);
   va_end(args);
   if ((unsigned)request == KBASE_IOCTL_JOB_SUBMIT) {
      struct kbase_ioctl_job_submit *submit = arg;
      const char *mode = getenv("T09_PERMON_CONTROL");
      if (!mode || submit->stride != 64 || submit->nr_atoms > 122) abort();
      unsigned changed = 0, vtc = 0;
      for (unsigned i = 0; i < submit->nr_atoms; i++) {
         uint8_t *atom = (uint8_t *)(uintptr_t)submit->addr + i * submit->stride;
         uint32_t req, original;
         memcpy(&req, atom + 44, sizeof(req));
         original = req;
         if ((req & ~(UINT32_C(1) << 12 | UINT32_C(1) << 7)) != 0x16) continue;
         vtc++;
         if (!strcmp(mode, "on")) req |= UINT32_C(1) << 7;
         else if (!strcmp(mode, "off")) req &= ~(UINT32_C(1) << 7);
         else if (strcmp(mode, "observe")) abort();
         changed += req != original;
         memcpy(atom + 44, &req, sizeof(req));
      }
      fprintf(stderr, "PERMON mode=%s atoms=%u vtc=%u changed=%u\n",
              mode, submit->nr_atoms, vtc, changed);
   }
   return real_ioctl(fd, request, arg);
}
