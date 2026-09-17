/*
 * test_kbase3.c — KBASE_IOCTL_GET_GPUPROPS di Mali-G57 MC2 / MediaTek Helio G99
 *
 * Lanjutan dari test_kbase2.c (yang udah buktiin VERSION_CHECK -> SET_FLAGS ->
 * MEM_ALLOC -> mmap jalan). Yang ini narik gpuprops — data konkret pertama yang
 * dibutuhin buat ngisi `pan_kmod_dev_props` di PanVK.
 *
 * Compile (Termux atau proot Debian, dua-duanya jalan):
 *     gcc -O2 -Wall -Wextra -o test_kbase3 test_kbase3.c
 * Jalanin:
 *     ./test_kbase3
 *
 * ---------------------------------------------------------------------------
 * SUMBER TIAP DEFINISI DI BAWAH (bukan tebakan, bukan dari ingatan):
 *
 *  a) Header GPL resmi ARM, mirror ExtremeXT/valhall_drivers:
 *       driver/product/kernel/include/uapi/gpu/arm/midgard/mali_kbase_ioctl.h
 *     -> struct kbase_ioctl_get_gpuprops, nomor ioctl 3, ID prop 1..85.
 *
 *  b) Disassembly modul kbase LIVE di device ini
 *       (mali_kbase_mt6789_a16w_jm.ko, r54p1, UAPI 11.46),
 *     handler ke-inline di kbase_ioctl+0x154..0x1a0:
 *
 *       1ce34: mov  w9, #0x8003
 *       1ce38: movk w9, #0x4010, lsl #16   ; 0x40108003 = _IOW(0x80, 3, 16B)
 *       1ce3c: cmp  w1, w9
 *       1ce40: b.ne 1cfa0                  ; ke gate kctx
 *       1ce44: add  x0, sp, #0x28
 *       1ce4c: mov  w2, #0x10              ; sizeof(struct) == 16
 *       1ce54: bl   _inline_copy_from_user
 *       1ce5c: ldr  w8, [sp, #52]          ; offset 12 -> .flags
 *       1ce64: cbnz w8, 1d78c              ; flags != 0 -> dev_err -> -EINVAL
 *       1ce68: ldr  w8, [sp, #48]          ; offset  8 -> .size
 *       1ce6c: ldr  w19, [x21, #6664]      ; kbdev->gpu_props.prop_buffer_size
 *       1ce70: cbz  w8, 1d5f0              ; size == 0 -> return required size
 *       1ce74: cmp  w8, w19
 *       1ce78: b.cs 1d31c                  ; size >= required -> copy_to_user
 *       1ce7c: mov  w19, #0xffffffea       ; else -EINVAL (-22)
 *
 *     -> jadi offset field (.buffer=0, .size=8, .flags=12) TERVERIFIKASI di
 *        binary device ini, bukan cuma dari header mirror versi 11.43.
 *
 *  c) Handler ini ada di switch pertama kbase_kfile_ioctl(), yaitu grup
 *     "Only these ioctls are available until setup is complete"
 *     (mali_kbase_core_linux.c:1855-1894), DI ATAS gate
 *     `kctx = kbase_file_get_kctx_if_setup_complete(kfile)` (baris 1896).
 *     Di binary device: gate-nya di 1cfa0
 *       (`ldr w9,[x20,#32]` = setup_state; `cmp w9,#4` = KBASE_FILE_COMPLETE).
 *     -> GET_GPUPROPS BISA dipanggil TANPA bikin context. Program ini nge-tes
 *        klaim itu langsung, karena itu persis yang dibutuhin jalur
 *        vkEnumeratePhysicalDevices.
 *
 *  d) Encoding isi buffer, dari mali_kbase_gpuprops.c
 *     (kbase_gpuprops_populate_user_buffer):
 *       WRITE_U32((type << 2) | type_size);  lalu nilainya
 *       #define WRITE_U8(v) (*p++ = (v)&0xFF)   <- per byte, little-endian
 *       size += (u32)(4 + gpu_property_mapping[i].size);
 *     -> rapat, TANPA padding, TANPA terminator. Total = jumlah (4 + size).
 *        Karena ga ada padding, nilai bisa ga ter-align: WAJIB memcpy.
 * ---------------------------------------------------------------------------
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#define KBASE_IOCTL_TYPE 0x80

/* --- uapi: sama persis kaya test_kbase2.c, biar prolog-nya konsisten --- */

struct kbase_ioctl_version_check {
	uint16_t major;
	uint16_t minor;
};
#define KBASE_IOCTL_VERSION_CHECK \
	_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)

struct kbase_ioctl_set_flags {
	uint32_t create_flags;
};
#define KBASE_IOCTL_SET_FLAGS \
	_IOW(KBASE_IOCTL_TYPE, 1, struct kbase_ioctl_set_flags)

/* --- uapi baru: gpuprops --- */

struct kbase_ioctl_get_gpuprops {
	uint64_t buffer; /* offset  0 */
	uint32_t size;   /* offset  8 */
	uint32_t flags;  /* offset 12 */
};
#define KBASE_IOCTL_GET_GPUPROPS \
	_IOW(KBASE_IOCTL_TYPE, 3, struct kbase_ioctl_get_gpuprops)

/* Kode ukuran nilai, dari mali_kbase_ioctl.h:687-690 */
#define KBASE_GPUPROP_VALUE_SIZE_U8  0x0
#define KBASE_GPUPROP_VALUE_SIZE_U16 0x1
#define KBASE_GPUPROP_VALUE_SIZE_U32 0x2
#define KBASE_GPUPROP_VALUE_SIZE_U64 0x3

/* ID prop, dari mali_kbase_ioctl.h:692-785. Index array == ID.
 * Nomor yang bolong (5, 7) emang dipensiunin ARM (dulu GPU speed max/min). */
#define GPUPROP_MAX_ID 85
static const char *const prop_names[GPUPROP_MAX_ID + 1] = {
	[1] = "PRODUCT_ID",
	[2] = "VERSION_STATUS",
	[3] = "MINOR_REVISION",
	[4] = "MAJOR_REVISION",
	[6] = "GPU_FREQ_KHZ_MAX",
	[8] = "LOG2_PROGRAM_COUNTER_SIZE",
	[9] = "TEXTURE_FEATURES_0",
	[10] = "TEXTURE_FEATURES_1",
	[11] = "TEXTURE_FEATURES_2",
	[12] = "GPU_AVAILABLE_MEMORY_SIZE",
	[13] = "L2_LOG2_LINE_SIZE",
	[14] = "L2_LOG2_CACHE_SIZE",
	[15] = "L2_NUM_L2_SLICES",
	[16] = "TILER_BIN_SIZE_BYTES",
	[17] = "TILER_MAX_ACTIVE_LEVELS",
	[18] = "MAX_THREADS",
	[19] = "MAX_WORKGROUP_SIZE",
	[20] = "MAX_BARRIER_SIZE",
	[21] = "MAX_REGISTERS",
	[22] = "MAX_TASK_QUEUE",
	[23] = "MAX_THREAD_GROUP_SPLIT",
	[24] = "IMPL_TECH",
	[25] = "RAW_SHADER_PRESENT",
	[26] = "RAW_TILER_PRESENT",
	[27] = "RAW_L2_PRESENT",
	[28] = "RAW_STACK_PRESENT",
	[29] = "RAW_L2_FEATURES",
	[30] = "RAW_CORE_FEATURES",
	[31] = "RAW_MEM_FEATURES",
	[32] = "RAW_MMU_FEATURES",
	[33] = "RAW_AS_PRESENT",
	[34] = "RAW_JS_PRESENT",
	[35] = "RAW_JS_FEATURES_0",
	[36] = "RAW_JS_FEATURES_1",
	[37] = "RAW_JS_FEATURES_2",
	[38] = "RAW_JS_FEATURES_3",
	[39] = "RAW_JS_FEATURES_4",
	[40] = "RAW_JS_FEATURES_5",
	[41] = "RAW_JS_FEATURES_6",
	[42] = "RAW_JS_FEATURES_7",
	[43] = "RAW_JS_FEATURES_8",
	[44] = "RAW_JS_FEATURES_9",
	[45] = "RAW_JS_FEATURES_10",
	[46] = "RAW_JS_FEATURES_11",
	[47] = "RAW_JS_FEATURES_12",
	[48] = "RAW_JS_FEATURES_13",
	[49] = "RAW_JS_FEATURES_14",
	[50] = "RAW_JS_FEATURES_15",
	[51] = "RAW_TILER_FEATURES",
	[52] = "RAW_TEXTURE_FEATURES_0",
	[53] = "RAW_TEXTURE_FEATURES_1",
	[54] = "RAW_TEXTURE_FEATURES_2",
	[55] = "RAW_GPU_ID",
	[56] = "RAW_THREAD_MAX_THREADS",
	[57] = "RAW_THREAD_MAX_WORKGROUP_SIZE",
	[58] = "RAW_THREAD_MAX_BARRIER_SIZE",
	[59] = "RAW_THREAD_FEATURES",
	[60] = "RAW_COHERENCY_MODE",
	[61] = "COHERENCY_NUM_GROUPS",
	[62] = "COHERENCY_NUM_CORE_GROUPS",
	[63] = "COHERENCY_COHERENCY",
	[64] = "COHERENCY_GROUP_0",
	[65] = "COHERENCY_GROUP_1",
	[66] = "COHERENCY_GROUP_2",
	[67] = "COHERENCY_GROUP_3",
	[68] = "COHERENCY_GROUP_4",
	[69] = "COHERENCY_GROUP_5",
	[70] = "COHERENCY_GROUP_6",
	[71] = "COHERENCY_GROUP_7",
	[72] = "COHERENCY_GROUP_8",
	[73] = "COHERENCY_GROUP_9",
	[74] = "COHERENCY_GROUP_10",
	[75] = "COHERENCY_GROUP_11",
	[76] = "COHERENCY_GROUP_12",
	[77] = "COHERENCY_GROUP_13",
	[78] = "COHERENCY_GROUP_14",
	[79] = "COHERENCY_GROUP_15",
	[80] = "TEXTURE_FEATURES_3",
	[81] = "RAW_TEXTURE_FEATURES_3",
	[82] = "NUM_EXEC_ENGINES",
	[83] = "RAW_THREAD_TLS_ALLOC",
	[84] = "TLS_ALLOC",
	[85] = "RAW_GPU_FEATURES",
};

/* Nilai yang ke-decode, disimpan buat ringkasan pan_kmod di akhir. */
static uint64_t prop_val[GPUPROP_MAX_ID + 1];
static int prop_seen[GPUPROP_MAX_ID + 1];

static const char *size_code_name(unsigned code)
{
	switch (code) {
	case KBASE_GPUPROP_VALUE_SIZE_U8:
		return "u8";
	case KBASE_GPUPROP_VALUE_SIZE_U16:
		return "u16";
	case KBASE_GPUPROP_VALUE_SIZE_U32:
		return "u32";
	default:
		return "u64";
	}
}

/* Satu panggilan GET_GPUPROPS. Balikin nilai balik ioctl apa adanya
 * (>=0 = jumlah byte, <0 = error; errno di-set). */
static int get_gpuprops(int fd, void *buf, uint32_t size, uint32_t flags)
{
	struct kbase_ioctl_get_gpuprops req;

	memset(&req, 0, sizeof(req));
	req.buffer = (uint64_t)(uintptr_t)buf;
	req.size = size;
	req.flags = flags;

	return ioctl(fd, KBASE_IOCTL_GET_GPUPROPS, &req);
}

int main(void)
{
	int fd;
	int ret;
	int pre_ctx_size = -1;
	uint32_t need;
	uint8_t *buf;
	size_t off;
	unsigned n_props = 0;
	int fail = 0;

	printf("=== test_kbase3: KBASE_IOCTL_GET_GPUPROPS ===\n");
	printf("sizeof(struct kbase_ioctl_get_gpuprops) = %zu (harus 16)\n",
	       sizeof(struct kbase_ioctl_get_gpuprops));
	printf("KBASE_IOCTL_GET_GPUPROPS = 0x%08lx (harus 0x40108003)\n",
	       (unsigned long)KBASE_IOCTL_GET_GPUPROPS);
	if (sizeof(struct kbase_ioctl_get_gpuprops) != 16 ||
	    (unsigned long)KBASE_IOCTL_GET_GPUPROPS != 0x40108003UL) {
		fprintf(stderr,
			"FATAL: definisi ioctl ga cocok sama yang di binary driver.\n");
		return 1;
	}

	printf("\n[1] open /dev/mali0\n");
	fd = open("/dev/mali0", O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "FATAL open: %s\n", strerror(errno));
		return 1;
	}
	printf("    ok, fd=%d\n", fd);

	/* PROBE A — sebelum VERSION_CHECK sama sekali.
	 * Menurut source, switch pre-setup dimasukin tanpa syarat di awal
	 * kbase_kfile_ioctl(), jadi ini SEHARUSNYA jalan. Dites, bukan diasumsiin. */
	printf("\n[2] PROBE A: GET_GPUPROPS(size=0) SEBELUM VERSION_CHECK\n");
	errno = 0;
	ret = get_gpuprops(fd, NULL, 0, 0);
	if (ret < 0)
		printf("    ditolak: %s (errno %d)\n", strerror(errno), errno);
	else
		printf("    JALAN, required size = %d byte\n", ret);

	printf("\n[3] VERSION_CHECK\n");
	{
		struct kbase_ioctl_version_check vc;

		/* Minor sengaja diisi tinggi: driver nge-clamp turun ke minor
		 * dia sendiri terus nulis balik (lihat kbase_ioctl+0x1a4). */
		vc.major = 11;
		vc.minor = 0xffff;
		if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &vc) < 0) {
			fprintf(stderr, "FATAL VERSION_CHECK: %s\n",
				strerror(errno));
			close(fd);
			return 1;
		}
		printf("    UAPI driver = %u.%u\n", vc.major, vc.minor);
	}

	/* PROBE B — sesudah VERSION_CHECK tapi SEBELUM SET_FLAGS.
	 * Ini state persis yang dipakai vkEnumeratePhysicalDevices: belum ada
	 * context sama sekali. */
	printf("\n[4] PROBE B: GET_GPUPROPS(size=0) SEBELUM SET_FLAGS (tanpa context)\n");
	errno = 0;
	ret = get_gpuprops(fd, NULL, 0, 0);
	if (ret < 0) {
		printf("    ditolak: %s (errno %d)\n", strerror(errno), errno);
	} else {
		pre_ctx_size = ret;
		printf("    JALAN, required size = %d byte\n", ret);
		printf("    -> props bisa dibaca TANPA context. Bagus buat enumerasi.\n");
	}

	printf("\n[5] SET_FLAGS(create_flags=0)\n");
	{
		struct kbase_ioctl_set_flags sf = { .create_flags = 0 };

		if (ioctl(fd, KBASE_IOCTL_SET_FLAGS, &sf) < 0) {
			fprintf(stderr, "FATAL SET_FLAGS: %s\n", strerror(errno));
			close(fd);
			return 1;
		}
		printf("    ok, context kebentuk\n");
	}

	printf("\n[6] GET_GPUPROPS(size=0) sesudah context ada\n");
	errno = 0;
	ret = get_gpuprops(fd, NULL, 0, 0);
	if (ret < 0) {
		fprintf(stderr, "FATAL GET_GPUPROPS pass-1: %s\n", strerror(errno));
		close(fd);
		return 1;
	}
	need = (uint32_t)ret;
	printf("    required size = %u byte\n", need);
	if (pre_ctx_size >= 0 && pre_ctx_size != ret) {
		printf("    ANEH: beda sama sebelum context (%d vs %d)\n",
		       pre_ctx_size, ret);
		fail = 1;
	}

	/* Tes negatif. Ini yang ngebuktiin pemahaman offset field kita bener,
	 * bukan cuma "ioctl-nya balikin sesuatu". */
	printf("\n[7] Tes negatif (mastiin offset field bener)\n");

	printf("    [7a] flags=1 -> harusnya EINVAL (asm: cbnz w8 di offset 12)\n");
	errno = 0;
	ret = get_gpuprops(fd, NULL, 0, 1);
	if (ret < 0 && errno == EINVAL) {
		printf("         ok, ditolak EINVAL\n");
	} else if (ret < 0) {
		printf("         ditolak tapi errno %d (%s), bukan EINVAL\n",
		       errno, strerror(errno));
		fail = 1;
	} else {
		printf("         GAGAL: diterima (ret=%d), padahal flags != 0\n", ret);
		fail = 1;
	}

	printf("    [7b] size=1 (kurang dari %u) -> harusnya EINVAL (asm: b.cs)\n",
	       need);
	{
		uint8_t tiny[8];

		errno = 0;
		ret = get_gpuprops(fd, tiny, 1, 0);
		if (ret < 0 && errno == EINVAL) {
			printf("         ok, ditolak EINVAL\n");
		} else if (ret < 0) {
			printf("         ditolak tapi errno %d (%s), bukan EINVAL\n",
			       errno, strerror(errno));
			fail = 1;
		} else {
			printf("         GAGAL: diterima (ret=%d), padahal buffer kekecilan\n",
			       ret);
			fail = 1;
		}
	}

	printf("\n[8] GET_GPUPROPS pass-2: ambil %u byte beneran\n", need);
	buf = calloc(1, need);
	if (!buf) {
		fprintf(stderr, "FATAL calloc(%u): %s\n", need, strerror(errno));
		close(fd);
		return 1;
	}
	errno = 0;
	ret = get_gpuprops(fd, buf, need, 0);
	if (ret < 0) {
		fprintf(stderr, "FATAL GET_GPUPROPS pass-2: %s\n", strerror(errno));
		free(buf);
		close(fd);
		return 1;
	}
	printf("    ok, kernel nulis %d byte\n", ret);
	if ((uint32_t)ret != need) {
		printf("    ANEH: pass-2 balikin %d, pass-1 bilang %u\n", ret, need);
		fail = 1;
	}

	printf("\n[9] Decode key/value\n");
	printf("    %-6s %-32s %-4s %s\n", "id", "nama", "tipe", "nilai");
	printf("    ---------------------------------------------------------------\n");
	off = 0;
	while (off + 4 <= need) {
		uint32_t key;
		unsigned id, code, vsz;
		uint64_t val = 0;

		memcpy(&key, buf + off, 4);
		off += 4;

		id = key >> 2;
		code = key & 3;
		vsz = 1u << code; /* 0->1, 1->2, 2->4, 3->8 */

		if (off + vsz > need) {
			printf("    !! buffer kepotong di offset %zu (butuh %u byte lagi)\n",
			       off, vsz);
			fail = 1;
			break;
		}

		/* WAJIB memcpy: nilai dipacking rapat, bisa ga ter-align. */
		memcpy(&val, buf + off, vsz);
		off += vsz;
		n_props++;

		if (id <= GPUPROP_MAX_ID) {
			prop_val[id] = val;
			prop_seen[id] = 1;
		}

		printf("    %-6u %-32s %-4s 0x%016" PRIx64 "  (%" PRIu64 ")\n", id,
		       (id <= GPUPROP_MAX_ID && prop_names[id]) ? prop_names[id]
							        : "<?>",
		       size_code_name(code), val, val);
	}
	if (off != need) {
		printf("    !! sisa %zu byte ga ke-decode (off=%zu, need=%u)\n",
		       need - off, off, need);
		fail = 1;
	}
	printf("    total %u prop, %zu byte kebaca habis\n", n_props, off);

	/* Ringkasan: yang dibutuhin pan_kmod_dev_props.
	 * Mapping diambil dari panfrost_kmod.c:176-210 (panfrost_dev_query_props)
	 * dan :97-174 (panfrost_dev_query_thread_props), dicocokin ke ID kbase. */
	printf("\n[10] Ringkasan buat pan_kmod_dev_props\n");
	printf("     (pemetaan dari DRM_PANFROST_PARAM_* ke KBASE_GPUPROP_*)\n\n");

#define SHOW(field, id)                                                       \
	do {                                                                  \
		if (prop_seen[id])                                            \
			printf("     %-28s <- %-28s 0x%" PRIx64 "\n", field,  \
			       prop_names[id], prop_val[id]);                 \
		else                                                          \
			printf("     %-28s <- %-28s ABSEN\n", field,          \
			       prop_names[id] ? prop_names[id] : "?");        \
	} while (0)

	SHOW("gpu_id", 55);
	SHOW("shader_present", 25);
	SHOW("tiler_features", 51);
	SHOW("mem_features", 31);
	SHOW("mmu_features", 32);
	SHOW("l2_features", 29);
	SHOW("texture_features[0]", 52);
	SHOW("texture_features[1]", 53);
	SHOW("texture_features[2]", 54);
	SHOW("texture_features[3]", 81);
	SHOW("max_threads_per_core", 56);
	SHOW("max_threads_per_wg", 57);
	SHOW("(thread_features)", 59);
	SHOW("max_tls_instance_per_core", 83);
#undef SHOW

	if (prop_seen[59]) {
		uint32_t tf = (uint32_t)prop_val[59];

		printf("\n     turunan dari RAW_THREAD_FEATURES 0x%08x:\n", tf);
		printf("       max_tasks_per_core      = %u  (tf >> 24, min 1)\n",
		       (tf >> 24) ? (tf >> 24) : 1u);
		printf("       num_registers_per_core  = %u  (tf & 0xffff)\n",
		       tf & 0xffffu);
	}

	if (prop_seen[55]) {
		uint64_t gid = prop_val[55];

		printf("\n     GPU_ID 0x%" PRIx64 " -> product 0x%04x, arch v%u\n",
		       gid, (unsigned)((gid >> 16) & 0xffff),
		       (unsigned)((gid >> 28) & 0xf));
	}

	printf("\n     CATATAN: afbc_features dan timestamp_frequency TIDAK punya\n");
	printf("     KBASE_GPUPROP_* padanan. panfrost_kmod.c ngambil keduanya lewat\n");
	printf("     DRM_PANFROST_PARAM_AFBC_FEATURES / _SYSTEM_TIMESTAMP_FREQUENCY.\n");
	printf("     Di kbase, timestamp ada di KBASE_IOCTL_GET_CPU_GPU_TIMEINFO (nr 50);\n");
	printf("     AFBC belum ketemu sumbernya — masih terbuka.\n");

	free(buf);
	close(fd);

	printf("\n=== %s ===\n", fail ? "ADA YANG GA BERES (lihat tanda !!/GAGAL/ANEH di atas)"
				     : "SEMUA CEK LULUS");
	return fail ? 1 : 0;
}
