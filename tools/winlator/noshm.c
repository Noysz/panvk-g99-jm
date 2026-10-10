/* noshm.c: LD_PRELOAD shim for GE-Proton (The412Banner bionic build) on
 * Termux:X11. Its winex11.so draws windows through MIT-SHM with segments
 * from Winlator's libandroid-sysvshm, which the Termux:X11 server cannot
 * attach, so every window stays black. Making shmget() fail makes winex11
 * fall back to plain XPutImage. Only used by run_ge.sh. */
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

int
shmget(key_t key, size_t size, int flags)
{
   (void)key;
   (void)size;
   (void)flags;
   errno = ENOSYS;
   return -1;
}
