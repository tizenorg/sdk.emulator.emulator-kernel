#ifndef _VIGS_COMM_H_
#define _VIGS_COMM_H_

#include <linux/types.h>
#include "vigs_protocol.h"

struct drm_device;
struct drm_file;
struct vigs_device;
struct vigs_gem_object;

struct vigs_comm
{
    struct vigs_device *vigs_dev;

    /*
     * From vigs_device::io_map::handle for speed.
     */
    void __iomem *io_ptr;

    struct vigs_gem_object *cmd_gem;
};

int vigs_comm_create(struct vigs_device *vigs_dev,
                     struct vigs_comm **comm);

void vigs_comm_destroy(struct vigs_comm *comm);

int vigs_comm_reset(struct vigs_comm *comm);

int vigs_comm_create_surface(struct vigs_comm *comm,
                             unsigned int width,
                             unsigned int height,
                             unsigned int stride,
                             vigsp_surface_format format,
                             struct vigs_gem_object *sfc_gem,
                             vigsp_surface_id *id);

int vigs_comm_destroy_surface(struct vigs_comm *comm, vigsp_surface_id id);

int vigs_comm_set_root_surface(struct vigs_comm *comm, vigsp_surface_id id);

/*
 * IOCTLs
 * @{
 */

int vigs_comm_get_protocol_version_ioctl(struct drm_device *drm_dev,
                                         void *data,
                                         struct drm_file *file_priv);

/*
 * @}
 */

#endif
