#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdint.h>

#define KBASE_IOCTL_VERSION_CHECK _IOWR(0x80, 0, struct kbase_ioctl_version_check)
struct kbase_ioctl_version_check { uint16_t major, minor; };

int main() {
    int fd = open("/dev/mali0", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }
    struct kbase_ioctl_version_check v = {0};
    if (ioctl(fd, KBASE_IOCTL_VERSION_CHECK, &v) < 0) { perror("ioctl"); return 1; }
    printf("Kbase UAPI version: major=%d minor=%d\n", v.major, v.minor);
    close(fd);
    return 0;
}
