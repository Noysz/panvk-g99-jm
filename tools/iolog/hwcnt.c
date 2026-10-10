/*
 * hwcnt.c: sample Mali hardware counters through kbase kinstr_prfcnt
 * (KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO / _SETUP; the older
 * HWCNT_READER_SETUP is not built into the r54p1 kernel on the G99). The
 * counters are GPU-wide, so this runs as its own process next to the game,
 * whatever driver the game uses.
 *
 *   hwcnt <out.bin> <interval_ms> <seconds>
 *
 * Output, all little endian:
 *   header: "HWC2", u32 num_block_kinds, then per kind
 *           {u8 type, u8 pad[3], u16 instances, u16 values}
 *   per sample: u64 ts_start, u64 ts_end, u32 flags, u32 num_blocks,
 *           u64 cycles[4] (clock domains), then per block
 *           {u8 type, u8 idx, u16 values, u32 state, u64 values[values]}
 * Decode with hwcnt.py.
 *
 * Build (Termux):
 *   clang -O2 -o hwcnt hwcnt.c -I$KBASE/include/uapi/gpu/arm/midgard
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "mali_kbase_hwcnt_reader.h"
#include "mali_kbase_ioctl.h"

#define MAX_KINDS 16

struct kind {
   uint8_t type;
   uint16_t instances, values;
};

static uint64_t
now_ns(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

int
main(int argc, char **argv)
{
   if (argc < 4) {
      fprintf(stderr, "usage: %s <out.bin> <interval_ms> <seconds>\n",
              argv[0]);
      return 2;
   }
   uint64_t interval_ns = (uint64_t)atoi(argv[2]) * 1000000ull;
   unsigned seconds = atoi(argv[3]);

   int fd = open("/dev/mali0", O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      perror("/dev/mali0");
      return 1;
   }
   struct kbase_ioctl_version_check ver = {.major = 11, .minor = 46};
   if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &ver) < 0) {
      perror("VERSION_CHECK");
      return 1;
   }
   struct kbase_ioctl_set_flags cflags = {.create_flags = 0};
   if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &cflags) < 0) {
      perror("SET_FLAGS");
      return 1;
   }

   /* What the kernel offers. */
   struct kbase_ioctl_kinstr_prfcnt_enum_info ei = {0};
   if (ioctl(fd, KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO, &ei) < 0) {
      perror("KINSTR_PRFCNT_ENUM_INFO");
      return 1;
   }
   struct prfcnt_enum_item *items = calloc(ei.info_item_count, ei.info_item_size);
   ei.info_list_ptr = (uintptr_t)items;
   if (ioctl(fd, KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO, &ei) < 0) {
      perror("KINSTR_PRFCNT_ENUM_INFO (list)");
      return 1;
   }

   struct kind kinds[MAX_KINDS];
   unsigned nkinds = 0;
   for (unsigned i = 0; i < ei.info_item_count; i++) {
      struct prfcnt_enum_item *it =
         (void *)((uint8_t *)items + (size_t)i * ei.info_item_size);
      if (it->hdr.item_type == PRFCNT_ENUM_TYPE_BLOCK && nkinds < MAX_KINDS &&
          it->u.block_counter.set == PRFCNT_SET_PRIMARY &&
          it->u.block_counter.num_instances && it->u.block_counter.num_values) {
         kinds[nkinds].type = it->u.block_counter.block_type;
         kinds[nkinds].instances = it->u.block_counter.num_instances;
         kinds[nkinds].values = it->u.block_counter.num_values;
         fprintf(stderr, "hwcnt: block type %u: %u instance(s), %u values\n",
                 kinds[nkinds].type, kinds[nkinds].instances,
                 kinds[nkinds].values);
         nkinds++;
      }
   }

   /* Periodic, global scope, every counter of every block kind. */
   struct prfcnt_request_item req[MAX_KINDS + 3];
   memset(req, 0, sizeof(req));
   unsigned nreq = 0;
   req[nreq].hdr.item_type = PRFCNT_REQUEST_TYPE_MODE;
   req[nreq].hdr.item_version = PRFCNT_READER_API_VERSION;
   req[nreq].u.req_mode.mode = PRFCNT_MODE_PERIODIC;
   req[nreq].u.req_mode.mode_config.periodic.period_ns = interval_ns;
   nreq++;
   for (unsigned k = 0; k < nkinds; k++) {
      req[nreq].hdr.item_type = PRFCNT_REQUEST_TYPE_ENABLE;
      req[nreq].hdr.item_version = PRFCNT_READER_API_VERSION;
      req[nreq].u.req_enable.block_type = kinds[k].type;
      req[nreq].u.req_enable.set = PRFCNT_SET_PRIMARY;
      req[nreq].u.req_enable.enable_mask[0] = ~0ull;
      req[nreq].u.req_enable.enable_mask[1] = ~0ull;
      nreq++;
   }
   req[nreq].hdr.item_type = PRFCNT_REQUEST_TYPE_SCOPE;
   req[nreq].hdr.item_version = PRFCNT_READER_API_VERSION;
   req[nreq].u.req_scope.scope = PRFCNT_SCOPE_GLOBAL;
   nreq++;
   req[nreq].hdr.item_type = FLEX_LIST_TYPE_NONE; /* sentinel */
   nreq++;

   union kbase_ioctl_kinstr_prfcnt_setup su = {0};
   su.in.request_item_count = nreq;
   su.in.request_item_size = sizeof(req[0]);
   su.in.requests_ptr = (uintptr_t)req;
   int rfd = ioctl(fd, KBASE_IOCTL_KINSTR_PRFCNT_SETUP, &su);
   if (rfd < 0) {
      perror("KINSTR_PRFCNT_SETUP");
      return 1;
   }
   size_t map_size = su.out.prfcnt_mmap_size_bytes;
   uint8_t *map = mmap(NULL, map_size, PROT_READ, MAP_SHARED, rfd, 0);
   if (map == MAP_FAILED) {
      perror("mmap");
      return 1;
   }

   FILE *out = fopen(argv[1], "wb");
   if (!out) {
      perror(argv[1]);
      return 1;
   }
   fwrite("HWC2", 4, 1, out);
   fwrite(&nkinds, 4, 1, out);
   for (unsigned k = 0; k < nkinds; k++) {
      uint8_t b[8] = {kinds[k].type, 0, 0, 0};
      memcpy(b + 4, &kinds[k].instances, 2);
      memcpy(b + 6, &kinds[k].values, 2);
      fwrite(b, 8, 1, out);
   }

   struct prfcnt_control_cmd cmd = {.cmd = PRFCNT_CONTROL_CMD_START};
   if (ioctl(rfd, KBASE_IOCTL_KINSTR_PRFCNT_CMD, &cmd) < 0) {
      perror("CMD_START");
      return 1;
   }

   unsigned n = 0;
   const uint64_t end = now_ns() + (uint64_t)seconds * 1000000000ull;
   while (now_ns() < end) {
      struct pollfd p = {.fd = rfd, .events = POLLIN};
      if (poll(&p, 1, 1000) <= 0)
         continue;
      struct prfcnt_sample_access acc = {0};
      if (ioctl(rfd, KBASE_IOCTL_KINSTR_PRFCNT_GET_SAMPLE, &acc) < 0) {
         if (errno == EAGAIN || errno == ENODATA)
            continue;
         perror("GET_SAMPLE");
         break;
      }

      uint64_t ts0 = 0, ts1 = 0, cycles[4] = {0};
      uint32_t sflags = 0, nblocks = 0;
      struct prfcnt_metadata *md = (void *)(map + acc.sample_offset_bytes);
      for (struct prfcnt_metadata *m = md; m->hdr.item_type != FLEX_LIST_TYPE_NONE;
           m = (void *)((uint8_t *)m + su.out.prfcnt_metadata_item_size)) {
         if (m->hdr.item_type == PRFCNT_SAMPLE_META_TYPE_SAMPLE) {
            ts0 = m->u.sample_md.timestamp_start;
            ts1 = m->u.sample_md.timestamp_end;
            sflags = m->u.sample_md.flags;
         } else if (m->hdr.item_type == PRFCNT_SAMPLE_META_TYPE_CLOCK) {
            for (unsigned d = 0; d < 4 && d < m->u.clock_md.num_domains; d++)
               cycles[d] = m->u.clock_md.cycles[d];
         } else if (m->hdr.item_type == PRFCNT_SAMPLE_META_TYPE_BLOCK) {
            nblocks++;
         }
      }
      fwrite(&ts0, 8, 1, out);
      fwrite(&ts1, 8, 1, out);
      fwrite(&sflags, 4, 1, out);
      fwrite(&nblocks, 4, 1, out);
      fwrite(cycles, 8, 4, out);
      for (struct prfcnt_metadata *m = md; m->hdr.item_type != FLEX_LIST_TYPE_NONE;
           m = (void *)((uint8_t *)m + su.out.prfcnt_metadata_item_size)) {
         if (m->hdr.item_type != PRFCNT_SAMPLE_META_TYPE_BLOCK)
            continue;
         uint16_t nval = 0;
         for (unsigned k = 0; k < nkinds; k++)
            if (kinds[k].type == m->u.block_md.block_type)
               nval = kinds[k].values;
         uint8_t b[8] = {m->u.block_md.block_type, m->u.block_md.block_idx};
         memcpy(b + 2, &nval, 2);
         memcpy(b + 4, &m->u.block_md.block_state, 4);
         fwrite(b, 8, 1, out);
         fwrite(map + m->u.block_md.values_offset, 8, nval, out);
      }
      n++;

      if (ioctl(rfd, KBASE_IOCTL_KINSTR_PRFCNT_PUT_SAMPLE, &acc) < 0) {
         perror("PUT_SAMPLE");
         break;
      }
   }

   cmd.cmd = PRFCNT_CONTROL_CMD_STOP;
   ioctl(rfd, KBASE_IOCTL_KINSTR_PRFCNT_CMD, &cmd);
   fclose(out);
   fprintf(stderr, "hwcnt: %u samples\n", n);
   return 0;
}
