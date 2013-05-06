#ifndef _VIGS_MMAN_H_
#define _VIGS_MMAN_H_

#include "drmP.h"
#include <ttm/ttm_bo_driver.h>

struct vigs_mman_ops
{
    /*
     * 'bo' is unreserved while calling these.
     */
    int (*map)(void *user_data, struct ttm_buffer_object *bo);
    void (*unmap)(void *user_data, struct ttm_buffer_object *bo);
    /*
     * @}
     */

    /*
     * 'bo' is reserved while calling these.
     * @{
     */
    void (*vram_to_gpu)(void *user_data, struct ttm_buffer_object *bo);
    void (*gpu_to_vram)(void *user_data, struct ttm_buffer_object *bo,
                        unsigned long new_offset);
    /*
     * @}
     */
};

struct vigs_mman
{
    struct drm_global_reference mem_global_ref;
    struct ttm_bo_global_ref bo_global_ref;
    struct ttm_bo_device bo_dev;

    resource_size_t vram_base;
    resource_size_t ram_base;

    struct vigs_mman_ops *ops;
    void *user_data;
};

static inline struct vigs_mman *bo_dev_to_vigs_mman(struct ttm_bo_device *bo_dev)
{
    return container_of(bo_dev, struct vigs_mman, bo_dev);
}

int vigs_mman_create(resource_size_t vram_base,
                     resource_size_t vram_size,
                     resource_size_t ram_base,
                     resource_size_t ram_size,
                     struct vigs_mman_ops *ops,
                     void *user_data,
                     struct vigs_mman **mman);

void vigs_mman_destroy(struct vigs_mman *mman);

int vigs_mman_mmap(struct vigs_mman *mman,
                   struct file *filp,
                   struct vm_area_struct *vma);

#endif
