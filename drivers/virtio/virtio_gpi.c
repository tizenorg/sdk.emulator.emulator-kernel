/* Virtio general purpose interface implementation
 *
 *  Copyright 2011 Dongkyun Yun
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/miscdevice.h>
#include <linux/virtio.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <asm/uaccess.h>

/* include/linux/virtio_balloon.h */
#define VIRTIO_GPI_F_MUST_TELL_HOST 0

#define DEVICE_NAME "virt_gpi"
#define DEVICE_MINOR_NUM 70

/* Define to use debugging checksums on transfers */
#undef DEBUG_EXIO

/* Enable debug messages. */
#define VIRTIO_GPI_DEBUG

#if defined(VIRTIO_GPI_DEBUG)
#define logout(fmt, ...) \
	printk(KERN_INFO "[virt_gpi][%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

struct virtio_gpi_data {
	int pid;

	int w_buf_size;
	char *w_buf;

	int r_buf_size;
	char *r_buf;
}__packed;

#define to_virtio_gpi_data(a)   ((struct virtio_gpi_data *)(a)->private_data)

static struct virtqueue *vq = NULL;

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPI, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* This is videobuf_vmalloc_to_sg() from videobuf-dma-sg.c with
 * some modifications
 */
static struct scatterlist *vmalloc_to_sg(struct scatterlist *sg_list,
		unsigned char *virt, unsigned int pages)
{
	struct page *pg;

	/* unaligned */
	BUG_ON((ulong)virt & ~PAGE_MASK);

	/* Fill with elements for the data */
	while (pages) {
		pg = vmalloc_to_page(virt);
		if (!pg)
			goto err;

		sg_set_page(sg_list, pg, PAGE_SIZE, 0);
		virt += PAGE_SIZE;
		sg_list++;
		pages--;
	}

	return sg_list;

err:
	kfree(sg_list);
	sg_list = NULL;
	return NULL;
}

static unsigned int put_data(struct virtio_gpi_data *exdata)
{
	struct scatterlist *sg;
	static struct scatterlist *sg_list = NULL;
	unsigned int ret, count, i_page, o_page, sg_entries;

	ret = exdata->w_buf_size;

	o_page = (exdata->w_buf_size + PAGE_SIZE-1) >> PAGE_SHIFT;
	i_page = (exdata->r_buf_size + PAGE_SIZE-1) >> PAGE_SHIFT;
	if (!o_page)
		o_page = 1;
	if (!i_page)
		i_page = 1;

	logout("add_buf(out:%ld, in:%ld) \n", o_page*PAGE_SIZE, i_page*PAGE_SIZE);

	sg_entries = o_page + i_page;

	sg_list = kcalloc(sg_entries, sizeof(struct scatterlist), GFP_KERNEL);
	if (!sg_list) {
		logout("kcalloc is fail ");
		ret = -EIO;
		goto out;
	}

	sg_init_table(sg_list, sg_entries);

	sg = vmalloc_to_sg(sg_list, exdata->w_buf, o_page);
	sg = vmalloc_to_sg(sg, exdata->r_buf, i_page);
	if (!sg) {
		logout("vmalloc_to_sq is fail ");
		ret = -EIO;
		goto out;
	}

	/* Transfer data */
	if (vq->vq_ops->add_buf(vq, sg_list, o_page, i_page, (void *)1) >= 0) {
		vq->vq_ops->kick(vq);
		/* Chill out until it's done with the buffer. */
		while (!vq->vq_ops->get_buf(vq, &count))
			cpu_relax();
	}

out:
	if (sg_list){
		kfree(sg_list);
		sg_list = NULL;
	}

	return ret;
}

static void free_buffer(struct virtio_gpi_data *exdata)
{
	if (exdata->w_buf) {
		exdata->w_buf_size = 0;
		vfree(exdata->w_buf);
		exdata->w_buf= NULL;
	}
	if (exdata->r_buf) {
		exdata->r_buf_size = 0;
		vfree(exdata->r_buf);
		exdata->r_buf= NULL;
	}
} 
static int virt_gpi_open(struct inode *inode, struct file *file)
{
	struct virtio_gpi_data *exdata = NULL;

	exdata = kzalloc(sizeof(struct virtio_gpi_data), GFP_KERNEL);
	if (!exdata)
		return -ENXIO;

	exdata->pid = pid_nr(task_pid(current));
	logout("pid(%d) inode(%p) file(%p) \n", exdata->pid, inode, file);

	file->private_data = exdata;

	return 0;
}

static int log_dump(char *buffer, int size)
{
	logout("buffer[%p] size[%d] ", buffer, size);

#if 0
	int i;
	unsigned char *ptr = (unsigned char*)buffer;

	logout("DATA BEGIN -------------- ");

	for(i=0; i < size; i++){
		logout(" %d =  %02x(%c) ", i, ptr[i], ptr[i]);
	}
	logout("DATA END  -------------- ");
#endif

	return 0;
}

static ssize_t virt_gpi_write(struct file *filp, const char *buffer,
		size_t size, loff_t * posp)
{
	struct virtio_gpi_data *exdata = to_virtio_gpi_data(filp);
	ssize_t ret;

	logout("filep(%p) size(%d) \n", filp, size);

	/* for now, just allow one buffer to write(). */
	if (exdata->w_buf){
		logout("just allow one buffer to write \n");
		return -EIO;
	}

	exdata->w_buf_size = size;
	exdata->w_buf = vmalloc(exdata->w_buf_size);
	if (!exdata->w_buf)
		return -ENOMEM;

	exdata->r_buf_size = size;
	exdata->r_buf = vmalloc(exdata->r_buf_size);
	if (!exdata->r_buf)
		return -ENOMEM;

	if (copy_from_user(exdata->w_buf, buffer, exdata->w_buf_size))
		return -EINVAL;

	logout("write data \n");
	log_dump(exdata->w_buf, exdata->w_buf_size);

	put_data(exdata);

	logout("return data \n");
	log_dump(exdata->r_buf, exdata->r_buf_size);

	ret = exdata->w_buf_size;

	return ret;
}

static ssize_t virt_gpi_read(struct file *filp, char __user *buffer,
		size_t size, loff_t * posp)
{
	struct virtio_gpi_data *exdata = to_virtio_gpi_data(filp);
	ssize_t ret;

	logout("file(%p) size(%d)  \n", filp, size);

	if (!exdata->r_buf){
		logout("read buffer is NULL \n");
		return -EPERM;
	}

	if (size > exdata->r_buf_size || size < 0){
		logout("read size is not correct \n");
		return -EINVAL;
	}

	if (copy_to_user(buffer, exdata->r_buf, size))
		return -EINVAL;

	log_dump(exdata->r_buf, exdata->r_buf_size);

	ret = size;

	return ret;
}

static int virt_gpi_release(struct inode *inode, struct file *file)
{
	struct virtio_gpi_data *exdata = to_virtio_gpi_data(file);

	logout("virio gpi release \n");

	if (exdata) {
		free_buffer(exdata);
	}

	kfree(exdata);
	return 0;
}

static const struct file_operations virt_gpi_fops = {
	.owner		= THIS_MODULE,
	.open		= virt_gpi_open,
	.write      = virt_gpi_write,
	.read		= virt_gpi_read,
	.release	= virt_gpi_release,
};

static struct miscdevice virt_gpi_dev = {
	DEVICE_MINOR_NUM,
	DEVICE_NAME,
	&virt_gpi_fops
};

static int virt_gpi_probe(struct virtio_device *vdev)
{
	int ret;

	logout("virtio gpi probe \n");

	/* We expect a single virtqueue. */
	vq = virtio_find_single_vq(vdev, NULL, "output");
	if (IS_ERR(vq))
		return PTR_ERR(vq);

	ret = misc_register(&virt_gpi_dev);
	if (ret) {
		printk(KERN_ERR "virt_gpi: cannot register virt_gpi_dev as misc");
		return -ENODEV;
	}

	return 0;
}

static void __devexit virt_gpi_remove(struct virtio_device *vdev)
{
	logout("\n");
	vdev->config->reset(vdev);
	misc_deregister(&virt_gpi_dev);
	vdev->config->del_vqs(vdev);
}

static void virt_gpi_changed(struct virtio_device *vdev)
{
	logout("\n");
	//struct virtio_balloon *vb = vdev->priv;
	//wake_up(&vb->config_change);
}


static unsigned int features[] = { VIRTIO_GPI_F_MUST_TELL_HOST };

static struct virtio_driver virtio_gpi = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virt_gpi_probe,
	.remove =	__devexit_p(virt_gpi_remove),
	.config_changed = virt_gpi_changed,
};

static int __init init(void)
{
	logout("virtio gpi init \n");
	return register_virtio_driver(&virtio_gpi);
}

static void __exit fini(void)
{
	logout("virtio gpi exit \n");
	unregister_virtio_driver(&virtio_gpi);
}

module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio general purpose driver");
MODULE_LICENSE("GPL v2");

