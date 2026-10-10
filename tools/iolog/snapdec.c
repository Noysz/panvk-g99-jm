/*
 * snapdec <snapshot> <gpu_id hex> [max_chains]
 * Decodes the job chains of an iolog snapshot (tools/iolog/iolog.c,
 * IOLOG_SNAP) with pandecode, shaders disassembled. Chains that point into
 * memory the snapshot does not have are cut short (the fault is caught and
 * the next chain is decoded). Output goes to pandecode's dump file
 * (PANDECODE_DUMP_FILE, default pandecode.dump.*). Test tool only.
 */
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decode.h"
#include "pan_compiler.h"

static sigjmp_buf jmp;

static void
on_fault(int sig)
{
   (void)sig;
   siglongjmp(jmp, 1);
}

int
main(int argc, char **argv)
{
   if (argc < 3) {
      fprintf(stderr, "usage: %s <snapshot> <gpu_id hex> [max_chains]\n",
              argv[0]);
      return 2;
   }
   FILE *f = fopen(argv[1], "rb");
   if (!f) {
      perror(argv[1]);
      return 1;
   }
   uint64_t gpu_id = strtoull(argv[2], NULL, 16);
   unsigned max_chains = argc > 3 ? atoi(argv[3]) : 1000;

   char tag[4];
   uint32_t n = 0;
   if (fread(tag, 4, 1, f) != 1 || memcmp(tag, "JCHN", 4) ||
       fread(&n, 4, 1, f) != 1) {
      fprintf(stderr, "bad snapshot\n");
      return 1;
   }
   uint64_t *jc = calloc(n ? n : 1, 8);
   uint32_t *req = calloc(n ? n : 1, 4);
   for (uint32_t i = 0; i < n; i++) {
      uint32_t pad;
      if (fread(&jc[i], 8, 1, f) != 1 || fread(&req[i], 4, 1, f) != 1 ||
          fread(&pad, 4, 1, f) != 1)
         return 1;
   }

   struct pandecode_context *ctx = pandecode_create_context(false);
   pandecode_set_disassemble(ctx, pan_disassemble);
   unsigned maps = 0;
   while (fread(tag, 4, 1, f) == 1) {
      uint32_t zero;
      uint64_t va, size;
      if (memcmp(tag, "MMAP", 4) || fread(&zero, 4, 1, f) != 1 ||
          fread(&va, 8, 1, f) != 1 || fread(&size, 8, 1, f) != 1)
         break;
      void *data = malloc(size);
      if (!data || fread(data, 1, size, f) != size)
         break;
      char name[32];
      snprintf(name, sizeof(name), "map%u", maps++);
      pandecode_inject_mmap(ctx, va, data, size, name);
   }
   fclose(f);
   fprintf(stderr, "snapdec: %u chains, %u mappings\n", n, maps);

   signal(SIGSEGV, on_fault);
   signal(SIGBUS, on_fault);
   signal(SIGABRT, on_fault);
   for (uint32_t i = 0; i < n && i < max_chains; i++) {
      fprintf(stderr, "chain %u jc 0x%" PRIx64 " core_req 0x%x\n", i, jc[i],
              req[i]);
      if (sigsetjmp(jmp, 1) == 0)
         pandecode_jc(ctx, jc[i], gpu_id);
      else {
         /* pandecode_jc held the context lock when it faulted */
         simple_mtx_unlock(&ctx->lock);
         fprintf(stderr, "chain %u: cut short\n", i);
      }
      pandecode_next_frame(ctx);
   }
   return 0;
}
