/*
 * gedprobe: read-only MediaTek GED queries, one line per sample.
 *   gedprobe [samples] [interval_ms]
 * QUERY_INFO (bridge 5): loading, current / max frequency.
 * QUERY_GPU_DVFS_INFO (bridge 16): current, max, predicted frequency.
 * Structures from the MediaTek GED headers (ged_bridge_id.h, ged_type.h);
 * function_id carries the full ioctl command, as the vendor libged does.
 * Only queries; nothing is set.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

struct ged_pkg {
   uint32_t function_id;
   int32_t size;
   void *in;
   int32_t in_size;
   void *out;
   int32_t out_size;
};

#define GED_MAGIC 'g'
#define GED_IOWR(n) _IOWR(GED_MAGIC, n, struct ged_pkg)
#define GED_QUERY_INFO 5
#define GED_QUERY_GPU_DVFS_INFO 16

enum { GED_LOADING = 0, GED_CUR_FREQ = 5, GED_MAX_FREQ_IDX_FREQ = 8,
       GED_MIN_FREQ_IDX_FREQ = 10 };

static int
ged_call(int fd, unsigned id, void *in, int in_size, void *out, int out_size)
{
   struct ged_pkg p = {
      .function_id = GED_IOWR(id), .size = sizeof(p),
      .in = in, .in_size = in_size, .out = out, .out_size = out_size,
   };
   return ioctl(fd, GED_IOWR(id), &p);
}

static long long
query_info(int fd, int type)
{
   int32_t in = type;
   uint64_t out = 0;
   if (ged_call(fd, GED_QUERY_INFO, &in, sizeof(in), &out, sizeof(out)))
      return -errno;
   return (long long)out;
}

int
main(int argc, char **argv)
{
   int n = argc > 1 ? atoi(argv[1]) : 1;
   int ms = argc > 2 ? atoi(argv[2]) : 500;
   int fd = open("/proc/ged", O_RDONLY);
   if (fd < 0) {
      perror("open /proc/ged");
      return 1;
   }
   for (int i = 0; i < n; i++) {
      struct timespec ts;
      clock_gettime(CLOCK_MONOTONIC, &ts);
      int32_t din[3] = {getpid(), 0, 0};
      int32_t dout[8] = {0};
      int r = ged_call(fd, GED_QUERY_GPU_DVFS_INFO, din, sizeof(din), dout,
                       sizeof(dout));
      printf("t=%ld.%03ld loading=%lld cur=%lld max=%lld min=%lld | dvfs "
             "ret=%d err=%d cur=%d max=%d pred=%d target_fps=%d "
             "gpu_time=%d\n",
             (long)ts.tv_sec, ts.tv_nsec / 1000000,
             query_info(fd, GED_LOADING), query_info(fd, GED_CUR_FREQ),
             query_info(fd, GED_MAX_FREQ_IDX_FREQ),
             query_info(fd, GED_MIN_FREQ_IDX_FREQ), r, dout[0], dout[1],
             dout[2], dout[3], dout[4], dout[7]);
      fflush(stdout);
      if (i + 1 < n)
         usleep(ms * 1000);
   }
   close(fd);
   return 0;
}
