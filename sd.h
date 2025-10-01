#ifndef SD_H
#define SD_H

#define DEVICE "/dev/spidev0.0"
// Card type flags
#define CT_SD1   0x01
#define CT_SD2   0x02
#define CT_BLOCK 0x04
#define CT_MMC   0x08

extern int spi_fd;
uint32_t speed = 125000;   // 125 kHz init speed
uint8_t bits = 8;
int CardType = 0;

int sd_init();
int sd_read_block(uint32_t block, uint8_t *buf);
int sd_write_block(uint32_t block, const uint8_t *buf);
#endif
