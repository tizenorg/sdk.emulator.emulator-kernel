#ifndef _VIGS_DEVICE_H_
#define _VIGS_DEVICE_H_

#include "drmP.h"
#include "vigs_protocol.h"

struct vigs_mman;
struct vigs_comm;
struct vigs_fbdev;
struct vigs_surface;

struct vigs_device
{
    struct device *dev;
    struct drm_device *drm_dev;
    struct pci_dev *pci_dev;

    resource_size_t vram_base;
    resource_size_t vram_size;

    resource_size_t ram_base;
    resource_size_t ram_size;

    resource_size_t io_base;
    resource_size_t io_size;

    struct idr surface_idr;

    /* Map of IO BAR. */
    drm_local_map_t *io_map;

    struct vigs_mman *mman;

    struct vigs_comm *comm;

    struct vigs_fbdev *fbdev;
};

int vigs_device_init(struct vigs_device *vigs_dev,
                     struct drm_device *drm_dev,
                     struct pci_dev *pci_dev,
                     unsigned long flags);

void vigs_device_cleanup(struct vigs_device *vigs_dev);

int vigs_device_mmap(struct file *filp, struct vm_area_struct *vma);

/*
 * Must be called with drm_device::struct_mutex held.
 * @{
 */
int vigs_device_add_surface(struct vigs_device *vigs_dev,
                            struct vigs_surface *sfc,
                            vigsp_surface_id* id);

void vigs_device_remove_surface(struct vigs_device *vigs_dev,
                                vigsp_surface_id sfc_id);
/*
 * @}
 */

int vigs_device_add_surface_unlocked(struct vigs_device *vigs_dev,
                                     struct vigs_surface *sfc,
                                     vigsp_surface_id* id);

struct vigs_surface
    *vigs_device_reference_surface_unlocked(struct vigs_device *vigs_dev,
                                            vigsp_surface_id sfc_id);

void vigs_device_remove_surface_unlocked(struct vigs_device *vigs_dev,
                                         vigsp_surface_id sfc_id);

/*
 * IOCTLs
 * @{
 */

int vigs_device_exec_ioctl(struct drm_device *drm_dev,
                           void *data,
                           struct drm_file *file_priv);

/*
 * @}
 */

#endif
