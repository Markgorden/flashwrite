#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/ioport.h>	// for request_region()
#include <asm/io.h>

#define EBD_FLASH_BASE	0xffc00000

#define AT26DF321_SIZE		0x00400000
#define AT26DF321_SECTOR_SIZE	0x10000
#define AT26DF321_SECTOR_COUNT	64	
#define	AT26DF321_ID		0x471f 

enum {
	FL_UNKNOWN,
	FL_READY,
	FL_READING,
	FL_WRITING,
	FL_ERASING,
};

#define DEBUG	1

#ifdef DEBUG
#define dprintk(x, args...) do { printk(x, ##args); } while (0)
#else
#define dprintk(x, args...)
#endif

static unsigned int flash_state = FL_UNKNOWN;
//static spinlock_t flash_mutex = SPIN_LOCK_UNLOCKED;
static unsigned int flash_id;
static unsigned int flash_size = 0x00400000;	// size = 4MB

#define REG_SPI_OUTPORT		0xFC00
#define	REG_SPI_INPORT		REG_SPI_OUTPORT+1
#define REG_SPI_CONTROL		REG_SPI_OUTPORT+2
#define	REG_SPI_STATUS		REG_SPI_OUTPORT+3
#define REG_SPI_CSSEL		REG_SPI_OUTPORT+4
#define REG_SPI_ERROR		REG_SPI_OUTPORT+5

#define WRITE_OK	0x10
#define	GET_OK		0x20
#define WAIT_LOOP	0xFFFFFFFF

#define FIFO_SIZE	16
#define MAX_CYCLE	256

#define SPICS_LOW()  	outb(0, REG_SPI_CSSEL);
#define SPICS_HIGH()	outb(1, REG_SPI_CSSEL);

#define CMD_READ_STATUS		0x05
#define CMD_WRITE_ENABLE	0x06
#define CMD_WRITE_DISABLE	0x04
#define CMD_SECTOR_ERASE	0x20
#define CMD_BLOCK_ERASE		0xd8
#define CMD_PROGRAM		0x02
#define CMD_READ		0x03
#define CMD_FAST_READ		0x0b

#define FIFO_ENABLE		0x10
#define AUTO_FETCH_DISABLE	0x20

inline void SPI_OUTPUT_COMPLETE(void)
{
	unsigned char val;
	unsigned long loop = WAIT_LOOP;
	while ( loop-- ) {
	  val = inb(REG_SPI_STATUS);
	  if( val & WRITE_OK )
		break;
	}
}						

inline void SPI_DATA_READY(void)
{
	unsigned char val;
   	unsigned long loop = WAIT_LOOP;

	outb(0, REG_SPI_INPORT);	// strange, but it is necessary.
   	while ( loop-- ) {
		val = inb(REG_SPI_STATUS);
		if( val & GET_OK )
			break;
        }
}

inline void SPI_WAIT_BUSY(void)
{
	unsigned char val;
	unsigned long loop = WAIT_LOOP;

	SPICS_HIGH();	// should be removed
	while ( loop-- ) {
		SPICS_LOW();
		outb(CMD_READ_STATUS, REG_SPI_OUTPORT);
		SPI_OUTPUT_COMPLETE();

		SPI_DATA_READY();
		SPICS_HIGH();

		val = inb(REG_SPI_INPORT);
		if ( !(val & 1) )
			break;
	}
}

// Note: Should use SPICS_HIGH() function to complete one access cycle.
inline void SPI_WRITE(unsigned char cmd, unsigned char ad1, unsigned char ad2, unsigned char ad3)
{
	SPI_WAIT_BUSY();

	SPICS_LOW();
	outb(CMD_WRITE_ENABLE, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	SPICS_HIGH();

	SPICS_LOW();
	outb(cmd, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	outb(ad1, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	outb(ad2, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	outb(ad3, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
}

// Note: Should use SPICS_HIGH() function to complete one access cycle.
inline void SPI_READ(unsigned char cmd, unsigned char ad1, unsigned char ad2, unsigned char ad3)
{
	SPI_WAIT_BUSY();

	SPICS_LOW();
	outb(cmd, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	outb(ad1, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	outb(ad2, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
	outb(ad3, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();
}

inline void SPI_ENTER(void)
{
	SPICS_HIGH();
	outb(FIFO_ENABLE | AUTO_FETCH_DISABLE | 5, REG_SPI_CONTROL);
}

inline void SPI_EXIT(void)
{
	SPICS_HIGH();
	outb(0x52, REG_SPI_CONTROL); // return to regular memory access
}

static unsigned int spiflash_identify(void)
{
	unsigned int val;

	val = 0;

	SPI_ENTER();

	SPICS_LOW();
	outb(0x9f, REG_SPI_OUTPORT);
	SPI_OUTPUT_COMPLETE();

	SPI_DATA_READY();
	val = inb(REG_SPI_INPORT);
	val <<= 8;
	SPI_DATA_READY();
	val |= inb(REG_SPI_INPORT);
	val <<= 8;
	SPI_DATA_READY();
	val |= inb(REG_SPI_INPORT);
	SPICS_HIGH();

	SPI_EXIT();

	return val;
}

static int flash_write_word(__u32 dst_addr, __u32 src_addr, int count)
{
	unsigned int i;
	unsigned int nr256, tmp, offset;
	unsigned char *psrc = (unsigned char *) src_addr;
	unsigned char ad1, ad2, ad3;

	if ( flash_state != FL_READY )
		return -EIO;

	flash_state = FL_WRITING;
	SPI_ENTER();

	nr256 = count / MAX_CYCLE ;
	
	for(i = 0; i < nr256; i++, dst_addr += MAX_CYCLE) {
		int cnt=0 ; 
	     
		offset = dst_addr;
		ad3 = (unsigned char) (offset & 0x000000FF);
		ad2 = (unsigned char) ((offset & 0x0000FF00) >> 8);
		ad1 = (unsigned char) ((offset & 0x00FF0000) >> 16);
	   
		SPI_WRITE(CMD_PROGRAM, ad1, ad2, ad3);
		while ( 1 ) {		  	         	   
			outb(*psrc++, REG_SPI_OUTPORT);
		
			if ( (cnt & (FIFO_SIZE-1)) == (FIFO_SIZE-1) )
		   		SPI_OUTPUT_COMPLETE();

			if( (cnt & (MAX_CYCLE-1)) == (MAX_CYCLE-1) ) {
				SPICS_HIGH();
				SPI_WAIT_BUSY();
				break ;
			}
			cnt ++;
		}
	}

	if ( count % 256 ) {   
		tmp = count - MAX_CYCLE * nr256 ;
		offset = dst_addr;
		ad3 = (unsigned char) (offset & 0x000000FF)  ;
		ad2 = (unsigned char) ((offset & 0x0000FF00) >> 8);
		ad1 = (unsigned char) ((offset & 0x00FF0000) >> 16);
           
		SPI_WRITE(CMD_PROGRAM, ad1, ad2, ad3);
	   
		for (i = 0 ;i < tmp ; i++) {
			outb(*psrc++, REG_SPI_OUTPORT);
			SPI_OUTPUT_COMPLETE();
		}

		SPICS_HIGH();
	 }	   

	SPI_EXIT();
	flash_state = FL_READY;

	return 0;
}

static int flash_small_block_erase(__u32 addr)
{
	unsigned char ad1,ad2,ad3;
  
	if ( flash_state != FL_READY )
		return -EIO;

	flash_state = FL_ERASING;
	SPI_ENTER();

	ad3 = (char) (addr & 0x000000FF);
	ad2 = (char) ((addr & 0x0000FF00) >> 8);
	ad1 = (char) ((addr & 0x00FF0000) >> 16);

	SPI_WRITE(CMD_SECTOR_ERASE, ad1, ad2, ad3); 
	SPICS_HIGH();

	SPI_WAIT_BUSY();

	SPI_EXIT();
	flash_state = FL_READY;

	return 0;
}

static int flash_erase_block(__u32 addr)
{
	unsigned char ad1,ad2,ad3;
  
	if ( flash_state != FL_READY )
		return -EIO;

	flash_state = FL_ERASING;
	SPI_ENTER();

	ad3 = (char) (addr & 0x000000FF);
	ad2 = (char) ((addr & 0x0000FF00) >> 8);
	ad1 = (char) ((addr & 0x00FF0000) >> 16);

	SPI_WRITE(CMD_BLOCK_ERASE, ad1, ad2, ad3); 
	SPICS_HIGH();

	SPI_WAIT_BUSY();

	SPI_EXIT();
	flash_state = FL_READY;

	return 0;
}

#define USE_SPI_READ	1

int flash_read(__u32 addr, char *buffer, int size)
{
        unsigned int i;
#if defined(USE_SPI_READ)
	unsigned char ad1, ad2, ad3;
#else
	char *srcaddr = (char *) (EBD_FLASH_BASE + addr);
#endif

	// modified by ethan on 06/23/2004 to serialize flash operations.
	while ( flash_state != FL_READY ) {
		dprintk("flash: waiting for reading\n");
		schedule_timeout(2);
	}
	dprintk("flash: reading address = 0x%08x, size = %d\n", addr, size);

	//spin_lock_bh(&flash_mutex);
	flash_state = FL_READING;

#if defined(USE_SPI_READ)
	SPI_ENTER();

	ad3 = (char) (addr & 0x000000FF);
	ad2 = (char) ((addr & 0x0000FF00) >> 8);
	ad1 = (char) ((addr & 0x00FF0000) >> 16);

#if 1
	// if FAST_READ is used, the first return byte is dummy byte.
	SPI_READ(CMD_FAST_READ, ad1, ad2, ad3); 
	SPI_DATA_READY();
	buffer[0] = inb(REG_SPI_INPORT);
#else
	SPI_READ(CMD_READ, ad1, ad2, ad3); 
#endif

	for (i = 0; i < size; i++) {
		SPI_DATA_READY();
		buffer[i] = inb(REG_SPI_INPORT);
	}
	SPICS_HIGH();

	SPI_WAIT_BUSY();

	SPI_EXIT();
#else
	for (i = 0; i < size; i++) {
		buffer[i] = srcaddr[i];
	}
#endif

	flash_state = FL_READY;
	//spin_unlock_bh(&flash_mutex);

	return size;
}

int flash_erase_write(__u32 addr, char *data, int len)
{
	int	status;
	__u32	offset, last_addr;

	printk("flash erase write: addr = %08x\n", addr);

	if ( len <= 0 || (addr + len) > flash_size )
		return -EINVAL;
	if ( addr >= 0x003e0000 )	// boot loader address space.
		return -EINVAL;

	offset = addr;
	last_addr = addr + len - 1;

	if ( addr >= 0x003d0000 && addr < 0x003e0000 ) {	// configuration space
		// sector size = 4KB
		if ( (addr & 0x0fff) != 0 ) {
			printk("Address(%08x) is not aligned flash boundary\n", addr);
			return -EINVAL;
		}

		if ( (addr + len) > 0x003e0000 ) {
			printk("Address + Length (%08x + %08x) is out of configuration space\n", addr, len);
			return -EINVAL;
		}
		
		while ( offset < last_addr ) {
			if ( (status=flash_small_block_erase(offset)) != 0 )
				return status;
			offset += 0x1000;
		}
	} else {
		// block size = 64KB
		if ( (addr & 0xffff) != 0 ) {
			printk("Address(%08x) is not aligned flash boundary\n", addr);
			return -EINVAL;
		}

		while ( offset < last_addr ) {
			if ( (status=flash_erase_block(offset)) != 0 )
				return status;
			offset += 0x10000;
		}
	}

	if ( (status=flash_write_word(addr, (__u32) data, len)) != 0 )
		return status;

	return 0;
}

// Kernel+FileSystem:	0x00000000 - 0x003cffff
// Config. Data:	0x003d0000 - 0x003dffff
// Boot Loader:		0x003e0000 - 0x003fffff
static int __init spiflash_init(void)
{
	request_region(0xfc00, 0x10, "spiflash");

	outl(0x80000040, 0xcf8);
	outl(0xfc01, 0xcfc);

	flash_id = spiflash_identify();
	printk("flash id: %04x\n", flash_id);

	flash_state = FL_READY;

#if 0
	{
		int i;
		char *buf;
		buf = (char *) kmalloc(0x1000, GFP_KERNEL);
#if 1
		flash_read(0x003d0000, buf, 0x1000);
		for (i = 0; i < 0x100; i++) {
			if ( (i % 16) == 0 )
				printk("\n");
			printk("%02x ", (unsigned char) buf[i]);
		}
		printk("\n");
#else
		for (i = 0; i < 0x1000; i++)
			buf[i] = i;	
		flash_erase_write(0x003d0000, buf, 0x1000);
#endif
	}
#endif
	return 0;
}

static void __exit spiflash_exit(void)
{
}

module_init(spiflash_init);
module_exit(spiflash_exit);
