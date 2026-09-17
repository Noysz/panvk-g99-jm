/* verify_atom_size.c (v2 - hindari bentrok nama __u* dengan header sistem) */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

struct base_jd_udata {
    uint64_t blob[2];
};

struct base_dependency {
    uint8_t atom_id;
    uint8_t dependency_type;
};

struct base_jd_atom_v2 {
    uint64_t jc;
    struct base_jd_udata udata;
    uint64_t extres_list;
    uint16_t nr_extres;
    uint8_t jit_id[2];
    struct base_dependency pre_dep[2];
    uint8_t atom_number;
    uint8_t prio;
    uint8_t device_nr;
    uint8_t jobslot;
    uint32_t core_req;
    uint8_t padding[8];
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
