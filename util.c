#include "util.h"

FATFS fs;
FIL fil;
FRESULT fr;
char buffer[64];
UINT br;
DIR dir;
FILINFO fno;

int mount_fs() {
    fr = f_mount(&fs, "", 1);
    if (fr != FR_OK) {
        printf("f_mount failed: %d\n", fr);
        return 1;
    }
    return 0;
}

void list_dir(const char *path) {
    fr = f_opendir(&dir, path);
    if (fr == FR_OK) {
        for (;;) {
            fr = f_readdir(&dir, &fno);
            if (fr != FR_OK || fno.fname[0] == 0) break;
            printf("%s%s\n", fno.fname, (fno.fattrib & AM_DIR) ? "/" : "");
        }
        f_closedir(&dir);
    }
}

void write_file(const char *file_name, const char *in_buffer) {
    fr = f_open(&fil, file_name, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        UINT bw;
        f_write(&fil, in_buffer, strlen(in_buffer), &bw);
        f_close(&fil);
        printf("Wrote %u bytes to %s\n", bw, file_name);
    } else {
        printf("Failed to open %s for write (%d)\n", file_name, fr);
    }
}

void read_file(const char *file_name, char *out_buffer, size_t bufsize) {
    fr = f_open(&fil, file_name, FA_READ);
    if (fr == FR_OK) {
        f_read(&fil, out_buffer, bufsize-1, &br);
        out_buffer[br] = '\0';
        printf("Contents: %s\n", out_buffer);
        f_close(&fil);
    } else {
        printf("Failed to open %s (%d)\n", file_name, fr);
    }
}

void umount_fs() {
    f_mount(0, "", 0);
    printf("Unmounted SD card.\n");
}

void fs_info() {
    DWORD fre_clust, fre_sect, tot_sect;
    FATFS *pfs;

    fr = f_getfree("", &fre_clust, &pfs);
    if (fr == FR_OK) {
        tot_sect = (pfs->n_fatent - 2) * pfs->csize;
        fre_sect = fre_clust * pfs->csize;

        printf("Total: %u KB\nFree: %u KB\n",
               tot_sect / 2, fre_sect / 2);
    } else {
        printf("f_getfree failed: %d\n", fr);
    }
}

void make_dir(const char *name) {
    fr = f_mkdir(name);
    if (fr == FR_OK) printf("Created directory: %s\n", name);
    else printf("Failed to create dir %s (%d)\n", name, fr);
}

void delete_file(const char *name) {
    fr = f_unlink(name);
    if (fr == FR_OK) printf("Deleted: %s\n", name);
    else printf("Failed to delete %s (%d)\n", name, fr);
}

void rename_file(const char *old_name, const char *new_name) {
    fr = f_rename(old_name, new_name);
    if (fr == FR_OK) printf("Renamed %s -> %s\n", old_name, new_name);
    else printf("Failed to rename %s (%d)\n", old_name, fr);
}
/*
void pwd() {
    char path[64];
    fr = f_getcwd(path, sizeof(path));
    if (fr == FR_OK) printf("Current directory: %s\n", path);
    else printf("f_getcwd failed (%d)\n", fr);
}*/
