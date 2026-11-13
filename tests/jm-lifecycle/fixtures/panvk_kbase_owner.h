#ifndef PANVK_KBASE_OWNER_H
#define PANVK_KBASE_OWNER_H

#include <stdbool.h>

struct panvk_kbase_owner;

struct panvk_kbase_owner *panvk_kbase_owner_create(void *data);
struct panvk_kbase_owner *panvk_kbase_owner_ref(struct panvk_kbase_owner *owner);
void panvk_kbase_owner_unref(struct panvk_kbase_owner *owner);
void *panvk_kbase_owner_enter(struct panvk_kbase_owner *owner);
void panvk_kbase_owner_leave(struct panvk_kbase_owner *owner);
void panvk_kbase_owner_close(struct panvk_kbase_owner *owner);

#endif
