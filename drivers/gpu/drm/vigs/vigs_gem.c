#include "vigs_gem.h"
#include "vigs_device.h"
#include "vigs_mman.h"
#include "vigs_surface.h"
#include <drm/vigs_drm.h>
#include <ttm/ttm_placement.h>

static void vigs_gem_bo_destroy(struct ttm_buffer_object *bo)
{
    struct vigs_gem_object *vigs_gem = bo_to_vigs_gem(bo);

    kfree(vigs_gem);
}

int vigs_gem_init(struct vigs_gem_object *vigs_gem,
                  struct vigs_device *vigs_dev,
                  enum ttm_object_type type,
                  unsigned long size,
                  bool kernel,
                  vigs_gem_destroy_func destroy)
{
    u32 placements[1];
    struct ttm_placement placement;
    enum ttm_bo_type bo_type;
    int ret = 0;

    size = roundup(size, PAGE_SIZE);

    if (size == 0) {
        kfree(vigs_gem);
        return -EINVAL;
    }

    if (type == VIGS_GEM_TYPE_SURFACE) {
        placements[0] =
            TTM_PL_FLAG_CACHED | TTM_PL_FLAG_TT | TTM_PL_FLAG_NO_EVICT;
    } else if (type == VIGS_GEM_TYPE_EXECBUFFER) {
        placements[0] =
            TTM_PL_FLAG_CACHED | TTM_PL_FLAG_PRIV0 | TTM_PL_FLAG_NO_EVICT;
    } else {
        kfree(vigs_gem);
        return -EINVAL;
    }

    memset(&placement, 0, sizeof(placement));

    placement.placement = placements;
    placement.busy_placement = placements;
    placement.num_placement = 1;
    placement.num_busy_placement = 1;

    if (kernel) {
        bo_type = ttm_bo_type_kernel;
    } else {
        bo_type = ttm_bo_type_device;
    }

    if (unlikely(vigs_dev->mman->bo_dev.dev_mapping == NULL)) {
        vigs_dev->mman->bo_dev.dev_mapping = vigs_dev->drm_dev->dev_mapping;
    }

    ret = ttm_bo_init(&vigs_dev->mman->bo_dev, &vigs_gem->bo, size, bo_type,
                      &placement, 0, 0,
                      false, NULL, size,
                      &vigs_gem_bo_destroy);

    if (ret != 0) {
        return ret;
    }

    vigs_gem->type = type;
    vigs_gem->pin_count = 0;
    vigs_gem->destroy = destroy;

    ret = drm_gem_object_init(vigs_dev->drm_dev, &vigs_gem->base, size);

    if (ret != 0) {
        struct ttm_buffer_object *bo = &vigs_gem->bo;
        ttm_bo_unref(&bo);
        return ret;
    }

    DRM_DEBUG_DRIVER("GEM created (type = %u, off = 0x%llX, sz = %lu)\n",
                     type,
                     vigs_gem_mmap_offset(vigs_gem),
                     vigs_gem_size(vigs_gem));

    return 0;
}

void vigs_gem_cleanup(struct vigs_gem_object *vigs_gem)
{
    struct ttm_buffer_object *bo = &vigs_gem->bo;

    vigs_gem_reserve(vigs_gem);

    vigs_gem_kunmap(vigs_gem);

    vigs_gem_unreserve(vigs_gem);

    DRM_DEBUG_DRIVER("GEM destroyed (type = %u, off = 0x%llX, sz = %lu)\n",
                     vigs_gem->type,
                     vigs_gem_mmap_offset(vigs_gem),
                     vigs_gem_size(vigs_gem));

    drm_gem_object_release(&vigs_gem->base);
    ttm_bo_unref(&bo);
}

int vigs_gem_pin(struct vigs_gem_object *vigs_gem)
{
    u32 placements[1];
    struct ttm_placement placement;
    int ret;

    if (vigs_gem->pin_count) {
        ++vigs_gem->pin_count;
        return 0;
    }

    if (vigs_gem->type == VIGS_GEM_TYPE_EXECBUFFER) {
        return 0;
    }

    placements[0] =
        TTM_PL_FLAG_CACHED | TTM_PL_FLAG_VRAM | TTM_PL_FLAG_NO_EVICT;

    memset(&placement, 0, sizeof(placement));

    placement.placement = placements;
    placement.busy_placement = placements;
    placement.num_placement = 1;
    placement.num_busy_placement = 1;

    ret = ttm_bo_validate(&vigs_gem->bo, &placement, false, false, false);

    if (ret != 0) {
        DRM_ERROR("GEM pin failed (type = %u, off = 0x%llX, sz = %lu)\n",
                  vigs_gem->type,
                  vigs_gem_mmap_offset(vigs_gem),
                  vigs_gem_size(vigs_gem));
        return ret;
    }

    vigs_gem->pin_count = 1;

    DRM_DEBUG_DRIVER("GEM pinned (type = %u, off = 0x%llX, sz = %lu)\n",
                     vigs_gem->type,
                     vigs_gem_mmap_offset(vigs_gem),
                     vigs_gem_size(vigs_gem));

    return 0;
}

void vigs_gem_unpin(struct vigs_gem_object *vigs_gem)
{
    u32 placements[2];
    struct ttm_placement placement;
    int ret;

    BUG_ON(vigs_gem->pin_count == 0);

    if (--vigs_gem->pin_count > 0) {
        return;
    }

    if (vigs_gem->type == VIGS_GEM_TYPE_EXECBUFFER) {
        return;
    }

    vigs_gem_kunmap(vigs_gem);

    placements[0] =
        TTM_PL_FLAG_CACHED | TTM_PL_FLAG_VRAM;
    placements[1] =
        TTM_PL_FLAG_CACHED | TTM_PL_FLAG_TT | TTM_PL_FLAG_NO_EVICT;

    memset(&placement, 0, sizeof(placement));

    placement.placement = placements;
    placement.busy_placement = placements;
    placement.num_placement = 2;
    placement.num_busy_placement = 2;

    ret = ttm_bo_validate(&vigs_gem->bo, &placement, false, false, false);

    if (ret != 0) {
        DRM_ERROR("GEM unpin failed (type = %u, off = 0x%llX, sz = %lu)\n",
                  vigs_gem->type,
                  vigs_gem_mmap_offset(vigs_gem),
                  vigs_gem_size(vigs_gem));
    } else {
        DRM_DEBUG_DRIVER("GEM unpinned (type = %u, off = 0x%llX, sz = %lu)\n",
                         vigs_gem->type,
                         vigs_gem_mmap_offset(vigs_gem),
                         vigs_gem_size(vigs_gem));
    }
}

int vigs_gem_kmap(struct vigs_gem_object *vigs_gem)
{
    bool is_iomem;
    int ret;

    BUG_ON((vigs_gem->type == VIGS_GEM_TYPE_SURFACE) &&
           (vigs_gem->pin_count == 0));

    if (vigs_gem->kptr) {
        return 0;
    }

    ret = ttm_bo_kmap(&vigs_gem->bo,
                      0,
                      vigs_gem->bo.num_pages,
                      &vigs_gem->kmap);

    if (ret != 0) {
        return ret;
    }

    vigs_gem->kptr = ttm_kmap_obj_virtual(&vigs_gem->kmap, &is_iomem);

    DRM_DEBUG_DRIVER("GEM (type = %u, off = 0x%llX, sz = %lu) mapped to 0x%p\n",
                     vigs_gem->type,
                     vigs_gem_mmap_offset(vigs_gem),
                     vigs_gem_size(vigs_gem),
                     vigs_gem->kptr);

    return 0;
}

void vigs_gem_kunmap(struct vigs_gem_object *vigs_gem)
{
    if (vigs_gem->kptr == NULL) {
        return;
    }

    vigs_gem->kptr = NULL;

    ttm_bo_kunmap(&vigs_gem->kmap);

    DRM_DEBUG_DRIVER("GEM (type = %u, off = 0x%llX, sz = %lu) unmapped\n",
                     vigs_gem->type,
                     vigs_gem_mmap_offset(vigs_gem),
                     vigs_gem_size(vigs_gem));
}

void vigs_gem_free_object(struct drm_gem_object *gem)
{
    struct vigs_gem_object *vigs_gem = gem_to_vigs_gem(gem);

    vigs_gem->destroy(vigs_gem);
}

int vigs_gem_init_object(struct drm_gem_object *gem)
{
    return 0;
}

int vigs_gem_open_object(struct drm_gem_object *gem,
                         struct drm_file *file_priv)
{
    return 0;
}

void vigs_gem_close_object(struct drm_gem_object *gem,
                           struct drm_file *file_priv)
{
}

int vigs_gem_dumb_create(struct drm_file *file_priv,
                         struct drm_device *drm_dev,
                         struct drm_mode_create_dumb *args)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct vigs_surface *sfc = NULL;
    uint32_t handle;
    int ret;

    if (args->bpp != 32) {
        DRM_ERROR("Only 32 bpp surfaces are supported for now\n");
        return -EINVAL;
    }

    args->pitch = args->width * ((args->bpp + 7) / 8);

    ret = vigs_surface_create(vigs_dev,
                              args->width,
                              args->height,
                              args->pitch,
                              vigsp_surface_bgrx8888,
                              &sfc);

    if (ret != 0) {
        return ret;
    }

    args->size = vigs_gem_size(&sfc->gem);

    ret = drm_gem_handle_create(file_priv,
                                &sfc->gem.base,
                                &handle);

    drm_gem_object_unreference_unlocked(&sfc->gem.base);

    if (ret == 0) {
        args->handle = handle;
    }

    return 0;
}

int vigs_gem_dumb_destroy(struct drm_file *file_priv,
                          struct drm_device *drm_dev,
                          uint32_t handle)
{
    return drm_gem_handle_delete(file_priv, handle);
}

int vigs_gem_dumb_map_offset(struct drm_file *file_priv,
                             struct drm_device *drm_dev,
                             uint32_t handle, uint64_t *offset_p)
{
    struct drm_gem_object *gem;
    struct vigs_gem_object *vigs_gem;

    BUG_ON(!offset_p);

    gem = drm_gem_object_lookup(drm_dev, file_priv, handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    *offset_p = vigs_gem_mmap_offset(vigs_gem);

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}
