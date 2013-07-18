#ifndef _VIGS_EXECBUFFER_H_
#define _VIGS_EXECBUFFER_H_

#include "drmP.h"
#include "vigs_gem.h"

struct vigs_execbuffer
{
    /*
     * Must be first member!
     */
    struct vigs_gem_object gem;
};

static inline struct vigs_execbuffer *vigs_gem_to_vigs_execbuffer(struct vigs_gem_object *vigs_gem)
{
    return container_of(vigs_gem, struct vigs_execbuffer, gem);
}

int vigs_execbuffer_create(struct vigs_device *vigs_dev,
                           unsigned long size,
                           bool kernel,
                           struct vigs_execbuffer **execbuffer);

/*
 * IOCTLs
 * @{
 */

int vigs_execbuffer_create_ioctl(struct drm_device *drm_dev,
                                 void *data,
                                 struct drm_file *file_priv);

/*
 * @}
 */

#endif
