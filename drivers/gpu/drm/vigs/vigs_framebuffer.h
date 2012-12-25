#ifndef _VIGS_FRAMEBUFFER_H_
#define _VIGS_FRAMEBUFFER_H_

#include "drmP.h"
#include "vigs_protocol.h"

struct vigs_device;
struct vigs_comm;
struct vigs_gem_object;

struct vigs_framebuffer
{
    struct drm_framebuffer base;

    /*
     * Cached from 'vigs_device' for speed.
     */
    struct vigs_comm *comm;

    vigsp_surface_format format;

    struct vigs_gem_object *fb_gem;

    /*
     * Each DRM framebuffer has a surface on host, this is
     * its id.
     */
    vigsp_surface_id sfc_id;
};

static inline struct vigs_framebuffer *fb_to_vigs_fb(struct drm_framebuffer *fb)
{
    return container_of(fb, struct vigs_framebuffer, base);
}

void vigs_framebuffer_config_init(struct vigs_device *vigs_dev);

/*
 * Creates a framebuffer object.
 * Note that it also gets a reference to 'fb_gem' (in case of success), so
 * don't forget to unreference it in the calling code.
 */
int vigs_framebuffer_create(struct vigs_device *vigs_dev,
                            struct drm_mode_fb_cmd2 *mode_cmd,
                            struct vigs_gem_object *fb_gem,
                            struct vigs_framebuffer **vigs_fb);

/*
 * IOCTLs
 * @{
 */

int vigs_framebuffer_info_ioctl(struct drm_device *drm_dev,
                                void *data,
                                struct drm_file *file_priv);

/*
 * @}
 */

#endif
