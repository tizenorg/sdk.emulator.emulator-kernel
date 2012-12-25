#include "vigs_buffer.h"
#include "vigs_mman.h"
#include <drm/vigs_drm.h>
#include <ttm/ttm_placement.h>

static void vigs_buffer_destroy(struct kref *kref)
{
    struct vigs_buffer_object *vigs_bo = kref_to_vigs_buffer(kref);
    struct ttm_buffer_object *bo = &(vigs_bo->base);

    vigs_buffer_kunmap(vigs_bo);

    DRM_DEBUG_DRIVER("buffer destroyed (dom = %u, off = %lu, sz = %lu)\n",
                     vigs_bo->domain,
                     vigs_buffer_offset(vigs_bo),
                     vigs_buffer_accounted_size(vigs_bo));

    ttm_bo_unref(&bo);
}

static void vigs_buffer_base_destroy(struct ttm_buffer_object *bo)
{
    struct vigs_buffer_object *vigs_bo = bo_to_vigs_buffer(bo);

    kfree(vigs_bo);
}

int vigs_buffer_create(struct vigs_mman *mman,
                       unsigned long size,
                       bool kernel,
                       u32 domain,
                       struct vigs_buffer_object **vigs_bo)
{
    u32 placements[1];
    struct ttm_placement placement;
    enum ttm_bo_type type;
    int ret = 0;

    if (size == 0) {
        return -EINVAL;
    }

    *vigs_bo = NULL;

    if (domain == DRM_VIGS_GEM_DOMAIN_VRAM) {
        placements[0] =
            TTM_PL_FLAG_CACHED | TTM_PL_FLAG_VRAM | TTM_PL_FLAG_NO_EVICT;
    } else if (domain == DRM_VIGS_GEM_DOMAIN_RAM) {
        placements[0] =
            TTM_PL_FLAG_CACHED | TTM_PL_FLAG_PRIV0 | TTM_PL_FLAG_NO_EVICT;
    } else {
        return -EINVAL;
    }

    memset(&placement, 0, sizeof(placement));

    placement.placement = placements;
    placement.busy_placement = placements;
    placement.num_placement = 1;
    placement.num_busy_placement = 1;

    if (kernel) {
        type = ttm_bo_type_kernel;
    } else {
        type = ttm_bo_type_device;
    }

    *vigs_bo = kzalloc(sizeof(**vigs_bo), GFP_KERNEL);

    if (!*vigs_bo) {
        return -ENOMEM;
    }

    ret = ttm_bo_init(&mman->bo_dev, &(*vigs_bo)->base, size, type,
                      &placement, 0, 0,
                      false, NULL, size,
                      &vigs_buffer_base_destroy);

    if (ret != 0) {
        /*
         * '*vigs_bo' is freed by 'ttm_bo_init'
         */
        *vigs_bo = NULL;
        return ret;
    }

    (*vigs_bo)->domain = domain;

    kref_init(&(*vigs_bo)->kref);

    DRM_DEBUG_DRIVER("buffer created (dom = %u, off = %lu, sz = %lu)\n",
                     (*vigs_bo)->domain,
                     vigs_buffer_offset(*vigs_bo),
                     vigs_buffer_accounted_size(*vigs_bo));

    return 0;
}

void vigs_buffer_acquire(struct vigs_buffer_object *vigs_bo)
{
    if (vigs_bo) {
        kref_get(&vigs_bo->kref);
    }
}

void vigs_buffer_release(struct vigs_buffer_object *vigs_bo)
{
    if (vigs_bo) {
        kref_put(&vigs_bo->kref, vigs_buffer_destroy);
    }
}

int vigs_buffer_kmap(struct vigs_buffer_object *vigs_bo)
{
    bool is_iomem;
    int ret;

    if (vigs_bo->kptr) {
        return 0;
    }

    ret = ttm_bo_kmap(&vigs_bo->base,
                      0,
                      vigs_bo->base.num_pages,
                      &vigs_bo->kmap);

    if (ret != 0) {
        return ret;
    }

    vigs_bo->kptr = ttm_kmap_obj_virtual(&vigs_bo->kmap, &is_iomem);

    DRM_DEBUG_DRIVER("buffer (dom = %u, off = %lu, sz = %lu) mapped to %p\n",
                     vigs_bo->domain,
                     vigs_buffer_offset(vigs_bo),
                     vigs_buffer_accounted_size(vigs_bo),
                     vigs_bo->kptr);

    return 0;
}

void vigs_buffer_kunmap(struct vigs_buffer_object *vigs_bo)
{
    if (vigs_bo->kptr == NULL) {
        return;
    }

    vigs_bo->kptr = NULL;

    ttm_bo_kunmap(&vigs_bo->kmap);

    DRM_DEBUG_DRIVER("buffer (dom = %u, off = %lu, sz = %lu) unmapped\n",
                     vigs_bo->domain,
                     vigs_buffer_offset(vigs_bo),
                     vigs_buffer_accounted_size(vigs_bo));
}
