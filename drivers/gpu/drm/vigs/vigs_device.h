#ifndef _VIGS_DEVICE_H_
#define _VIGS_DEVICE_H_

#include "drmP.h"

struct vigs_mman;
struct vigs_comm;
struct vigs_fbdev;

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
