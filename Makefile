module-name	:= uartModule
obj-m		:= $(module-name).o
KDIR		:= /lib/modules/$(shell uname -r)/build
PWD		:= $(shell pwd)
MAKE := make

CC = gcc

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) userProcess.c -o userProcess
	
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean