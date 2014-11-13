/*
 * Copied from:
 *      drivers/gpu/drm/vmwgfx/vmwgfx_prime.c
 *
 */

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
