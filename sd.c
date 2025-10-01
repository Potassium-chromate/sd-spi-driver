#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <time.h>
# include "sd.h"


int spi_fd = -1;

// --- SPI helpers ---
uint8_t xchg_spi(uint8_t val) {
    uint8_t tx[1] = {val};
    uint8_t rx[1] = {0};
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = 1,
        .speed_hz = speed,
        .bits_per_word = bits,
    };
    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr) < 1) {
        perror("SPI_IOC_MESSAGE");
        exit(1);
    }
    return rx[0];
}

void deselect() {
    xchg_spi(0xFF); // one dummy byte with CS high
}

// --- SD command helpers ---
uint8_t send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    uint8_t buf[6];
    buf[0] = 0x40 | cmd;
    buf[1] = (arg >> 24) & 0xFF;
    buf[2] = (arg >> 16) & 0xFF;
    buf[3] = (arg >> 8) & 0xFF;
    buf[4] = arg & 0xFF;
    buf[5] = crc;

    // send command
    for (int i = 0; i < 6; i++) xchg_spi(buf[i]);

    // wait for response (max 8 bytes)
    for (int i = 0; i < 8; i++) {
        uint8_t r = xchg_spi(0xFF);
        if (r != 0xFF) return r;
    }
    return 0xFF;
}

void send_cmd_r7(uint8_t *resp) {
    for (int i = 0; i < 4; i++) resp[i] = xchg_spi(0xFF);
}

void send_cmd_r3(uint8_t *resp) {
    for (int i = 0; i < 4; i++) resp[i] = xchg_spi(0xFF);
}

// --- SD initialization ---
int sd_init() {
    CardType = 0;

    // 80 dummy clocks
    for (int i = 0; i < 10; i++) xchg_spi(0xFF);

    // CMD0: go idle
    if (send_cmd(0, 0, 0x95) != 0x01) {
        printf("No response to CMD0\n");
        return 0;
    }

    // CMD8: check SD v2
    if (send_cmd(8, 0x1AA, 0x87) == 0x01) {
        uint8_t r7[4];
        send_cmd_r7(r7);
        if (r7[2] == 0x01 && r7[3] == 0xAA) {
            // v2 card, ACMD41 with HCS
            time_t t0 = time(NULL);
            uint8_t resp;
            do {
                send_cmd(55, 0, 0x01);
                resp = send_cmd(41, 1UL<<30, 0x01);
            } while (resp != 0x00 && (time(NULL) - t0) < 1);
            if (resp == 0x00 && send_cmd(58, 0, 0x01) == 0x00) {
                uint8_t ocr[4];
                send_cmd_r3(ocr);
                if (ocr[0] & 0x40) {
                    CardType = CT_SD2 | CT_BLOCK; // SDHC
                } else {
                    CardType = CT_SD2; // SDSC
                }
            }
        }
    } else {
        // v1 or MMC
        uint8_t cmd;
        if (send_cmd(55, 0, 0x01) <= 1 && send_cmd(41, 0, 0x01) <= 1) {
            CardType = CT_SD1; cmd = 41;
        } else {
            CardType = CT_MMC; cmd = 1;
        }
        time_t t0 = time(NULL);
        while (send_cmd(cmd, 0, 0x01) != 0x00 && (time(NULL) - t0) < 1);

        if (!(CardType & CT_BLOCK)) {
            if (send_cmd(16, 512, 0x01) != 0x00) CardType = 0;
        }
    }

    deselect();

    if (CardType) {
        speed = 4000000; // 4 MHz after init
        ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
        printf("SD card initialized. Type: %d\n", CardType);
        return 1;
    } else {
        printf("SD init failed\n");
        return 0;
    }
}

int sd_read_block(uint32_t block, uint8_t *buf) {
    uint32_t addr = (CardType & CT_BLOCK) ? block : block * 512;

    if (send_cmd(17, addr, 0x01) != 0x00) {
        printf("CMD17 failed\n");
        return 0;
    }

    // Wait for data token 0xFE
    uint8_t token;
    int timeout = 10000;
    do {
        token = xchg_spi(0xFF);
    } while (token == 0xFF && --timeout);

    if (token != 0xFE) {
        printf("Read timeout or bad token: 0x%02X\n", token);
        return 0;
    }

    // Read 512 bytes
    for (int i = 0; i < 512; i++) {
        buf[i] = xchg_spi(0xFF);
    }

    // Read 2-byte CRC (ignored here)
    xchg_spi(0xFF);
    xchg_spi(0xFF);

    deselect();
    return 1;
}

#include "spi.h"
#include <stdio.h>
#include <unistd.h>

// Send a single 512-byte block to the SD card
int sd_write_block(uint32_t block, const uint8_t *buf) {
    uint32_t addr = (CardType & CT_BLOCK) ? block : block * 512;

    // Send CMD24 (WRITE_SINGLE_BLOCK)
    if (send_cmd(24, addr, 0x01) != 0x00) {
        printf("CMD24 failed\n");
        return 0;
    }

    // Send one byte gap
    xchg_spi(0xFF);

    // Send start token (0xFE)
    xchg_spi(0xFE);

    // Send 512 bytes of data
    for (int i = 0; i < 512; i++) {
        xchg_spi(buf[i]);
    }

    // Send dummy CRC
    xchg_spi(0xFF);
    xchg_spi(0xFF);

    // Get data response token
    uint8_t resp = xchg_spi(0xFF);
    if ((resp & 0x1F) != 0x05) {
        printf("Write rejected, resp=0x%02X\n", resp);
        return 0;
    }

    // Wait for card to finish programming
    while (xchg_spi(0xFF) == 0x00) {
        usleep(1000); // small delay
    }

    return 1; // success
}

