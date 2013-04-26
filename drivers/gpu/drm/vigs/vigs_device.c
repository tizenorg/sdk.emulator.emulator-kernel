#include "vigs_device.h"
#include "vigs_mman.h"
#include "vigs_crtc.h"
#include "vigs_output.h"
#include "vigs_framebuffer.h"
#include "vigs_comm.h"
#include "vigs_fbdev.h"
#include "vigs_execbuffer.h"
#include "vigs_surface.h"
#include <drm/vigs_drm.h>

int vigs_device_init(struct vigs_device *vigs_dev,
                     struct drm_device *drm_dev,
                     struct pci_dev *pci_dev,
                     unsigned long flags)
{
    int ret;

    DRM_DEBUG_DRIVER("enter\n");

    vigs_dev->dev = &pci_dev->dev;
    vigs_dev->drm_dev = drm_dev;
    vigs_dev->pci_dev = pci_dev;

    vigs_dev->vram_base = pci_resource_start(pci_dev, 0);
    vigs_dev->vram_size = pci_resource_len(pci_dev, 0);

    vigs_dev->ram_base = pci_resource_start(pci_dev, 1);
    vigs_dev->ram_size = pci_resource_len(pci_dev, 1);

    vigs_dev->io_base = pci_resource_start(pci_dev, 2);
    vigs_dev->io_size = pci_resource_len(pci_dev, 2);

    idr_init(&vigs_dev->surface_idr);

    if (!vigs_dev->vram_base || !vigs_dev->ram_base || !vigs_dev->io_base) {
        DRM_ERROR("VRAM, RAM or IO bar not found on device\n");
        ret = -ENODEV;
        goto fail1;
    }

    if ((vigs_dev->io_size < sizeof(void*)) ||
        ((vigs_dev->io_size % sizeof(void*)) != 0)) {
        DRM_ERROR("IO bar has bad size: %u bytes\n", vigs_dev->io_size);
        ret = -ENODEV;
        goto fail1;
    }

    ret = drm_addmap(vigs_dev->drm_dev,
                     vigs_dev->io_base,
                     vigs_dev->io_size,
                     _DRM_REGISTERS,
                     0,
                     &vigs_dev->io_map);
    if (ret != 0) {
        goto fail1;
    }

    ret = vigs_mman_create(vigs_dev->vram_base, vigs_dev->vram_size,
                           vigs_dev->ram_base, vigs_dev->ram_size,
                           &vigs_dev->mman);

    if (ret != 0) {
        goto fail2;
    }

    ret = vigs_comm_create(vigs_dev, &vigs_dev->comm);

    if (ret != 0) {
        goto fail3;
    }

    drm_mode_config_init(vigs_dev->drm_dev);

    vigs_framebuffer_config_init(vigs_dev);

    ret = vigs_crtc_init(vigs_dev);

    if (ret != 0) {
        goto fail4;
    }

    ret = vigs_output_init(vigs_dev);

    if (ret != 0) {
        goto fail4;
    }

    ret = vigs_fbdev_create(vigs_dev, &vigs_dev->fbdev);

    if (ret != 0) {
        goto fail4;
    }

    return 0;

fail4:
    drm_mode_config_cleanup(vigs_dev->drm_dev);
    vigs_comm_destroy(vigs_dev->comm);
fail3:
    vigs_mman_destroy(vigs_dev->mman);
fail2:
    drm_rmmap(vigs_dev->drm_dev, vigs_dev->io_map);
fail1:
    idr_destroy(&vigs_dev->surface_idr);

    return ret;
}

void vigs_device_cleanup(struct vigs_device *vigs_dev)
{
    DRM_DEBUG_DRIVER("enter\n");

    vigs_fbdev_destroy(vigs_dev->fbdev);
    drm_mode_config_cleanup(vigs_dev->drm_dev);
    vigs_comm_destroy(vigs_dev->comm);
    vigs_mman_destroy(vigs_dev->mman);
    drm_rmmap(vigs_dev->drm_dev, vigs_dev->io_map);
    idr_destroy(&vigs_dev->surface_idr);
}

int vigs_device_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct drm_file *file_priv = filp->private_data;
    struct vigs_device *vigs_dev = file_priv->minor->dev->dev_private;

    if (vigs_dev == NULL) {
        DRM_ERROR("no device\n");
        return -EINVAL;
    }

    return vigs_mman_mmap(vigs_dev->mman, filp, vma);
}

int vigs_device_add_surface(struct vigs_device *vigs_dev,
                            struct vigs_surface *sfc,
                            vigsp_surface_id* id)
{
    int ret, tmp_id = 0;

    do {
        if (unlikely(idr_pre_get(&vigs_dev->surface_idr, GFP_KERNEL) == 0)) {
            return -ENOMEM;
        }

        ret = idr_get_new_above(&vigs_dev->surface_idr, sfc, 1, &tmp_id);
    } while (ret == -EAGAIN);

    *id = tmp_id;

    return ret;
}

void vigs_device_remove_surface(struct vigs_device *vigs_dev,
                                vigsp_surface_id sfc_id)
{
    idr_remove(&vigs_dev->surface_idr, sfc_id);
}

int vigs_device_add_surface_unlocked(struct vigs_device *vigs_dev,
                                     struct vigs_surface *sfc,
                                     vigsp_surface_id* id)
{
    int ret;

    mutex_lock(&vigs_dev->drm_dev->struct_mutex);
    ret = vigs_device_add_surface(vigs_dev, sfc, id);
    mutex_unlock(&vigs_dev->drm_dev->struct_mutex);

    return ret;
}

struct vigs_surface
    *vigs_device_reference_surface_unlocked(struct vigs_device *vigs_dev,
                                            vigsp_surface_id sfc_id)
{
    struct vigs_surface *sfc;

    mutex_lock(&vigs_dev->drm_dev->struct_mutex);

    sfc = idr_find(&vigs_dev->surface_idr, sfc_id);

    if (sfc) {
        drm_gem_object_reference(&sfc->gem.base);
    }

    mutex_unlock(&vigs_dev->drm_dev->struct_mutex);

    return sfc;
}

void vigs_device_remove_surface_unlocked(struct vigs_device *vigs_dev,
                                         vigsp_surface_id sfc_id)
{
    mutex_lock(&vigs_dev->drm_dev->struct_mutex);
    vigs_device_remove_surface(vigs_dev, sfc_id);
    mutex_unlock(&vigs_dev->drm_dev->struct_mutex);
}

int vigs_device_exec_ioctl(struct drm_device *drm_dev,
                           void *data,
                           struct drm_file *file_priv)
{
    struct vigs_device *vigs_dev = drm_dev->dev_private;
    struct drm_vigs_exec *args = data;
    struct drm_gem_object *gem;
    struct vigs_gem_object *vigs_gem;
    struct vigs_execbuffer *execbuffer;

    gem = drm_gem_object_lookup(drm_dev, file_priv, args->handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    if (vigs_gem->type != VIGS_GEM_TYPE_EXECBUFFER) {
        return -ENOENT;
    }

    execbuffer = vigs_gem_to_vigs_execbuffer(vigs_gem);

    vigs_comm_exec(vigs_dev->comm, execbuffer);

    drm_gem_object_unreference_unlocked(gem);

    return 0;
}
