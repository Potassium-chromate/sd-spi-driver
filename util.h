#include <stdio.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include "sdspi_ioctl.h"
#include "ff.h"

int mount_fs();
void list_dir(const char *path);
void write_file(const char *file_name, const char *in_buffer);
void read_file(const char *file_name, char *out_buffer, size_t bufsize);
void umount_fs();
void fs_info();
void make_dir(const char *name);
void delete_file(const char *name);
void rename_file(const char *old_name, const char *new_name);
//void pwd();
