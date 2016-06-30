/*
 * MARU SDP Memory Driver
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd. All rights reserved.
 *
 * Contact:
 * Dongkyun Yun <dk77.yun@samsung.com>
 * Jinhyung Choi <jinh0.choi@samsung.com>
 * Hyunjin Lee <hyunjin816.lee@samsung.com>
 * SangHo Park <sangho.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *					Boston, MA  02110-1301, USA.
 *
 * Contributors:
 * - S-Core Co., Ltd
 *
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/miscdevice.h>
#include <linux/vmalloc.h>

#include "maru_dummy.h"

#define DRV_NAME			"sdp_mem"
#define SDP_MEM_MINOR		193

static LIST_HEAD(region_list);
static DEFINE_MUTEX(region_mutex);

struct sdp_mem_region
{
	struct list_head list;
	unsigned long vm_pgoff;
	unsigned long size;
	void *vaddr;
};

static int
sdp_mem_mmap(struct file * file, struct vm_area_struct * vma)
{
	int ret;
	unsigned long size = vma->vm_end - vma->vm_start;
	struct sdp_mem_region *region;
	int pages;

	maru_device_dbg(1, "[%d:%s] \n", current->pid, current->comm);

	mutex_lock(&region_mutex);

	list_for_each_entry(region, &region_list, list) {
		if( region->vm_pgoff == vma->vm_pgoff ) {
			if( region->size != size ) {
				maru_device_err("size mismatch \n");
				/* TODO: use first mapping size */
				size = region->size;
			}
			maru_device_dbg(1, "pgoff %lx found \n", vma->vm_pgoff);
			goto found;
		}
	}

	maru_device_dbg(1, "pgoff %lx not found \n", vma->vm_pgoff);

	region = kzalloc(sizeof(struct sdp_mem_region), GFP_KERNEL);
	if (!region) {
		maru_device_err("kzalloc fail \n");
		ret = -ENOMEM;
		goto error;
	}

	region->vm_pgoff = vma->vm_pgoff;
	region->size = size;

	pages = PAGE_ALIGN(size);
	region->vaddr = vmalloc_user(pages);
	if (!region->vaddr) {
		maru_device_err("vmalloc_user fail \n");
		kfree(region);
		ret = -ENOMEM;
		goto error;
	}
	list_add_tail(&region->list, &region_list);

	maru_device_dbg(1, "vaddr(%p) with size(%lu) \n", region->vaddr, size);

found:

	/* Try to remap memory */
	ret = remap_vmalloc_range(vma, region->vaddr, 0);
	if (ret) {
		maru_device_err("remap_vmalloc_range failed (ret:%d) \n", ret);
		goto error;
	}

	maru_device_info("%s/%d mmap phy addr(0x%lx, size:%lu) to 0x%p\n",
			current->comm, current->pid, vma->vm_pgoff, size, region->vaddr);

error:
	mutex_unlock(&region_mutex);
	return ret;
}

static int sdp_mem_open(struct inode *inode, struct file *file)
{
	maru_device_dbg(1, "open\n");
	return 0;
}


static int sdp_mem_close(struct inode *inode, struct file *file)
{
	maru_device_dbg(1, "close\n");
	return 0;
}

static const struct file_operations sdp_mem_fops = {
	.owner = THIS_MODULE,
	.open  = sdp_mem_open,
	.release = sdp_mem_close,
	.mmap = sdp_mem_mmap,
};

static struct miscdevice sdp_mem_dev = {
	.minor = SDP_MEM_MINOR,
	.name = DRV_NAME,
	.fops = &sdp_mem_fops
};

static int __init maru_sdp_mem_init(void)
{
	int ret_val = 0;

	ret_val = misc_register(&sdp_mem_dev);

	if(ret_val){
		maru_device_err("misc_register is failed.");
		return ret_val;
	}

	maru_device_info("sdp_mem initialized.");

	return ret_val;
}

static void __exit maru_sdp_mem_exit(void)
{
	misc_deregister(&sdp_mem_dev);
}

module_init(maru_sdp_mem_init);
module_exit(maru_sdp_mem_exit);
