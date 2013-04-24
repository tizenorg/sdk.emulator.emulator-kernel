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

static inline vigsp_surface_id vigs_surface_id(struct vigs_surface *sfc)
{
    return vigs_gem_mmap_offset(&sfc->gem);
}

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

/*
 * @}
 */

#endif
