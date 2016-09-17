/*
* Created on: 2011.10.07
* Author: Clemens Fischer clemens.fischer@h-da.de
*
* This application demonstrates the use of kernel memory from userspace
*
* mmap is called with the file descriptor of the character device
*
* For demonstration the value is decremented once
*/
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

/* message length */
#define MESSAGE_LENGTH 16

int main()
{
  char *kadr;
  char dev[] = "/dev/myChardev";
  int fd;
  /*
  * len is the size of the memory segment
  * it must be a factor of pagesize
  * one page is 4096 bytes on the beagleboard
  * which is enough for one value
  */
  int len = getpagesize();
  printf("pagesize:%d\n",len);
  /*
  * Open the character device
  */
  fd = open(dev, O_RDWR|O_SYNC);
  if (fd < 0) 
  {
    printf("Error open()\n");
    return 1;
  }
  /*
  * Mapping kernel memory to kadr
  */
  kadr = mmap(0, len, PROT_READ|PROT_WRITE,
	      MAP_SHARED| MAP_LOCKED, fd, 0);
  if (kadr < 0) 
  {
    printf("Error mmap()\n");
    return 1;
  }
  /*
  * getValue
  */
//  char *ubuffer;
//  ubuffer = calloc(MESSAGE_LENGTH, sizeof(char));
//  char userBuffer[MESSAGE_LENGTH];
  int ret;

  while((ret = read(fd, kadr, MESSAGE_LENGTH)) == 0) {
    if (ret < 0) {
      printf("Error read()\n");
      return 1;
    }
    printf("Waiting for message...\n");
    sleep(1);
  }
  printf("Incomming Message:\n");
  int i;
  for (i=0; i<MESSAGE_LENGTH; i++) {
    printf("- %#.2hhx ", *(kadr+i*sizeof(char)));
  }
  printf("\n");
  return 0;
}
 
