#include "vigs_plane.h"
#include "vigs_device.h"
#include "vigs_framebuffer.h"
#include "vigs_surface.h"
#include "vigs_comm.h"

static const uint32_t formats[] =
{
    DRM_FORMAT_XRGB8888,
    DRM_FORMAT_ARGB8888,
};

static int vigs_plane_update(struct drm_plane *plane,
                             struct drm_crtc *crtc,
                             struct drm_framebuffer *fb,
                             int crtc_x, int crtc_y,
                             unsigned int crtc_w,
                             unsigned int crtc_h,
                             uint32_t src_x, uint32_t src_y,
                             uint32_t src_w, uint32_t src_h)
{
    struct vigs_plane *vigs_plane = plane_to_vigs_plane(plane);
    struct vigs_device *vigs_dev = plane->dev->dev_private;
    struct vigs_framebuffer *vigs_fb = fb_to_vigs_fb(fb);
    int ret;
    uint32_t src_x_whole = src_x >> 16;
    uint32_t src_y_whole = src_y >> 16;
    uint32_t src_w_whole = src_w >> 16;
    uint32_t src_h_whole = src_h >> 16;

    DRM_DEBUG_KMS("enter: crtc_x = %d, crtc_y = %d, crtc_w = %u, crtc_h = %u, src_x = %u, src_y = %u, src_w = %u, src_h = %u\n",
                  crtc_x, crtc_y, crtc_w, crtc_h, src_x, src_y, src_w, src_h);

    if (vigs_fb->fb_sfc->scanout) {
        vigs_gem_reserve(&vigs_fb->fb_sfc->gem);

        if (vigs_gem_in_vram(&vigs_fb->fb_sfc->gem) &&
            vigs_surface_need_gpu_update(vigs_fb->fb_sfc)) {
            vigs_comm_update_gpu(vigs_dev->comm,
                                 vigs_fb->fb_sfc->id,
                                 vigs_fb->fb_sfc->width,
                                 vigs_fb->fb_sfc->height,
                                 vigs_gem_offset(&vigs_fb->fb_sfc->gem));
        }

        vigs_gem_unreserve(&vigs_fb->fb_sfc->gem);
    }

    ret = vigs_comm_set_plane(vigs_dev->comm,
                              vigs_plane->index,
                              vigs_fb->fb_sfc->id,
                              src_x_whole,
                              src_y_whole,
                              src_w_whole,
                              src_h_whole,
                              crtc_x,
                              crtc_y,
                              crtc_w,
                              crtc_h,
                              vigs_plane->z_pos);

    if (ret == 0) {
        vigs_plane->src_x = src_x;
        vigs_plane->src_y = src_y;
        vigs_plane->src_w = src_w;
        vigs_plane->src_h = src_h;

        vigs_plane->crtc_x = crtc_x;
        vigs_plane->crtc_y = crtc_y;
        vigs_plane->crtc_w = crtc_w;
        vigs_plane->crtc_h = crtc_h;

        vigs_plane->enabled = true;
    }

    return ret;
}

static int vigs_plane_disable(struct drm_plane *plane)
{
    struct vigs_plane *vigs_plane = plane_to_vigs_plane(plane);
    struct vigs_device *vigs_dev = plane->dev->dev_private;
    int ret;

    DRM_DEBUG_KMS("enter\n");

    if (!vigs_plane->enabled) {
        return 0;
    }

    ret = vigs_comm_set_plane(vigs_dev->comm,
                              vigs_plane->index,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0,
                              0);

    if (ret == 0) {
        vigs_plane->src_x = 0;
        vigs_plane->src_y = 0;
        vigs_plane->src_w = 0;
        vigs_plane->src_h = 0;

        vigs_plane->crtc_x = 0;
        vigs_plane->crtc_y = 0;
        vigs_plane->crtc_w = 0;
        vigs_plane->crtc_h = 0;

        vigs_plane->enabled = false;
    }

    return ret;
}

static void vigs_plane_destroy(struct drm_plane *plane)
{
    struct vigs_plane *vigs_plane = plane_to_vigs_plane(plane);

    DRM_DEBUG_KMS("enter\n");

    vigs_plane_disable(plane);
    drm_plane_cleanup(plane);
    kfree(vigs_plane);
}

static const struct drm_plane_funcs vigs_plane_funcs =
{
    .update_plane = vigs_plane_update,
    .disable_plane = vigs_plane_disable,
    .destroy = vigs_plane_destroy,
};

int vigs_plane_init(struct vigs_device *vigs_dev, u32 index)
{
    struct vigs_plane *vigs_plane;
    int ret;

    DRM_DEBUG_KMS("enter\n");

    vigs_plane = kzalloc(sizeof(*vigs_plane), GFP_KERNEL);

    if (!vigs_plane) {
        return -ENOMEM;
    }

    vigs_plane->index = index;

    ret = drm_plane_init(vigs_dev->drm_dev,
                         &vigs_plane->base,
                         (1 << 0),
                         &vigs_plane_funcs,
                         formats,
                         ARRAY_SIZE(formats),
                         false);

    if (ret != 0) {
        return ret;
    }

    return 0;
}
