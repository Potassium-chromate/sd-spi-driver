#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "util.h"

void print_help() {
    printf("Available commands:\n");
    printf("  mount                  - Mount the filesystem\n");
    printf("  umount                 - Unmount the filesystem\n");
    printf("  ls [path]              - List directory contents (default: /)\n");
    printf("  cat <file>             - Show file contents\n");
    printf("  write <file> <text>    - Write text to file (overwrite)\n");
    printf("  rm <file>              - Delete file\n");
    printf("  mv <old> <new>         - Rename file\n");
    printf("  mkdir <dir>            - Create directory\n");
    printf("  info                   - Show filesystem info\n");
    printf("  pwd                    - Print current directory\n");
    printf("  help                   - Show this help\n");
    printf("  exit                   - Quit CLI\n");
}

int main() {
    char cmd[256];

    printf("Simple SD-Card CLI (FatFs)\n");
    print_help();

    while (1) {
        printf("sd> ");
        if (!fgets(cmd, sizeof(cmd), stdin)) break;

        // remove trailing newline
        cmd[strcspn(cmd, "\n")] = 0;

        if (strncmp(cmd, "mount", 5) == 0) {
            mount_fs();
        } else if (strncmp(cmd, "umount", 6) == 0) {
            umount_fs();
        } else if (strncmp(cmd, "ls", 2) == 0) {
            char *arg = strtok(cmd, " ");
            arg = strtok(NULL, " "); // path
            if (!arg) arg = "/";
            list_dir(arg);
        } else if (strncmp(cmd, "cat ", 4) == 0) {
            char buf[512];
            read_file(cmd + 4, buf, sizeof(buf));
        } else if (strncmp(cmd, "write ", 6) == 0) {
            char *file = strtok(cmd + 6, " ");
            char *text = strtok(NULL, "");
            if (file && text) {
                write_file(file, text);
            } else {
                printf("Usage: write <file> <text>\n");
            }
        } else if (strncmp(cmd, "rm ", 3) == 0) {
            delete_file(cmd + 3);
        } else if (strncmp(cmd, "mv ", 3) == 0) {
            char *old = strtok(cmd + 3, " ");
            char *new = strtok(NULL, "");
            if (old && new) {
                rename_file(old, new);
            } else {
                printf("Usage: mv <old> <new>\n");
            }
        } else if (strncmp(cmd, "mkdir ", 6) == 0) {
            make_dir(cmd + 6);
        } else if (strncmp(cmd, "info", 4) == 0) {
            fs_info();
        } else if (strncmp(cmd, "pwd", 3) == 0) {
            //pwd();
            ;
        } else if (strncmp(cmd, "help", 4) == 0) {
            print_help();
        } else if (strncmp(cmd, "exit", 4) == 0) {
            break;
        } else if (strlen(cmd) > 0) {
            printf("Unknown command: %s\n", cmd);
        }
    }

    return 0;
}
