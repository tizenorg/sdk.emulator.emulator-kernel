#include <linux/module.h>
#include <linux/moduleparam.h>

#include <linux/kernel.h>	/* printk(), min() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/proc_fs.h>
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/uaccess.h>

#define VDPRAM_MAJOR		249 /* 240 */
#define VDPRAM_NR_DEVS		2
#define VDPRAM_BUFFER		(16*1024)
#define VDPRAM_SEM_UNLOCK	100
#define VDPRAM_SEM_LOCK		200
#define VDPRAM_STATUS		300
#define VDPRAM_LOCK_ENABLE


/* #if 1
#define printk(...)
#endif
*/

MODULE_LICENSE("GPL");

struct buffer_t {
	char *begin;
	char *end;
	int buffersize;
	struct semaphore sem;
};

struct queue_t {
	wait_queue_head_t inq;
	wait_queue_head_t outq;
};

struct vdpram_dev {
	int flag;
	struct vdpram_dev *adj;
        char *rp, *wp;                     /* where to read, where to write */
        int nreaders, nwriters;            /* number of openings for r/w */
        struct fasync_struct *async_queue; /* asynchronous readers */
        struct cdev cdev;                  /* Char device structure */
	int index;
	int adj_index;
};

struct vdpram_status_dev {
        int index;
        char *rp, *wp;                     /* where to read, where to write */
        int rp_cnt;
        int wp_cnt;

        int adj_index;
        char *adj_rp, *adj_wp;                     /* where to read, where to write */
        int adj_rp_cnt;
        int adj_wp_cnt;
};


/* parameters */
static int vdpram_nr_devs = VDPRAM_NR_DEVS;	/* number of devices */
int vdpram_buffer = VDPRAM_BUFFER;		/* buffer size */
dev_t vdpram_devno;				/* Our first device number */
int vdpram_major = VDPRAM_MAJOR;

module_param(vdpram_nr_devs, int, 0);		/* FIXME check perms */
module_param(vdpram_buffer, int, 0);

static struct vdpram_dev *vdpram_devices;
static struct buffer_t *buffer;
static struct queue_t *queue;

static int vdpram_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct vdpram_dev *dev);

int vdpram_ioctl(struct inode *inode, struct file *filp,
                 unsigned int cmd,  unsigned long  arg);

/* Open and close */
static int vdpram_open(struct inode *inode, struct file *filp)
{
	struct vdpram_dev *dev;
	int index;



	dev = container_of(inode->i_cdev, struct vdpram_dev, cdev);
	filp->private_data = dev;
	index = dev->index ;

//	printk("%s:%d:index:%d\n", __FUNCTION__,current->pid,index);
	
#ifdef VDPRAM_LOCK_ENABLE
	if (down_interruptible(&buffer[index].sem))
		return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE

	dev->wp = dev->adj->rp = buffer[index].begin; /* rd and wr from the beginning */

	/* use f_mode,not  f_flags: it's cleaner (fs/open.c tells why) */
	if (filp->f_mode & FMODE_READ)
		dev->nreaders++;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters++;
#ifdef VDPRAM_LOCK_ENABLE
	up(&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE

	return nonseekable_open(inode, filp);
}



static int vdpram_release(struct inode *inode, struct file *filp)
{
	struct vdpram_dev *dev = filp->private_data;
	int index = dev->index ;

//	printk("%s:%d\n", __FUNCTION__,current->pid);

	/* remove this filp from the asynchronously notified filp's */
	vdpram_fasync(-1, filp, 0);
#ifdef VDPRAM_LOCK_ENABLE
	down(&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
	if (filp->f_mode & FMODE_READ)
		dev->nreaders--;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters--;
	if (dev->nreaders + dev->nwriters == 0) {
		//kfree(dev->buffer);
		//dev->buffer = NULL; /* the other fields are not checked on open */
	}
#ifdef VDPRAM_LOCK_ENABLE
	up(&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
	return 0;
}


/*
 * Data management: read and write
 */

static ssize_t vdpram_read (struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct vdpram_dev *dev = filp->private_data;
	int index = dev->adj_index ;
	char *curr_rp, *curr_adj_wp;
#if 0
	int i = 0; /* for debug */
#endif
//	printk("%s:%d start rp=%x adj_wp=%x \n", __FUNCTION__,current->pid, dev->rp, dev->adj->wp);
	
#ifdef VDPRAM_LOCK_ENABLE
	if (down_interruptible(&buffer[index].sem))
		return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE
	curr_rp = dev->rp;
	curr_adj_wp = dev->adj->wp ;
#ifdef VDPRAM_LOCK_ENABLE
	up(&buffer[index].sem); /* release the lock */
#endif //VDPRAM_LOCK_ENABLE

	while (curr_rp == curr_adj_wp) { /* nothing to read */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		if (wait_event_interruptible(queue[index].inq, (dev->adj->wp != dev->rp)))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
		/* otherwise loop, but first reacquire the lock */
#ifdef VDPRAM_LOCK_ENABLE
		if (down_interruptible(&buffer[index].sem))
			return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE
		curr_rp = dev->rp;
		curr_adj_wp = dev->adj->wp ;
#ifdef VDPRAM_LOCK_ENABLE
		up(&buffer[index].sem); /* release the lock */
#endif //VDPRAM_LOCK_ENABLE
	}
	/* ok, data is there, return something */
#ifdef VDPRAM_LOCK_ENABLE
	if (down_interruptible(&buffer[index].sem))
		return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE

	if (dev->adj->wp > dev->rp)
	{
		count = min(count, (size_t)(dev->adj->wp - dev->rp));
		if (copy_to_user(buf, dev->rp, count)) {
#ifdef VDPRAM_LOCK_ENABLE
			up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
			return -EFAULT;
		}

		dev->rp += count;
		if (dev->rp >= buffer[index].end)
			dev->rp = buffer[index].begin; /* wrapped */
	}
	else /* the write pointer has wrapped, return data up to dev->end */
	{
		int tmp_count ;
		
		tmp_count = min(count, (size_t)(buffer[index].end - dev->rp + 1));
		if (copy_to_user(buf, dev->rp,tmp_count)) {
#ifdef VDPRAM_LOCK_ENABLE
			up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
			return -EFAULT;
		}

		dev->rp += tmp_count;

		if ( count - tmp_count > 0)
		{
			tmp_count = min ( count - tmp_count, sizeof(dev->adj->wp - buffer[index].begin));
			if (copy_to_user(buf,dev->rp,tmp_count)) {
#ifdef VDPRAM_LOCK_ENABLE
				up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
				return -EFAULT;
			}
			dev->rp += tmp_count;
			
			if (dev->rp != dev->adj->wp)
				dev->rp = buffer[index].begin; /* wrapped */
		}
	}

#ifdef VDPRAM_LOCK_ENABLE
	up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE

#ifdef NOT_CIRCLE_QUEUE // hwjang del for circular queue
	if (copy_to_user(buf, dev->rp, count)) {
#ifdef VDPRAM_LOCK_ENABLE
		up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
		return -EFAULT;
	}

	dev->rp += count;
	if(dev->rp >= buffer[index].end)
		dev->rp = buffer[index].begin; /* wrapped */
#ifdef VDPRAM_LOCK_ENABLE
	up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
#endif //NOT_CIRCLE_QUEUE

//	printk("%s:%d rp[%d]=%d cnt=%d \n", __FUNCTION__,current->pid,dev->index, dev->rp-buffer[index].begin,count);
	/* finally, awake any writers and return */
	wake_up_interruptible(&queue[index].outq);
	return count;
}

/* Wait for space for writing; caller must hold device semaphore.  On
 * error the semaphore will be released before returning. */
static int vdpram_getwritespace(struct vdpram_dev *dev, struct file *filp)
{
	int ret ;
	int index = dev->index ;

	while (spacefree(dev) == 0) { /* full */
		DEFINE_WAIT(wait);
		
#ifdef VDPRAM_LOCK_ENABLE
		up(&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		prepare_to_wait(&queue[index].outq, &wait, TASK_INTERRUPTIBLE);
#ifdef VDPRAM_LOCK_ENABLE
		if (down_interruptible(&buffer[index].sem))
			return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE
		ret = spacefree(dev) ;
#ifdef VDPRAM_LOCK_ENABLE
		up(&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
		if (ret == 0)
			schedule();
		finish_wait(&queue[index].outq, &wait);
		if (signal_pending(current))
			return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
#ifdef VDPRAM_LOCK_ENABLE
		if (down_interruptible(&buffer[index].sem))
			return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE
	}
	return 0;
}	

/* How much space is free? */
static int spacefree(struct vdpram_dev *dev)
{
	int index = dev->index;
	if (dev->wp == dev->adj->rp)
		return buffer[index].buffersize - 1;
	return ((dev->adj->rp + buffer[index].buffersize - dev->wp) % buffer[index].buffersize) - 1;
}

static ssize_t vdpram_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
	struct vdpram_dev *dev = filp->private_data;
	int result;
	int index = dev->index ;

#ifdef VDPRAM_LOCK_ENABLE
	if (down_interruptible(&buffer[index].sem))
		return -ERESTARTSYS;
#endif //VDPRAM_LOCK_ENABLE

	/* Make sure there's space to write */
	result = vdpram_getwritespace(dev, filp);
	if (result)
		return result; /* vdpram_getwritespace called up(&dev->sem) */

	/* ok, space is there, accept something */
	count = min(count, (size_t)spacefree(dev));
	// hwjang need to be fixed for full circular queue
	if ( count > 0 )
	{
		if (dev->wp >= dev->adj->rp)
		{
			int tmp_count ;
			tmp_count = min(count, (size_t)(buffer[index].end - dev->wp)); /* to end-of-buf */

			if (tmp_count != 0 && copy_from_user(dev->wp, buf, tmp_count))
			{
#ifdef VDPRAM_LOCK_ENABLE
				up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
				return -EFAULT;
			}

			dev->wp += tmp_count;
			if (dev->wp > buffer[index].end  )
			{
//				printk("%s: back 0 !! \n",__FUNCTION__);
				dev->wp = buffer[index].begin; /* wrapped */
			}

			if (count-tmp_count > 0 )
			{
				tmp_count = min ( count - tmp_count, sizeof(dev->adj->rp - buffer[index].begin));
				if(copy_from_user(dev->wp, buf + tmp_count, tmp_count)) 
				{
#ifdef VDPRAM_LOCK_ENABLE
				up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
				return -EFAULT;
				}
			}
		}
		else /* the write pointer has wrapped, fill up to rp-1 */
		{
			count = min(count, (size_t)(dev->adj->rp - dev->wp - 1));

			if (copy_from_user(dev->wp, buf, count)) {
#ifdef VDPRAM_LOCK_ENABLE
				up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
				return -EFAULT;
			}

			dev->wp += count;
			if (dev->wp > buffer[index].end)
			{
//				printk("%s: back 0 !! \n",__FUNCTION__);
				dev->wp = buffer[index].begin; /* wrapped */
			}
		
		}
	}
	/* for debug */
#if 0
        printk("write[%d]: ", index);
        for(i=0; i<count; ++i) {
                printk("%x ", *(dev->wp+i));
        }
        printk("\n");
#endif

#ifdef NOT_CIRCLE_QUEUE // hwjang del for circular queue
	if (copy_from_user(dev->wp, buf, count)) {
#ifdef VDPRAM_LOCK_ENABLE
		up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
		return -EFAULT;
	}
	dev->wp += count;
	if (dev->wp == buffer[index].end)
		dev->wp = buffer[index].begin; /* wrapped */
#endif  // NOT_CIRCLE_QUEUE


#ifdef VDPRAM_LOCK_ENABLE
	up(&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
	
//	printk("%s:%d wp[%d]=%d, cnt=%d \n", __FUNCTION__,current->pid,dev->index, dev->wp -buffer[index].begin, count);
	/* finally, awake any reader */
	wake_up_interruptible(&queue[index].inq);  /* blocked in read() and select() */

	/* and signal asynchronous readers, explained late in chapter 5 */
	if (dev->async_queue)
		kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
	return count;
}

static unsigned int vdpram_poll(struct file *filp, poll_table *wait)
{
	struct vdpram_dev *dev = filp->private_data;
	unsigned int mask = 0;
	int index = dev->index ;

//	printk("%s:%d:index:%d\n", __FUNCTION__,current->pid,index);
	/*
	 * The buffer is circular; it is considered full
	 * if "wp" is right behind "rp" and empty if the
	 * two are equal.
	 */
	poll_wait(filp, &queue[dev->adj_index].inq,  wait);
	poll_wait(filp, &queue[dev->index].outq, wait);

#ifdef VDPRAM_LOCK_ENABLE
	down(&buffer[dev->adj_index].sem);
#endif //VDPRAM_LOCK_ENABLE
	if (dev->rp && dev->adj->wp && (dev->rp != dev->adj->wp))
		mask |= POLLIN | POLLRDNORM;	/* readable */
#ifdef VDPRAM_LOCK_ENABLE
	up(&buffer[dev->adj_index].sem);
#endif //VDPRAM_LOCK_ENABLE

#ifdef VDPRAM_LOCK_ENABLE
	down(&buffer[dev->index].sem);
#endif //VDPRAM_LOCK_ENABLE
	if (spacefree(dev))
		mask |= POLLOUT | POLLWRNORM;	/* writable */
#ifdef VDPRAM_LOCK_ENABLE
	up(&buffer[dev->index].sem);
#endif //VDPRAM_LOCK_ENABLE
//	printk("%s:%d:index:%d:end!!\n", __FUNCTION__,current->pid,index);
	return mask;
}


static int vdpram_fasync(int fd, struct file *filp, int mode)
{
	struct vdpram_dev *dev = filp->private_data;

	return fasync_helper(fd, filp, mode, &dev->async_queue);
}


/*
 * The file operations for the vdpram device
 */
struct file_operations vdpram_even_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		vdpram_read,
	.write =	vdpram_write,
	.poll =		vdpram_poll,
	.open =		vdpram_open,
	.release =	vdpram_release,
	.fasync =	vdpram_fasync,
	.ioctl =	vdpram_ioctl,
};

struct file_operations vdpram_odd_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.read =		vdpram_read,
	.write =	vdpram_write,
	.poll =		vdpram_poll,
	.open =		vdpram_open,
	.release =	vdpram_release,
	.fasync =	vdpram_fasync,
	.ioctl =	vdpram_ioctl,
};

int vdpram_ioctl(struct inode *inode, struct file *filp,
                 unsigned int cmd, unsigned long arg)
{
	struct vdpram_dev *dev;
	struct vdpram_status_dev dev_status;
	int index;

	dev = container_of(inode->i_cdev, struct vdpram_dev, cdev);
	filp->private_data = dev;
	index = dev->index ;

	memset( &dev_status, 0, sizeof(struct vdpram_status_dev));

	switch( cmd )
	{
		case VDPRAM_SEM_UNLOCK :
#ifdef VDPRAM_LOCK_ENABLE
			up (&buffer[index].sem);
#endif //VDPRAM_LOCK_ENABLE
			break;
		case VDPRAM_STATUS :
			dev_status.index = dev->index;
			dev_status.adj_index = dev->adj_index;

			dev_status.rp = dev->rp;
			dev_status.wp = dev->wp;
			dev_status.adj_rp = dev->rp;
			dev_status.adj_wp = dev->wp;

			if ( dev_status.rp != 0 )
				dev_status.rp_cnt = dev->rp-buffer[index].begin;

			if ( dev_status.wp != 0 )
				dev_status.wp_cnt = dev->wp-buffer[index].begin;

			dev_status.adj_rp = dev->adj->rp;
			dev_status.adj_wp = dev->adj->wp;

			if ( dev_status.adj_rp != 0 )
				dev_status.adj_rp_cnt = dev->adj->rp-buffer[dev->adj_index].begin;
			if ( dev_status.adj_wp != 0 )
				dev_status.adj_wp_cnt = dev->adj->wp-buffer[dev->adj_index].begin;

			
			if (copy_to_user((char*)arg, &dev_status, sizeof(struct vdpram_status_dev))) {
				return -EFAULT;
			}
		default : 
//			printk("%s[%d]:p=%d:cmd=%d\n", __FUNCTION__,index,current->pid,cmd);
			break;
	
	}
	return 0;
}

/*
 * Set up a cdev entry.
 */
static void vdpram_setup_cdev(struct vdpram_dev *dev, int index)
{
	int err, devno = vdpram_devno + index;
    	
	if (index % 2 ==0) {
		dev->flag = 1;
	 	cdev_init(&dev->cdev, &vdpram_even_fops);
		dev->cdev.owner = THIS_MODULE;
		err = cdev_add (&dev->cdev, devno, 1);
		dev->index = index;
	}
	else {
		dev->flag = 0;
	 	cdev_init(&dev->cdev, &vdpram_odd_fops);
		dev->cdev.owner = THIS_MODULE;
		err = cdev_add (&dev->cdev, devno, 1);
		dev->index = index;
	}
	
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding deive%d\n", err, index);
}

 

/* Initialize the devs; return how many we did */
int vdpram_init(void)
{
	int i, result;
//	dev_t dev;
	dev_t dev = MKDEV(vdpram_major, 0);

//	printk("Initializing vdpram device driver ...\n");
	result = register_chrdev_region(dev, vdpram_nr_devs, "vdpram");
//	result = alloc_chrdev_region(&dev, 0, vdpram_nr_devs, "vdpram");
	if (result < 0) {
		printk("Unable to get vdpram region, error %d\n", result);
		return 0;
	}
//	vdpram_major = MAJOR( dev );
	printk( " vdpram device major num = %d \n", vdpram_major );
	vdpram_devno = dev;
	vdpram_devices = kmalloc(vdpram_nr_devs * sizeof(struct vdpram_dev), GFP_KERNEL);
	if (vdpram_devices == NULL) {
		unregister_chrdev_region(dev, vdpram_nr_devs);
		return 0;
	}
	memset(vdpram_devices, 0, vdpram_nr_devs * sizeof(struct vdpram_dev));
	for (i = 0; i < vdpram_nr_devs; i++) {
		if (i% 2 ==1) {
			vdpram_devices[i].adj = &vdpram_devices[i-1];
			vdpram_devices[i-1].adj = &vdpram_devices[i];
// hwjang 
			vdpram_devices[i].adj->adj_index=i;
			vdpram_devices[i-1].adj->adj_index=i-1;
		}
		vdpram_setup_cdev(vdpram_devices + i, i);
	}

	buffer = kmalloc(vdpram_nr_devs * sizeof(struct buffer_t) , GFP_KERNEL);
	memset(buffer, 0, vdpram_nr_devs * sizeof(struct buffer_t));
	queue = kmalloc(vdpram_nr_devs * sizeof(struct queue_t) , GFP_KERNEL);
	// hwjang fix buffer -> queue
	memset(queue, 0, vdpram_nr_devs * sizeof(struct queue_t));
	for (i = 0; i < vdpram_nr_devs; i++) {
#ifdef VDPRAM_LOCK_ENABLE
		init_MUTEX(&buffer[i].sem);
#endif //VDPRAM_LOCK_ENABLE
		buffer[i].begin = kmalloc(vdpram_buffer, GFP_KERNEL);
		buffer[i].buffersize = vdpram_buffer;
		buffer[i].end = buffer[i].begin + buffer[i].buffersize;

		init_waitqueue_head(&queue[i].inq);
		init_waitqueue_head(&queue[i].outq);
//printk("%s buffer[%x].begin      =%x\n", __FUNCTION__, i,  buffer[i].begin );
//printk("%s buffer[%x].buffersize =%x\n", __FUNCTION__, i,  buffer[i].buffersize );
//printk("%s buffer[%x].end        =%x\n", __FUNCTION__, i,  buffer[i].end );
	} 

	return 0;
}

/*
 * This is called by cleanup_module or on failure.
 * It is required to never fail, even if nothing was initialized first
 */
void vdpram_cleanup(void)
{
	int i;

//	printk("%s:%d\n", __FUNCTION__,current->pid);
	
	if (!vdpram_devices)
		return; /* nothing else to release */

	for (i = 0; i < vdpram_nr_devs; i++) {
		cdev_del(&vdpram_devices[i].cdev);
	}
	kfree(vdpram_devices);

	for (i= 0;i < vdpram_nr_devs ; i++) {
		kfree(buffer[i].begin);
	}
	kfree(buffer);

	unregister_chrdev_region(vdpram_devno, vdpram_nr_devs);
	vdpram_devices = NULL; /* pedantic */
}

module_init(vdpram_init);
module_exit(vdpram_cleanup);
