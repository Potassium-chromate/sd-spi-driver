# Simple Kbuild makefile
CC = gcc
CFLAGS = -Wall
file_name = main
obj-m += $(file_name).o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

ins:
	sudo insmod $(file_name).ko
rm:
	sudo rmmod $(file_name)
	
user_test:
	$(CC) $(CFLAGS) -o user user.c sdspi_ioctl.h
	sudo ./user
