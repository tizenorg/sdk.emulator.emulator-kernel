#include "vigs_gem.h"
#include "vigs_buffer.h"
#include "vigs_device.h"
#include <drm/vigs_drm.h>

int vigs_gem_create(struct vigs_device *vigs_dev,
                    unsigned long size,
                    bool kernel,
                    u32 domain,
                    struct vigs_gem_object **vigs_gem)
{
    int ret = 0;

    size = roundup(size, PAGE_SIZE);

    if (size == 0) {
        return -EINVAL;
    }

    *vigs_gem = kzalloc(sizeof(**vigs_gem), GFP_KERNEL);

    if (!*vigs_gem) {
        ret = -ENOMEM;
        goto fail1;
    }

    ret = vigs_buffer_create(vigs_dev->mman,
                             size,
                             kernel,
                             domain,
                             &(*vigs_gem)->bo);

    if (ret != 0) {
        goto fail2;
    }

    ret = drm_gem_object_init(vigs_dev->drm_dev, &(*vigs_gem)->base, size);

    if (ret != 0) {
        goto fail3;
    }

    return 0;

fail3:
    vigs_buffer_release((*vigs_gem)->bo);
fail2:
    kfree(*vigs_gem);
fail1:
    *vigs_gem = NULL;

    return ret;
}

void vigs_gem_free_object(struct drm_gem_object *gem)
{
    struct vigs_gem_object *vigs_gem = gem_to_vigs_gem(gem);

    vigs_buffer_release(vigs_gem->bo);

    kfree(vigs_gem);
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
    struct vigs_gem_object *vigs_gem = NULL;
    uint32_t handle;
    int ret;

    args->pitch = args->width * ((args->bpp + 7) / 8);
    args->size = args->pitch * args->height;
    args->size = ALIGN(args->size, PAGE_SIZE);

    ret = vigs_gem_create(vigs_dev,
                          args->size,
                          false,
                          DRM_VIGS_GEM_DOMAIN_VRAM,
                          &vigs_gem);

    if (ret != 0) {
        return ret;
    }

    ret = drm_gem_handle_create(file_priv,
                                &vigs_gem->base,
                                &handle);

    drm_gem_object_unreference_unlocked(&vigs_gem->base);

    if (ret == 0) {
        args->handle = handle;
    }

    DRM_DEBUG_DRIVER("GEM %u created\n", handle);

    return 0;
}

int vigs_gem_dumb_destroy(struct drm_file *file_priv,
                          struct drm_device *drm_dev,
                          uint32_t handle)
{
    DRM_DEBUG_DRIVER("destroying GEM %u\n", handle);

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

    *offset_p = vigs_buffer_mmap_offset(vigs_gem->bo);

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}

int vigs_gem_create_ioctl(struct drm_device *drm_dev,
                          void *data,
                          struct drm_file *file_priv)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct drm_vigs_gem_create *args = data;
    struct vigs_gem_object *vigs_gem = NULL;
    uint32_t handle;
    int ret;

    ret = vigs_gem_create(vigs_dev,
                          args->size,
                          false,
                          args->domain,
                          &vigs_gem);

    if (ret != 0) {
        return ret;
    }

    ret = drm_gem_handle_create(file_priv,
                                &vigs_gem->base,
                                &handle);

    drm_gem_object_unreference_unlocked(&vigs_gem->base);

    if (ret == 0) {
        args->size = vigs_buffer_size(vigs_gem->bo);
        args->handle = handle;
        args->domain_offset = vigs_buffer_offset(vigs_gem->bo);
        DRM_DEBUG_DRIVER("GEM %u created\n", handle);
    }

    return ret;
}

int vigs_gem_mmap_ioctl(struct drm_device *drm_dev,
                        void *data,
                        struct drm_file *file_priv)
{
    struct drm_vigs_gem_mmap *args = data;
    struct drm_gem_object *gem;
    struct vigs_gem_object *vigs_gem;

    gem = drm_gem_object_lookup(drm_dev, file_priv, args->handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    args->offset = vigs_buffer_mmap_offset(vigs_gem->bo);

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}

int vigs_gem_info_ioctl(struct drm_device *drm_dev,
                        void *data,
                        struct drm_file *file_priv)
{
    struct drm_vigs_gem_info *args = data;
    struct drm_gem_object *gem;
    struct vigs_gem_object *vigs_gem;

    gem = drm_gem_object_lookup(drm_dev, file_priv, args->handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    args->domain = vigs_gem->bo->domain;
    args->domain_offset = vigs_buffer_offset(vigs_gem->bo);

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}
