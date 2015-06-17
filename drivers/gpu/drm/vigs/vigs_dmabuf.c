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

#include "vigs_gem.h"
#include <linux/dma-buf.h>

static int vigs_dmabuf_attach(struct dma_buf *dma_buf,
                              struct device *target_dev,
                              struct dma_buf_attachment *attach)
{
    DRM_DEBUG_PRIME("enter");

    return -ENOSYS;
}

static void vigs_dmabuf_detach(struct dma_buf *dma_buf,
                               struct dma_buf_attachment *attach)
{
    DRM_DEBUG_PRIME("enter");
}

static struct sg_table *vigs_dmabuf_map(struct dma_buf_attachment *attach,
                                        enum dma_data_direction dir)
{
    DRM_DEBUG_PRIME("enter");

    return ERR_PTR(-ENOSYS);
}

static void vigs_dmabuf_unmap(struct dma_buf_attachment *attach,
                              struct sg_table *sgb,
                              enum dma_data_direction dir)
{
    DRM_DEBUG_PRIME("enter");
}

static void *vigs_dmabuf_kmap(struct dma_buf *dma_buf,
                              unsigned long page_num)
{
    DRM_DEBUG_PRIME("enter");

    return NULL;
}

static void *vigs_dmabuf_kmap_atomic(struct dma_buf *dma_buf,
                                     unsigned long page_num)
{
    DRM_DEBUG_PRIME("enter");

    return NULL;
}

static void vigs_dmabuf_kunmap(struct dma_buf *dma_buf,
                               unsigned long page_num,
                               void *addr)
{
    DRM_DEBUG_PRIME("enter");
}

static void vigs_dmabuf_kunmap_atomic(struct dma_buf *dma_buf,
                                      unsigned long page_num,
                                      void *addr)
{
    DRM_DEBUG_PRIME("enter");
}

static int vigs_dmabuf_mmap(struct dma_buf *dma_buf,
                            struct vm_area_struct *vma)
{
    DRM_DEBUG_PRIME("enter");

    return -ENOSYS;
}

static void *vigs_dmabuf_vmap(struct dma_buf *dma_buf)
{
    DRM_DEBUG_PRIME("enter");

    return NULL;
}

static void vigs_dmabuf_vunmap(struct dma_buf *dma_buf,
                               void *vaddr)
{
    DRM_DEBUG_PRIME("enter");
}

static int vigs_dmabuf_begin_cpu_access(struct dma_buf *dma_buf,
                                        size_t start,
                                        size_t length,
                                        enum dma_data_direction direction)
{
    DRM_DEBUG_PRIME("enter");

    return 0;
}

static void vigs_dmabuf_end_cpu_access(struct dma_buf *dma_buf,
                                       size_t start,
                                       size_t length,
                                       enum dma_data_direction direction)
{
    DRM_DEBUG_PRIME("enter");
}

const struct dma_buf_ops vigs_dmabuf_ops =  {
    .attach = vigs_dmabuf_attach,
    .detach = vigs_dmabuf_detach,
    .map_dma_buf = vigs_dmabuf_map,
    .unmap_dma_buf = vigs_dmabuf_unmap,
    .release = drm_gem_dmabuf_release,
    .kmap = vigs_dmabuf_kmap,
    .kmap_atomic = vigs_dmabuf_kmap_atomic,
    .kunmap = vigs_dmabuf_kunmap,
    .kunmap_atomic = vigs_dmabuf_kunmap_atomic,
    .mmap = vigs_dmabuf_mmap,
    .vmap = vigs_dmabuf_vmap,
    .vunmap = vigs_dmabuf_vunmap,
    .begin_cpu_access = vigs_dmabuf_begin_cpu_access,
    .end_cpu_access = vigs_dmabuf_end_cpu_access,
};

int vigs_prime_handle_to_fd(struct drm_device *dev,
                            struct drm_file *file_priv,
                            uint32_t handle,
                            uint32_t flags,
                            int *prime_fd)
{
    DRM_DEBUG_PRIME("enter");

    return drm_gem_prime_handle_to_fd(dev, file_priv, handle, flags, prime_fd);
}

int vigs_prime_fd_to_handle(struct drm_device *dev,
                            struct drm_file *file_priv,
                            int fd,
                            uint32_t *handle)
{
    DRM_DEBUG_PRIME("enter");

    return drm_gem_prime_fd_to_handle(dev, file_priv, fd, handle);
}

struct dma_buf *vigs_dmabuf_prime_export(struct drm_device *dev,
                                         struct drm_gem_object *gem_obj,
                                         int flags)
{
    struct vigs_gem_object *vigs_gem = gem_to_vigs_gem(gem_obj);

    DRM_DEBUG_PRIME("enter");

    return dma_buf_export(gem_obj,
                          &vigs_dmabuf_ops,
                          vigs_gem_size(vigs_gem),
                          flags);
}

struct drm_gem_object *vigs_dmabuf_prime_import(struct drm_device *dev,
                                                struct dma_buf *dma_buf)
{
    struct drm_gem_object *obj;

    DRM_DEBUG_PRIME("enter");

    if (dma_buf->ops == &vigs_dmabuf_ops) {
        obj = dma_buf->priv;

        if (obj->dev == dev) {
            /*
             * Importing dmabuf exported from our own gem increases
             * refcount on gem itself instead of f_count of dmabuf.
             */
            drm_gem_object_reference(obj);
            return obj;
        }
    }

    return ERR_PTR(-ENOSYS);
}
