/* verify_atom_size.c
 *
 * Verifikasi sizeof(struct base_jd_atom_v2) LANGSUNG dari compiler ARM64
 * asli di device, bukan hitungan manual. Struct di-declare ulang PERSIS
 * sesuai definisi di mali_base_jm_kernel.h (r54p1, UAPI 11.46) - tanpa
 * include header kernel manapun, biar tidak ada dependency yang bisa
 * beda hasil.
 *
 * Referensi field (dari source resmi Arm r54p1):
 *   struct base_jd_atom_v2 {
 *       __u64 jc;
 *       struct base_jd_udata udata;       // { __u64 blob[2]; }
 *       __u64 extres_list;
 *       __u16 nr_extres;
 *       __u8 jit_id[2];
 *       struct base_dependency pre_dep[2]; // { __u8 atom_id; __u8 dependency_type; }
 *       base_atom_id atom_number;          // __u8
 *       base_jd_prio prio;                 // __u8
 *       __u8 device_nr;
 *       __u8 jobslot;
 *       base_jd_core_req core_req;         // __u32
 *       __u8 padding[8];
 *   };
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

struct base_jd_udata {
    __u64 blob[2];
};

struct base_dependency {
    __u8 atom_id;
    __u8 dependency_type;
};

typedef __u8  base_atom_id;
typedef __u8  base_jd_prio;
typedef __u32 base_jd_core_req;

struct base_jd_atom_v2 {
    __u64 jc;
    struct base_jd_udata udata;
    __u64 extres_list;
    __u16 nr_extres;
    __u8 jit_id[2];
    struct base_dependency pre_dep[2];
    base_atom_id atom_number;
    base_jd_prio prio;
    __u8 device_nr;
    __u8 jobslot;
    base_jd_core_req core_req;
    __u8 padding[8];
};

int main(void) {
    printf("sizeof(struct base_jd_atom_v2) = %zu bytes\n", sizeof(struct base_jd_atom_v2));
    printf("alignof(struct base_jd_atom_v2) = %zu bytes\n", _Alignof(struct base_jd_atom_v2));
    printf("\n--- offsetof tiap field ---\n");
    printf("jc            = %zu\n", offsetof(struct base_jd_atom_v2, jc));
    printf("udata         = %zu\n", offsetof(struct base_jd_atom_v2, udata));
    printf("extres_list   = %zu\n", offsetof(struct base_jd_atom_v2, extres_list));
    printf("nr_extres     = %zu\n", offsetof(struct base_jd_atom_v2, nr_extres));
    printf("jit_id        = %zu\n", offsetof(struct base_jd_atom_v2, jit_id));
    printf("pre_dep       = %zu\n", offsetof(struct base_jd_atom_v2, pre_dep));
    printf("atom_number   = %zu\n", offsetof(struct base_jd_atom_v2, atom_number));
    printf("prio          = %zu\n", offsetof(struct base_jd_atom_v2, prio));
    printf("device_nr     = %zu\n", offsetof(struct base_jd_atom_v2, device_nr));
    printf("jobslot       = %zu\n", offsetof(struct base_jd_atom_v2, jobslot));
    printf("core_req      = %zu\n", offsetof(struct base_jd_atom_v2, core_req));
    printf("padding       = %zu\n", offsetof(struct base_jd_atom_v2, padding));
    return 0;
}
