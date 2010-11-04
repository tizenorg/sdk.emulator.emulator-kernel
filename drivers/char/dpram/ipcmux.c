/******************************************************************************
*******************************************************************************
*
*							This is MUX/DEMUX driver for samsung IPC 
*
******************************************************************************
******************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/freezer.h>

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/poll.h>


/******************************************************************************
*******************************************************************************
*
*							DEBUG 
*
******************************************************************************
******************************************************************************/

//#define IPCMUX_MSGWARN
//#define IPCMUX_MSGHIGH
//#define IPCMUX_MSGLOW
//#define IPCMUX_HEXDUMP

#define TXIPC 	1
#define TXHDLC 	2
#define RXHDLC 	3
#define RXIPC 	4


#ifdef IPCMUX_MSGWARN
#define MSG_WARN(X...) \
		do { \
			printk("[IPCMUX WARNING] [%s():%d]", __FUNCTION__, __LINE__); \
			printk(X); \
		} while (0)
#else
#define MSG_WARN(X...)		do { } while (0)
#endif

#ifdef IPCMUX_MSGHIGH
#define MSG_HIGH(X...) \
		do { \
			printk("[IPCMUX] [%s():%d] ", __FUNCTION__, __LINE__); \
			printk(X); \
		} while (0)
#else
#define MSG_HIGH(X...)	do { } while (0)
#endif

#ifdef IPCMUX_MSGLOW
#define MSG_LOW(X...) \
		do { \
			printk("[IPCMUX] [%s():%d] ", __FUNCTION__, __LINE__); \
			printk(X); \
		} while (0)
#else
#define MSG_LOW(X...)	do { } while (0)
#endif

#ifdef IPCMUX_HEXDUMP
int ipcmux_dump(const char *buf, int len, int direction)
{
	char str[80], octet[10];
	int ofs, i, k;
	printk( "=============================================================================\n");

	switch(direction)
	{
		case TXIPC:
			printk( "=============      [IPCMUX] APP -->  MUX   : %03d bytes        ==============\n", len);
			break;
			
		case TXHDLC:
			printk( "=============      [IPCMUX] MUX --> DPRAM : %03d bytes        ==============\n", len);
			break;
			
		case RXHDLC:
			printk( "=============      [IPCMUX] MUX <-- DPRAM : %03d bytes        ==============\n", len);
			break;
			
		case RXIPC:				
			printk( "=============      [IPCMUX]  APP <--  MUX  : %03d bytes         ==============\n", len);
			break;				
	}	

	for (ofs = 0; ofs < len; ofs += 16) 
	{
		sprintf( str, "%03d:", ofs );

		for (i = 0; i < 16; i++) 
		{
			if ((i + ofs) < len)
				sprintf( octet, "[%02x]", buf[ofs + i] );
			else
				strcpy( octet, "   " );
			strcat( str, octet );
		}
		strcat( str, "  " );
		k = strlen( str );
		
		str[k] = '\0';
		printk( "[IPCMUX] %s\n", str );
	}
	printk( "=============================================================================\n");

	return 0;
}
#else
int ipcmux_dump(const char *buf, int len, int direction)
{
	return 0;
}
#endif



/*********************************************************************************************
**********************************************************************************************
*
*							Global Variables & Structures
*
**********************************************************************************************
**********************************************************************************************/

/*********************************************************************************************/
/*     H D L C   F O R M A T		*/
/*--------------------------------------------------------------------------------------------
  | Start_Flag(0x7F) | LENGTH(2) | CONTROL(1) | IPC-MESSAGE(x)  | Stop_Flag(0x7E) |
  --------------------------------------------------------------------------------------------*/
/*********************************************************************************************/

/*********************************************************************************************/
/*    G S M   I P C   M E S S A G E   F O R M A T		*/
/*--------------------------------------------------------------------------------------------
  | LENGTH(2) | MSG_SEQ(1) | ACK_SEQ(1) | MAIN_CMD(1) | SUB_CMD(1) | CMD_TYPE(1) | PARAMETER(x) |
   -------------------------------------------------------------------------------------------*/
/*********************************************************************************************/




#define IPC_HEADER_SIZE 					7
#define HDLC_FRAME_HEADER_SIZE 			3
#define MAX_HDLC_INFO_SIZE					1000	
#define MAX_HDLC_FRAME_SIZE 				(MAX_HDLC_INFO_SIZE + HDLC_FRAME_HEADER_SIZE)
#define MAX_HDLC_FRAME_SIZE_WITH_FLAGS	(MAX_HDLC_FRAME_SIZE  + 2)
#define START_FLAG							0x7F
#define END_FLAG							0x7E

#define APP_DEVNAME						"ipcmux"
#define DPRAM_DEVNAME						"/dev/dpram0"  

/* DPRAM ioctls for DPRAM tty devices */
#define IOC_MZ_MAGIC					('h')
#define HN_DPRAM_PHONE_ON				_IO(IOC_MZ_MAGIC, 0xd0)
#define HN_DPRAM_PHONE_OFF			_IO(IOC_MZ_MAGIC, 0xd1)
#define HN_DPRAM_PHONE_GETSTATUS	_IOR(IOC_MZ_MAGIC, 0xd2, unsigned int)




/* Type definition for IPC Header */
typedef  struct {
	unsigned short		len;
	unsigned char			msg_seq;
	unsigned char			ack_seq;
	unsigned char			main_cmd;
	unsigned char			sub_cmd;
	unsigned char			cmd_type;
} __attribute__((packed)) ipc_hdr_t;


typedef enum {
  IPC_GSM_PWR_CMD=0x01,   /* 0x01 : Power Control Commands */
  IPC_GSM_CALL_CMD,            /* 0x02 : Call Control Commands */
  IPC_GSM_DATA_CMD,           /* 0x03 : Data Control Commands */
  IPC_GSM_SMS_CMD,             /* 0x04 : Short Message Service Commands */
  IPC_GSM_SEC_CMD,              /* 0x05 : Security - SIM control Commands */
  IPC_GSM_PB_CMD,               /* 0x06 : Phonebook Control Commands */
  IPC_GSM_DISP_CMD,            /* 0x07 : Display Control Commands */
  IPC_GSM_NET_CMD,             /* 0x08 : Network Commands */
  IPC_GSM_SND_CMD,             /* 0x09 : Sound Control Commands */
  IPC_GSM_MISC_CMD,           /* 0x0A : Miscellaneous Control Commands */
  IPC_GSM_SVC_CMD,             /* 0x0B : Service Mode Control Commands - Factory Test or Debug Screen Control */
  IPC_GSM_SS_CMD,               /* 0x0C : Supplementary Service Control Command */
  IPC_GSM_GPRS_CMD,           /* 0x0D : GPRS(AT Command to IPC) Commands */  
  IPC_GSM_SAT_CMD,             /* 0x0E : SIM Toolkit Commands */
  IPC_GSM_CFG_CMD,             /* 0x0F : Configuration Commands */
  IPC_GSM_IMEI_CMD,             /* 0x10 : IMEI Tool Commands */
  IPC_GSM_GPS_CMD,			 /* 0x11 : GPSl Commands */
  IPC_GSM_SAP_CMD,            /* 0x12 : SIM Access Profile Commands */
  IPC_GSM_GEN_CMD=0x80,    /* 0x80 : General Response Command */
  IPC_GSM_CMD_MAX
} ipc_main_cmd_t;

typedef struct {
	unsigned char 	m_StartMagicCode;
	unsigned short 	m_Length;
	unsigned char 	m_CtrlInfo;		// control information id
	void* 			m_pValue;
	unsigned char 	m_EndMagicCode;
} HDLCFrame_t;

// For multi HDLC frame.
typedef struct tagHDLCNode
{
	struct tagHDLCNode*	m_pNext;
	HDLCFrame_t 			m_HDLCNode;
	
} HDLCNode_t;

static HDLCFrame_t 		s_FrameRecvInfo = {0, };
static HDLCNode_t*		g_pHead = NULL;
static HDLCNode_t*		g_pTail = NULL;



typedef struct {
	unsigned char	id;
	char		ifname[16];
} __attribute__ ((packed)) vsc_arg_t;

typedef struct {
	u_int8_t			id;
	u_int8_t*			tx_buf;
	struct tty_driver	mux_tty_driver; 
	int 				open_count;
	char				tty_name[16]; 
	struct tty_struct*	tty;
//	struct semaphore	write_lock; // if need, we will add sem.	
} dev_info_t;

//#define MAX_CHANNEL_NUMBER				3
#define MAX_CHANNEL_NUMBER				2
#define CHANNEL_ID_1						1
#define CHANNEL_ID_2						2
//#define CHANNEL_ID_3						3

static vsc_arg_t g_vsc_arg[MAX_CHANNEL_NUMBER]=
{
	{CHANNEL_ID_1, "ttyTEL"},
	{CHANNEL_ID_2, "ttyDNET"},
//	{CHANNEL_ID_3, "ttySIM"},
};

static dev_info_t*			dev_table[MAX_CHANNEL_NUMBER];






static DECLARE_MUTEX(dev_lock);

static struct task_struct *dpram_task;
static struct file *dpram_filp;
static DECLARE_COMPLETION(dpram_complete);

extern void dpram_phone_on(void);
extern void dpram_phone_off(void);
extern unsigned int dpram_phone_getstatus(void);

static int ipc_mux(dev_info_t *dev, const void *data, size_t len  );
static int ipc_demux(void);


static int vsc_read(dev_info_t *dev, unsigned char* pdata, int len);


/*****************************************************************************
******************************************************************************
*
*							device table functions
*
******************************************************************************
******************************************************************************/

static inline dev_info_t * devtbl_get_dev_by_id(u_int8_t id)
{
	int slot;
	dev_info_t *dev = NULL;
	
	MSG_LOW("enter \n");

	for (slot = 0; slot < MAX_CHANNEL_NUMBER; slot++) {
		if (dev_table[slot] && dev_table[slot]->id == id) {			
			dev = dev_table[slot];
			up(&dev_lock);
			return dev;
		}
	}
	return NULL;
}

static inline dev_info_t * devtbl_get_dev_by_name(const char *name)
{
	int slot;
	dev_info_t *dev;
	
	MSG_LOW("enter\n");
	
	for (slot = 0; slot < MAX_CHANNEL_NUMBER; slot++) {
		dev = dev_table[slot];		
		if (dev && strcmp(name, dev->tty_name) == 0) {
			up(&dev_lock);
			return dev;
		}
	}
	return NULL;
}

static inline int devtbl_add_dev(dev_info_t *dev)
{
	int slot;

	MSG_LOW("enter \n");

	if (devtbl_get_dev_by_id(dev->id)) {
		return -EBUSY; 
	}

	for (slot = 0; slot < MAX_CHANNEL_NUMBER; slot++) {
		if (dev_table[slot] == NULL) {
			dev_table[slot] = dev;
			up(&dev_lock);
			return slot;
		}
	}
	return -ENOSPC;
}

static inline dev_info_t * devtbl_remove_dev_by_id(u_int8_t id)
{
	int slot;
	dev_info_t *dev;

	MSG_LOW("enter \n");

	for (slot = 0; slot < MAX_CHANNEL_NUMBER; slot++) {
		if (dev_table[slot] && dev_table[slot]->id == id) {
			dev = dev_table[slot];
			dev_table[slot] = NULL;
			up(&dev_lock);
			return dev;
		}
	}
	return NULL;
}

static inline dev_info_t * devtbl_remove_dev_by_slot(int slot)
{
	dev_info_t *dev;
	
	MSG_LOW("enter \n");

	dev = dev_table[slot];
	dev_table[slot] = NULL;
	return dev;
}



/*****************************************************************************
******************************************************************************
*
*							hdlc functions
*
******************************************************************************
******************************************************************************/
/* ===	For MUX	=== */
static unsigned char hdlc_get_control_infoid(void)
{
	static unsigned char s_infoid = 0;
	
	MSG_LOW("enter \n");

	if (s_infoid >= 128)
		s_infoid = 0;
	++s_infoid;

	return s_infoid;
}

 
/* ===	For DEMUX	=== */
static void hdlc_clear_all_mfhdlc_nodes(void)
{
	HDLCNode_t* pnode_tmp;
	HDLCNode_t* pnode = g_pHead;

	MSG_LOW("enter \n");
	
	while (pnode)
	{
		pnode_tmp = pnode;
		pnode = pnode->m_pNext;

		// free data
		if (pnode_tmp->m_HDLCNode.m_pValue)
			kfree(pnode_tmp->m_HDLCNode.m_pValue);

		kfree(pnode_tmp);
	}

	g_pHead = g_pTail = NULL;
	
}


static int hdlc_push_mfhdlc(HDLCFrame_t const* pframe)
{
	HDLCNode_t* pnode = NULL;

	MSG_LOW("enter \n");

	if (!pframe){
		MSG_WARN("HDLC frame is NULL\n");
		return -1;
	}


	pnode = kmalloc(sizeof(HDLCNode_t), GFP_KERNEL);
	if (pnode == NULL) {
		MSG_WARN("out of memory\n");
		return -ENOMEM;
	}
	memset(pnode, 0, sizeof(HDLCNode_t));	
	

	pnode->m_HDLCNode.m_Length = pframe->m_Length;
	pnode->m_HDLCNode.m_CtrlInfo = pframe->m_CtrlInfo;

	if ((pframe->m_Length - HDLC_FRAME_HEADER_SIZE) > 0)
	{
		pnode->m_HDLCNode.m_pValue = kmalloc(pframe->m_Length - HDLC_FRAME_HEADER_SIZE, GFP_KERNEL);
		if (pnode->m_HDLCNode.m_pValue == NULL) {
			MSG_WARN("out of memory\n");
			return -ENOMEM;
		}		
		memcpy(pnode->m_HDLCNode.m_pValue, pframe->m_pValue, pframe->m_Length - HDLC_FRAME_HEADER_SIZE);
	}

	if (!g_pHead)
	{
		g_pHead = pnode;
	}
	else
	{
		if (g_pTail)
		{
			g_pTail->m_pNext = pnode;
	}
	}

	g_pTail = pnode;
	
	return 0;
	
}




static HDLCFrame_t const* hdlc_get_last_mfhdlc(void)
{
	MSG_LOW("enter \n");

	if (g_pTail)
	{
		return &(g_pTail->m_HDLCNode);
	}
	else
	{
		// Do nothing...
	}
	
	return NULL;
}




/*	Extract an IPC Message from Multi HDLC Frame 	*/
static unsigned char*	hdlc_extract_ipc_from_mfhdlc(void)
{
	unsigned char* 	pipc_data = NULL;
	unsigned char*	ptr = NULL;
	int 				ipc_size = 0;
	HDLCNode_t* 		pnode = g_pHead;

	MSG_LOW("enter \n");

	while (pnode) // calculate ipc length
	{
		ipc_size += pnode->m_HDLCNode.m_Length - HDLC_FRAME_HEADER_SIZE;
		pnode = pnode->m_pNext;
	}

	if (ipc_size > 0)
	{
		pipc_data = (unsigned char*)kmalloc(ipc_size, GFP_KERNEL);
		if (pipc_data == NULL) {
			MSG_WARN("out of memory\n");
			return NULL;
		}		
		ptr = pipc_data;
		pnode = g_pHead;
		
		while (pnode)
		{
			memcpy(ptr, pnode->m_HDLCNode.m_pValue, (pnode->m_HDLCNode.m_Length - HDLC_FRAME_HEADER_SIZE));
			
			ptr += (pnode->m_HDLCNode.m_Length - HDLC_FRAME_HEADER_SIZE);

			pnode = pnode->m_pNext;
		}
	}

	hdlc_clear_all_mfhdlc_nodes();

	return pipc_data;
	
}

static int hdlc_get_chid_from_ipc(unsigned char* pipc)
{
	int ch_id=0;
	ipc_hdr_t ipc_hdr;
	unsigned char ipc_rx_main_cmd; // for general response

	MSG_LOW("enter \n");

	memcpy(&ipc_hdr, pipc, IPC_HEADER_SIZE);

	switch (ipc_hdr.main_cmd)
	{
		case IPC_GSM_PWR_CMD:
		case IPC_GSM_CALL_CMD:
		case IPC_GSM_SMS_CMD:
		case IPC_GSM_DISP_CMD:
		case IPC_GSM_NET_CMD:
		case IPC_GSM_SND_CMD:
		case IPC_GSM_MISC_CMD:
		case IPC_GSM_SVC_CMD:
		case IPC_GSM_SS_CMD:
		case IPC_GSM_CFG_CMD:
		case IPC_GSM_IMEI_CMD:
		case IPC_GSM_GPS_CMD:			
			ch_id = CHANNEL_ID_1;
			break;

		case IPC_GSM_DATA_CMD:
		case IPC_GSM_GPRS_CMD:
			ch_id = CHANNEL_ID_2;
			break;
			
		case IPC_GSM_SEC_CMD:
		case IPC_GSM_PB_CMD:
		case IPC_GSM_SAT_CMD:
		case IPC_GSM_SAP_CMD:			
			ch_id = CHANNEL_ID_1;
			break;
			
		case IPC_GSM_GEN_CMD:
			memcpy(&ipc_rx_main_cmd, pipc+IPC_HEADER_SIZE, 1);
			switch(ipc_rx_main_cmd)
			{
				case IPC_GSM_PWR_CMD:
				case IPC_GSM_CALL_CMD:
				case IPC_GSM_SMS_CMD:
				case IPC_GSM_DISP_CMD:
				case IPC_GSM_NET_CMD:
				case IPC_GSM_SND_CMD:
				case IPC_GSM_MISC_CMD:
				case IPC_GSM_SVC_CMD:
				case IPC_GSM_SS_CMD:
				case IPC_GSM_CFG_CMD:
				case IPC_GSM_IMEI_CMD:
				case IPC_GSM_GPS_CMD:			
					ch_id = CHANNEL_ID_1;
					break;

				case IPC_GSM_DATA_CMD:
				case IPC_GSM_GPRS_CMD:
					ch_id = CHANNEL_ID_2;
					break;
					
				case IPC_GSM_SEC_CMD:
				case IPC_GSM_PB_CMD:
				case IPC_GSM_SAT_CMD:
				case IPC_GSM_SAP_CMD:			
					ch_id = CHANNEL_ID_1;	
					break;

				default:
					MSG_WARN("unknown IPC RX MAIN CMD FOR GEN CMD :  0x%02x\n", ipc_rx_main_cmd);
					ch_id = CHANNEL_ID_1; // unknown cmd is sent to Telephony.
					break;			
			}			
			break;
			


		default:
			MSG_WARN("unknown IPC MAIN CMD : 0x%02x\n", ipc_hdr.main_cmd);
			ch_id = CHANNEL_ID_1; // unknown cmd is sent to Telephony.
			break;

	}

	return ch_id;

}



/*	Extract an IPC Packet from SFHDLC Frame or MFHDLC Frames 	*/
static unsigned char* hdlc_extract_ipc(HDLCFrame_t const* pframe)
{
	unsigned char	*pipc;
	
	MSG_LOW("enter \n");

	if (pframe->m_CtrlInfo & 0x80)
	{
		MSG_HIGH("This is MFHDLC frame!!\n");	
		hdlc_push_mfhdlc(pframe);
		return NULL;
	}
	else
	{
		HDLCFrame_t const* plast_info;
		HDLCFrame_t const* plast_multi_info;

		plast_info = &s_FrameRecvInfo;
		plast_multi_info = hdlc_get_last_mfhdlc();

		/* 		MFHDLC		*/
		if (plast_multi_info && plast_info && ( (plast_multi_info->m_CtrlInfo & 0x7f) == (plast_info->m_CtrlInfo & 0x7f))) 
		{
			MSG_HIGH("Making IPC from MF-HDLC..\n");	
			
			pipc = hdlc_extract_ipc_from_mfhdlc();
			return pipc;
			
		}

		/* 		SFHDLC		*/		
		else 
		{
			if (pframe->m_Length >= 10)
			{
				pipc = (unsigned char *) pframe->m_pValue;
				return pipc;
			}
			else
			{
				MSG_WARN("IPC Packet is NULL\n");
				return NULL;
			}			
		}		
	}

}




/*****************************************************************************
******************************************************************************
*
*							dpram functions
*
******************************************************************************
******************************************************************************/

static inline struct file *dpram_open(void)
{
	int ret = 0;
	struct file *filp;
	struct termios termios;
	mm_segment_t oldfs;

	MSG_LOW("enter \n");

	filp = filp_open(DPRAM_DEVNAME, O_RDWR, 0);
	if (IS_ERR(filp)) {
		MSG_LOW("filp_open() failed~!: %ld\n", PTR_ERR(filp));
		return NULL;
	}
	oldfs = get_fs(); set_fs(get_ds());
	ret = filp->f_op->unlocked_ioctl(filp, 
				TCGETA, (unsigned long)&termios);
	set_fs(oldfs);
	if (ret < 0) {
		MSG_LOW("f_op->ioctl() failed: %d\n", ret);
		filp_close(filp, current->files);
		return NULL;
	}
	termios.c_cflag = CS8 | CREAD | HUPCL | CLOCAL | B115200;
	termios.c_iflag = IGNBRK | IGNPAR;
	termios.c_lflag = 0;
	termios.c_oflag = 0;
	termios.c_cc[VMIN] = 1;
	termios.c_cc[VTIME] = 1;

	oldfs = get_fs(); set_fs(get_ds());
	ret = 0;
	ret = filp->f_op->unlocked_ioctl(filp, 
				TCSETA, (unsigned long)&termios);
	set_fs(oldfs);
	if (ret < 0) {
		MSG_LOW("f_op->ioctl() failed: %d\n", ret);
		filp_close(filp, current->files);
		return NULL;
	}

	return filp;
}

static inline void dpram_close(struct file *filp)
{
	MSG_LOW("enter \n");

	filp_close(filp, current->files);
}

static inline int dpram_poll(struct file *filp)
{
	int ret;
	unsigned int mask;
	struct poll_wqueues wait_table;
	mm_segment_t oldfs;
	
	MSG_LOW("enter \n");

	poll_initwait(&wait_table);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);

		oldfs = get_fs(); set_fs(get_ds());
		mask = filp->f_op->poll(filp, &wait_table.pt);
		set_fs(oldfs);

		if (mask & POLLIN) {
			ret = 0;
			break;
		}

		if (wait_table.error) {
			MSG_LOW( "error in f_op->poll()\n");
			ret = wait_table.error;
			break;
		}

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}

		schedule();
	}
	set_current_state(TASK_RUNNING);
	poll_freewait(&wait_table);

	return ret;
}

static inline int dpram_write(struct file *filp, const void *buf, size_t count, int nonblock)
{
	int ret, n = 0;
	mm_segment_t oldfs;

	MSG_LOW("enter \n");

	ipcmux_dump(buf, count, TXHDLC);
	
	while (count) {
		if (!dpram_filp) {
			MSG_LOW( "DPRAM not available\n");
			return -ENODEV;
		}

		if (nonblock) dpram_filp->f_flags |= O_NONBLOCK;
		oldfs = get_fs(); set_fs(get_ds());
		ret = filp->f_op->write(filp, buf + n, count, &filp->f_pos);
		set_fs(oldfs);
		if (nonblock) dpram_filp->f_flags &= ~O_NONBLOCK;
		if (ret < 0) {
			if (ret == -EAGAIN && !nonblock) {
				continue;
			}
			return ret;
		}
		n += ret;
		count -= ret;
	}
	return n;
}

static inline int dpram_read(struct file *filp, void *buf, size_t count)
{
	int ret, n = 0;
	mm_segment_t oldfs;

	MSG_LOW("enter \n");

	while (count) {
		oldfs = get_fs(); set_fs(get_ds());
		ret = filp->f_op->read(filp, buf + n, count, &filp->f_pos);
		set_fs(oldfs);
		if (ret < 0) {
			if (ret == -EAGAIN) continue;
			MSG_LOW("f_op->read() failed: %d\n", ret);
			return ret;
		}
		n += ret;
		count -= ret;
	}
	return n;
}

static int dpram_thread(void *data)
{
	int ret = 0;
	struct file *filp;

	MSG_LOW("enter \n");
	
	dpram_task = current;

	daemonize("dpram_thread");
	
	strcpy(current->comm, "ipcmux");

	siginitsetinv(&current->blocked, sigmask(SIGUSR1));
	recalc_sigpending();

	filp = dpram_open();
	if (filp == NULL)
		goto out;

	dpram_filp = filp;

	complete(&dpram_complete);

	while (1) {
		ret = dpram_poll(filp);

		if (ret == -ERESTARTSYS) {
			if (sigismember(&current->pending.signal, SIGUSR1)) {
				MSG_LOW("got SIGUSR1 signal\n");
				sigdelset(&current->pending.signal, SIGUSR1);
				recalc_sigpending();
				ret = 0;
				break;
			}
		} else if (ret < 0) {
			MSG_WARN("dpram_poll() failed\n");
			break;
		} else {
			ret = ipc_demux();
			if (ret < 0) {
				MSG_WARN("ipc_demux() failed\n");
			}
		}

		try_to_freeze();
	}

	dpram_close(filp);
	dpram_filp = NULL;
out:
	dpram_task = NULL;
	complete_and_exit(&dpram_complete, ret);
}

static int vsc_open(struct tty_struct *tty, struct file *filp)
{
	dev_info_t *dev;
	
	MSG_LOW("enter \n");

	down(&dev_lock);
	dev = devtbl_get_dev_by_name(tty->driver->name);
	if (dev == NULL) {
		up(&dev_lock);
		return -ENODEV;
	}
	if (dev->open_count > 0) {
		MSG_WARN("%s has been already opened!!\n", dev->tty_name);
		up(&dev_lock);
		//return 0; // jspark test!!!
		return -EBUSY;
	}
	dev->open_count = 1;
	tty->driver_data = (void *)dev;
	tty->low_latency = 1;
	dev->tty = tty;
	up(&dev_lock);	

	return 0;
}

static void vsc_close(struct tty_struct *tty, struct file *filp)
{
	dev_info_t *dev;

	MSG_LOW("enter \n");
	MSG_HIGH("ifname:[%s] \n",tty->driver->name);

	down(&dev_lock);
	dev = devtbl_get_dev_by_name(tty->driver->name);
	if (dev != NULL){
		dev->open_count = 0;
		dev->tty = NULL;
	}
	up(&dev_lock);	
}


static int vsc_write(struct tty_struct *tty,   const unsigned char *buf, int count)
{
	int ret;	
	
	dev_info_t *dev = (dev_info_t *)tty->driver_data;

	if(!dev)
		return -ENODEV;

	MSG_LOW("enter \n");

	MSG_HIGH("channel id: %u, count: %d\n",dev->id, count);

	ipcmux_dump(buf, count, TXIPC);

	ret = ipc_mux(dev, buf, count);
	if (ret == 0) 
	{
		ret = count;
	}
	
	return ret;
}
static int vsc_read(dev_info_t *dev, unsigned char* pdata, int len)
{
	struct tty_struct *tty = dev->tty;

	int alloc_buffer_size = 0 ;
	int remain_read_size = len ;
                                                                                      
	int offset =0;
	unsigned char *char_buf_ptr;

	MSG_LOW("enter \n");
	
	MSG_HIGH("channel id: %u, count: %d\n",dev->id, len);
	ipcmux_dump(pdata, len, RXIPC);
                                                                                      
	do {	
		if (tty){
			char_buf_ptr = NULL;
			alloc_buffer_size=tty_prepare_flip_string(tty, &char_buf_ptr, remain_read_size) ;
			MSG_LOW("offset=%d, remain_read_size=%d, alloc_buffer_size=%d \n", offset, remain_read_size, alloc_buffer_size);
			
			remain_read_size -= alloc_buffer_size ;

			memcpy(char_buf_ptr, pdata+offset, alloc_buffer_size);	
			offset += alloc_buffer_size;
			
			tty_flip_buffer_push(tty);
		}
		else{
			MSG_WARN("no tty!!\n");
			remain_read_size=0;
		}
	} while (remain_read_size > 0) ;

	return 0;
}

static int vsc_write_room(struct tty_struct *tty) 
{
	MSG_LOW("enter \n");

	return N_TTY_BUF_SIZE;
//	return TTY_FLIPBUF_SIZE;
}

static int vsc_chars_in_buffer(struct tty_struct *tty) 
{
	MSG_LOW("enter \n");

	return 0;
}

static int vsc_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	mm_segment_t oldfs;
	int ret;

	MSG_LOW("enter \n");

	MSG_LOW("ioctl command: 0x%04x\n", cmd);

	switch (cmd) 
	{
		case HN_DPRAM_PHONE_ON:
			MSG_LOW("HN_DPRAM_PHONE_ON\n");

			oldfs = get_fs(); set_fs(get_ds());
			ret = dpram_filp->f_op->unlocked_ioctl(dpram_filp,cmd, arg);			
			set_fs(oldfs);	
		
			return ret;

		case HN_DPRAM_PHONE_OFF:
			MSG_LOW("HN_DPRAM_PHONE_OFF\n");
			
			oldfs = get_fs(); set_fs(get_ds());
			ret = dpram_filp->f_op->unlocked_ioctl(dpram_filp,cmd, arg);
			set_fs(oldfs);				
		
			return ret;


		case HN_DPRAM_PHONE_GETSTATUS:
			MSG_LOW("HN_DPRAM_PHONE_GETSTATUS\n");

			oldfs = get_fs(); set_fs(get_ds());
			ret = dpram_filp->f_op->unlocked_ioctl(dpram_filp,cmd, arg);
			set_fs(oldfs);				
			
			return ret;

		default:
			break;
	}

	return -ENOIOCTLCMD;

}

static void vsc_break_ctl(struct tty_struct *tty, int break_state)
{
}


static const struct tty_operations vsc_ops =
{
	.open			= vsc_open,
	.close			= vsc_close,
	.write			= vsc_write,
	.write_room		= vsc_write_room,
	.chars_in_buffer	= vsc_chars_in_buffer,
	.ioctl				= vsc_ioctl,
	.break_ctl		= vsc_break_ctl,	
};

static int vsc_register_dev(dev_info_t *dev)
{
	struct tty_driver *mux_tty_driver; 

	MSG_LOW("enter \n");

	mux_tty_driver 				= &dev->mux_tty_driver;
	mux_tty_driver->minor_start 	= 0;
			
	mux_tty_driver->magic			= TTY_DRIVER_MAGIC;
	mux_tty_driver->driver_name	= "ipcmux";			// unique name. (#cat /proc/tty/drivers)
	mux_tty_driver->name			= dev->tty_name;	// node name. (# ls -al /dev/ or ls -al /sys/class/tty/)
	mux_tty_driver->major			= 0; //  auto assinment
	mux_tty_driver->num			= 1;
	mux_tty_driver->type			= TTY_DRIVER_TYPE_SERIAL;
	mux_tty_driver->subtype		= SERIAL_TYPE_NORMAL;
	mux_tty_driver->flags			= TTY_DRIVER_REAL_RAW;
	mux_tty_driver->ops			= &vsc_ops;
	mux_tty_driver->init_termios 	= tty_std_termios; // provides a set of line setting if the ports is used before it is initialized by a user

	return  tty_register_driver(mux_tty_driver);

}

static void vsc_unregister_dev(dev_info_t *dev)
{
	struct tty_driver *mux_tty_driver = NULL;
	
	MSG_LOW("enter \n");

	mux_tty_driver = &dev->mux_tty_driver;	

	tty_unregister_driver(mux_tty_driver);
}



static int ipc_mux(dev_info_t *dev, const void *data, size_t len)
{
	int 					ret;

 	const unsigned char	*ipc;
	unsigned short		ipc_len;
	
 	unsigned char 		*tx_buf;
	unsigned int			tx_buf_len = 0;

	unsigned short	hdlc_size; // hdlc_size = hdlc header size + info(ipc msg) size
	unsigned char 	start_flag, end_flag, ctrl_info, mfhdlc_ctrl;
	unsigned char 	endian_len[2] = {0,};
	int				mfhdlc_info_current_len = 0;
	int				mfhdlc_info_remain_len = 0;
	unsigned int		offset = 0;

	MSG_LOW("enter \n");

	tx_buf = dev->tx_buf;
	ipc_len = (unsigned short)len;
	
	//TODO: when ipc_len isn't equal to real buf size.....
	
	



	/**************************************************
	********              Making HDLC Frame 	 		*******
	***************************************************/

	start_flag = START_FLAG;
	end_flag = END_FLAG;
	ctrl_info = 0;
	hdlc_size =  HDLC_FRAME_HEADER_SIZE  + ipc_len;

	ctrl_info = hdlc_get_control_infoid();
	
	/* 	SFHDLC 	*/
	if (hdlc_size <= MAX_HDLC_FRAME_SIZE  )
	{
		ipc = data;
		endian_len[0] = hdlc_size & 0x00ff;
		endian_len[1] = (hdlc_size >> 8) & 0x00ff;

		offset = 0;
		memcpy(tx_buf, &start_flag, 1);	
		offset = offset + 1;
		memcpy(tx_buf+offset, &endian_len, 2);	
		offset = offset + 2;
		memcpy(tx_buf+offset, &ctrl_info, 1);			
		offset = offset + 1;
		memcpy(tx_buf+offset, ipc, ipc_len);			
		offset = offset + ipc_len;
		memcpy(tx_buf+offset, &end_flag, 1);			
		offset = offset + 1;
		tx_buf_len = offset;
	
		ret = dpram_write(dpram_filp, tx_buf, tx_buf_len, 1);
		if (ret < 0) 
		{
			MSG_HIGH("check plz.... ret:[%d]\n", ret);
			return ret;
		}

		
	}
	
	/*	MFHDLC	*/
	else
	{
		ipc = (unsigned char *)data;
		MSG_HIGH("This is MFHDLC frame !!!!!\n");
		
		mfhdlc_info_remain_len = ipc_len;
		
		while(mfhdlc_info_remain_len)
		{
			if (mfhdlc_info_remain_len > MAX_HDLC_INFO_SIZE)
			{
				mfhdlc_info_current_len = MAX_HDLC_INFO_SIZE;
				mfhdlc_ctrl = (1 << 7) | ctrl_info;
				hdlc_size = (unsigned short)MAX_HDLC_FRAME_SIZE ;
			}
			else
			{
				mfhdlc_info_current_len = mfhdlc_info_remain_len;
				mfhdlc_ctrl = ctrl_info;
				hdlc_size = (unsigned short)(mfhdlc_info_current_len + HDLC_FRAME_HEADER_SIZE);
			}

			endian_len[0] = hdlc_size & 0x00ff;
			endian_len[1] = (hdlc_size >> 8) & 0x00ff;


			offset = 0;
			memcpy(tx_buf, &start_flag, 1);	
			offset += 1;
			memcpy(tx_buf+offset, &endian_len, 2);	
			offset += 2;
			memcpy(tx_buf+offset, &mfhdlc_ctrl, 1);			
			offset += 1;
			memcpy(tx_buf+offset, ipc, mfhdlc_info_current_len);			
			offset += mfhdlc_info_current_len;
			memcpy(tx_buf+offset, &end_flag, 1);			
			offset += 1;
			tx_buf_len = offset;

			ret = dpram_write(dpram_filp, tx_buf, tx_buf_len, 1);
			if (ret < 0) 
			{
				MSG_HIGH("check plz.... ret:[%d]\n", ret);
				return ret;
			}
			
			ipc += mfhdlc_info_current_len;
			mfhdlc_info_remain_len -= mfhdlc_info_current_len;

			MSG_HIGH("writing hdlc info bytes:[%d], Remaining bytes [%d]\n", mfhdlc_info_current_len, mfhdlc_info_remain_len);
			
		}

		MSG_HIGH("Finished wriitng the MFHDLC to dpram\n");
		
	}
		
	return 0;		

}

static int ipc_demux(void)
{
	int 				ret;
	int 				info_len=0;
	char 			hdlc_len[2] = {0, };
	HDLCFrame_t		frame = {0,};

	dev_info_t *dev = NULL;
	int ch_id;
	unsigned char	*pipc;
	ipc_hdr_t	ipc_hdr;

	MSG_LOW("enter \n");		

	/* 	STEP #1 : read start flag - 1byte	&&  check for start flag ==  0x7E 		*/
	ret = dpram_read(dpram_filp, &(frame.m_StartMagicCode), 1);
	if (ret < 0) {
		MSG_WARN("dpram_read() failed: %d\n", ret);
		return ret;
	}
	
	if (frame.m_StartMagicCode != START_FLAG)
	{
		MSG_WARN("Invalid HDLC Frame Start flag : (0x7F != 0x%x)\n", frame.m_StartMagicCode);
		return -1;
	}


	/*	STEP #2 : Read the HDLC frame Len 2 Bytes . Low & High Byte 	*/
	ret = dpram_read(dpram_filp, &(hdlc_len[0]), 1);
	if (ret < 0) {
		MSG_WARN("dpram_read() failed: %d\n", ret);
		return ret;
	}
	ret = dpram_read(dpram_filp, &(hdlc_len[1]), 1);	
	if (ret < 0) {
		MSG_WARN("dpram_read() failed: %d\n", ret);
		return ret;
	}
	
	/*	copy the len to frame */    
	memcpy(&(frame.m_Length), hdlc_len, 2);
	if (frame.m_Length <= 0)
	{
		MSG_WARN("Invalid HDLC Frame Packet Length : %d\n ",frame.m_Length);
		return -1;
	}

	/*	STEP #3: Read the Control Byte 	*/
	ret = dpram_read(dpram_filp, &(frame.m_CtrlInfo), 1);	
	if (ret < 0) {
		MSG_WARN("dpram_read() failed: %d\n", ret);
		return ret;
	}
	
	MSG_LOW("HDLC frame Length:[%d], control:[%02x] \n", frame.m_Length, frame.m_CtrlInfo);

	/*	STEP #4: info_len = length - header len 	*/
	info_len = frame.m_Length - HDLC_FRAME_HEADER_SIZE;

	/*	STEP #5: read the data from dpram, and copy to frame data area.  	*/
	if (info_len > 0)
	{
		/*	 allocate memory by data size  */
		frame.m_pValue = kmalloc(info_len, GFP_KERNEL);
		if (frame.m_pValue == NULL) {
			MSG_WARN("out of memory\n");
			return -ENOMEM;
		}
		memset(frame.m_pValue, 0, info_len);	
		
		/*	read data from dpram 	*/
		ret = dpram_read(dpram_filp, frame.m_pValue, info_len);	
		if (ret < 0) {
			MSG_WARN("dpram_read() failed: %d\n", ret);
			return ret;
		}	
		
	}		
	else
	{
		frame.m_pValue = NULL;
		MSG_WARN("ipc len = 0 \n");
	}

	/*	STEP #6:  Read the End Flag  0x7E 	*/
	ret = dpram_read(dpram_filp, &(frame.m_EndMagicCode), 1);
	if (ret < 0) {
		MSG_WARN("dpram_read() failed: %d\n", ret);
		return ret;
	}
	
	if (frame.m_EndMagicCode != END_FLAG)
	{
		MSG_WARN("Invalid HDLC Frame End Flag : (0x7E != 0x%x)\n", frame.m_EndMagicCode);
		return -1;
	}

	/*======== 	End of HDLC frame	========*/            

	/*	save the last recv HDLC frame. */	
	s_FrameRecvInfo.m_CtrlInfo = frame.m_CtrlInfo;
	s_FrameRecvInfo.m_Length = frame.m_Length;	

	
	/*	Make an IPC Packet now from HDLC frame 	*/
	if (frame.m_pValue==NULL)
	{
		MSG_WARN("frame.m_pValue is NULL!!\n");
		return 0;
	}
	else
	{		
		pipc= hdlc_extract_ipc(&frame);
		
		if(pipc == NULL)
			return 0; // this is mfhdlc or ipc msg is NULL.

		memcpy(&ipc_hdr, pipc, IPC_HEADER_SIZE);

		ch_id =hdlc_get_chid_from_ipc(pipc);

		down(&dev_lock);
		dev = devtbl_get_dev_by_id(ch_id);	
		if (dev == NULL) {
			MSG_WARN("invalid channel id: %u\n", ch_id);
			ret = -ENODEV;
			goto err;
		}
		if (dev->tty == NULL) {
			MSG_WARN("channel id(%u) has been closed. ignore data\n", ch_id);
			ret = -1; 
			goto err;
		}


		ret = vsc_read(dev, pipc, ipc_hdr.len);
		if (ret < 0) {
			MSG_WARN("vsc_read() failed\n");			
			goto err;
		}
		up(&dev_lock);
		
		kfree(frame.m_pValue);

		return 0;
		
	}

	
err:

	up(&dev_lock);
	kfree(frame.m_pValue);

	return ret;	
}

static int ipcmux_activate(vsc_arg_t *vsc_arg)
{
	int ret;
	dev_info_t *dev;

	MSG_LOW("vsc_arg->id:[%d], vsc_arg->ifname:[%s]\n", vsc_arg->id, vsc_arg->ifname);

	dev = kmalloc(sizeof(dev_info_t) + MAX_HDLC_FRAME_SIZE_WITH_FLAGS, GFP_KERNEL); // it has replaced alloc_tty_driver().
	if (dev == NULL) {
		MSG_WARN("out of memory\n");
		return -ENOMEM;
	}
	memset(dev, 0, sizeof(dev_info_t)); 

	dev->id = vsc_arg->id;
	dev->tx_buf = (u_int8_t *)(dev + 1);

	//init_MUTEX(&dev->write_lock);
	strcpy(dev->tty_name, vsc_arg->ifname);

	ret = vsc_register_dev(dev);	
	if (ret < 0) {		 // it has replaced put_tty_driver()
		MSG_WARN("vsc_register_dev() failed\n");
		kfree(dev);
		return ret;
	}

	down(&dev_lock);
	ret = devtbl_add_dev(dev);
	if (ret < 0) {
		MSG_WARN("devtbl_add_dev() failed\n");
		up(&dev_lock);
		vsc_unregister_dev(dev);
		kfree(dev);
		return ret;
	}
	up(&dev_lock);	

	MSG_LOW("%s(id: %u) serial device is created.\n", dev->mux_tty_driver.name, dev->id);

	return 0;
}

static int ipcmux_deactivate(vsc_arg_t *vsc_arg)
{
	dev_info_t *dev = NULL;

	MSG_LOW("vsc_arg->id: %d\n", vsc_arg->id);

	down(&dev_lock);
	dev = devtbl_get_dev_by_id(vsc_arg->id);

	if (dev == NULL) {
		MSG_LOW("not found id: %u\n", vsc_arg->id);
		up(&dev_lock);		
		return -EINVAL;
	}

	devtbl_remove_dev_by_id(vsc_arg->id);
	up(&dev_lock);
	
	MSG_LOW("%s(id: %u) serial device is removed\n", dev->mux_tty_driver.name, dev->id);
	
	vsc_unregister_dev(dev);

	kfree(dev);

	return 0;
}

static void __exit ipcmux_cleanup(void)
{
	int slot;
	dev_info_t *dev;

	MSG_LOW("enter \n");


	down(&dev_lock);	
	for (slot = 0; slot < MAX_CHANNEL_NUMBER; slot++) 
	{
		dev = devtbl_remove_dev_by_slot(slot);
		if (dev) 
		{
			MSG_HIGH("%s(id: %u) serial device is removed\n", dev->mux_tty_driver.name, dev->id);
			vsc_unregister_dev(dev);
			kfree(dev);
		}
	}
	up(&dev_lock);	
	
}

static struct miscdevice ipcmux_dev = {
	.minor =	133,  //  refer to miscdevice.h 
	.name =	APP_DEVNAME,	
};

static int ipcmux_proc_read(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	char *p = page;
	int len;

	p += sprintf(p, "ipcmux driver is created on (date : %s, time: %s)\n", __DATE__, __TIME__);
	
	down(&dev_lock);	
	for (len = 0; len < MAX_CHANNEL_NUMBER; len++) {
		dev_info_t *dev = dev_table[len];
		if (!dev) continue;

		p += sprintf(p,
			     "name: %s\t, id: %-3u\n",
			     dev->tty_name,
			     dev->id);
	}
	up(&dev_lock);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}


static int __init ipcmux_init(void)
{
	int ret;
	int i,k;

	MSG_LOW("enter \n");

	init_completion(&dpram_complete);
	ret = kernel_thread(dpram_thread, NULL, CLONE_FS | CLONE_FILES);
	if (ret < 0) {
		MSG_WARN("kernel_thread() failed\n");
		return ret;
	}
	wait_for_completion(&dpram_complete);
	if (!dpram_task) {
		MSG_WARN("DPRAM I/O thread error\n");
		return -EIO;
	}

	for(i=0; i<MAX_CHANNEL_NUMBER; i++)
	{
		ret = ipcmux_activate(&g_vsc_arg[i]);
		if (ret < 0) 
		{
			MSG_WARN("failed to create a serial device for %s\n", g_vsc_arg[i].ifname);
			if(i>0)
			{
				for(k=0; k<i;k++)
				{
					ipcmux_deactivate(&g_vsc_arg[k]);
				}
			}
			goto err;
		}

	}

	ret = misc_register(&ipcmux_dev);
	if (ret < 0) {
		MSG_WARN("misc_register() failed\n");
		goto err;
	}
	
	create_proc_read_entry(APP_DEVNAME, 0, NULL, ipcmux_proc_read, NULL);


	MSG_HIGH("ipcmux intialization is completed.(date : %s, time: %s)!!\n", __DATE__, __TIME__);
	return 0;

err:
	MSG_WARN("error!!\n");
	if (dpram_task) {
		send_sig(SIGUSR1, dpram_task, 1);
		wait_for_completion(&dpram_complete);
	}
	return ret;
}

static void __exit ipcmux_exit(void)
{
	MSG_LOW("enter \n");

	remove_proc_entry(APP_DEVNAME, NULL);

	misc_deregister(&ipcmux_dev);

	ipcmux_cleanup();

	if (dpram_task) {
		send_sig(SIGUSR1, dpram_task, 1);
		wait_for_completion(&dpram_complete);
	}
}

module_init(ipcmux_init);
module_exit(ipcmux_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");
MODULE_DESCRIPTION("IPC Multiplexer / Demutilplexer");
MODULE_LICENSE("GPL");
