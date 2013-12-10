#include "vigs_framebuffer.h"
#include "vigs_device.h"
#include "vigs_surface.h"
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
    struct vigs_gem_object *vigs_gem;
    struct vigs_surface *vigs_sfc;
    struct vigs_framebuffer *vigs_fb;
    int ret;

    DRM_DEBUG_KMS("enter\n");

    gem = drm_gem_object_lookup(drm_dev, file_priv, mode_cmd->handles[0]);

    if (!gem) {
        DRM_ERROR("GEM lookup failed, handle = %u\n", mode_cmd->handles[0]);
        return ERR_PTR(-ENOENT);
    }

    vigs_gem = gem_to_vigs_gem(gem);

    if (vigs_gem->type != VIGS_GEM_TYPE_SURFACE) {
        DRM_ERROR("GEM is not a surface, handle = %u\n", mode_cmd->handles[0]);
        drm_gem_object_unreference_unlocked(gem);
        return ERR_PTR(-ENOENT);
    }

    vigs_sfc = vigs_gem_to_vigs_surface(vigs_gem);

    ret = vigs_framebuffer_create(vigs_dev,
                                  mode_cmd,
                                  vigs_sfc,
                                  &vigs_fb);

    drm_gem_object_unreference_unlocked(gem);

    if (ret != 0) {
        DRM_ERROR("unable to create the framebuffer: %d\n", ret);
        return ERR_PTR(ret);
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
     * And we can finally free the GEM.
     */

    drm_gem_object_unreference_unlocked(&vigs_fb->fb_sfc->gem.base);
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

    return drm_gem_handle_create(file_priv, &vigs_fb->fb_sfc->gem.base, handle);
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
                            struct vigs_surface *fb_sfc,
                            struct vigs_framebuffer **vigs_fb)
{
    int ret = 0;

    DRM_DEBUG_KMS("enter\n");

    *vigs_fb = kzalloc(sizeof(**vigs_fb), GFP_KERNEL);

    if (!*vigs_fb) {
        ret = -ENOMEM;
        goto fail1;
    }

    if ((fb_sfc->width != mode_cmd->width) ||
        (fb_sfc->height != mode_cmd->height) ||
        (fb_sfc->stride != mode_cmd->pitches[0])) {
        DRM_DEBUG_KMS("surface format mismatch, sfc - (%u,%u,%u), mode - (%u,%u,%u)\n",
                      fb_sfc->width, fb_sfc->height, fb_sfc->stride,
                      mode_cmd->width, mode_cmd->height, mode_cmd->pitches[0]);
        ret = -EINVAL;
        goto fail2;
    }

    (*vigs_fb)->comm = vigs_dev->comm;
    (*vigs_fb)->fb_sfc = fb_sfc;

    ret = drm_framebuffer_init(vigs_dev->drm_dev,
                               &(*vigs_fb)->base,
                               &vigs_framebuffer_funcs);

    if (ret != 0) {
        goto fail2;
    }

    drm_helper_mode_fill_fb_struct(&(*vigs_fb)->base, mode_cmd);

    drm_gem_object_reference(&fb_sfc->gem.base);

    return 0;

fail2:
    kfree(*vigs_fb);
fail1:
    *vigs_fb = NULL;

    return ret;
}

int vigs_framebuffer_pin(struct vigs_framebuffer *vigs_fb)
{
    int ret;

    vigs_gem_reserve(&vigs_fb->fb_sfc->gem);

    ret = vigs_gem_pin(&vigs_fb->fb_sfc->gem);

    vigs_gem_unreserve(&vigs_fb->fb_sfc->gem);

    return ret;
}

void vigs_framebuffer_unpin(struct vigs_framebuffer *vigs_fb)
{
    vigs_gem_reserve(&vigs_fb->fb_sfc->gem);

    vigs_gem_unpin(&vigs_fb->fb_sfc->gem);

    vigs_gem_unreserve(&vigs_fb->fb_sfc->gem);
}
