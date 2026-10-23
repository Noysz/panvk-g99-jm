/* pngdump.h -- write framebuffer dumps as PNG instead of PPM, no zlib needed.
 *
 * Drop-in for the existing dump code in the harnesses, which all do
 *     f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", w, h);
 *     ...fwrite 3 bytes per pixel...; fclose(f);
 * Replacing fopen/fclose with png_open/png_close keeps every pixel loop as is:
 * png_open hands back an in-memory stream, png_close parses the P6 image that
 * was written into it and saves a PNG. The pixel bytes are the same, so a
 * framebuffer hash computed from the decoded PNG equals the old PPM hash.
 *
 * Deflate uses stored (uncompressed) blocks, which is valid PNG; the files are
 * small at 64x64. A path ending in ".ppm" is written as ".png".
 */
#ifndef PNGDUMP_H
#define PNGDUMP_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char  *pngdump_buf;
static size_t pngdump_len;
static char   pngdump_path[1024];

static uint32_t pngdump_crc(uint32_t c, const uint8_t *p, size_t n)
{
   static uint32_t t[256];
   if (!t[1])
      for (uint32_t i = 0; i < 256; i++) {
         uint32_t v = i;
         for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
         t[i] = v;
      }
   c = ~c;
   while (n--) c = t[(c ^ *p++) & 0xff] ^ (c >> 8);
   return ~c;
}

static void pngdump_be32(uint8_t *p, uint32_t v)
{
   p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void pngdump_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n)
{
   uint8_t b[4];
   pngdump_be32(b, n); fwrite(b, 1, 4, f);
   uint32_t c = pngdump_crc(0, (const uint8_t *)type, 4);
   c = pngdump_crc(c, d, n);
   fwrite(type, 1, 4, f); fwrite(d, 1, n, f);
   pngdump_be32(b, c); fwrite(b, 1, 4, f);
}

static int pngdump_write(const char *path, const uint8_t *rgb, uint32_t w, uint32_t h)
{
   const size_t row = (size_t)w * 3 + 1, raw_n = row * h;
   uint8_t *raw = malloc(raw_n);
   if (!raw) return -1;
   for (uint32_t y = 0; y < h; y++) {
      raw[y * row] = 0; /* filter: none */
      memcpy(raw + y * row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
   }
   const size_t nblk = raw_n / 65535 + 1;
   uint8_t *z = malloc(2 + raw_n + nblk * 5 + 4);
   if (!z) { free(raw); return -1; }
   size_t o = 0, i = 0;
   z[o++] = 0x78; z[o++] = 0x01;
   do {
      size_t n = raw_n - i > 65535 ? 65535 : raw_n - i;
      z[o++] = i + n == raw_n;
      z[o++] = n; z[o++] = n >> 8; z[o++] = ~n; z[o++] = (~n) >> 8;
      memcpy(z + o, raw + i, n); o += n; i += n;
   } while (i < raw_n);
   uint32_t a = 1, b = 0;
   for (size_t k = 0; k < raw_n; k++) { a = (a + raw[k]) % 65521; b = (b + a) % 65521; }
   pngdump_be32(z + o, (b << 16) | a); o += 4;
   FILE *f = fopen(path, "wb");
   if (!f) { free(raw); free(z); return -1; }
   static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
   fwrite(sig, 1, 8, f);
   uint8_t ihdr[13];
   pngdump_be32(ihdr, w); pngdump_be32(ihdr + 4, h);
   ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
   pngdump_chunk(f, "IHDR", ihdr, 13);
   pngdump_chunk(f, "IDAT", z, (uint32_t)o);
   pngdump_chunk(f, "IEND", NULL, 0);
   fclose(f);
   free(raw); free(z);
   return 0;
}

static FILE *png_open(const char *path, const char *mode)
{
   (void)mode;
   snprintf(pngdump_path, sizeof(pngdump_path), "%s", path);
   size_t n = strlen(pngdump_path);
   if (n > 4 && !strcmp(pngdump_path + n - 4, ".ppm"))
      memcpy(pngdump_path + n - 4, ".png", 4);
   return open_memstream(&pngdump_buf, &pngdump_len);
}

static int png_close(FILE *f)
{
   fclose(f);
   unsigned w = 0, h = 0, mx = 0; int off = 0;
   if (sscanf(pngdump_buf, "P6 %u %u %u%n", &w, &h, &mx, &off) != 3 || mx != 255) {
      fprintf(stderr, "pngdump: not a P6 image\n");
      free(pngdump_buf); return -1;
   }
   off++; /* the single whitespace after maxval */
   int r = pngdump_write(pngdump_path, (const uint8_t *)pngdump_buf + off, w, h);
   if (!r) printf("PNG_DUMP: %s\n", pngdump_path);
   free(pngdump_buf); pngdump_buf = NULL;
   return r;
}
#endif
