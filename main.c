// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/delay.h> 

#include "sdspi_ioctl.h"

#define SDSPI_NAME      "sdspi"
#define SDSPI_NODE      "sdspi0"

struct sdspi_dev {
    struct spi_device   *spi;
    struct miscdevice    miscdev;
    struct mutex         lock;
    bool                 initialized;
    bool                 hc;      /* SDHC/SDXC block addressing */
};

static int sdspi_spi_xfer(struct spi_device *spi, const u8 *tx, u8 *rx, size_t len)
{
    struct spi_transfer tr = {
        .tx_buf   = tx,
        .rx_buf   = rx,
        .len      = len,
        .bits_per_word = 8,
        /* keep speed modest until after init */
    };
    struct spi_message m;

    spi_message_init(&m);
    spi_message_add_tail(&tr, &m);
    return spi_sync(spi, &m);
}

static u8 sd_xchg_byte(struct spi_device *spi, u8 data)
{
    u8 tx = data, rx = 0xFF;
    sdspi_spi_xfer(spi, &tx, &rx, 1);
    return rx;
}

static u8 send_cmd(struct spi_device *spi, u8 cmd, u32 arg, u8 crc)
{
    u8 buf[6];
    int i;

    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;
    buf[5] = crc;

    for (i = 0; i < 6; i++)
        sd_xchg_byte(spi, buf[i]);

    for (i = 0; i < 8; i++) {
        u8 r = sd_xchg_byte(spi, 0xFF);
        if (r != 0xFF)
            return r;
    }
    return 0xFF;  // timeout
}

/* TODO: implement SD init using CMD0/CMD8/ACMD41/CMD58 with spi_sync_transfer */
static int sdspi_card_init(struct sdspi_dev *dev)
{
    int ret = 0;
    u8 resp, ocr[4], r7[4];
    unsigned long timeout;

    mutex_lock(&dev->lock);

    /* Send 80 dummy clocks (10 bytes of 0xFF) */
    for (int i = 0; i < 10; i++)
        sd_xchg_byte(dev->spi, 0xFF);

    /* CMD0: go idle */
    resp = send_cmd(dev->spi, 0, 0, 0x95);
    if (resp != 0x01) {
        pr_err("sdspi: no response to CMD0 (got 0x%02x)\n", resp);
        goto fail;
    }

    /* CMD8: check SD v2 */
    resp = send_cmd(dev->spi, 8, 0x1AA, 0x87);
    if (resp == 0x01) {
        /* read R7 (4 bytes) */
        for (int i = 0; i < 4; i++)
            r7[i] = sd_xchg_byte(dev->spi, 0xFF);

        if (r7[2] == 0x01 && r7[3] == 0xAA) {
            /* loop ACMD41 until ready, max ~1s */
            timeout = jiffies + HZ;
            do {
                send_cmd(dev->spi, 55, 0, 0x01);
                resp = send_cmd(dev->spi, 41, 1UL << 30, 0x01);
                if (resp == 0x00)
                    break;
                msleep(10);
            } while (time_before(jiffies, timeout));

            if (resp == 0x00 && send_cmd(dev->spi, 58, 0, 0x01) == 0x00) {
                for (int i = 0; i < 4; i++)
                    ocr[i] = sd_xchg_byte(dev->spi, 0xFF);
                if (ocr[0] & 0x40)
                    dev->hc = true; /* SDHC */
            }
        }
    } else {
        /* Older SD v1 or MMC init */
        pr_info("sdspi: SD v1/MMC not fully implemented here\n");
    }

    /* If success: bump SPI speed */
    dev->spi->max_speed_hz = 4000000;
    spi_setup(dev->spi);

    dev->initialized = true;
    pr_info("sdspi: SD card initialized (HC=%d)\n", dev->hc);

    mutex_unlock(&dev->lock);
    return 0;

fail:
    mutex_unlock(&dev->lock);
    return -EIO;
}

/* TODO: implement CMD17 read using spi_sync_transfer */
static int sdspi_read_block(struct sdspi_dev *dev, u32 lba, u8 *buf)
{
    int ret = 0;
    u32 addr;
    u8 resp, token;
    int i, timeout;

    mutex_lock(&dev->lock);

    if (!dev->initialized) {
        ret = -ENODEV;
        goto out;
    }

    /* Convert LBA to byte address if not HC */
    addr = dev->hc ? lba : lba * 512;

    /* CMD17 = READ_SINGLE_BLOCK */
    resp = send_cmd(dev->spi, 17, addr, 0x01);
    if (resp != 0x00) {
        pr_err("sdspi: CMD17 failed (resp=0x%02x)\n", resp);
        ret = -EIO;
        goto out;
    }

    /* Wait for data token 0xFE */
    timeout = 10000;
    do {
        token = sd_xchg_byte(dev->spi, 0xFF);
    } while (token == 0xFF && --timeout);

    if (token != 0xFE) {
        pr_err("sdspi: bad token 0x%02x\n", token);
        ret = -EIO;
        goto out;
    }

    /* Read 512-byte block */
    for (i = 0; i < 512; i++)
        buf[i] = sd_xchg_byte(dev->spi, 0xFF);

    /* Read and discard 2 CRC bytes */
    sd_xchg_byte(dev->spi, 0xFF);
    sd_xchg_byte(dev->spi, 0xFF);

    ret = 0; /* success */

out:
    mutex_unlock(&dev->lock);
    pr_info("sdspi: sdspi_read_block success\n");
    return ret;
}

static int sdspi_write_block(struct sdspi_dev *dev, u32 lba, const u8 *buf)
{
    u32 addr;
    u8 resp;
    int i, timeout;
    int ret = 0;

    mutex_lock(&dev->lock);

    if (!dev->initialized) {
        ret = -ENODEV;
        goto out;
    }

    /* Convert LBA to byte address if not HC */
    addr = dev->hc ? lba : lba * 512;

    /* CMD24 = WRITE_SINGLE_BLOCK */
    resp = send_cmd(dev->spi, 24, addr, 0x01);
    if (resp != 0x00) {
        pr_err("sdspi: CMD24 failed (resp=0x%02x)\n", resp);
        ret = -EIO;
        goto out;
    }

    /* One byte gap */
    sd_xchg_byte(dev->spi, 0xFF);

    /* Start token */
    sd_xchg_byte(dev->spi, 0xFE);

    /* Write 512 data bytes */
    for (i = 0; i < 512; i++)
        sd_xchg_byte(dev->spi, buf[i]);

    /* Two dummy CRC bytes */
    sd_xchg_byte(dev->spi, 0xFF);
    sd_xchg_byte(dev->spi, 0xFF);

    /* Data response */
    resp = sd_xchg_byte(dev->spi, 0xFF);
    if ((resp & 0x1F) != 0x05) {
        pr_err("sdspi: Write rejected (resp=0x%02x)\n", resp);
        ret = -EIO;
        goto out;
    }

    /* Wait while card is busy (MISO = 0) */
    timeout = 50000; /* ~50ms max */
    do {
        resp = sd_xchg_byte(dev->spi, 0xFF);
        if (resp == 0xFF)
            break;
        udelay(1);
    } while (--timeout);

    if (!timeout) {
        pr_err("sdspi: Write busy timeout\n");
        ret = -ETIMEDOUT;
        goto out;
    }

    ret = 0; /* success */

out:
    mutex_unlock(&dev->lock);
    return ret;
}
/* ------------ miscdevice (char dev) ------------- */

static long sdspi_unlocked_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct sdspi_dev *dev = container_of(filp->private_data, struct sdspi_dev, miscdev);
    struct sdspi_xfer x;

    switch (cmd) {
    case SDSPI_IOC_INIT_CARD:
        return sdspi_card_init(dev);

    case SDSPI_IOC_READ_BLOCK:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x)))
            return -EFAULT;
        if (!dev->initialized)
            return -ENODEV;
        if (sdspi_read_block(dev, x.lba, x.buf))
            return -EIO;
        if (copy_to_user((void __user *)arg, &x, sizeof(x)))
            return -EFAULT;
        return 0;
        
    case SDSPI_IOC_WRITE_BLOCK:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x)))
            return -EFAULT;
        if (!dev->initialized)
            return -ENODEV;
        if (sdspi_write_block(dev, x.lba, x.buf))
            return -EIO;
        return 0;

    default:
        return -ENOTTY;
    }
}

static int sdspi_open(struct inode *inode, struct file *filp)
{
    return 0;
}

static const struct file_operations sdspi_fops = {
    .owner          = THIS_MODULE,
    .unlocked_ioctl = sdspi_unlocked_ioctl,
    .open           = sdspi_open,
    .llseek         = noop_llseek,
};

static int sdspi_probe(struct spi_device *spi)
{
    int ret;
    struct sdspi_dev *dev;

    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->spi = spi;
    mutex_init(&dev->lock);

    /* Configure SPI mode 0, bits, speed */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 200000; /* safe for init, you can bump later with dev->initialized */
    ret = spi_setup(spi);
    if (ret)
        return ret;

    dev->miscdev.minor = MISC_DYNAMIC_MINOR;
    dev->miscdev.name  = SDSPI_NODE;
    dev->miscdev.fops  = &sdspi_fops;

    ret = misc_register(&dev->miscdev);
    if (ret)
        return ret;

    spi_set_drvdata(spi, dev);
    dev_info(&spi->dev, "sdspi probed\n");
    return 0;
}

static void sdspi_remove(struct spi_device *spi)
{
    struct sdspi_dev *dev = spi_get_drvdata(spi);
    misc_deregister(&dev->miscdev);
    dev_info(&spi->dev, "sdspi removed\n");
}

static const struct of_device_id sdspi_of_match[] = {
    { .compatible = "ncku,sdspi", },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sdspi_of_match);

static struct spi_driver sdspi_driver = {
    .driver = {
        .name           = SDSPI_NAME,
        .of_match_table = sdspi_of_match,
    },
    .probe  = sdspi_probe,
    .remove = sdspi_remove,
};
module_spi_driver(sdspi_driver);

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("NCKU, Taiwan");
MODULE_DESCRIPTION("Custom SD-over-SPI demo driver (char device)");
