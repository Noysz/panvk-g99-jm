/*
 * test_kbase2.c
 *
 * Lanjutan dari test_kbase.c (yang sudah sukses: buka /dev/mali0 dan
 * KBASE_IOCTL_VERSION_CHECK => UAPI 11.46).
 *
 * Langkah berikutnya menuju backend kbase_kmod.c untuk pan_kmod:
 *   1. Buka /dev/mali0
 *   2. KBASE_IOCTL_VERSION_CHECK  (konfirmasi ulang, sama seperti sebelumnya)
 *   3. KBASE_IOCTL_SET_FLAGS      (wajib dipanggil sebelum operasi memory,
 *                                  ini yang membuat fd jadi kbase_context aktif)
 *   4. KBASE_IOCTL_MEM_ALLOC      (minta GPU mengalokasikan memory,
 *                                  dengan flag BASE_MEM_SAME_VA supaya
 *                                  alamat CPU dan GPU sama)
 *   5. mmap() hasil alokasi itu ke address space CPU, pakai gpu_va
 *      sebagai offset mmap (ini konvensi kbase untuk SAME_VA region)
 *   6. Tulis pola byte ke memory itu dari CPU, baca balik untuk
 *      verifikasi mapping benar-benar hidup
 *
 * Struct & ioctl number diambil dari kernel driver Mali Valhall/JM resmi
 * ARM (referensi: android-gs-raviole-5.10-android13, Pixel 6/Mali-G78,
 * sama-sama Valhall Gen1/JM seperti G57). Nomor-nomor ioctl dasar ini
 * (VERSION_CHECK=0, SET_FLAGS=1, MEM_ALLOC=5) sudah stabil sejak versi
 * UAPI 11.x lama dan additive di versi-versi berikutnya, jadi seharusnya
 * cocok dengan UAPI 11.46 yang ada di device ini -- tapi INI BELUM
 * DIVERIFIKASI terhadap kernel driver yang benar-benar jalan di device
 * (r54p1). Kalau ioctl gagal dengan -ENOTTY / -EINVAL, kemungkinan ada
 * pergeseran nomor/struct spesifik ke versi ARM DDK ini yang perlu
 * dicek ulang dari source ARM langsung.
 *
 * Compile di Termux:
 *   gcc -o test_kbase2 test_kbase2.c
 * Run:
 *   ./test_kbase2
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>

/* ---- dari mali_kbase_ioctl.h (uapi umum) ---- */
#define KBASE_IOCTL_TYPE 0x80

/* struct kbase_ioctl_set_flags */
struct kbase_ioctl_set_flags {
    uint32_t create_flags;
};
#define KBASE_IOCTL_SET_FLAGS \
    _IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags)

/* union kbase_ioctl_mem_alloc */
union kbase_ioctl_mem_alloc {
    struct {
        uint64_t va_pages;
        uint64_t commit_pages;
        uint64_t extension;
        uint64_t flags;
    } in;
    struct {
        uint64_t flags;
        uint64_t gpu_va;
    } out;
};
#define KBASE_IOCTL_MEM_ALLOC \
    _IOWR(KBASE_IOCTL_TYPE, 5, union kbase_ioctl_mem_alloc)

/* ---- dari jm/mali_kbase_jm_ioctl.h (khusus JM, non-CSF) ---- */
struct kbase_ioctl_version_check {
    uint16_t major;
    uint16_t minor;
};
#define KBASE_IOCTL_VERSION_CHECK \
    _IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)

/* ---- dari jm/mali_base_jm_kernel.h (flag alokasi memory) ---- */
typedef uint32_t base_mem_alloc_flags;
#define BASE_MEM_PROT_CPU_RD   ((base_mem_alloc_flags)1 << 0)
#define BASE_MEM_PROT_CPU_WR   ((base_mem_alloc_flags)1 << 1)
#define BASE_MEM_PROT_GPU_RD   ((base_mem_alloc_flags)1 << 2)
#define BASE_MEM_PROT_GPU_WR   ((base_mem_alloc_flags)1 << 3)
#define BASE_MEM_SAME_VA       ((base_mem_alloc_flags)1 << 13)

#define KBASE_PAGE_SIZE 4096

int main(void)
{
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "FATAL open /dev/mali0: %s\n", strerror(errno));
        return 1;
    }
    printf("Berhasil buka /dev/mali0, fd=%d\n", fd);

    /* --- Step 1: version check, sama seperti test_kbase.c awal --- */
    struct kbase_ioctl_version_check vc = { 0, 0 };
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &vc) < 0) {
        fprintf(stderr, "FATAL KBASE_IOCTL_VERSION_CHECK: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    printf("UAPI version: major=%u minor=%u\n", vc.major, vc.minor);

    /* --- Step 2: set flags -- ini yang mengaktifkan fd sebagai kbase_context --- */
    struct kbase_ioctl_set_flags sf = { 0 }; /* BASE_CONTEXT_CREATE_FLAG_NONE */
    if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf) < 0) {
        fprintf(stderr, "FATAL KBASE_IOCTL_SET_FLAGS: %s (errno=%d)\n",
                strerror(errno), errno);
        fprintf(stderr, "Kalau ini gagal, context belum aktif -- MEM_ALLOC "
                        "di bawah kemungkinan besar juga bakal gagal.\n");
        close(fd);
        return 1;
    }
    printf("KBASE_IOCTL_SET_FLAGS OK -- context aktif\n");

    /* --- Step 3: alokasi memory GPU, 4 halaman (16KB), CPU+GPU RD/WR, SAME_VA --- */
    union kbase_ioctl_mem_alloc ma;
    memset(&ma, 0, sizeof(ma));
    ma.in.va_pages     = 4;
    ma.in.commit_pages = 4;
    ma.in.extension    = 0;
    ma.in.flags = BASE_MEM_PROT_CPU_RD | BASE_MEM_PROT_CPU_WR |
                  BASE_MEM_PROT_GPU_RD | BASE_MEM_PROT_GPU_WR |
                  BASE_MEM_SAME_VA;

    if (ioctl(fd, KBASE_IOCTL_MEM_ALLOC, &ma) < 0) {
        fprintf(stderr, "FATAL KBASE_IOCTL_MEM_ALLOC: %s (errno=%d)\n",
                strerror(errno), errno);
        close(fd);
        return 1;
    }
    printf("KBASE_IOCTL_MEM_ALLOC OK -- gpu_va=0x%llx out_flags=0x%llx\n",
           (unsigned long long)ma.out.gpu_va,
           (unsigned long long)ma.out.flags);

    /* --- Step 4: mmap hasil alokasi. Untuk region SAME_VA, offset mmap
     * yang dipakai kbase adalah gpu_va itu sendiri. --- */
    size_t size = 4 * KBASE_PAGE_SIZE;
    void *mapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
                         fd, (off_t)ma.out.gpu_va);
    if (mapped == MAP_FAILED) {
        fprintf(stderr, "FATAL mmap: %s (errno=%d)\n", strerror(errno), errno);
        fprintf(stderr, "Kalau mmap gagal tapi MEM_ALLOC sukses, kemungkinan "
                        "offset yang dipakai kbase versi ini beda dari "
                        "gpu_va langsung -- perlu dicek source "
                        "mali_kbase_mem_linux.c versi yang cocok.\n");
        close(fd);
        return 1;
    }
    printf("mmap() OK, alamat CPU=%p\n", mapped);

    /* --- Step 5: tulis pola byte, baca balik untuk verifikasi --- */
    memset(mapped, 0xAB, size);
    unsigned char check = ((unsigned char *)mapped)[0];
    unsigned char check_last = ((unsigned char *)mapped)[size - 1];
    printf("Tulis 0xAB ke seluruh %zu byte, baca balik: byte[0]=0x%02x "
           "byte[last]=0x%02x -- %s\n",
           size, check, check_last,
           (check == 0xAB && check_last == 0xAB) ? "COCOK" : "TIDAK COCOK");

    munmap(mapped, size);
    close(fd);
    printf("\n=== SELESAI: version check + set flags + mem alloc + mmap "
           "semua berhasil ===\n");
    return 0;
}
