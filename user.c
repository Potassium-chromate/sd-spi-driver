#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "sdspi_ioctl.h"   // header with your ioctl numbers/structs

int main(void) {
    int fd = open("/dev/sdspi0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // Initialize card
    if (ioctl(fd, SDSPI_IOC_INIT_CARD) < 0) {
        perror("ioctl INIT");
        return 1;
    }

    // Read one block
    struct sdspi_xfer x;
    x.lba = 0;
    if (ioctl(fd, SDSPI_IOC_READ_BLOCK, &x) < 0) {
        perror("ioctl READ_BLOCK");
        return 1;
    }

    // Print first 16 bytes
    for (int i = 0; i < 16; i++)
        printf("%02X ", x.buf[i]);
    printf("\n");

    close(fd);
    return 0;
}

