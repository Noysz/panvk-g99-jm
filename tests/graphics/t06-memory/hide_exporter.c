#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

FILE *fopen(const char *path, const char *mode)
{
   static FILE *(*real_fopen)(const char *, const char *);
   if (!real_fopen) real_fopen = dlsym(RTLD_NEXT, "fopen");
   if (!strncmp(path, "/proc/self/fdinfo/", 18)) {
      fprintf(stderr, "EXPORTER_METADATA_HIDDEN\n");
      errno = EACCES;
      return NULL;
   }
   return real_fopen(path, mode);
}
