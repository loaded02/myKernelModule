# myKernelModule
linux charDev which maps kernelspace into userSpace.
Uart input is simulated through array.

This kernel module is tested with kernel 3.16.0-23-generic
on a linux kubuntu vm
This module creates a kernel memory space which is then remapped into
userspace via a reimplemented character device mmap function.

For demonstration purposes the first value inside the "shared memory"
is incremented by an interrupt. IRQ is attached to an existing IRQ.
## run
```
$ sudo mknod /dev/myChardev c 60 0
$ ls -l /dev/char_dev
> crw-r--r--. 1 root root 60, 0 Mar 17 09:03 /dev/myChardev
$ insmod uartModule.ko
$ ./userProcess
```
