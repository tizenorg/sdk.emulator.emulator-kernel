#include "vigs_crtc.h"
#include "vigs_device.h"
#include "vigs_framebuffer.h"
#include "vigs_surface.h"
#include "vigs_comm.h"
#include "drm_crtc_helper.h"

struct vigs_crtc
{
    struct drm_crtc base;
};

static inline struct vigs_crtc *crtc_to_vigs_crtc(struct drm_crtc *crtc)
{
    return container_of(crtc, struct vigs_crtc, base);
}

static void vigs_crtc_destroy(struct drm_crtc *crtc)
{
    struct vigs_crtc *vigs_crtc = crtc_to_vigs_crtc(crtc);

    DRM_DEBUG_KMS("enter");

    drm_crtc_cleanup(crtc);

    kfree(vigs_crtc);
}

static void vigs_crtc_dpms(struct drm_crtc *crtc, int mode)
{
    DRM_DEBUG_KMS("enter: mode = %d\n", mode);
}

static bool vigs_crtc_mode_fixup(struct drm_crtc *crtc,
                                 struct drm_display_mode *mode,
                                 struct drm_display_mode *adjusted_mode)
{
    DRM_DEBUG_KMS("enter\n");

    return true;
}

static int vigs_crtc_mode_set_base(struct drm_crtc *crtc, int x, int y,
                                   struct drm_framebuffer *old_fb)
{
    struct vigs_device *vigs_dev = crtc->dev->dev_private;
    struct vigs_framebuffer *vigs_fb;
    int ret;

    /*
     * New framebuffer has been attached, notify the host that
     * root surface has been updated.
     */

    DRM_DEBUG_KMS("enter: x = %d, y = %d\n", x, y);

    if (!crtc->fb) {
        DRM_ERROR("crtc->fb is NULL\n");
        return -EINVAL;
    }

    vigs_fb = fb_to_vigs_fb(crtc->fb);

    ret = vigs_framebuffer_pin(vigs_fb);

    if (ret != 0) {
        return ret;
    }

    ret = vigs_comm_set_root_surface(vigs_dev->comm,
                                     vigs_surface_id(vigs_fb->fb_sfc),
                                     vigs_gem_offset(&vigs_fb->fb_sfc->gem));

    if (ret != 0) {
        vigs_framebuffer_unpin(vigs_fb);
        return ret;
    }

    if (old_fb) {
        vigs_framebuffer_unpin(fb_to_vigs_fb(old_fb));
    }

    return 0;
}

static int vigs_crtc_mode_set(struct drm_crtc *crtc,
                              struct drm_display_mode *mode,
                              struct drm_display_mode *adjusted_mode,
                              int x, int y,
                              struct drm_framebuffer *old_fb)
{
    DRM_DEBUG_KMS("enter: x = %d, y = %d\n", x, y);

    return vigs_crtc_mode_set_base(crtc, x, y, old_fb);
}

static void vigs_crtc_prepare(struct drm_crtc *crtc)
{
    DRM_DEBUG_KMS("enter\n");
}

static void vigs_crtc_commit(struct drm_crtc *crtc)
{
    DRM_DEBUG_KMS("enter\n");
}

static void vigs_crtc_load_lut(struct drm_crtc *crtc)
{
}

static void vigs_crtc_disable(struct drm_crtc *crtc)
{
    struct vigs_device *vigs_dev = crtc->dev->dev_private;

    /*
     * Framebuffer has been detached, notify the host that
     * root surface is gone.
     */

    DRM_DEBUG_KMS("enter\n");

    if (!crtc->fb) {
        /*
         * No current framebuffer, no need to notify the host.
         */

        return;
    }

    vigs_comm_set_root_surface(vigs_dev->comm, 0, 0);

    vigs_framebuffer_unpin(fb_to_vigs_fb(crtc->fb));
}

static const struct drm_crtc_funcs vigs_crtc_funcs =
{
    .set_config = drm_crtc_helper_set_config,
    .destroy = vigs_crtc_destroy,
};

static const struct drm_crtc_helper_funcs vigs_crtc_helper_funcs =
{
    .dpms = vigs_crtc_dpms,
    .mode_fixup = vigs_crtc_mode_fixup,
    .mode_set = vigs_crtc_mode_set,
    .mode_set_base = vigs_crtc_mode_set_base,
    .prepare = vigs_crtc_prepare,
    .commit = vigs_crtc_commit,
    .load_lut = vigs_crtc_load_lut,
    .disable = vigs_crtc_disable,
};

int vigs_crtc_init(struct vigs_device *vigs_dev)
{
    struct vigs_crtc *vigs_crtc;
    int ret;

    DRM_DEBUG_KMS("enter\n");

    vigs_crtc = kzalloc(sizeof(*vigs_crtc), GFP_KERNEL);

    if (!vigs_crtc) {
        return -ENOMEM;
    }

    ret = drm_crtc_init(vigs_dev->drm_dev,
                        &vigs_crtc->base,
                        &vigs_crtc_funcs);

    if (ret != 0) {
        kfree(vigs_crtc);
        return ret;
    }

    drm_crtc_helper_add(&vigs_crtc->base, &vigs_crtc_helper_funcs);

    return 0;
}
