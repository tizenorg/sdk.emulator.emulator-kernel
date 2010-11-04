/*
 * Multiple PDP Mux / Demux Driver based on WinCE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/random.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/freezer.h>

#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/poll.h>
#include <linux/workqueue.h>

typedef struct pdp_arg {
	unsigned char	id;
	char		ifname[16];
} __attribute__ ((packed)) pdp_arg_t;

#define IOC_MZ2_MAGIC		(0xC1)

#define HN_PDP_ACTIVATE		_IOWR(IOC_MZ2_MAGIC, 0xe0, pdp_arg_t)
#define HN_PDP_DEACTIVATE	_IOW (IOC_MZ2_MAGIC, 0xe1, pdp_arg_t)
#define HN_PDP_ADJUST		_IOW (IOC_MZ2_MAGIC, 0xe2, int)

//#include <asm/hardware.h>
#include <asm/uaccess.h>

#define MULTIPDP_ERROR
#undef USE_LOOPBACK_PING

#ifdef USE_LOOPBACK_PING
#include <linux/ip.h>
#include <linux/icmp.h>
#include <net/checksum.h>
#endif

#ifdef MULTIPDP_ERROR
#define EPRINTK(X...) \
		do { \
			printk("%s(): ", __FUNCTION__); \
			printk(X); \
		} while (0)
#else
#define EPRINTK(X...)		do { } while (0)
#endif

#define CONFIG_MULTIPDP_DEBUG 1

#if (CONFIG_MULTIPDP_DEBUG > 0)
#define DPRINTK(N, X...) \
		do { \
			if (N <= CONFIG_MULTIPDP_DEBUG) { \
				printk("%s(): ", __FUNCTION__); \
				printk(X); \
			} \
		} while (0)
#else
#define DPRINTK(N, X...)	do { } while (0)
#endif

#define MAX_PDP_CONTEXT			10
#define MAX_PDP_DATA_LEN		1500
#define MAX_PDP_PACKET_LEN		(MAX_PDP_DATA_LEN + 4 + 2)
#define VNET_PREFIX			"pdp"
#define APP_DEVNAME			"multipdp"
#define DPRAM_DEVNAME			"/dev/dpram1"

#define DEV_TYPE_NET			0
#define DEV_TYPE_SERIAL			1

#define DEV_FLAG_STICKY			0x1

#define CSD_MAJOR_NUM			0
#define CSD_MINOR_NUM			0

#define DELAY_RETRY				10

struct pdp_hdr {
	u_int16_t	len;
	u_int8_t	id;
	u_int8_t	control;
} __attribute__ ((packed));

struct pdp_info {
	u_int8_t		id;
	unsigned		type;
	unsigned		flags;
	u_int8_t		*tx_buf;

	union {
		struct {
			struct net_device	*net;
			struct net_device_stats	stats;
			struct delayed_work	xmit_task;
		} vnet_u;

		struct {
			struct tty_driver	tty_driver[3];
			int			refcount;
			struct tty_struct	*tty_table[1];
			struct ktermios		*termios[1];
			struct ktermios		*termios_locked[1];
			char			tty_name[16];
			struct tty_struct	*tty;
			struct semaphore	write_lock;
		} vs_u;
	} dev_u;
#define vn_dev		dev_u.vnet_u
#define vs_dev		dev_u.vs_u
};

static struct pdp_info *pdp_table[MAX_PDP_CONTEXT];
static DECLARE_MUTEX(pdp_lock);

static struct task_struct *dpram_task;
static struct file *dpram_filp;
static DECLARE_COMPLETION(dpram_complete);

static int pdp_mux(struct pdp_info *dev, const void *data, size_t len  );
static int pdp_demux(void);
static inline struct pdp_info * pdp_get_serdev(const char *name);

static int g_adjust = 0;

#define PDP_BUFFER_SIZE 16
struct pdp_buffer {
	struct sk_buff *buf_arr[PDP_BUFFER_SIZE];
	int head;
	int tail;
};

static struct pdp_buffer pdp_buf;

static inline void vnet_buffer_init(void)
{
	pdp_buf.head = 0;
	pdp_buf.tail = PDP_BUFFER_SIZE - 1;
}

static inline int vnet_buffer_empty(void)
{
	int next_tail = pdp_buf.tail + 1;
	if (next_tail == PDP_BUFFER_SIZE)
		next_tail = 0;
	if (next_tail == pdp_buf.head)
		return 1;
	return 0;
}
static inline int vnet_buffer_add(struct sk_buff *skb)
{
	if (pdp_buf.head == pdp_buf.tail)
		return -1;
	pdp_buf.buf_arr[pdp_buf.head] = skb;
	pdp_buf.head++;
	if (pdp_buf.head == PDP_BUFFER_SIZE)
		pdp_buf.head = 0;
	return 0;
}

static inline struct sk_buff *vnet_buffer_extract(void)
{
	struct sk_buff *skb;
	int next_tail;
	if (vnet_buffer_empty())
		return NULL;
	next_tail = pdp_buf.tail + 1;
	if (next_tail == PDP_BUFFER_SIZE)
		next_tail = 0;

	skb = pdp_buf.buf_arr[next_tail];
	return skb;
}

static inline void vnet_buffer_move_tail(void)
{
	pdp_buf.tail++;
	if (pdp_buf.tail == PDP_BUFFER_SIZE)
		pdp_buf.tail = 0;
}

static inline struct file *dpram_open(void)
{
	int ret = 0;
	struct file *filp;
	struct termios termios;
	mm_segment_t oldfs;

	filp = filp_open(DPRAM_DEVNAME, O_RDWR, 0);
	if (IS_ERR(filp)) {
		DPRINTK(1, "filp_open() failed~!: %ld\n", PTR_ERR(filp));
		return NULL;
	}

	oldfs = get_fs(); set_fs(get_ds());
	ret = filp->f_op->unlocked_ioctl(filp, 
				TCGETA, (unsigned long)&termios);
	set_fs(oldfs);
	if (ret < 0) {
		DPRINTK(1, "f_op->ioctl() failed: %d\n", ret);
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
		DPRINTK(1, "f_op->ioctl() failed: %d\n", ret);
		filp_close(filp, current->files);
		return NULL;
	}

	return filp;
}

static inline void dpram_close(struct file *filp)
{
	filp_close(filp, current->files);
}

static inline int dpram_poll(struct file *filp)
{
	int ret;
	unsigned int mask;
	struct poll_wqueues wait_table;
	mm_segment_t oldfs;

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
			DPRINTK(1, "error in f_op->poll()\n");
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

static inline int dpram_write(struct file *filp, const void *buf, size_t count,
			      int nonblock)
{
	int ret, n = 0;
	mm_segment_t oldfs;

	while (count) {
		if (!dpram_filp) {
			DPRINTK(1, "DPRAM not available\n");
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

	while (count) {
		oldfs = get_fs(); set_fs(get_ds());
		ret = filp->f_op->read(filp, buf + n, count, &filp->f_pos);
		set_fs(oldfs);
		if (ret < 0) {
			if (ret == -EAGAIN) continue;
			DPRINTK(1, "f_op->read() failed: %d\n", ret);
			return ret;
		}
		n += ret;
		count -= ret;
	}
	return n;
}

static inline int dpram_flush_rx(struct file *filp, size_t count)
{
	int ret, n = 0;
	char *buf;
	mm_segment_t oldfs;

	buf = kmalloc(count, GFP_KERNEL);
	if (buf == NULL) return -ENOMEM;

	while (count) {
		oldfs = get_fs(); set_fs(get_ds());
		ret = filp->f_op->read(filp, buf + n, count, &filp->f_pos);
		set_fs(oldfs);
		if (ret < 0) {
			if (ret == -EAGAIN) continue;
			DPRINTK(1, "f_op->read() failed: %d\n", ret);
			kfree(buf);
			return ret;
		}
		n += ret;
		count -= ret;
	}
	kfree(buf);
	return n;
}


static int dpram_thread(void *data)
{
	int ret = 0;
	struct file *filp;

	dpram_task = current;

	daemonize("dpram_thread");
	
	strcpy(current->comm, "multipdp");

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
				DPRINTK(1, "got SIGUSR1 signal\n");
				sigdelset(&current->pending.signal, SIGUSR1);
				recalc_sigpending();
				ret = 0;
				break;
			}
		} else if (ret < 0) {
			EPRINTK("dpram_poll() failed\n");
			break;
		} else {
			ret = pdp_demux();
			if (ret < 0) {
				EPRINTK("pdp_demux() failed\n");
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

static int vnet_open(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->priv;
	INIT_DELAYED_WORK(&dev->vn_dev.xmit_task, NULL);
	netif_start_queue(net);
	return 0;
}

static int vnet_stop(struct net_device *net)
{
	netif_stop_queue(net);
	flush_scheduled_work();
	return 0;
}

static void vnet_defer_xmit(struct work_struct *data)
{
	int ret;
	struct sk_buff *skb = NULL;
	struct net_device *net = NULL;
	struct pdp_info *dev = NULL;
	
	if (vnet_buffer_empty())
		goto out;
	
	while (!vnet_buffer_empty()) {
		skb = (struct sk_buff *)vnet_buffer_extract();
		if (!skb)
			goto out;
		net = (struct net_device *)skb->dev;
		dev = (struct pdp_info *)net->priv;

		ret = pdp_mux(dev, skb->data, skb->len);
		if (ret < 0)
			goto out;
		else {
			vnet_buffer_move_tail();
			net->trans_start = jiffies;
			dev->vn_dev.stats.tx_bytes += skb->len;
			dev->vn_dev.stats.tx_packets++;
			dev_kfree_skb_any(skb);
		}
		netif_wake_queue(net);
	}

out:
	if (!vnet_buffer_empty()) {
		PREPARE_DELAYED_WORK(&dev->vn_dev.xmit_task, vnet_defer_xmit);
		schedule_delayed_work(&dev->vn_dev.xmit_task, DELAY_RETRY);
	}
}

static int vnet_start_xmit(struct sk_buff *skb, struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->priv;
	int ret;
#ifdef USE_LOOPBACK_PING
	struct sk_buff *skb2;
	struct icmphdr *icmph;
	struct iphdr *iph;
#endif

	DPRINTK(3, "id: %d, skb->len: %d\n", dev->id, skb->len);

#ifdef USE_LOOPBACK_PING
	dev->vn_dev.stats.tx_bytes += skb->len;
	dev->vn_dev.stats.tx_packets++;

	skb2 = alloc_skb(skb->len, GFP_ATOMIC);
	if (skb2 == NULL) {
		DPRINTK(1, "alloc_skb() failed\n");
		dev_kfree_skb_any(skb);
		return -ENOMEM;
	}

	memcpy(skb2->data, skb->data, skb->len);
	skb_put(skb2, skb->len);
	dev_kfree_skb_any(skb);

	icmph = (struct icmphdr *)(skb2->data + sizeof(struct iphdr));
	iph = (struct iphdr *)skb2->data;

	icmph->type = __constant_htons(ICMP_ECHOREPLY);

	ret = iph->daddr;
	iph->daddr = iph->saddr;
	iph->saddr = ret;
	iph->check = 0;
	iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);

	skb2->dev = net;
	skb2->protocol = __constant_htons(ETH_P_IP);

	netif_rx(skb2);

	dev->vn_dev.stats.rx_packets++;
	dev->vn_dev.stats.rx_bytes += skb->len;
#else
	ret = vnet_buffer_add(skb);
	if (ret < 0) {
		dev->vn_dev.stats.tx_dropped++;
		dev_kfree_skb_any(skb);
	}
	PREPARE_WORK(&dev->vn_dev.xmit_task.work, vnet_defer_xmit);
	schedule_work(&dev->vn_dev.xmit_task.work);
	if (!ret)
		netif_stop_queue(net);
#endif

	return 0;
}

static int vnet_recv(struct pdp_info *dev, size_t len)
{
	struct sk_buff *skb;
	int ret;

	if (!netif_running(dev->vn_dev.net)) {
		DPRINTK(1, "%s(id: %u) is not running\n", 
			dev->vn_dev.net->name, dev->id);
		return -ENODEV;
	}

	skb = alloc_skb(len, GFP_ATOMIC);
	if (skb == NULL) {
		DPRINTK(1, "alloc_skb() failed\n");
		return -ENOMEM;
	}

	ret = dpram_read(dpram_filp, skb->data, len);
	if (ret < 0) {
		DPRINTK(1, "dpram_read() failed: %d\n", ret);
		dev_kfree_skb_any(skb);
		return ret;
	}

	skb_put(skb, ret);

	skb->dev = dev->vn_dev.net;
	skb->protocol = __constant_htons(ETH_P_IP);

	netif_rx(skb);

	dev->vn_dev.stats.rx_packets++;
	dev->vn_dev.stats.rx_bytes += skb->len;
	return 0;
}

static struct net_device_stats *vnet_get_stats(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->priv;
	return &dev->vn_dev.stats;
}

static void vnet_tx_timeout(struct net_device *net)
{
	struct pdp_info *dev = (struct pdp_info *)net->priv;
	DPRINTK(3, "enter");
	net->trans_start = jiffies;
	dev->vn_dev.stats.tx_errors++;
	netif_wake_queue(net);
}

static struct net_device *vnet_add_dev(void *priv)
{
	int ret;
	struct net_device *net;

	net = kmalloc(sizeof(*net), GFP_KERNEL);
	if (net == NULL) {
		DPRINTK(1, "out of memory\n");
		return NULL;
	}
	memset(net, 0, sizeof(*net));

	strcpy(net->name, VNET_PREFIX "%d");
	net->open		= vnet_open;
	net->stop		= vnet_stop;
	net->hard_start_xmit	= vnet_start_xmit;
	net->get_stats		= vnet_get_stats;
	net->tx_timeout		= vnet_tx_timeout;
	net->type		= ARPHRD_PPP;
	net->hard_header_len 	= 0;
	net->mtu		= MAX_PDP_DATA_LEN;
	net->addr_len		= 0;
	net->tx_queue_len	= 1000;
	net->flags		= IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	net->watchdog_timeo	= 5 * HZ;
	net->priv		= priv;

	ret = register_netdev(net);
	if (ret != 0) {
		DPRINTK(1, "register_netdevice failed: %d\n", ret);
		kfree(net);
		return NULL;
	}

	return net;
}

static void vnet_del_dev(struct net_device *net)
{
	unregister_netdev(net);
	kfree(net);
}

static int vs_open(struct tty_struct *tty, struct file *filp)
{
	struct pdp_info *dev;

	DPRINTK(3, "enter\n");

	down(&pdp_lock);
	dev = pdp_get_serdev(tty->driver->name);
	if (dev == NULL) {
		up(&pdp_lock);
		return -ENODEV;
	}
	tty->driver_data = (void *)dev;
	tty->low_latency = 1;
	dev->vs_dev.tty = tty;
	up(&pdp_lock);

	return 0;
}

static void vs_close(struct tty_struct *tty, struct file *filp)
{
	struct pdp_info *dev;

	DPRINTK(3, "enter\n");

	down(&pdp_lock);
	dev = pdp_get_serdev(tty->driver->name);
	if (dev != NULL)
		dev->vs_dev.tty = NULL;
	up(&pdp_lock);
}


static int vs_write(struct tty_struct *tty,   const unsigned char *buf, int count)
{
	int ret;
	
	
	struct pdp_info *dev = (struct pdp_info *)tty->driver_data;

	DPRINTK(3, "id: %u, count: %d\n",dev->id, count);

	ret = pdp_mux(dev, buf, count);
	if (ret == 0) {
		ret = count;
	}
	
	return ret;
}

static int vs_write_room(struct tty_struct *tty) 
{
	return N_TTY_BUF_SIZE;
//	return TTY_FLIPBUF_SIZE;
}

static int vs_chars_in_buffer(struct tty_struct *tty) 
{
	return 0;
}

static int vs_read(struct pdp_info *dev, size_t len)
{
	struct tty_struct *tty = dev->vs_dev.tty;

	int remain_buffer_size = 0 ;
	int alloc_buffer_size = 0 ;
                                                                                      
	int remain_read_size = len ;
                                                                                      
	int ret = 0;
                                                                                      
	unsigned char *char_buf_ptr;
                                                                                      
	do {
		char_buf_ptr = NULL;
		alloc_buffer_size=tty_prepare_flip_string(tty, &char_buf_ptr, remain_read_size) ;
		remain_read_size -= alloc_buffer_size ;
                                                                                      
		remain_buffer_size = alloc_buffer_size ; 
		do {
			ret = dpram_read(dpram_filp, char_buf_ptr, remain_buffer_size);
			remain_buffer_size -= ret ;
			char_buf_ptr += ret ;
			if (ret < 0) {
				DPRINTK(1, "dpram_read() failed: %d\n", ret);
				return ret;
			}
		} while ( remain_buffer_size > 0 );
                                                                                      
		tty_flip_buffer_push(tty);
                                                                                      
	} while (remain_read_size > 0) ;

	return 0;
}

static int vs_ioctl(struct tty_struct *tty, struct file *file, 
		    unsigned int cmd, unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static void vs_break_ctl(struct tty_struct *tty, int break_state)
{
}

static const struct tty_operations vs_ops =
{
	.open			= vs_open,
	.close			= vs_close,
	.write			= vs_write,
	.write_room		= vs_write_room,
	.chars_in_buffer	= vs_chars_in_buffer,
	.ioctl			= vs_ioctl,
	.break_ctl		= vs_break_ctl,
};

static int vs_add_dev(struct pdp_info *dev)
{
	struct tty_driver *tty_driver;

	switch (dev->id) {
		case 1:
			tty_driver = &dev->vs_dev.tty_driver[0];
			tty_driver->minor_start = CSD_MINOR_NUM;
			break;

		case 25:
			tty_driver = &dev->vs_dev.tty_driver[1];
			tty_driver->minor_start = 1;
			break;

		case 5:
			tty_driver = &dev->vs_dev.tty_driver[2];
			tty_driver->minor_start = 2;
			break;

		default:
			tty_driver = NULL;
	}

	if (!tty_driver) {
		printk("tty driver is NULL!\n");
		return -1;
	}

	tty_driver->magic	= TTY_DRIVER_MAGIC;
	tty_driver->driver_name	= "multipdp";
	tty_driver->name	= dev->vs_dev.tty_name;
	tty_driver->major	= CSD_MAJOR_NUM;
	tty_driver->num		= 1;
	tty_driver->type	= TTY_DRIVER_TYPE_SERIAL;
	tty_driver->subtype	= SERIAL_TYPE_NORMAL;
	tty_driver->flags	= TTY_DRIVER_REAL_RAW;
	tty_driver->refcount	= dev->vs_dev.refcount;
	tty_driver->ttys	= dev->vs_dev.tty_table;
	tty_driver->termios	= dev->vs_dev.termios;
	tty_driver->termios_locked	= dev->vs_dev.termios_locked;
	tty_driver->ops		= &vs_ops;
	tty_driver->init_termios = tty_std_termios;

	return tty_register_driver(tty_driver);
}

static void vs_del_dev(struct pdp_info *dev)
{
	struct tty_driver *tty_driver = NULL;

	switch (dev->id) {
		case 1:
			tty_driver = &dev->vs_dev.tty_driver[0];
			break;

		case 25:
			tty_driver = &dev->vs_dev.tty_driver[1];
			break;

		case 5:
			tty_driver = &dev->vs_dev.tty_driver[2];
			break;
	}

	tty_unregister_driver(tty_driver);
}

static inline struct pdp_info * pdp_get_dev(u_int8_t id)
{
	int slot;

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		if (pdp_table[slot] && pdp_table[slot]->id == id) {
			return pdp_table[slot];
		}
	}
	return NULL;
}

static inline struct pdp_info * pdp_get_serdev(const char *name)
{
	int slot;
	struct pdp_info *dev;

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		dev = pdp_table[slot];
		if (dev && dev->type == DEV_TYPE_SERIAL &&
		    strcmp(name, dev->vs_dev.tty_name) == 0) {
			return dev;
		}
	}
	return NULL;
}

static inline int pdp_add_dev(struct pdp_info *dev)
{
	int slot;

	if (pdp_get_dev(dev->id)) {
		return -EBUSY;
	}

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		if (pdp_table[slot] == NULL) {
			pdp_table[slot] = dev;
			return slot;
		}
	}
	return -ENOSPC;
}

static inline struct pdp_info * pdp_remove_dev(u_int8_t id)
{
	int slot;
	struct pdp_info *dev;

	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		if (pdp_table[slot] && pdp_table[slot]->id == id) {
			dev = pdp_table[slot];
			pdp_table[slot] = NULL;
			return dev;
		}
	}
	return NULL;
}

static inline struct pdp_info * pdp_remove_slot(int slot)
{
	struct pdp_info *dev;

	dev = pdp_table[slot];
	pdp_table[slot] = NULL;
	return dev;
}

static int pdp_mux(struct pdp_info *dev, const void *data, size_t len   )
{
	int ret;
	size_t nbytes;
	u_int8_t *tx_buf;
	struct pdp_hdr *hdr;
	const u_int8_t *buf;

	tx_buf = dev->tx_buf;
	hdr = (struct pdp_hdr *)(tx_buf + 1);
	buf = data;

	hdr->id = dev->id;

	hdr->control = 0;

	while (len) {
		if (len > MAX_PDP_DATA_LEN) {
			nbytes = MAX_PDP_DATA_LEN;
		} else {
			nbytes = len;
		}
		hdr->len = nbytes + sizeof(struct pdp_hdr);

		tx_buf[0] = 0x7f;
		
		memcpy(tx_buf + 1 + sizeof(struct pdp_hdr), buf,  nbytes);
		
		tx_buf[1 + hdr->len] = 0x7e;

		DPRINTK(2, "hdr->id: %d, hdr->len: %d\n", hdr->id, hdr->len);

		ret = dpram_write(dpram_filp, tx_buf, hdr->len + 2, 1);
		if (ret < 0) {
			return ret;
		}
		buf += nbytes;
		len -= nbytes;
	}

	return 0;
}

static int pdp_demux(void)
{
	int ret;
	u_int8_t ch;
	size_t len;
	struct pdp_info *dev = NULL;
	struct pdp_hdr hdr;

	ret = dpram_read(dpram_filp, &ch, sizeof(ch));
	if (ret < 0) {
		DPRINTK(1, "dpram_read() failed: %d\n", ret);
		return ret;
	}
	if (ch != 0x7f) {
		DPRINTK(1, "invalid start byte: 0x%02x\n", ch);
		return -1;
	}

	ret = dpram_read(dpram_filp, &hdr, sizeof(hdr));
	if (ret < 0) {
		DPRINTK(1, "dpram_read() failed: %d\n", ret);
		return ret;
	}

	DPRINTK(2, "hdr->id: %d, hdr->len: %d\n", hdr.id, hdr.len);

	len = hdr.len - sizeof(struct pdp_hdr);

	down(&pdp_lock);
	dev = pdp_get_dev(hdr.id);
	if (dev == NULL) {
		DPRINTK(1, "invalid id: %u\n", hdr.id);
		ret = -ENODEV;
		goto err;
	}

	if (dev->type == DEV_TYPE_SERIAL && dev->vs_dev.tty == NULL) {
		DPRINTK(1, "closed serial id: %u. ignore data\n", hdr.id);
		ret = 0; // just ignore it.
		goto err;
	}

	if (dev->type == DEV_TYPE_NET) {
		ret = vnet_recv(dev, len);
		if (ret < 0) {
			DPRINTK(1, "vnet_recv() failed\n");
			goto err;
		}
	} else if (dev->type == DEV_TYPE_SERIAL) {
		ret = vs_read(dev, len);
		if (ret < 0) {
			DPRINTK(1, "vs_read() failed\n");
			goto err;
		}
	}
	up(&pdp_lock);

	ret = dpram_read(dpram_filp, &ch, sizeof(ch));
	if (ret < 0) {
		DPRINTK(1, "dpram_read() failed: %d\n", ret);
		return ret;
	}
	if (ch != 0x7e) {
		DPRINTK(1, "invalid stop byte: 0x%02x\n", ch);
		return -1;
	}
	return 0;

err:
	up(&pdp_lock);
	dpram_flush_rx(dpram_filp, len + 1);

	return ret;
}

static int pdp_activate(pdp_arg_t *pdp_arg, unsigned type, unsigned flags)
{
	int ret;
	struct pdp_info *dev;
	struct net_device *net;

	DPRINTK(2, "id: %d\n", pdp_arg->id);

	dev = kmalloc(sizeof(struct pdp_info) + MAX_PDP_PACKET_LEN, GFP_KERNEL);
	if (dev == NULL) {
		DPRINTK(1, "out of memory\n");
		return -ENOMEM;
	}
	memset(dev, 0, sizeof(struct pdp_info));

	if (type == DEV_TYPE_NET) {
		dev->id = pdp_arg->id + g_adjust;
	}

	else {
		dev->id = pdp_arg->id;
	}

	dev->type = type;
	dev->flags = flags;
	dev->tx_buf = (u_int8_t *)(dev + 1);

	if (type == DEV_TYPE_NET) {
		net = vnet_add_dev((void *)dev);
		if (net == NULL) {
			kfree(dev);
			return -ENOMEM;
		}

		dev->vn_dev.net = net;
		strcpy(pdp_arg->ifname, net->name);

		down(&pdp_lock);
		ret = pdp_add_dev(dev);
		if (ret < 0) {
			DPRINTK(1, "pdp_add_dev() failed\n");
			up(&pdp_lock);
			vnet_del_dev(dev->vn_dev.net);
			kfree(dev);
			return ret;
		}
		up(&pdp_lock);

		DPRINTK(1, "%s(id: %u) network device created\n", 
			net->name, dev->id);
	} else if (type == DEV_TYPE_SERIAL) {
		init_MUTEX(&dev->vs_dev.write_lock);
		strcpy(dev->vs_dev.tty_name, pdp_arg->ifname);

		ret = vs_add_dev(dev);
		if (ret < 0) {
			kfree(dev);
			return ret;
		}

		down(&pdp_lock);
		ret = pdp_add_dev(dev);
		if (ret < 0) {
			DPRINTK(1, "pdp_add_dev() failed\n");
			up(&pdp_lock);
			vs_del_dev(dev);
			kfree(dev);
			return ret;
		}
		up(&pdp_lock);

		if (dev->id == 1) {
			DPRINTK(1, "%s(id: %u) serial device is created.\n",
					dev->vs_dev.tty_driver[0].name, dev->id);
		}

		else if (dev->id == 25) {
			DPRINTK(1, "%s(id: %u) serial device is created.\n",
					dev->vs_dev.tty_driver[1].name, dev->id);
		}

		else if (dev->id == 5) {
			DPRINTK(1, "%s(id: %u) serial device is created.\n",
					dev->vs_dev.tty_driver[2].name, dev->id);
		}
	}

	return 0;
}

static int pdp_deactivate(pdp_arg_t *pdp_arg, int force)
{
	struct pdp_info *dev = NULL;

	DPRINTK(1, "id: %d\n", pdp_arg->id);

	down(&pdp_lock);

	if (pdp_arg->id == 1) {
		DPRINTK(1, "Channel ID is 1, we will remove the network device (pdp) of channel ID: %d.\n",
				pdp_arg->id + g_adjust);
	}

	else {
		DPRINTK(1, "Channel ID: %d\n", pdp_arg->id);
	}

	pdp_arg->id = pdp_arg->id + g_adjust;
	DPRINTK(1, "ID is adjusted, new ID: %d\n", pdp_arg->id);

	dev = pdp_get_dev(pdp_arg->id);

	if (dev == NULL) {
		DPRINTK(1, "not found id: %u\n", pdp_arg->id);
		up(&pdp_lock);
		return -EINVAL;
	}
	if (!force && dev->flags & DEV_FLAG_STICKY) {
		DPRINTK(1, "sticky id: %u\n", pdp_arg->id);
		up(&pdp_lock);
		return -EACCES;
	}

	pdp_remove_dev(pdp_arg->id);
	up(&pdp_lock);

	if (dev->type == DEV_TYPE_NET) {
		DPRINTK(1, "%s(id: %u) network device removed\n", 
			dev->vn_dev.net->name, dev->id);
		vnet_del_dev(dev->vn_dev.net);
	} else if (dev->type == DEV_TYPE_SERIAL) {
		if (dev->id == 1) {
			DPRINTK(1, "%s(id: %u) serial device removed\n",
					dev->vs_dev.tty_driver[0].name, dev->id);
		}

		else if (dev->id == 25) {
			DPRINTK(1, "%s(id: %u) serial device removed\n",
					dev->vs_dev.tty_driver[1].name, dev->id);
		}

		else if (dev->id == 5) {
			DPRINTK(1, "%s(id: %u) serial device removed\n",
					dev->vs_dev.tty_driver[2].name, dev->id);
		}

		vs_del_dev(dev);
	}

	kfree(dev);

	return 0;
}

static void __exit pdp_cleanup(void)
{
	int slot;
	struct pdp_info *dev;

	down(&pdp_lock);
	for (slot = 0; slot < MAX_PDP_CONTEXT; slot++) {
		dev = pdp_remove_slot(slot);
		if (dev) {
			if (dev->type == DEV_TYPE_NET) {
				DPRINTK(1, "%s(id: %u) network device removed\n", 
					dev->vn_dev.net->name, dev->id);
				vnet_del_dev(dev->vn_dev.net);
			} else if (dev->type == DEV_TYPE_SERIAL) {
				if (dev->id == 1) {
					DPRINTK(1, "%s(id: %u) serial device removed\n", 
							dev->vs_dev.tty_driver[0].name, dev->id);
				}

				else if (dev->id == 25) {
					DPRINTK(1, "%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver[1].name, dev->id);
				}

				else if (dev->id == 5) {
					DPRINTK(1, "%s(id: %u) serial device removed\n",
							dev->vs_dev.tty_driver[2].name, dev->id);
				}

				vs_del_dev(dev);
			}

			kfree(dev);
		}
	}
	up(&pdp_lock);
}

static int pdp_adjust(const int adjust)
{
	g_adjust = adjust;
	printk("adjusting value: %d\n", adjust);
	return 0;
}

static int multipdp_ioctl(struct inode *inode, struct file *file, 
			      unsigned int cmd, unsigned long arg)
{
	int ret, adjust;
	pdp_arg_t pdp_arg;

	switch (cmd) {
	case HN_PDP_ACTIVATE:
		if (copy_from_user(&pdp_arg, (void *)arg, sizeof(pdp_arg)))
			return -EFAULT;
		ret = pdp_activate(&pdp_arg, DEV_TYPE_NET, 0);
		if (ret < 0) {
			return ret;
		}
		return copy_to_user((void *)arg, &pdp_arg, sizeof(pdp_arg));

	case HN_PDP_DEACTIVATE:
		printk("1\n");
		if (copy_from_user(&pdp_arg, (void *)arg, sizeof(pdp_arg)))
			return -EFAULT;
		return pdp_deactivate(&pdp_arg, 0);

	case HN_PDP_ADJUST:
		if (copy_from_user(&adjust, (void *)arg, sizeof (int)))
			return -EFAULT;

		return pdp_adjust(adjust);
	}

	return -EINVAL;
}

static struct file_operations multipdp_fops = {
	.owner =	THIS_MODULE,
	.ioctl =	multipdp_ioctl,
	.llseek =	no_llseek,
};

static struct miscdevice multipdp_dev = {
	.minor =	132, 
	.name =		APP_DEVNAME,
	.fops =		&multipdp_fops,
};

#ifdef CONFIG_PROC_FS
static int multipdp_proc_read(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{

	char *p = page;
	int len;

	down(&pdp_lock);

	p += sprintf(p, "modified multipdp driver on 20070205\n");
	for (len = 0; len < MAX_PDP_CONTEXT; len++) {
		struct pdp_info *dev = pdp_table[len];
		if (!dev) continue;

		p += sprintf(p,
			     "name: %s\t, id: %-3u, type: %-7s, flags: 0x%04x\n",
			     dev->type == DEV_TYPE_NET ? 
			     dev->vn_dev.net->name : dev->vs_dev.tty_name,
			     dev->id, 
			     dev->type == DEV_TYPE_NET ? "network" : "serial",
			     dev->flags);
	}
	up(&pdp_lock);

	len = (p - page) - off;
	if (len < 0)
		len = 0;

	*eof = (len <= count) ? 1 : 0;
	*start = page + off;

	return len;
}
#endif

static int __init multipdp_init(void)
{
	int ret;
	pdp_arg_t pdp_arg = { .id = 1, .ifname = "ttyCSD", };
	pdp_arg_t router_arg = { .id = 25, .ifname = "ttyROUTER", };
	pdp_arg_t gps_arg = { .id = 5, .ifname = "ttyGPS", };

	vnet_buffer_init();

	init_completion(&dpram_complete);
	ret = kernel_thread(dpram_thread, NULL, CLONE_FS | CLONE_FILES);
	if (ret < 0) {
		EPRINTK("kernel_thread() failed\n");
		return ret;
	}
	wait_for_completion(&dpram_complete);
	if (!dpram_task) {
		EPRINTK("DPRAM I/O thread error\n");
		return -EIO;
	}

	ret = pdp_activate(&pdp_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		EPRINTK("failed to create a serial device for CSD\n");
		goto err0;
	}

	ret = pdp_activate(&router_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		EPRINTK("failed to create a serial device for ROUTER\n");
		goto err1;
	}

	ret = pdp_activate(&gps_arg, DEV_TYPE_SERIAL, DEV_FLAG_STICKY);
	if (ret < 0) {
		EPRINTK("failed to create a serial device for ROUTER\n");
		goto err1;
	}

	ret = misc_register(&multipdp_dev);
	if (ret < 0) {
		EPRINTK("misc_register() failed\n");
		goto err1;
	}

#ifdef CONFIG_PROC_FS
	create_proc_read_entry(APP_DEVNAME, 0, NULL,
			       multipdp_proc_read, NULL);
#endif

	printk(KERN_INFO 
	       "$Id: multipdp.c,v 1.1 2008/03/17 12:31:17 kdsoo Exp $\n");
	return 0;

err1:
	pdp_deactivate(&pdp_arg, 1);

err0:
	if (dpram_task) {
		send_sig(SIGUSR1, dpram_task, 1);
		wait_for_completion(&dpram_complete);
	}
	return ret;
}

static void __exit multipdp_exit(void)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry(APP_DEVNAME, NULL);
#endif

	misc_deregister(&multipdp_dev);

	pdp_cleanup();

	if (dpram_task) {
		send_sig(SIGUSR1, dpram_task, 1);
		wait_for_completion(&dpram_complete);
	}
}

module_init(multipdp_init);
module_exit(multipdp_exit);

MODULE_AUTHOR("SAMSUNG ELECTRONICS CO., LTD");
MODULE_DESCRIPTION("Multiple PDP Muxer / Demuxer");
MODULE_LICENSE("GPL");
