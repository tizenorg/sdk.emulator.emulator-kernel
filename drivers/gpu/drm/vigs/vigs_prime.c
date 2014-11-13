/**************************************************************************
 *
 * Based on drivers/gpu/drm/vmwgfx/vmwgfx_prime.c
 *
 * Copyright © 2013 VMware, Inc., Palo Alto, CA., USA
 * Copyright © 2014 Samsung Electronics Co., Ltd.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <linux/dma-buf.h>
#include <drm/ttm/ttm_object.h>

/*
 * DMA-BUF attach- and mapping methods. No need to implement
 * these until we have other virtual devices use them.
 */

static int vigs_prime_map_attach(struct dma_buf *dma_buf,
                                 struct device *target_dev,
                                 struct dma_buf_attachment *attach)
{
    return -ENOSYS;
}

static void vigs_prime_map_detach(struct dma_buf *dma_buf,
                                  struct dma_buf_attachment *attach)
{
}

static struct sg_table *vigs_prime_map_dma_buf(struct dma_buf_attachment *attach,
                                               enum dma_data_direction dir)
{
    return ERR_PTR(-ENOSYS);
}

static void vigs_prime_unmap_dma_buf(struct dma_buf_attachment *attach,
                                     struct sg_table *sgb,
                                     enum dma_data_direction dir)
{
}

static void *vigs_prime_dmabuf_vmap(struct dma_buf *dma_buf)
{
    return NULL;
}

static void vigs_prime_dmabuf_vunmap(struct dma_buf *dma_buf, void *vaddr)
{
}

static void *vigs_prime_dmabuf_kmap_atomic(struct dma_buf *dma_buf,
                                           unsigned long page_num)
{
    return NULL;
}

static void vigs_prime_dmabuf_kunmap_atomic(struct dma_buf *dma_buf,
                                            unsigned long page_num, void *addr)
{

}
static void *vigs_prime_dmabuf_kmap(struct dma_buf *dma_buf,
                                    unsigned long page_num)
{
    return NULL;
}

static void vigs_prime_dmabuf_kunmap(struct dma_buf *dma_buf,
		unsigned long page_num, void *addr)
{

}

static int vigs_prime_dmabuf_mmap(struct dma_buf *dma_buf,
                                  struct vm_area_struct *vma)
{
    WARN_ONCE(true, "Attempted use of dmabuf mmap. Bad.\n");
    return -ENOSYS;
}

const struct dma_buf_ops vigs_prime_dmabuf_ops =  {
    .attach = vigs_prime_map_attach,
    .detach = vigs_prime_map_detach,
    .map_dma_buf = vigs_prime_map_dma_buf,
    .unmap_dma_buf = vigs_prime_unmap_dma_buf,
    .release = NULL,
    .kmap = vigs_prime_dmabuf_kmap,
    .kmap_atomic = vigs_prime_dmabuf_kmap_atomic,
    .kunmap = vigs_prime_dmabuf_kunmap,
    .kunmap_atomic = vigs_prime_dmabuf_kunmap_atomic,
    .mmap = vigs_prime_dmabuf_mmap,
    .vmap = vigs_prime_dmabuf_vmap,
    .vunmap = vigs_prime_dmabuf_vunmap,
};
