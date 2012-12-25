#include "vigs_framebuffer.h"
#include "vigs_device.h"
#include "vigs_gem.h"
#include "vigs_fbdev.h"
#include "vigs_comm.h"
#include "drm_crtc_helper.h"
#include <drm/vigs_drm.h>

static struct drm_framebuffer *vigs_fb_create(struct drm_device *drm_dev,
                                              struct drm_file *file_priv,
                                              struct drm_mode_fb_cmd2 *mode_cmd)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct drm_gem_object *gem;
    struct vigs_framebuffer *vigs_fb;
    int ret;

    DRM_DEBUG_KMS("enter\n");

    gem = drm_gem_object_lookup(drm_dev, file_priv, mode_cmd->handles[0]);

    if (!gem) {
        DRM_ERROR("GEM lookup failed, handle = %u\n", mode_cmd->handles[0]);
        return NULL;
    }

    ret = vigs_framebuffer_create(vigs_dev,
                                  mode_cmd,
                                  gem_to_vigs_gem(gem),
                                  &vigs_fb);

    drm_gem_object_unreference_unlocked(gem);

    if (ret != 0) {
        DRM_ERROR("unable to create the framebuffer: %d\n", ret);
        return NULL;
    }

    return &vigs_fb->base;
}

static void vigs_output_poll_changed(struct drm_device *drm_dev)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;

    DRM_DEBUG_KMS("enter\n");

    if (vigs_dev->fbdev) {
        vigs_fbdev_output_poll_changed(vigs_dev->fbdev);
    }
}

static void vigs_framebuffer_destroy(struct drm_framebuffer *fb)
{
    struct vigs_framebuffer *vigs_fb = fb_to_vigs_fb(fb);

    DRM_DEBUG_KMS("enter\n");

    /*
     * First, we need to call 'drm_framebuffer_cleanup', this'll
     * automatically call 'vigs_crtc_disable' if needed, thus, notifying
     * the host that root surface is gone.
     */

    drm_framebuffer_cleanup(fb);

    /*
     * Here we can issue surface destroy command, since it's no longer
     * root surface, but it still exists on host.
     */

    vigs_comm_destroy_surface(vigs_fb->comm, vigs_fb->sfc_id);

    /*
     * And we can finally free the GEM.
     */

    drm_gem_object_unreference_unlocked(&vigs_fb->fb_gem->base);
    kfree(vigs_fb);
}

static int vigs_framebuffer_dirty(struct drm_framebuffer *fb,
                                  struct drm_file *file_priv,
                                  unsigned flags, unsigned color,
                                  struct drm_clip_rect *clips,
                                  unsigned num_clips)
{
    DRM_DEBUG_KMS("enter\n");

    return 0;
}

static int vigs_framebuffer_create_handle(struct drm_framebuffer *fb,
                                          struct drm_file *file_priv,
                                          unsigned int *handle)
{
    struct vigs_framebuffer *vigs_fb = fb_to_vigs_fb(fb);

    DRM_DEBUG_KMS("enter\n");

    return drm_gem_handle_create(file_priv, &vigs_fb->fb_gem->base, handle);
}

static struct drm_mode_config_funcs vigs_mode_config_funcs =
{
    .fb_create = vigs_fb_create,
    .output_poll_changed = vigs_output_poll_changed
};

static struct drm_framebuffer_funcs vigs_framebuffer_funcs =
{
    .destroy = vigs_framebuffer_destroy,
    .create_handle = vigs_framebuffer_create_handle,
    .dirty = vigs_framebuffer_dirty,
};

void vigs_framebuffer_config_init(struct vigs_device *vigs_dev)
{
    DRM_DEBUG_KMS("enter\n");

    vigs_dev->drm_dev->mode_config.min_width = 0;
    vigs_dev->drm_dev->mode_config.min_height = 0;

    vigs_dev->drm_dev->mode_config.max_width = 4096;
    vigs_dev->drm_dev->mode_config.max_height = 4096;

    vigs_dev->drm_dev->mode_config.funcs = &vigs_mode_config_funcs;
}

int vigs_framebuffer_create(struct vigs_device *vigs_dev,
                            struct drm_mode_fb_cmd2 *mode_cmd,
                            struct vigs_gem_object *fb_gem,
                            struct vigs_framebuffer **vigs_fb)
{
    int ret = 0;

    DRM_DEBUG_KMS("enter\n");

    *vigs_fb = kzalloc(sizeof(**vigs_fb), GFP_KERNEL);

    if (!*vigs_fb) {
        ret = -ENOMEM;
        goto fail1;
    }

    switch (mode_cmd->pixel_format) {
    case DRM_FORMAT_XRGB8888:
        (*vigs_fb)->format = vigsp_surface_bgrx8888;
        break;
    case DRM_FORMAT_ARGB8888:
        (*vigs_fb)->format = vigsp_surface_bgra8888;
        break;
    default:
        DRM_DEBUG_KMS("unsupported pixel format: %u\n", mode_cmd->pixel_format);
        ret = -EINVAL;
        goto fail2;
    }

    ret = vigs_comm_create_surface(vigs_dev->comm,
                                   mode_cmd->width,
                                   mode_cmd->height,
                                   mode_cmd->pitches[0],
                                   (*vigs_fb)->format,
                                   fb_gem,
                                   &(*vigs_fb)->sfc_id);

    if (ret != 0) {
        goto fail2;
    }

    (*vigs_fb)->comm = vigs_dev->comm;
    (*vigs_fb)->fb_gem = fb_gem;

    ret = drm_framebuffer_init(vigs_dev->drm_dev,
                               &(*vigs_fb)->base,
                               &vigs_framebuffer_funcs);

    if (ret != 0) {
        goto fail3;
    }

    drm_helper_mode_fill_fb_struct(&(*vigs_fb)->base, mode_cmd);

    drm_gem_object_reference(&fb_gem->base);

    return 0;

fail3:
    vigs_comm_destroy_surface(vigs_dev->comm, (*vigs_fb)->sfc_id);
fail2:
    kfree(*vigs_fb);
fail1:
    *vigs_fb = NULL;

    return ret;
}

int vigs_framebuffer_info_ioctl(struct drm_device *drm_dev,
                                void *data,
                                struct drm_file *file_priv)
{
    struct drm_vigs_fb_info *args = data;
    struct drm_mode_object *obj;
    struct drm_framebuffer *fb;
    struct vigs_framebuffer *vigs_fb;

    mutex_lock(&drm_dev->mode_config.mutex);

    obj = drm_mode_object_find(drm_dev, args->fb_id, DRM_MODE_OBJECT_FB);

    if (!obj) {
        mutex_unlock(&drm_dev->mode_config.mutex);
        return -ENOENT;
    }

    fb = obj_to_fb(obj);
    vigs_fb = fb_to_vigs_fb(fb);

    args->sfc_id = vigs_fb->sfc_id;

    mutex_unlock(&drm_dev->mode_config.mutex);

    return 0;
}
