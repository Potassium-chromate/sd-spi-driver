#ifndef __SDSPI_IOCTL_H__
#define __SDSPI_IOCTL_H__

#include <linux/ioctl.h>
#include <linux/types.h>   /* for __u32, __u8 */

/* ioctl magic */
#define SDSPI_IOC_MAGIC 'S'

/* ioctl commands */
struct sdspi_xfer {
    __u32 lba;         /* block number */
    __u8  buf[512];    /* data buffer */
};

#define SDSPI_IOC_INIT_CARD   _IO(SDSPI_IOC_MAGIC,  0x00)
#define SDSPI_IOC_WRITE_BLOCK  _IOWR(SDSPI_IOC_MAGIC, 0x01, struct sdspi_xfer)
#define SDSPI_IOC_READ_BLOCK  _IOWR(SDSPI_IOC_MAGIC, 0x02, struct sdspi_xfer)


#endif
