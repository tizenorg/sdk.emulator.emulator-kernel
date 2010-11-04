/*
 * DPRAM Device Driver Implementation.
 * Revision History -
 */

#define _DEBUG 0

#define USE_WORKQUEUE
#define ENABLE_ERROR_DEVICE

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#ifdef ENABLE_ERROR_DEVICE
#include <linux/poll.h>
#include <linux/cdev.h>
#endif

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include <mach/gpio.h>
#include <mach/pxa-regs.h>
#include <mach/pxa3xx-regs.h>
#include <mach/mfp-pxa300.h>
#else
#include <asm/arch/arch.h>
#include <asm/arch/gpio.h>
#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa3xx-regs.h>
#include <asm/arch/mfp-pxa300.h>
#endif

#ifdef CONFIG_EVENT_LOGGING
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/klog.h>
#include <asm/unistd.h>
#endif

#include "dpram.h"
#include "dprintk.h"

#define DRIVER_ID		"1.2"
#define DRIVER_NAME 		"DPRAM"
#define DRIVER_PROC_ENTRY	"driver/dpram"
#define DRIVER_TEST_ENTRY	"driver/dpram-dump"
#define DRIVER_MAJOR_NUM_ALLOC	0

#ifdef USE_WORKQUEUE
#define DPRAM_WORKQUEUE 	"DPRAM_QUEUE"
#endif

#ifdef CONFIG_EVENT_LOGGING
#define DPRAM_ID			3
#define DPRAM_READ			3
#define DPRAM_WRITE			4
#endif

#ifdef _DEBUG
#define DPRINTK(fmt, args...) printk("[DPRAM] " fmt, ## args)
#else
#define DPRINTK(fmt, args...)	do{} while(0)
#endif

#define DFC_LOCK  do{}while(0)
#define DFC_UNLOCK   do{}while(0)

#define write_to_dpram_no_lock(dest, src, size) \
	_memcpy((void *)(DPRAM_VBASE + dest), src, size)

#define read_from_dpram_no_lock(dest, src, size) \
	_memcpy(dest, (void *)(DPRAM_VBASE + src), size)

#define write_to_dpram(dest, src, size) \
	DFC_LOCK;\
	_memcpy((void *)(DPRAM_VBASE + dest), src, size);\
	DFC_UNLOCK

#define read_from_dpram(dest, src, size) \
	DFC_LOCK;\
	_memcpy(dest, (void *)(DPRAM_VBASE + src), size);\
	DFC_UNLOCK

#ifdef ENABLE_ERROR_DEVICE
#define DPRAM_ERR_MSG_LEN	65
#define DPRAM_ERR_DEVICE	"dpramerr"
#endif

#define PHONE_ON_GPIO81			81
#define PHONE_ACTIVE_GPIO107		107
#define PHONE_nRESET_GPIO102		102
#define PHONE_nINT_GPIO70		70
#define PHONE_TEST_nINT_GPIO1		1

static struct tty_driver *dpram_tty_driver;

static dpram_tasklet_data_t dpram_tasklet_data[MAX_INDEX];

static dpram_device_t dpram_table[MAX_INDEX] = {
	{
		.in_head_addr = DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_PHONE2PDA_FORMATTED_SIZE,

		.out_head_addr = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_FORMATTED_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_PDA2PHONE_FORMATTED_SIZE,

		.mask_req_ack = INT_MASK_REQ_ACK_F,
		.mask_res_ack = INT_MASK_RES_ACK_F,
		.mask_send = INT_MASK_SEND_F,
	},
	{
		.in_head_addr = DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS,
		.in_tail_addr = DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS,
		.in_buff_addr = DPRAM_PHONE2PDA_RAW_BUFFER_ADDRESS,
		.in_buff_size = DPRAM_PHONE2PDA_RAW_SIZE,

		.out_head_addr = DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS,
		.out_tail_addr = DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS,
		.out_buff_addr = DPRAM_PDA2PHONE_RAW_BUFFER_ADDRESS,
		.out_buff_size = DPRAM_PDA2PHONE_RAW_SIZE,

		.mask_req_ack = INT_MASK_REQ_ACK_R,
		.mask_res_ack = INT_MASK_RES_ACK_R,
		.mask_send = INT_MASK_SEND_R,
	},
};

static struct tty_struct *dpram_tty[MAX_INDEX];
static struct ktermios *dpram_termios[MAX_INDEX];
static struct ktermios *dpram_termios_locked[MAX_INDEX];

static void res_ack_tasklet_handler(unsigned long data);

static int debug_hexdump = 0;
static int debug_default = 0;

module_param(debug_hexdump, int, S_IRUGO);
module_param(debug_default, int, S_IRUGO);

#ifdef USE_WORKQUEUE
static void send_workqueue_handler(struct work_struct *dummy);
static struct workqueue_struct *dpram_workqueue;
typedef struct workqueue_data {
	struct work_struct work;
	unsigned long data;
} workqueue_data_t;

static workqueue_data_t workqueue_data[MAX_INDEX];
#else
static void send_tasklet_handler(unsigned long data);
static DECLARE_TASKLET(fmt_send_tasklet, send_tasklet_handler, 0);
static DECLARE_TASKLET(raw_send_tasklet, send_tasklet_handler, 0);
#endif

static DECLARE_TASKLET(fmt_res_ack_tasklet, res_ack_tasklet_handler,
		(unsigned long)&dpram_table[FORMATTED_INDEX]);
static DECLARE_TASKLET(raw_res_ack_tasklet, res_ack_tasklet_handler,
		(unsigned long)&dpram_table[RAW_INDEX]);

#ifdef ENABLE_ERROR_DEVICE
static unsigned int dpram_err_len;
static char dpram_err_buf[DPRAM_ERR_MSG_LEN];

struct class *dpram_class;

static DECLARE_WAIT_QUEUE_HEAD(dpram_err_wait_q);
static struct fasync_struct *dpram_err_async_q;
#endif

#define isprint(c)	((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
void hexdump(const char *buf, int len)
{
	char str[80], octet[10];
	int ofs, i, l;

	for (ofs = 0; ofs < len; ofs += 16) {
		sprintf( str, "%03d: ", ofs );

		for (i = 0; i < 16; i++) {
			if ((i + ofs) < len)
				sprintf( octet, "%02x ", buf[ofs + i] );
			else
				strcpy( octet, "   " );

			strcat( str, octet );
																					}
			strcat( str, "  " );
			l = strlen( str );

		for (i = 0; (i < 16) && ((i + ofs) < len); i++)
			str[l++] = isprint( buf[ofs + i] ) ? buf[ofs + i] : '.';

		str[l] = '\0';
		printk( "%s\n", str );
	}
}
#ifdef CONFIG_EVENT_LOGGING
static inline EVENT_HEADER *getPayloadHeader(int flag, int size)
{
	EVENT_HEADER *header;
	struct timeval time_val;

	header = (EVENT_HEADER *)kmalloc(sizeof (EVENT_HEADER), GFP_ATOMIC);
	do_gettimeofday(&time_val);

	header->timeVal = time_val;
	header->class = (flag == DPRAM_READ ? DPRAM_READ : DPRAM_WRITE);
	header->repeat_count = 0;
	header->payload_length = size;

	return header;
}

static inline void dpram_event_logging(int direction, void *src, int size)
{
	EVENT_HEADER *header;
	unsigned long flags;

	header = getPayloadHeader(direction, size);

	local_irq_save(flags);
	klog(header, sizeof (EVENT_HEADER), DPRAM_ID);

	if (direction == DPRAM_WRITE) {
		klog(src, size, DPRAM_ID);
	}

	else if (direction == DPRAM_READ) {
		klog((void *)(DPRAM_VBASE + src), size, DPRAM_ID);
	}

	local_irq_restore(flags);
	kfree(header);
}
#endif

static inline void byte_align(unsigned long dest, unsigned long src)
{
	u_int16_t *p_src;
	volatile u_int16_t *p_dest;

	if (!(dest & (2 - 1)) && !(src & (2 - 1))) {
		p_dest = (u_int16_t *)dest;
		p_src = (u_int16_t *)src;

		*p_dest = (*p_dest & 0xFF00) | (*p_src & 0x00FF);
	} else if ((dest & (2 - 1)) && (src & (2 - 1))) {
		p_dest = (u_int16_t *)(dest - 1);
		p_src = (u_int16_t *)(src - 1);

		*p_dest = (*p_dest & 0x00FF) | (*p_src & 0xFF00);
	} else if (!(dest & (2 - 1)) && (src & (2 - 1))) {
		p_dest = (u_int16_t *)dest;
		p_src = (u_int16_t *)(src - 1);

		*p_dest = (*p_dest & 0xFF00) | ((*p_src >> 8) & 0x00FF);
	} else if ((dest & (2 - 1)) && !(src & (2 - 1))) {
		p_dest = (u_int16_t *)(dest - 1);
		p_src = (u_int16_t *)src;

		*p_dest = (*p_dest & 0x00FF) | ((*p_src << 8) & 0xFF00);
	} else {
		if(debug_default)
			DPRINTK("oops.~\n");
	}
}

static inline void _memcpy(void *p_dest, const void *p_src, int size)
{
	unsigned long dest = (unsigned long)p_dest;
	unsigned long src = (unsigned long)p_src;

	if (size <= 0)
		return;

	if (dest & 1) {
		byte_align(dest, src);
		dest++, src++;
		size--;
	}

	if (size & 1) {
		byte_align(dest + size - 1, src + size - 1);
		size--;
	}

	if (src & 1) {
		unsigned char *s = (unsigned char *)src;
		volatile u_int16_t *d = (unsigned short *)dest;

		size >>= 1;

		while (size--) {
			*d++ = s[0] | (s[1] << 8);
			s += 2;
		}
	} else {
		u_int16_t *s = (u_int16_t *)src;
		volatile u_int16_t *d = (unsigned short *)dest;

		size >>= 1;

		while (size--) { *d++ = *s++; }
	}
}

static inline void send_interrupt_to_phone(u_int16_t irq_mask, int blend)
{
	if (blend) {
		u_int16_t tmp;
		
		read_from_dpram((void *)&tmp, DPRAM_PDA2PHONE_INTERRUPT_ADDRESS,
				DPRAM_INTERRUPT_PORT_SIZE);
		
		irq_mask |= tmp;
	}

	write_to_dpram(DPRAM_PDA2PHONE_INTERRUPT_ADDRESS, (void *)&irq_mask,
			DPRAM_INTERRUPT_PORT_SIZE);

	if(debug_default)
		DPRINTK("PDA -> Phone interrupt!\n");
}

static inline int dpram_write(dpram_device_t *device, const char *buf, int len)
{
	int retval, size, len_left;

	u_int16_t head, tail;
	u_int16_t irq_mask = 0;
	
	if(debug_hexdump){
		DPRINTK("\n\n################### dpram write(%d) #####################\n", len);
		hexdump(buf, len);
		DPRINTK("################# dpram write(%d) End ###################\n\n", len);
	}

	read_from_dpram((void *)&head, device->out_head_addr, sizeof (head));
	read_from_dpram((void *)&tail, device->out_tail_addr, sizeof (tail));

	if (head < tail) {
		retval = tail - head - 1;
		size = (retval >= len) ? len : retval;

		write_to_dpram(device->out_buff_addr + head, (void *)buf, size);
		retval = size;
	} else if (tail == 0) {
		retval = device->out_buff_size - head - 1;
		size = (retval >= len) ? len : retval;

		write_to_dpram(device->out_buff_addr + head, (void *)buf, size);
		retval = size;
	} else {
		retval = device->out_buff_size - head;
		size = (retval >= len) ? len : retval;

		write_to_dpram(device->out_buff_addr + head, (void *)buf, size);
		retval = size;

		len_left = len - retval;

		if (len_left > 0) {
			size = ((tail - 1) >= len_left) ? len_left : tail - 1;

			write_to_dpram(device->out_buff_addr, (void *)(buf + retval), size);
			retval += size;
		}
	}

	head = (u_int16_t)((head + retval) % device->out_buff_size);
	write_to_dpram(device->out_head_addr, (void *)&head, sizeof (head));

#ifdef CONFIG_EVENT_LOGGING
	dpram_event_logging(DPRAM_WRITE, (void *)&head, size);
#endif

	if(debug_hexdump)
		DPRINTK("head: %u, tail: %u\n", head, tail);

	irq_mask = INT_MASK_VALID | device->mask_send;

	if (head < tail)
		size = tail - head - 1;
	else
		size = (device->out_buff_size - head) + tail - 1;

	if (size < 1)
		irq_mask |= device->mask_req_ack;

	send_interrupt_to_phone(irq_mask, 0);
	return retval;
}

static inline int dpram_read(dpram_device_t *device, char *buf, int len)
{
	int retval, size, len_left;
	u_int16_t head, tail;
	read_from_dpram((void *)&head, device->in_head_addr, sizeof (head));
	read_from_dpram((void *)&tail, device->in_tail_addr, sizeof (tail));

	if(debug_hexdump)
		printk("dpram read : len(%d), head(%d), tail(%d)\n", len, head, tail);
	if (head == tail)
		return 0;

	if (head > tail) {
		retval = head - tail;
		size = (retval >= len) ? len : retval;

if(debug_hexdump)
		printk("read form dpram : size(%d)\n", size);
		read_from_dpram((void *)buf, device->in_buff_addr + tail, size);
		retval = size;
	} else {
		retval = device->in_buff_size - tail;
		size = (retval >= len) ? len : retval;

		if(debug_hexdump)
			printk("1. read form dpram : size(%d)\n", size);
		read_from_dpram((void *)buf, device->in_buff_addr + tail, size);
		retval = size;

		len_left = len - retval;

		if (len_left > 0) {
			size = (head >= len_left) ? len_left : head;

			if(debug_hexdump)
				printk("2. read form dpram : size(%d)\n", size);
			read_from_dpram((void *)buf + retval, device->in_buff_addr, size);
			retval += size;
		}
	}

	tail = (u_int16_t)((tail + retval) % device->in_buff_size);
	write_to_dpram(device->in_tail_addr, (void *)&tail, sizeof (tail));

#ifdef CONFIG_EVENT_LOGGING
	dpram_event_logging(DPRAM_READ, (void *)&tail, size);
#endif

	if(debug_hexdump){
		DPRINTK("head: %u, tail: %u\n", head, tail);
		printk("\n\n################### dpram read(%d) #####################\n", len);
		hexdump(buf, retval);
		printk("################# dpram read(%d) End ###################\n\n", len);
	}
	return retval;
}

static inline void dpram_clear(void)
{
	long i;
	unsigned long flags;
	
	u_int16_t value = 0;

	local_irq_save(flags);

	for (i = DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS;
			i < DPRAM_SIZE - (DPRAM_INTERRUPT_PORT_SIZE * 2);
			i += 2)
		*((u_int16_t *)(DPRAM_VBASE + i)) = value;

	local_irq_restore(flags);

	read_from_dpram_no_lock((void *)&value,
			DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, sizeof (value));
}

static inline void dpram_init_and_report(void)
{
	const u_int16_t magic_code = 0x00AA;
	const u_int16_t init_start = INT_COMMAND(INT_MASK_CMD_INIT_START);
	const u_int16_t init_end = INT_COMMAND(INT_MASK_CMD_INIT_END);

	u_int16_t access_enable = 0x0000;

	write_to_dpram_no_lock(DPRAM_PDA2PHONE_INTERRUPT_ADDRESS,
			(void *)&init_start, DPRAM_INTERRUPT_PORT_SIZE);

	write_to_dpram_no_lock(DPRAM_ACCESS_ENABLE_ADDRESS,
			(void *)&access_enable, sizeof (access_enable));

	dpram_clear();

	write_to_dpram_no_lock(DPRAM_MAGIC_CODE_ADDRESS,
			(void *)&magic_code, sizeof (magic_code));
	{
		unsigned short code = 0;
		read_from_dpram_no_lock((void *)&code, DPRAM_MAGIC_CODE_ADDRESS, sizeof(code));
		if(debug_default)
			printk("magic code = 0x%X\n", code);
	}

	access_enable = 0x0001;
	write_to_dpram_no_lock(DPRAM_ACCESS_ENABLE_ADDRESS,
			(void *)&access_enable, sizeof (access_enable));

	write_to_dpram_no_lock(DPRAM_PDA2PHONE_INTERRUPT_ADDRESS,
			(void *)&init_end, DPRAM_INTERRUPT_PORT_SIZE);

	if(debug_default)
		DPRINTK("DPRAM is initialized and report phone.\n");
}

static inline int dpram_get_read_available(dpram_device_t *device)
{
	int readable;
	u_int16_t head, tail;

	read_from_dpram((void *)&head, device->in_head_addr, sizeof (head));
	read_from_dpram((void *)&tail, device->in_tail_addr, sizeof (tail));

	readable = (head >= tail) ? (head - tail) : (device->in_buff_size - tail + head);

	if(debug_default)
		DPRINTK("readable room: %u\n", readable);
	return readable;
}

static inline void dpram_drop_data(dpram_device_t *device)
{
	u_int16_t head, tail;

	read_from_dpram((void *)&head, device->in_head_addr, sizeof (head));
	read_from_dpram((void *)&tail, device->in_tail_addr, sizeof (tail));

	if(debug_hexdump)
		DPRINTK("head: %u, tail: %u\n", head, tail);

	tail = head;

	write_to_dpram(device->in_tail_addr, (void *)&tail, sizeof (tail));
}

static void dpram_phone_on(void)
{
	if (gpio_get_value(PHONE_ACTIVE_GPIO107) == 0) {
		gpio_set_value(PHONE_nRESET_GPIO102, 1);

		gpio_set_value(PHONE_ON_GPIO81, 0);
		mdelay(500);
		gpio_set_value(PHONE_ON_GPIO81, 1);
		mdelay(500);
		gpio_set_value(PHONE_ON_GPIO81, 0);

		if(debug_default) {
			printk("[dpram_phone_on]MODEM is pwr status: %d\n "
					,gpio_get_value(PHONE_ACTIVE_GPIO107));
		}
	} else {
		if(debug_default){
			printk("MODEM is powered on already.!\n");
			printk("try to turn off phone!\n");
		}
											
		gpio_set_value(PHONE_nRESET_GPIO102, 0);
		
		gpio_set_value(PHONE_ON_GPIO81, 1);
		mdelay(500);
		gpio_set_value(PHONE_ON_GPIO81, 0);
		mdelay(500);
		gpio_set_value(PHONE_ON_GPIO81, 1);

		if(debug_default) {
			printk("[dpram_phone_on]MODEM is pwr status: %d\n "
					,gpio_get_value(PHONE_ACTIVE_GPIO107));
		}

		printk("try to turn on phone!\n");
		gpio_set_value(PHONE_nRESET_GPIO102, 1);
		
		gpio_set_value(PHONE_ON_GPIO81, 0);
		mdelay(500);
		gpio_set_value(PHONE_ON_GPIO81, 1);
		mdelay(500);
		gpio_set_value(PHONE_ON_GPIO81, 0);

		printk("[dpram_phone_on]MODEM is pwr status: %d\n "
				,gpio_get_value(PHONE_ACTIVE_GPIO107));
	}
}

static void dpram_phone_off(void)
{
	const u_int16_t access_enable = 0x0000;

	write_to_dpram(DPRAM_ACCESS_ENABLE_ADDRESS,
			(void *)&access_enable, sizeof (access_enable));

	gpio_set_value(PHONE_nRESET_GPIO102, 0);

	gpio_set_value(PHONE_ON_GPIO81, 1);
	mdelay(500);
	gpio_set_value(PHONE_ON_GPIO81, 0);
	mdelay(500);
	gpio_set_value(PHONE_ON_GPIO81, 1);

	if(debug_default) {
		printk("[dpram_phone_off]MODEM is pwr status: %d\n "
				,gpio_get_value(PHONE_ACTIVE_GPIO107));
	}
}

static inline unsigned int dpram_phone_getstatus(void)
{
	unsigned int ret = 0;

	ret = !!gpio_get_value(PHONE_ACTIVE_GPIO107);

	return ret;
}

#ifdef CONFIG_PROC_FS
static int dpram_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	char *p = page;
	int len;
#ifdef ENABLE_ERROR_DEVICE
	char buf[DPRAM_ERR_MSG_LEN];
	unsigned long flags;
#endif

	u_int16_t magic, enable;
	u_int16_t fmt_in_head, fmt_in_tail, fmt_out_head, fmt_out_tail;
	u_int16_t raw_in_head, raw_in_tail, raw_out_head, raw_out_tail;
	u_int16_t in_interrupt = 0, out_interrupt = 0;

	read_from_dpram((void *)&magic, DPRAM_MAGIC_CODE_ADDRESS, sizeof(magic));
	read_from_dpram((void *)&enable, DPRAM_ACCESS_ENABLE_ADDRESS, sizeof(enable));

	read_from_dpram((void *)&fmt_in_head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, 
			sizeof(fmt_in_head));
	read_from_dpram((void *)&fmt_in_tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, 
			sizeof(fmt_in_tail));
	read_from_dpram((void *)&fmt_out_head, DPRAM_PDA2PHONE_FORMATTED_HEAD_ADDRESS, 
			sizeof(fmt_out_head));
	read_from_dpram((void *)&fmt_out_tail, DPRAM_PDA2PHONE_FORMATTED_TAIL_ADDRESS, 
			sizeof(fmt_out_tail));

	read_from_dpram((void *)&raw_in_head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS, 
			sizeof(raw_in_head));
	read_from_dpram((void *)&raw_in_tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS, 
			sizeof(raw_in_tail));
	read_from_dpram((void *)&raw_out_head, DPRAM_PDA2PHONE_RAW_HEAD_ADDRESS, 
			sizeof(raw_out_head));
	read_from_dpram((void *)&raw_out_tail, DPRAM_PDA2PHONE_RAW_TAIL_ADDRESS, 
			sizeof(raw_out_tail));

	read_from_dpram((void *)&in_interrupt, DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, 
		    DPRAM_INTERRUPT_PORT_SIZE);
	read_from_dpram((void *)&out_interrupt, DPRAM_PDA2PHONE_INTERRUPT_ADDRESS, 
		    DPRAM_INTERRUPT_PORT_SIZE);

#ifdef ENABLE_ERROR_DEVICE
	memset((void *)buf, '\0', DPRAM_ERR_MSG_LEN);
	local_irq_save(flags);
	memcpy(buf, dpram_err_buf, DPRAM_ERR_MSG_LEN - 1);
	local_irq_restore(flags);
#endif

	p += sprintf(p,
			"-------------------------------------\n"
			"| NAME\t\t\t| VALUE\n"
			"-------------------------------------\n"
			"| MAGIC CODE\t\t| 0x%04x\n"
			"| ENABLE CODE\t\t| 0x%04x\n"
			"| PHONE->PDA FMT HEAD\t| %u\n"
			"| PHONE->PDA FMT TAIL\t| %u\n"
			"| PDA->PHONE FMT HEAD\t| %u\n"
			"| PDA->PHONE FMT TAIL\t| %u\n"
			"| PHONE->PDA RAW HEAD\t| %u\n"
			"| PHONE->PDA RAW TAIL\t| %u\n"
			"| PDA->PHONE RAW HEAD\t| %u\n"
			"| PDA->PHONE RAW TAIL\t| %u\n"
			"| PHONE->PDA INT.\t| 0x%04x\n"
			"| PDA->PHONE INT.\t| 0x%04x\n"
#ifdef ENABLE_ERROR_DEVICE
			"| LAST PHONE ERR MSG\t| %s\n"
#endif
			"| PHONE ACTIVE\t\t| %s\n"
			"-------------------------------------\n",
			magic, enable,
			fmt_in_head, fmt_in_tail, fmt_out_head, fmt_out_tail,
			raw_in_head, raw_in_tail, raw_out_head, raw_out_tail,
			in_interrupt, out_interrupt,

#ifdef ENABLE_ERROR_DEVICE
			(buf[0] != '\0' ? buf : "NONE"),
#endif

			(dpram_phone_getstatus() ? "ACTIVE" : "INACTIVE")
		);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}
static int dpram_dump_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data)
{
	char dump[16];
	unsigned int i=0;
	if(debug_hexdump)
		printk("dpram read dump========================================");

	for(i =0 ; i < 1024 ; i++){
		memset(dump, 0x0, sizeof(dump));
		read_from_dpram((void *)dump, 16*i, sizeof(dump));
		if(debug_hexdump)
			hexdump(dump, 16);
	}
	if(debug_hexdump)
		printk("=======================================================\n");

	*eof = 0;
	*start = 0;

	return 0;
}
#endif

static int dpram_tty_open(struct tty_struct *tty, struct file *file)
{
	dpram_device_t *device = &dpram_table[tty->index];

#if 0
   printk ("tty addr : %lx\n", tty);
   printk ("dpram device addr : %lx\n", device);
   printk ("dpram input head addr : %lx\n", device->in_head_addr);
   printk ("dpram input tail addr : %lx\n", device->in_tail_addr);
   printk ("dpram input buff addr : %lx\n", device->in_buff_addr);
   printk ("dpram input buff size : %ld\n", device->in_buff_size);
   printk ("dpram output head addr : %lx\n", device->out_head_addr);
   printk ("dpram output tail addr : %lx\n", device->out_tail_addr);
   printk ("dpram output buff addr : %lx\n", device->out_buff_addr);
   printk ("dpram output buff size : %ld\n", device->out_buff_size);
#endif

	device->serial.tty = tty;
	device->serial.open_count++;

	if (device->serial.open_count > 1) {
		device->serial.open_count--;
		return -EBUSY;
	}

	tty->driver_data = (void *)device;
	tty->low_latency = 1;

	if(debug_default)
		DPRINTK("Device is opened successfully.\n");

	tty_buffer_request_room(tty, 8192);

	return 0;
}

static void dpram_tty_close(struct tty_struct *tty, struct file *file)
{
	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (device && (device == &dpram_table[tty->index])) {
		down(&device->serial.sem);

		device->serial.open_count--;
		device->serial.tty = NULL;

		up(&device->serial.sem);

		if(debug_default)
			DPRINTK("Device is closed successfully. from pid(%d)\n", current->pid);
	} else {
		if(debug_default)
			DPRINTK("Device is empty or different.!!\n");
	}
}

static int dpram_tty_write(struct tty_struct *tty,
		const unsigned char *buffer, int count)
{
	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (!device)
		return 0;

	return dpram_write(device, buffer, count);
}

static int dpram_tty_write_room(struct tty_struct *tty)
{
	int avail;
	u_int16_t head, tail;

	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (!device)
		return 0;

	read_from_dpram((void *)&head, device->out_head_addr, sizeof (head));
	read_from_dpram((void *)&tail, device->out_tail_addr, sizeof (tail));

	avail = (head < tail) ? (tail - head - 1) : \
		(device->out_buff_size - head + tail - 1);

	return avail;
}

static int dpram_tty_ioctl(struct tty_struct *tty, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	unsigned int val;

	if (debug_default)
		DPRINTK("ioctl command: 0x%04x\n", cmd);

	switch (cmd) {
	case HN_DPRAM_PHONE_ON:
		printk("ioctl phome on command: 0x%04x\n", cmd);
		dpram_phone_on();
		return 0;

	case HN_DPRAM_PHONE_OFF:
		printk("ioctl phone off command: 0x%04x\n", cmd);
		dpram_phone_off();
		return 0;

	case HN_DPRAM_PHONE_GETSTATUS:
		printk("ioctl phone get status command: 0x%04x\n", cmd);
		val = dpram_phone_getstatus();
		return copy_to_user((unsigned int *)arg, &val, sizeof (val));

	default:
		break;
	}

	return -ENOIOCTLCMD;
}

static int dpram_tty_chars_in_buffer(struct tty_struct *tty)
{
	int avail, written;
	u_int16_t head, tail;

	dpram_device_t *device = (dpram_device_t *)tty->driver_data;

	if (!device)
		return 0;

	read_from_dpram((void *)&head, device->out_head_addr, sizeof (head));
	read_from_dpram((void *)&tail, device->out_tail_addr, sizeof (tail));

	avail = (head < tail) ? (tail - head - 1) : \
		(device->out_buff_size - head + tail - 1);

	written = device->out_buff_size - avail - 1;

	return written;
}

#ifdef ENABLE_ERROR_DEVICE
static int dpram_err_read(struct file *filp, char *buf, size_t count, loff_t *ppos)
{
	DECLARE_WAITQUEUE(wait, current);

	unsigned long flags;
	ssize_t ret;
	size_t ncopy;

	add_wait_queue(&dpram_err_wait_q, &wait);
	set_current_state(TASK_INTERRUPTIBLE);

	while (1) {
		local_irq_save(flags);

		if (dpram_err_len) {
			ncopy = min(count, dpram_err_len);

			if (copy_to_user(buf, dpram_err_buf, ncopy))
				ret = -EFAULT;
			else
				ret = ncopy;

			dpram_err_len = 0;
			
			local_irq_restore(flags);
			break;
		}

		local_irq_restore(flags);

		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		schedule();
	}

	set_current_state(TASK_RUNNING);
	remove_wait_queue(&dpram_err_wait_q, &wait);

	return ret;
}

static int dpram_err_fasync(int fd, struct file *filp, int mode)
{
	return fasync_helper(fd, filp, mode, &dpram_err_async_q);
}

static unsigned int dpram_err_poll(struct file *filp,
		struct poll_table_struct *wait)
{
	poll_wait(filp, &dpram_err_wait_q, wait);
	return ((dpram_err_len) ? (POLLIN | POLLRDNORM) : 0);
}
#endif

static void res_ack_tasklet_handler(unsigned long data)
{
	dpram_device_t *device = (dpram_device_t *)data;

	if (device && device->serial.tty) {
		struct tty_struct *tty = device->serial.tty;

		if(debug_default)
			DPRINTK("device id: %d\n", tty->index);

		tty_wakeup(tty);
	}
}

#ifdef USE_WORKQUEUE
static void send_workqueue_handler(struct work_struct *dummy)
#else
static void send_tasklet_handler(unsigned long data)
#endif
{
	workqueue_data_t *p = container_of(dummy, workqueue_data_t, work);
	dpram_tasklet_data_t *tasklet_data = (dpram_tasklet_data_t *)p->data;

	dpram_device_t *device = tasklet_data->device;
	u_int16_t non_cmd = tasklet_data->non_cmd;

	int ret = 0;
	int readable;

	unsigned char* char_buf_ptr = NULL;
	if (device && device->serial.tty) {
		struct tty_struct *tty = device->serial.tty;

		while ((readable = dpram_get_read_available(device)) > 0) {

			ret=tty_prepare_flip_string(tty, &char_buf_ptr, readable);

			if (ret <= 0) {
				if(debug_default)
					DPRINTK("tty buffer allocation failed.\n");
					break;
			} else {
				ret = dpram_read(device, char_buf_ptr, ret);
				if (ret < 0) {
					if(debug_default)
						DPRINTK("dpram_read failed.\n");
					break;
				}
			}	
		}

		tty_flip_buffer_push(tty);
	} else {
		if(debug_default)
			DPRINTK("Drop data due to no corresponding tty.\n");
		dpram_drop_data(device);
	}
	if (non_cmd & device->mask_req_ack) {
		if(debug_default)
				DPRINTK("Send to the phone response ack.\n");
		send_interrupt_to_phone(INT_NON_COMMAND(device->mask_res_ack), 0);
	}
}

static inline void cmd_req_active_handler(void)
{
	send_interrupt_to_phone(INT_COMMAND(INT_MASK_CMD_RES_ACTIVE), 1);
}

static inline void cmd_error_display_handler(void)
{
	char buf[DPRAM_ERR_MSG_LEN];
#ifdef ENABLE_ERROR_DEVICE
	unsigned long flags;
#endif

	memset((void *)buf, 0, sizeof (buf));
	read_from_dpram((void *)buf, DPRAM_PHONE2PDA_FORMATTED_BUFFER_ADDRESS,
			sizeof (buf) - 1);

	if (debug_default)
		printk("[PHONE ERROR] ->> %s\n", buf);

#ifdef ENABLE_ERROR_DEVICE
	local_irq_save(flags);
	memcpy(dpram_err_buf, buf, DPRAM_ERR_MSG_LEN);
	dpram_err_len = 64;
	local_irq_restore(flags);

	wake_up_interruptible(&dpram_err_wait_q);
	kill_fasync(&dpram_err_async_q, SIGIO, POLL_IN);
#endif
}

static inline void cmd_phone_start_handler(void)
{
	dpram_init_and_report();
}

static inline void cmd_req_time_sync_handler(void)
{
	/* TODO: add your codes here.. */
}

static inline void cmd_phone_deep_sleep_handler(void)
{
	/* TODO: add your codes here.. */
}

static inline void cmd_nv_rebuilding_handler(void)
{
	/* TODO: add your codes here.. */
}

static inline void cmd_emer_down_handler(void)
{
	/* TODO: add your codes here.. */
}

static inline void command_handler(u_int16_t irq_mask)
{
	u_int16_t cmd = (irq_mask &= ~(INT_MASK_VALID | INT_MASK_COMMAND));

	if (debug_default)
		DPRINTK("command code: 0x%04x\n", cmd);

	switch (cmd) {
		case INT_MASK_CMD_REQ_ACTIVE:
			cmd_req_active_handler();
			break;

		case INT_MASK_CMD_ERR_DISPLAY:
			cmd_error_display_handler();
			break;

		case INT_MASK_CMD_PHONE_START:
if(debug_default)
			DPRINTK("phone start handler called.\n");
			cmd_phone_start_handler();
			break;

		case INT_MASK_CMD_REQ_TIME_SYNC:
			cmd_req_time_sync_handler();
			break;

		case INT_MASK_CMD_PHONE_DEEP_SLEEP:
			cmd_phone_deep_sleep_handler();
			break;

		case INT_MASK_CMD_NV_REBUILDING:
			cmd_nv_rebuilding_handler();
			break;

		case INT_MASK_CMD_EMER_DOWN:
			cmd_emer_down_handler();
			break;

		default:
			if (debug_default)
				DPRINTK("Unknown command..\n");
	}
}

static inline void non_command_handler(u_int16_t irq_mask)
{
	u_int16_t fmt_head, fmt_tail;
	u_int16_t raw_head, raw_tail;

	u_int16_t non_cmd = (irq_mask &= ~INT_MASK_VALID);

	if (debug_default)
		DPRINTK("non-command code: 0x%04x\n", non_cmd);

	read_from_dpram_no_lock((void *)&fmt_head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS,
			sizeof (fmt_head));
	read_from_dpram_no_lock((void *)&fmt_tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS,
			sizeof (fmt_tail));
	read_from_dpram_no_lock((void *)&raw_head, DPRAM_PHONE2PDA_RAW_HEAD_ADDRESS,
			sizeof (raw_head));
	read_from_dpram_no_lock((void *)&raw_tail, DPRAM_PHONE2PDA_RAW_TAIL_ADDRESS,
			sizeof (raw_tail));

	if (fmt_head != fmt_tail)
		non_cmd |= INT_MASK_SEND_F;

	if (raw_head != raw_tail)
		non_cmd |= INT_MASK_SEND_R;

	if (non_cmd & INT_MASK_SEND_F) {
		dpram_tasklet_data[FORMATTED_INDEX].device = &dpram_table[FORMATTED_INDEX];
		dpram_tasklet_data[FORMATTED_INDEX].non_cmd = non_cmd;
		
#ifdef USE_WORKQUEUE 
		workqueue_data[FORMATTED_INDEX].data = (unsigned long)&dpram_tasklet_data[FORMATTED_INDEX];
		schedule_work(&workqueue_data[FORMATTED_INDEX].work);
#else
		fmt_send_tasklet.data = (unsigned long)&dpram_tasklet_data[FORMATTED_INDEX];
		tasklet_schedule(&fmt_send_tasklet);
#endif
	}

	if (non_cmd & INT_MASK_SEND_R) {
		dpram_tasklet_data[RAW_INDEX].device = &dpram_table[RAW_INDEX];
		dpram_tasklet_data[RAW_INDEX].non_cmd = non_cmd;

#ifdef USE_WORKQUEUE
		workqueue_data[RAW_INDEX].data = (unsigned long)&dpram_tasklet_data[RAW_INDEX];
		schedule_work(&workqueue_data[RAW_INDEX].work);
#else
		raw_send_tasklet.data = (unsigned long)&dpram_tasklet_data[RAW_INDEX];
		tasklet_schedule(&raw_send_tasklet);
#endif
	}

	if (non_cmd & INT_MASK_RES_ACK_F)
		tasklet_schedule(&fmt_res_ack_tasklet);

	if (non_cmd & INT_MASK_RES_ACK_R)
		tasklet_schedule(&raw_res_ack_tasklet);
}

static irqreturn_t dpram_interrupt(int irq, void *dev_id)
{
	u_int16_t irq_mask = 0;

	read_from_dpram_no_lock((void *)&irq_mask,
		DPRAM_PHONE2PDA_INTERRUPT_ADDRESS, sizeof (irq_mask));

	if (!(irq_mask & INT_MASK_VALID)) {
		if (debug_default)
			DPRINTK("Invalid interrupt mask: 0x%04x\n", irq_mask);
		return IRQ_NONE;
	}

	if (irq_mask & INT_MASK_COMMAND)
		command_handler(irq_mask);
	else
		non_command_handler(irq_mask);

	return IRQ_HANDLED;
}

static irqreturn_t phone_active_interrupt(int irq, void *dev_id)
{
	if (debug_default) {
		unsigned int is_phone_active = dpram_phone_getstatus();
		DPRINTK("phone is %s.\n", is_phone_active ? "active" : "inactive");
	}

	return IRQ_HANDLED;
}

#ifdef ENABLE_ERROR_DEVICE
static struct file_operations dpram_err_ops = {
	.owner			= THIS_MODULE,
	.read			= dpram_err_read,
	.fasync			= dpram_err_fasync,
	.poll			= dpram_err_poll,
	.llseek			= no_llseek,

	/* TODO: add more operations */
};
#endif

static const struct tty_operations dpram_tty_ops = {
	.open			= dpram_tty_open,
	.close			= dpram_tty_close,
	.write			= dpram_tty_write,
	.write_room		= dpram_tty_write_room,
	.ioctl			= dpram_tty_ioctl,
	.chars_in_buffer	= dpram_tty_chars_in_buffer,

	/* TODO: add more operations */
};

#ifdef ENABLE_ERROR_DEVICE
static inline void unregister_dpram_err_device(void)
{
	unregister_chrdev(dpram_tty_driver->major, DPRAM_ERR_DEVICE);
	class_destroy(dpram_class);
}

static inline int __init register_dpram_err_device(void)
{
	struct device *dpram_err_dev_t;
	int ret = register_chrdev(dpram_tty_driver->major, DPRAM_ERR_DEVICE, &dpram_err_ops);
	if (ret < 0)
		return ret;

	dpram_class = class_create(THIS_MODULE, "err");
	if (IS_ERR(dpram_class)) {
		unregister_dpram_err_device();
		return -EFAULT;
	}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
	dpram_err_dev_t = device_create(dpram_class, NULL, MKDEV(dpram_tty_driver->major, 0), NULL, DPRAM_ERR_DEVICE);
#else
	dpram_err_dev_t = device_create(dpram_class, NULL, MKDEV(dpram_tty_driver->major, 0), DPRAM_ERR_DEVICE);
#endif
	if (IS_ERR(dpram_err_dev_t)) {
		unregister_dpram_err_device();
		return -EFAULT;
	}

	return 0;
}
#endif

static int __init register_dpram_driver(void)
{
	int retval = 0;

	dpram_tty_driver = alloc_tty_driver(MAX_INDEX);
	if (!dpram_tty_driver)
		return -ENOMEM;

	dpram_tty_driver->owner = THIS_MODULE;
	dpram_tty_driver->magic = TTY_DRIVER_MAGIC;
	dpram_tty_driver->driver_name = DRIVER_NAME;
	dpram_tty_driver->name = "dpram";
	dpram_tty_driver->major = DRIVER_MAJOR_NUM_ALLOC;
	dpram_tty_driver->minor_start = 1;
	dpram_tty_driver->num = 2;
	dpram_tty_driver->type = TTY_DRIVER_TYPE_SERIAL;
	dpram_tty_driver->subtype = SERIAL_TYPE_NORMAL;
	dpram_tty_driver->flags = TTY_DRIVER_REAL_RAW;
	dpram_tty_driver->init_termios = tty_std_termios;
	dpram_tty_driver->init_termios.c_cflag =
		(B115200 | CS8 | CREAD | CLOCAL | HUPCL);

	tty_set_operations(dpram_tty_driver, &dpram_tty_ops);
	dpram_tty_driver->ops = &dpram_tty_ops;

	dpram_tty_driver->ttys = dpram_tty;
	dpram_tty_driver->termios = dpram_termios;
	dpram_tty_driver->termios_locked = dpram_termios_locked;

	retval = tty_register_driver(dpram_tty_driver);
	if (retval) {
		if (debug_default)
			DPRINTK("tty_register_driver error\n");
		put_tty_driver(dpram_tty_driver);
		return retval;
	}

	return 0;
}

static inline void unregister_dpram_driver(void)
{
	int i;

	for (i = 0; i < MAX_INDEX; i++)
		tty_unregister_device(dpram_tty_driver, i);

	tty_unregister_driver(dpram_tty_driver);
}

static inline void __init init_devices(void)
{
	int i;

	for (i = 0; i < MAX_INDEX; i++) {
		init_MUTEX(&dpram_table[i].serial.sem);

		dpram_table[i].serial.open_count = 0;
		dpram_table[i].serial.tty = NULL;
	}
}

static inline void __exit kill_tasklets(void)
{
	tasklet_kill(&fmt_res_ack_tasklet);
	tasklet_kill(&raw_res_ack_tasklet);

#ifdef USE_WORKQUEUE
	destroy_workqueue(dpram_workqueue);
#else
	tasklet_kill(&fmt_send_tasklet);
	tasklet_kill(&raw_send_tasklet);
#endif
}

static int __init register_interrupt_handler(void)
{
	unsigned int dpram_irq, phone_active_irq;
	int retval = 0;

	dpram_clear();

	dpram_irq = IRQ_GPIO(PHONE_nINT_GPIO70);

	retval = request_irq(dpram_irq, dpram_interrupt, IRQF_DISABLED, DRIVER_NAME, NULL);
	if (retval) {
if(debug_default)
		DPRINTK("DPRAM interrupt handler failed.\n");
		unregister_dpram_driver();
		return -1;
	}
	set_irq_type(IRQ_GPIO(PHONE_nINT_GPIO70), IRQ_TYPE_EDGE_FALLING);

	phone_active_irq = IRQ_GPIO(PHONE_ACTIVE_GPIO107);
	retval = request_irq(phone_active_irq, phone_active_interrupt,
			IRQF_DISABLED, "Phone Active", NULL);
	if (retval) {
		if(debug_default)
			DPRINTK("Phone active interrupt handler failed.\n");
		free_irq(dpram_irq, NULL);
		unregister_dpram_driver();
		return -1;
	}
	set_irq_type(IRQ_GPIO(PHONE_ACTIVE_GPIO107), IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING);

	return 0;
}

static void __init check_miss_interrupt(void)
{
	unsigned long flags;
	int irq, pin_level;

	pin_level = gpio_get_value(PHONE_ACTIVE_GPIO107) &&
		    !gpio_get_value(PHONE_nINT_GPIO70);
	if (pin_level) {
		irq = IRQ_GPIO(PHONE_nINT_GPIO70);
		if (debug_default)
			DPRINTK("Missing interrupt!\n");
		local_irq_save(flags);
		dpram_interrupt(irq, NULL);
		local_irq_restore(flags);
	}
}

static int __devinit dpram_probe(struct platform_device *dev)
{
	int retval;

	if(debug_default)
		DPRINTK("dpram init\n");
	retval = register_dpram_driver();
	if (retval) {
		if(debug_default)
			DPRINTK("Failed to register dpram (tty) driver.\n");
		return -1;
	}

#ifdef ENABLE_ERROR_DEVICE
	retval = register_dpram_err_device();
	if (retval) {
if(debug_default)
		DPRINTK("Failed to register dpram error device.\n");

		unregister_dpram_driver();
		return -1;
	}

	memset((void *)dpram_err_buf, '\0', sizeof dpram_err_buf);
#endif

	/* SMC(Static Memory Controller) settting statements are moved to 'mirage-smemc.c' */

	if ((retval = register_interrupt_handler()) < 0)
		return -1;

	init_devices();  /// Occur Kernel Panic
#ifdef USE_WORKQUEUE
	dpram_workqueue = create_singlethread_workqueue(DPRAM_WORKQUEUE);
	INIT_WORK(&workqueue_data[FORMATTED_INDEX].work, send_workqueue_handler);
	INIT_WORK(&workqueue_data[RAW_INDEX].work, send_workqueue_handler);
#endif

#ifdef CONFIG_PROC_FS
	create_proc_read_entry(DRIVER_PROC_ENTRY, 0, 0, dpram_read_proc, NULL);
	create_proc_read_entry(DRIVER_TEST_ENTRY, 0, 0, dpram_dump_proc, NULL);
#endif

	check_miss_interrupt();

	memset((void *)dpram_err_buf, '\0', sizeof dpram_err_buf);

	if (debug_default)
		printk(DRIVER_ID "\n");

	return 0;
}

static void __exit dpram_exit(void)
{
	unregister_dpram_driver();

#ifdef ENABLE_ERROR_DEVICE
	unregister_dpram_err_device();
#endif

	free_irq(IRQ_GPIO(PHONE_nINT_GPIO70), NULL);
	free_irq(IRQ_GPIO(PHONE_ACTIVE_GPIO107), NULL);

	kill_tasklets();
}

#ifdef CONFIG_PM
/*
 * Power management hooks.  Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */
static int dpram_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int dpram_resume(struct platform_device *dev)
{
	u_int16_t head, tail;

	read_from_dpram((void *)&head, DPRAM_PHONE2PDA_FORMATTED_HEAD_ADDRESS, 
			sizeof(head));
	read_from_dpram((void *)&tail, DPRAM_PHONE2PDA_FORMATTED_TAIL_ADDRESS, 
		    sizeof(tail));

	if (head != tail) 
        dpram_interrupt(0, NULL);
    return 0;
}
#else
#define dpram_suspend	NULL
#define dpram_resume	NULL
#endif

static struct platform_driver dpram_driver = {
        .probe  = dpram_probe,
#ifdef CONFIG_PM
        .suspend	= dpram_suspend,
        .resume	= dpram_resume,
#endif
        .driver		= {
                .name	= "dpram",
        },
};        

int __devinit dpram_init(void)
{
    return platform_driver_register(&dpram_driver);
}


module_init(dpram_init);
module_exit(dpram_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");
MODULE_DESCRIPTION("DPRAM Device Driver for Linux SMART Phone");
MODULE_LICENSE("GPL");
