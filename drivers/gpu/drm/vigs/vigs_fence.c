#include "vigs_fence.h"
#include "vigs_fenceman.h"
#include "vigs_file.h"
#include "vigs_device.h"
#include "vigs_comm.h"
#include <drm/vigs_drm.h>

static void vigs_fence_destroy(struct vigs_fence *fence)
{
	fence_free(&fence->base);
}

static void vigs_user_fence_destroy(struct vigs_fence *fence)
{
    struct vigs_user_fence *user_fence = vigs_fence_to_vigs_user_fence(fence);

    ttm_base_object_kfree(user_fence, base);
}

static void vigs_fence_release(struct fence *f)
{
    struct vigs_fence *fence =
		container_of(f, struct vigs_fence, base);
	unsigned long flags;

    DRM_DEBUG_DRIVER("Fence destroyed (seq = %u, signaled = %u)\n",
                     f->seqno,
                     fence_is_signaled(f));

	spin_lock_irqsave(f->lock, flags);
    list_del_init(&fence->list);
	spin_unlock_irqrestore(f->lock, flags);
    fence->destroy(fence);
}

static void vigs_user_fence_refcount_release(struct ttm_base_object **base)
{
    struct ttm_base_object *tmp = *base;
    struct vigs_user_fence *user_fence = base_to_vigs_user_fence(tmp);

    vigs_fence_unref(&user_fence->fence);
    *base = NULL;
}

static const char* vigs_get_driver_name(struct fence *f)
{
    return "VIGS";
}

static const char *vigs_fence_get_timeline_name(struct fence *f)
{
    return "svga";
}

static bool vigs_fence_enable_signaling(struct fence *f)
{
    return true;
}

static signed long
vigs_fence_wait(struct fence *fence, bool intr, signed long timeout)
{
    // use default
    return fence_default_wait(fence, intr, timeout);
}

const struct fence_ops vigs_fence_ops = {
    .release = vigs_fence_release,
    .get_driver_name = vigs_get_driver_name,
    .get_timeline_name = vigs_fence_get_timeline_name,
    .enable_signaling = vigs_fence_enable_signaling,
    .wait = vigs_fence_wait,
};

static void vigs_fence_init(struct vigs_fence *fence,
                            struct vigs_fenceman *fenceman,
                            void (*destroy)(struct vigs_fence*))
{
    unsigned long flags;
    uint32_t seqno = vigs_fence_seq_next(&fenceman->seq);

    INIT_LIST_HEAD(&fence->list);
    fence->fenceman = fenceman;
    fence->destroy = destroy;

    spin_lock_irqsave(&fenceman->lock, flags);

    fence_init(&fence->base, &vigs_fence_ops, &fenceman->lock,
         fenceman->ctx, seqno);

    list_add_tail(&fence->list, &fenceman->fence_list);

    spin_unlock_irqrestore(&fenceman->lock, flags);

    DRM_DEBUG_DRIVER("Fence created (seq = %u)\n", fence->base.seqno);
}

int vigs_fence_create(struct vigs_fenceman *fenceman,
                      struct vigs_fence **fence)
{
    int ret = 0;

    *fence = kzalloc(sizeof(**fence), GFP_KERNEL);

    if (!*fence) {
        ret = -ENOMEM;
        goto fail1;
    }

    vigs_fence_init(*fence, fenceman, &vigs_fence_destroy);

    return 0;

fail1:
    *fence = NULL;

    return ret;
}

int vigs_user_fence_create(struct vigs_fenceman *fenceman,
                           struct drm_file *file_priv,
                           struct vigs_user_fence **user_fence,
                           uint32_t *handle)
{
    struct vigs_file *vigs_file = file_priv->driver_priv;
    int ret = 0;

    *user_fence = kzalloc(sizeof(**user_fence), GFP_KERNEL);

    if (!*user_fence) {
        ret = -ENOMEM;
        goto fail1;
    }

    vigs_fence_init(&(*user_fence)->fence, fenceman, &vigs_user_fence_destroy);

    vigs_fence_ref(&(*user_fence)->fence);
    ret = ttm_base_object_init(vigs_file->obj_file,
                               &(*user_fence)->base, false,
                               VIGS_FENCE_TYPE,
                               &vigs_user_fence_refcount_release,
                               NULL);
    if (ret != 0) {
        goto fail2;
    }

    /*
     * For ttm_base_object.
     */

    *handle = (*user_fence)->base.hash.key;

    return 0;

fail2:
    kfree(*user_fence);
fail1:
    *user_fence = NULL;

    return ret;
}

void vigs_fence_ref(struct vigs_fence *fence)
{
    if (fence)
        fence_get(&fence->base);
}

void vigs_fence_unref(struct vigs_fence *fence)
{
    if (fence)
        fence_put(&fence->base);
}

int vigs_fence_create_ioctl(struct drm_device *drm_dev,
                            void *data,
                            struct drm_file *file_priv)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct vigs_file *vigs_file = file_priv->driver_priv;
    struct drm_vigs_create_fence *args = data;
    struct vigs_user_fence *user_fence;
    uint32_t handle;
    int ret;

    ret = vigs_user_fence_create(vigs_dev->fenceman,
                                 file_priv,
                                 &user_fence,
                                 &handle);

    if (ret != 0) {
        goto out;
    }

    if (args->send) {
        ret = vigs_comm_fence(vigs_dev->comm, &user_fence->fence);

        if (ret != 0) {
            ttm_ref_object_base_unref(vigs_file->obj_file,
                                      handle,
                                      TTM_REF_USAGE);
            goto out;
        }
    }

    args->handle = handle;
    args->seq = user_fence->fence.base.seqno;

out:
    vigs_fence_unref(&user_fence->fence);

    return ret;
}

int vigs_fence_wait_ioctl(struct drm_device *drm_dev,
                          void *data,
                          struct drm_file *file_priv)
{
    struct vigs_file *vigs_file = file_priv->driver_priv;
    struct drm_vigs_fence_wait *args = data;
    struct ttm_base_object *base;
    struct vigs_user_fence *user_fence;
    int ret;

    base = ttm_base_object_lookup(vigs_file->obj_file, args->handle);

    if (!base) {
        return -ENOENT;
    }

    user_fence = base_to_vigs_user_fence(base);

    ret = fence_wait(&user_fence->fence.base, true);

    ttm_base_object_unref(&base);

    return ret;
}

int vigs_fence_signaled_ioctl(struct drm_device *drm_dev,
                              void *data,
                              struct drm_file *file_priv)
{
    struct vigs_file *vigs_file = file_priv->driver_priv;
    struct drm_vigs_fence_signaled *args = data;
    struct ttm_base_object *base;
    struct vigs_user_fence *user_fence;

    base = ttm_base_object_lookup(vigs_file->obj_file, args->handle);

    if (!base) {
        return -ENOENT;
    }

    user_fence = base_to_vigs_user_fence(base);

    args->signaled = fence_is_signaled(&user_fence->fence.base);

    ttm_base_object_unref(&base);

    return 0;
}

int vigs_fence_unref_ioctl(struct drm_device *drm_dev,
                           void *data,
                           struct drm_file *file_priv)
{
    struct vigs_file *vigs_file = file_priv->driver_priv;
    struct drm_vigs_fence_unref *args = data;

    return ttm_ref_object_base_unref(vigs_file->obj_file,
                                     args->handle,
                                     TTM_REF_USAGE);
}
