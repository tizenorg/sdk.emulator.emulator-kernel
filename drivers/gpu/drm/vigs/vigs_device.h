#ifndef _VIGS_DEVICE_H_
#define _VIGS_DEVICE_H_

#include "drmP.h"

struct vigs_mman;
struct vigs_comm;
struct vigs_fbdev;

#define VIGS_REG_RAM_OFFSET 0
#define VIGS_REG_CR0        8
#define VIGS_REG_CR1        16
#define VIGS_REG_CR2        24
#define VIGS_REG_CR3        32
#define VIGS_REG_CR4        40
#define VIGS_REGS_SIZE      64

#define VIGS_USER_PTR(io_ptr, index) ((io_ptr) + ((index) * VIGS_REGS_SIZE))

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

    /* slot contains DRM file pointer if user is active, NULL if slot can be used. */
    struct drm_file **user_map;

    /* Length of 'user_map'. Must be at least 1. */
    int user_map_length;

    /* Mutex used to serialize access to user_map. */
    struct mutex user_mutex;

    /* Communicator instance for kernel itself, takes slot #0 in user_map. */
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

int vigs_device_user_enter_ioctl(struct drm_device *drm_dev,
                                 void *data,
                                 struct drm_file *file_priv);

int vigs_device_user_leave_ioctl(struct drm_device *drm_dev,
                                 void *data,
                                 struct drm_file *file_priv);

void vigs_device_user_leave_all(struct vigs_device *vigs_dev,
                                struct drm_file *file_priv);

/*
 * @}
 */

#endif
