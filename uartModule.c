/*
*
* This kernel module is tested with kernel 3.16.0-23-generic
* on a linux kubuntu vm
* This module creates a kernel memory space which is then remapped into
* userspace via a reimplemented character device mmap function.
*
* For demonstration purposes the first value inside the "shared memory"
* is incremented by an interrupt. IRQ is attached to an existing IRQ.
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/kernel.h> /* printk */
#include <linux/errno.h> /* error codes */
#include <linux/device.h>
#include <linux/sched.h> /* wait_* */
#include <asm/uaccess.h> /* copy_*_user */

#include <linux/interrupt.h> //irq_handler, irq_thread_fn
#include <linux/gpio.h> //OMAP_GPIO_IRQ
#include <linux/mm.h> //remap_pfn_range
#include <linux/slab.h> //get_zeroed_page, kmalloc, kzalloc
#include <linux/fs.h> //open, release, mmap
#include <linux/kfifo.h> //kfifo

#define MODVERSIONS
MODULE_LICENSE("GPL");

#define TINT_IRQ 19
#define TINT_IRQ_TYPE IRQF_SHARED

/* stuffed source messages 29 byte in total */
const char stuffedsourcebuffer[] = {
	0x00, 0xba, 0x1b, 0x00, 0xbc, 0xbd, 0xbe, 0xbf, 0x01,
	0x00, 0xca, 0x1b, 0x1b, 0xcc, 0xcd, 0xce, 0x01,
	0x00, 0xda, 0xdb, 0x1b, 0x00, 0x1b, 0x1b, 0xdc, 0xdd, 0x1b, 0x01, 0x01
};

/* message length */
#define MESSAGE_LENGTH 16

/* current position in message */
int position;
/*buffer for incoming incomplete message*/
char* threadbuffer;
/*fifo queue for complete messages*/
static struct kfifo fifoBuffer;
/*state of reading message*/
typedef enum {NO_MSG, STUFF_STRT, IN_MSG} state_type;
/*shared memory pointer*/
static char *page_ptr;
static long vmaLength;
static int ret;
static const int myChardev_major = 60; // z.B. 60
static const char * myChardev_name = "myChardev";

/*Linux use. Int is shared. This is the cookie*/
struct tint_struct
{
  struct timespec last_time;
  struct timespec total_time;
  unsigned int counter;
  unsigned char wakeup;
} tint_data;

/* returns a byte from the "UART" */
char getByte(void){
	char currentByte;

	/* get value from stuffed uart buffer */
	currentByte = stuffedsourcebuffer[position];

	/* calculate the next position */
	if (position < sizeof(stuffedsourcebuffer)-1)
		position++;
	else
		position = 0;

	/* return the selected byte from the stuffed uart buffer */
	return currentByte;
}

/*
* Threaded irq function.
* On every thrown interrupt the next value from the "UART"
* is destuffed and copied to the threadbuffer.
*/
static irqreturn_t irq_thread_fn(int irq, void *dev_id)
{
  printk("shm_kernelspace_userspace module irq_thread_fn called\n");
  static state_type state = NO_MSG;
  static int queue_count = 0;	//position in shm queue
  char in_ch = getByte();
  
  if (state == NO_MSG) {
    if (in_ch == 0x00) {
      state = IN_MSG;
    }
  }
  else if (state == IN_MSG) {
    if (in_ch == 0x1b) {
      state = STUFF_STRT;
    }
    else if (in_ch == 0x01) {
      state = NO_MSG;
      while (queue_count < MESSAGE_LENGTH) {
	threadbuffer[queue_count] = 0x00;
	//kfifo_put(&fifoBuffer, 0x00);
	//*(page_ptr+queue_count*sizeof(char)) = 0x00;
	//printk("uart received:%x\n",(unsigned char)threadbuffer[queue_count]);
	queue_count++;
      }
      /*message complete. Write into Fifo*/
      kfifo_in(&fifoBuffer, threadbuffer, MESSAGE_LENGTH);
      queue_count = 0;
    }    
    else if (in_ch != 0x1b && in_ch != 0x00 && in_ch != 0x01){
      threadbuffer[queue_count] = in_ch;
      //*(page_ptr+queue_count*sizeof(char)) = in_ch;
      //kfifo_put(&fifoBuffer, in_ch);
      //printk("uart received:%x\n",(unsigned char)threadbuffer[queue_count]);
      queue_count++;
    }
  }
  else if (state == STUFF_STRT) {
    threadbuffer[queue_count] = in_ch;
    //*(page_ptr+queue_count*sizeof(char)) = in_ch;
    //kfifo_put(&fifoBuffer, in_ch);
    //printk("uart received:%x\n",(unsigned char)threadbuffer[queue_count]);
    queue_count++;
    state = IN_MSG;
  }

  return IRQ_HANDLED;
}
/*
* IRQ handler which wakes up the handler thread and runs irq_thread_fn
*/
static irqreturn_t irq_handler(int irq, void *dev_id)
{
  printk("shm_kernelspace_userspace module irq_handler called\n");
  /*
  * This return value wakes up the handler thread and
  * runs irq_thread_fn
  */
  return IRQ_WAKE_THREAD;
}
/*
* Reimplementation of the character device open function
* It is called when the device is opened
* This function is only for demonstration purposes
*/
static int chardev_open(struct inode *inode, struct file *file)
{
  return 0;
}

/*
* Reimplementation of the character device release function
* It is called when the device is released
* This function is only for demonstration purposes
*/
static int chardev_release(struct inode *inode, struct file *file)
{
  return 0;
}

/* 
 * Called when a process, which already opened the dev file, attempts to
 * read from it.
 */
static ssize_t chardev_read(struct file *filp,	/* see include/linux/fs.h   */
			   char *buffer,	/* buffer to fill with data */
			   size_t length,	/* length of the buffer     */
			   loff_t * offset)
{
  int ret = 0, j;
  unsigned int copied = 0;
  unsigned char i;
  printk(KERN_INFO "fifo len: %u\n", kfifo_len(&fifoBuffer));
  if (kfifo_len(&fifoBuffer) >= MESSAGE_LENGTH) {
    /* get max of length bytes from the fifo */
    ret = kfifo_out(&fifoBuffer, buffer, length);
    //printk(KERN_INFO "buf: %.*s\n", length, buffer);
    //ret = kfifo_to_user(&fifoBuffer, buffer, length, &copied);
  }
  /* 
    * return the number of bytes put into the buffer
    */
  return ret ? ret : copied;

}

/*
* Reimplementation of the character device mmap function
* It is called when mmap is called from user space with the character
* devices file descriptor.
*
* It maps the kernel space virtual memory into userspace address space
* and uses the user VMA = virtual memory area for that, which
* is generated by the kernel during the user mmap(). Fills in
* page frame number and size.
* Since we use virtual kernel memory, we don’t need to expect swapping
*/
static int chardev_mmap(struct file *file, struct vm_area_struct *vma)
{
  printk("shm_kernelspace_userspace chardev_mmap\n");
  vmaLength = vma->vm_end - vma->vm_start; //The length of the vma
  /*
  * remap kernel memory to userspace
  *
  * int remap_pfn_range ( struct vm_area_struct * vma,
  * 			unsigned long addr,
  *			unsigned long pfn,
  *			unsigned long size,
  *			pgprot_t prot);
  * vma: user vma to map to
  * addr: target user address to start at
  * pfn: physical address of kernel memory,
  *	page frame number of first page
  * size: size of map area
  * prot: page protection flags for this mapping
  */
  ret = remap_pfn_range( vma,
			  vma->vm_start,
			  virt_to_phys((void *)page_ptr) >> PAGE_SHIFT,
			  vmaLength,
			  vma->vm_page_prot
			  );
  if (ret < 0)
  {
    return ret;
  }
  return 0;
}

/*
* Definition of the file operations on the character device
*/
static struct file_operations chardevMmap_ops =
{
  .owner = THIS_MODULE,
  .open = chardev_open,
  .release = chardev_release,
  .mmap = chardev_mmap,
  .read = chardev_read,
};

static int __init initmodule(void)
{
  printk("shm_kernelspace_userspace module loading\n");
  /* This function registers a threaded Interrupt
  * If the interrupt occurs, a thread is created which
  * executes the function irq_thread
  */
  ret = request_threaded_irq (TINT_IRQ, irq_handler, irq_thread_fn,
			      TINT_IRQ_TYPE ,"led-driver",&tint_data);
  if(ret < 0)
  {
    printk("shm_kernelspace_userspace irq request failed\n");
    return -1;
  }
  printk("Allocate one page\n");
  /*
  * Allocate one zero filled page.
  * Because the page is mapped into user space
  * the page should be zero initialized
  * If it is not zero initialized
  * sensitive kernel data could be contained in the page
  */
  page_ptr = get_zeroed_page(GFP_KERNEL);
  /*
  * Allocating page failed
  */
  if(!page_ptr)
  {
    printk("Allocating page failed\n");
    return -1;
  }
  /*
  * Allocating threadbuffer
  */
  threadbuffer = kzalloc(MESSAGE_LENGTH*sizeof(char),GFP_KERNEL);
  if (!threadbuffer) 
  {
    printk("Allocating threadbuffer failed\n");
    return -1;
  }   
  /*
  * Allocating fifo queue
  */
  ret = kfifo_alloc(&fifoBuffer, 4096, GFP_KERNEL);
  if (ret) {
    printk(KERN_ERR "error kfifo_alloc\n");
    return -1;
  }
  /*
  * Register character device
  */
  if (register_chrdev(myChardev_major, myChardev_name,
		      &chardevMmap_ops) == 0)
  {
    printk("registered chardev %s\n", myChardev_name);
  }
  else
  {
    printk("register chardev %s failed!\n",myChardev_name);
  }
  /*
  * Inititlize the first value
  */
  page_ptr[0] = 0x42;
  threadbuffer[0] = 0x43;
  
  return 0;
}

static void __exit cleanupmodule(void)
{
  int i;
  printk("shm_kernelspace_userspace module unloading\n");
  /*
  * Free the interrupt
  */
  free_irq(TINT_IRQ, &tint_data);
  /*
  * Unregister the character device
  */
  unregister_chrdev(myChardev_major, myChardev_name);
  /*
  * Free allocated page
  */
  free_page(page_ptr);
  /*
  * Free allocated threadbuffer
  */
  kfree(threadbuffer);
  /*
  * Free allocated fifo queue
  */
  kfifo_free(&fifoBuffer);
}
module_init( initmodule);
module_exit( cleanupmodule);

 
