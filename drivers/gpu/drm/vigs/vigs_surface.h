#ifndef _VIGS_SURFACE_H_
#define _VIGS_SURFACE_H_

#include "drmP.h"
#include "vigs_protocol.h"
#include "vigs_gem.h"

struct vigs_surface
{
    /*
     * Must be first member!
     */
    struct vigs_gem_object gem;

    u32 width;
    u32 height;
    u32 stride;
    vigsp_surface_format format;
    vigsp_surface_id id;

    /*
     * Members below MUST be accessed between
     * vigs_gem_reserve/vigs_gem_unreserve.
     * @{
     */

    bool is_dirty;

    /*
     * @}
     */
};

static inline struct vigs_surface *vigs_gem_to_vigs_surface(struct vigs_gem_object *vigs_gem)
{
    return container_of(vigs_gem, struct vigs_surface, gem);
}

int vigs_surface_create(struct vigs_device *vigs_dev,
                        u32 width,
                        u32 height,
                        u32 stride,
                        vigsp_surface_format format,
                        struct vigs_surface **sfc);

/*
 * IOCTLs
 * @{
 */

int vigs_surface_create_ioctl(struct drm_device *drm_dev,
                              void *data,
                              struct drm_file *file_priv);

int vigs_surface_info_ioctl(struct drm_device *drm_dev,
                            void *data,
                            struct drm_file *file_priv);

int vigs_surface_set_dirty_ioctl(struct drm_device *drm_dev,
                                 void *data,
                                 struct drm_file *file_priv);

/*
 * @}
 */

#endif
