/*
 * Copyright (C) 2010 Intel Corporation
 *
 * Author: Ian Molton <ian.molton@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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

// include/linux/virtio_ids.h
#define VIRTIO_ID_EXAMPLE   10 /* virtio example */
// include/linux/virtio_balloon.h
#define VIRTIO_EXAMPLE_F_MUST_TELL_HOST 0

#define DEVICE_NAME "virtexample"

/* Define to use debugging checksums on transfers */
#undef DEBUG_EXIO

/* Enable debug messages. */
#define VIRTIO_EX_DEBUG

#if defined(VIRTIO_EX_DEBUG)
#define logout(fmt, ...) \
	printk(KERN_INFO "[virtio][%s][%d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#else
#define logout(fmt, ...) ((void)0)
#endif

struct virtio_ex_data {
	char *buffer;
	int pages;
	unsigned int pid;
};

struct virtio_ex_header {
	int pid;
	int buf_size;
	int r_buf_size;
#ifdef DEBUG_EXIO
	int sum;
#endif
	char buffer;
} __packed;

#define to_virtio_ex_data(a)   ((struct virtio_ex_data *)(a)->private_data)

#ifdef DEBUG_EXIO
#define SIZE_OUT_HEADER (sizeof(int)*4)
#define SIZE_IN_HEADER (sizeof(int)*2)
#else
#define SIZE_OUT_HEADER (sizeof(int)*3)
#define SIZE_IN_HEADER sizeof(int)
#endif

static struct virtqueue *vq;

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_EXAMPLE, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

/* This is videobuf_vmalloc_to_sg() from videobuf-dma-sg.c with
 * some modifications
 */
static struct scatterlist *vmalloc_to_sg(struct scatterlist *sg_list,
		unsigned char *virt, unsigned int pages)
{
	struct page *pg;

	logout("\n");
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
	return NULL;
}

static int put_data(struct virtio_ex_data *exdata)
{
	struct scatterlist *sg, *sg_list;
	unsigned int count, ret, o_page, i_page, sg_entries;
	struct virtio_ex_header *header =
		(struct virtio_ex_header *)exdata->buffer;

	logout("\n");
	ret = header->buf_size;

	o_page = (header->buf_size + PAGE_SIZE-1) >> PAGE_SHIFT;
	i_page = (header->r_buf_size + PAGE_SIZE-1) >> PAGE_SHIFT;

	header->pid = exdata->pid;

	if ((o_page && i_page) &&
			(o_page > exdata->pages || i_page > exdata->pages)) {
		i_page = 0;
	}

	if (o_page > exdata->pages)
		o_page = exdata->pages;

	if (i_page > exdata->pages)
		i_page = exdata->pages;

	if (!o_page)
		o_page = 1;

	sg_entries = o_page + i_page;

	sg_list = kcalloc(sg_entries, sizeof(struct scatterlist), GFP_KERNEL);

	if (!sg_list) {
		ret = -EIO;
		goto out;
	}

	sg_init_table(sg_list, sg_entries);

	sg = vmalloc_to_sg(sg_list, exdata->buffer, o_page);
	sg = vmalloc_to_sg(sg, exdata->buffer, i_page);

	if (!sg) {
		ret = -EIO;
		goto out_free;
	}

	/* Transfer data */
	if (vq->vq_ops->add_buf(vq, sg_list, o_page, i_page, (void *)1) >= 0) {
		vq->vq_ops->kick(vq);
		/* Chill out until it's done with the buffer. */
		while (!vq->vq_ops->get_buf(vq, &count))
			cpu_relax();
	}

out_free:
	kfree(sg_list);
out:
	return ret;
}

static void free_buffer(struct virtio_ex_data *exdata)
{
	logout("\n");
	if (exdata->buffer) {
		vfree(exdata->buffer);
		exdata->buffer = NULL;
	}
}

static int virtexample_open(struct inode *inode, struct file *file)
{
	struct virtio_ex_data *exdata = NULL;

	logout("\n");

	exdata = kzalloc(sizeof(struct virtio_ex_data), GFP_KERNEL);
	if (!exdata)
		return -ENXIO;

	exdata->pid = pid_nr(task_pid(current));

	file->private_data = exdata;

	return 0;
}

static int virtexample_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct virtio_ex_data *exdata = to_virtio_ex_data(filp);
	int pages = (vma->vm_end - vma->vm_start) / PAGE_SIZE;

	logout("\n");

	/* Set a reasonable limit */
	if (pages > 16)
		return -ENOMEM;

	/* for now, just allow one buffer to be mmap()ed. */
	if (exdata->buffer)
		return -EIO;

	exdata->buffer = vmalloc_user(pages*PAGE_SIZE);

	if (!exdata->buffer)
		return -ENOMEM;

	exdata->pages = pages;

	if (remap_vmalloc_range(vma, exdata->buffer, 0) < 0) {
		vfree(exdata->buffer);
		return -EIO;
	}

	vma->vm_flags |= VM_DONTEXPAND;

	return 0;
}

static int virtexample_fsync(struct file *filp, int datasync)
{
	struct virtio_ex_data *exdata = to_virtio_ex_data(filp);

	logout("\n");
	put_data(exdata);

	return 0;
}

static int virtexample_release(struct inode *inode, struct file *file)
{
	struct virtio_ex_data *exdata = to_virtio_ex_data(file);

	logout("\n");
	if (exdata && exdata->buffer) {
		struct virtio_ex_header *header =
			(struct virtio_ex_header *)exdata->buffer;

		/* Make sure the host hears about the process ending / dying */
		header->pid = exdata->pid;
		header->buf_size = SIZE_OUT_HEADER + 2;
		header->r_buf_size = SIZE_IN_HEADER;
		*(short *)(&header->buffer) = -1;

		put_data(exdata);
		free_buffer(exdata);
	}

	kfree(exdata);

	return 0;
}

static const struct file_operations virtexample_fops = {
	.owner		= THIS_MODULE,
	.open		= virtexample_open,
	.mmap		= virtexample_mmap,
	.fsync		= virtexample_fsync,
	.release	= virtexample_release,
};

static struct miscdevice virtexample_dev = {
	MISC_DYNAMIC_MINOR,
	DEVICE_NAME,
	&virtexample_fops
};

static int virtexample_probe(struct virtio_device *vdev)
{
	int ret;

	logout("\n");

	/* We expect a single virtqueue. */
	vq = virtio_find_single_vq(vdev, NULL, "output");
	if (IS_ERR(vq))
		return PTR_ERR(vq);

	ret = misc_register(&virtexample_dev);
	if (ret) {
		printk(KERN_ERR "virtexample: cannot register virtexample_dev as misc");
		return -ENODEV;
	}

	return 0;
}

static void __devexit virtexample_remove(struct virtio_device *vdev)
{
	logout("\n");
	vdev->config->reset(vdev);
	misc_deregister(&virtexample_dev);
	vdev->config->del_vqs(vdev);
}

static void virtexample_changed(struct virtio_device *vdev)
{
	logout("\n");
	//struct virtio_balloon *vb = vdev->priv;
	//wake_up(&vb->config_change);
}


static unsigned int features[] = { VIRTIO_EXAMPLE_F_MUST_TELL_HOST };

static struct virtio_driver virtio_example = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtexample_probe,
	.remove =	__devexit_p(virtexample_remove),
	.config_changed = virtexample_changed,
};

static int __init init(void)
{
	logout("\n");
	return register_virtio_driver(&virtio_example);
}

static void __exit fini(void)
{
	logout("\n");
	unregister_virtio_driver(&virtio_example);
}

module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio example driver");
MODULE_LICENSE("GPL v2");

