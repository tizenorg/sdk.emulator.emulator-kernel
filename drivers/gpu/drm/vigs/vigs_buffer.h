#ifndef _VIGS_BUFFER_H_
#define _VIGS_BUFFER_H_

#include "drmP.h"
#include <ttm/ttm_bo_driver.h>

struct vigs_mman;

struct vigs_buffer_object
{
    struct ttm_buffer_object base;

    u32 domain;

    /*
     * ttm_buffer_object::destroy isn't good enough for us because
     * we want to 'vigs_buffer_kunmap' before object destruction and
     * it's too late for that in ttm_buffer_object::destroy.
     */
    struct kref kref;

    /*
     * Valid only after successful call to 'vigs_buffer_kmap'.
     * @{
     */

    struct ttm_bo_kmap_obj kmap;
    void *kptr; /* Kernel pointer to buffer data. */

    /*
     * @}
     */
};

static inline struct vigs_buffer_object *bo_to_vigs_buffer(struct ttm_buffer_object *bo)
{
    return container_of(bo, struct vigs_buffer_object, base);
}

static inline struct vigs_buffer_object *kref_to_vigs_buffer(struct kref *kref)
{
    return container_of(kref, struct vigs_buffer_object, kref);
}

/*
 * when 'kernel' is true the buffer will be accessible from
 * kernel only.
 * 'domain' must be either VRAM or RAM. CPU domain is not supported.
 */
int vigs_buffer_create(struct vigs_mman *mman,
                       unsigned long size,
                       bool kernel,
                       u32 domain,
                       struct vigs_buffer_object **vigs_bo);

/*
 * Page aligned buffer size.
 */
static inline unsigned long vigs_buffer_size(struct vigs_buffer_object *vigs_bo)
{
    return vigs_bo->base.num_pages << PAGE_SHIFT;
}

/*
 * Actual size that was passed to 'vigs_buffer_create'.
 */
static inline unsigned long vigs_buffer_accounted_size(struct vigs_buffer_object *vigs_bo)
{
    return vigs_bo->base.acc_size;
}

/*
 * Buffer offset relative to 0.
 */
static inline unsigned long vigs_buffer_offset(struct vigs_buffer_object *vigs_bo)
{
    return vigs_bo->base.offset;
}

/*
 * Buffer offset relative to DRM_FILE_OFFSET. For kernel buffers it's always 0.
 */
static inline u64 vigs_buffer_mmap_offset(struct vigs_buffer_object *vigs_bo)
{
    return vigs_bo->base.addr_space_offset;
}

static inline void vigs_buffer_reserve(struct vigs_buffer_object *vigs_bo)
{
    int ret;

    ret = ttm_bo_reserve(&vigs_bo->base, false, false, false, 0);

    BUG_ON(ret != 0);
}

static inline void vigs_buffer_unreserve(struct vigs_buffer_object *vigs_bo)
{
    ttm_bo_unreserve(&vigs_bo->base);
}

/*
 * Functions below MUST NOT be called between
 * vigs_buffer_reserve/vigs_buffer_unreserve.
 * @{
 */

/*
 * Increments ref count.
 * Passing NULL won't hurt, this is for convenience.
 */
void vigs_buffer_acquire(struct vigs_buffer_object *vigs_bo);

/*
 * Decrements ref count, releases and sets 'vigs_bo' to NULL when 0.
 * Passing NULL won't hurt, this is for convenience.
 */
void vigs_buffer_release(struct vigs_buffer_object *vigs_bo);

/*
 * @}
 */

/*
 * Functions below MUST be called between
 * vigs_buffer_reserve/vigs_buffer_unreserve if simultaneous access
 * from different threads is possible.
 * @{
 */

int vigs_buffer_kmap(struct vigs_buffer_object *vigs_bo);

void vigs_buffer_kunmap(struct vigs_buffer_object *vigs_bo);

/*
 * @}
 */

#endif
