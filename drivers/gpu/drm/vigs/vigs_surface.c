#include "vigs_surface.h"
#include "vigs_device.h"
#include "vigs_comm.h"
#include <drm/vigs_drm.h>

static void vigs_surface_destroy(struct vigs_gem_object *gem)
{
    struct vigs_surface *sfc = vigs_gem_to_vigs_surface(gem);
    struct vigs_device *vigs_dev = gem->base.dev->dev_private;

    vigs_comm_destroy_surface(vigs_dev->comm, sfc->id);

    vigs_device_remove_surface(vigs_dev, sfc->id);

    vigs_gem_cleanup(&sfc->gem);
}

int vigs_surface_create(struct vigs_device *vigs_dev,
                        u32 width,
                        u32 height,
                        u32 stride,
                        vigsp_surface_format format,
                        struct vigs_surface **sfc)
{
    int ret = 0;

    *sfc = kzalloc(sizeof(**sfc), GFP_KERNEL);

    if (!*sfc) {
        ret = -ENOMEM;
        goto fail1;
    }

    (*sfc)->width = width;
    (*sfc)->height = height;
    (*sfc)->stride = stride;
    (*sfc)->format = format;

    ret = vigs_gem_init(&(*sfc)->gem,
                        vigs_dev,
                        VIGS_GEM_TYPE_SURFACE,
                        stride * height,
                        false,
                        &vigs_surface_destroy);

    if (ret != 0) {
        goto fail1;
    }

    ret = vigs_device_add_surface_unlocked(vigs_dev, *sfc, &(*sfc)->id);

    if (ret != 0) {
        goto fail2;
    }

    ret = vigs_comm_create_surface(vigs_dev->comm,
                                   width,
                                   height,
                                   stride,
                                   format,
                                   (*sfc)->id);

    if (ret != 0) {
        goto fail3;
    }

    return 0;

fail3:
    vigs_device_remove_surface_unlocked(vigs_dev, (*sfc)->id);
fail2:
    vigs_gem_cleanup(&(*sfc)->gem);
fail1:
    *sfc = NULL;

    return ret;
}

int vigs_surface_create_ioctl(struct drm_device *drm_dev,
                              void *data,
                              struct drm_file *file_priv)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct drm_vigs_create_surface *args = data;
    struct vigs_surface *sfc = NULL;
    uint32_t handle;
    int ret;

    ret = vigs_surface_create(vigs_dev,
                              args->width,
                              args->height,
                              args->stride,
                              args->format,
                              &sfc);

    if (ret != 0) {
        return ret;
    }

    ret = drm_gem_handle_create(file_priv,
                                &sfc->gem.base,
                                &handle);

    drm_gem_object_unreference_unlocked(&sfc->gem.base);

    if (ret == 0) {
        args->handle = handle;
        args->size = vigs_gem_size(&sfc->gem);
        args->mmap_offset = vigs_gem_mmap_offset(&sfc->gem);
        args->id = sfc->id;
    }

    return ret;
}

int vigs_surface_info_ioctl(struct drm_device *drm_dev,
                            void *data,
                            struct drm_file *file_priv)
{
    struct drm_vigs_surface_info *args = data;
    struct drm_gem_object *gem;
    struct vigs_gem_object *vigs_gem;
    struct vigs_surface *sfc;

    gem = drm_gem_object_lookup(drm_dev, file_priv, args->handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    if (vigs_gem->type != VIGS_GEM_TYPE_SURFACE) {
        drm_gem_object_unreference_unlocked(gem);
        return -ENOENT;
    }

    sfc = vigs_gem_to_vigs_surface(vigs_gem);

    args->width = sfc->width;
    args->height = sfc->height;
    args->stride = sfc->stride;
    args->format = sfc->format;
    args->size = vigs_gem_size(vigs_gem);
    args->mmap_offset = vigs_gem_mmap_offset(vigs_gem);
    args->id = sfc->id;

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}

int vigs_surface_set_dirty_ioctl(struct drm_device *drm_dev,
                                 void *data,
                                 struct drm_file *file_priv)
{
    struct drm_vigs_surface_set_dirty *args = data;
    struct drm_gem_object *gem;
    struct vigs_gem_object *vigs_gem;
    struct vigs_surface *sfc;

    gem = drm_gem_object_lookup(drm_dev, file_priv, args->handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    if (vigs_gem->type != VIGS_GEM_TYPE_SURFACE) {
        drm_gem_object_unreference_unlocked(gem);
        return -ENOENT;
    }

    sfc = vigs_gem_to_vigs_surface(vigs_gem);

    vigs_gem_reserve(&sfc->gem);

    if (vigs_gem_in_vram(&sfc->gem)) {
        sfc->is_dirty = true;
    }

    vigs_gem_unreserve(&sfc->gem);

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}
