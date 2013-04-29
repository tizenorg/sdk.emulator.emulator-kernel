#include "vigs_execbuffer.h"
#include <drm/vigs_drm.h>

static void vigs_execbuffer_destroy(struct vigs_gem_object *gem)
{
    struct vigs_execbuffer *execbuffer = vigs_gem_to_vigs_execbuffer(gem);

    vigs_gem_cleanup(&execbuffer->gem);
}

int vigs_execbuffer_create(struct vigs_device *vigs_dev,
                           unsigned long size,
                           bool kernel,
                           struct vigs_execbuffer **execbuffer)
{
    int ret = 0;

    *execbuffer = kzalloc(sizeof(**execbuffer), GFP_KERNEL);

    if (!*execbuffer) {
        ret = -ENOMEM;
        goto fail1;
    }

    ret = vigs_gem_init(&(*execbuffer)->gem,
                        vigs_dev,
                        VIGS_GEM_TYPE_EXECBUFFER,
                        size,
                        kernel,
                        &vigs_execbuffer_destroy);

    if (ret != 0) {
        goto fail1;
    }

    return 0;

fail1:
    *execbuffer = NULL;

    return ret;
}

int vigs_execbuffer_create_ioctl(struct drm_device *drm_dev,
                                 void *data,
                                 struct drm_file *file_priv)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct drm_vigs_create_execbuffer *args = data;
    struct vigs_execbuffer *execbuffer = NULL;
    uint32_t handle;
    int ret;

    ret = vigs_execbuffer_create(vigs_dev,
                                 args->size,
                                 false,
                                 &execbuffer);

    if (ret != 0) {
        return ret;
    }

    ret = drm_gem_handle_create(file_priv,
                                &execbuffer->gem.base,
                                &handle);

    drm_gem_object_unreference_unlocked(&execbuffer->gem.base);

    if (ret == 0) {
        args->size = vigs_gem_size(&execbuffer->gem);
        args->handle = handle;
        args->mmap_offset = vigs_gem_mmap_offset(&execbuffer->gem);
    }

    return ret;
}
