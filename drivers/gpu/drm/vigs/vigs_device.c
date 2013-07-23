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

static void vigs_device_mman_vram_to_gpu(void *user_data,
                                         struct ttm_buffer_object *bo)
{
    struct vigs_device *vigs_dev = user_data;
    struct vigs_gem_object *vigs_gem = bo_to_vigs_gem(bo);
    struct vigs_surface *vigs_sfc = vigs_gem_to_vigs_surface(vigs_gem);
    bool need_gpu_update = vigs_surface_need_gpu_update(vigs_sfc);

    if (!vigs_sfc->is_gpu_dirty && need_gpu_update) {
        DRM_INFO("vram_to_gpu: 0x%llX\n", bo->addr_space_offset);
        vigs_comm_update_gpu(vigs_dev->comm,
                             vigs_sfc->id,
                             vigs_sfc->width,
                             vigs_sfc->height,
                             vigs_gem_offset(vigs_gem));
    } else {
        DRM_INFO("vram_to_gpu: 0x%llX (no-op)\n", bo->addr_space_offset);
    }

    vigs_sfc->is_gpu_dirty = false;
}

static void vigs_device_mman_gpu_to_vram(void *user_data,
                                         struct ttm_buffer_object *bo,
                                         unsigned long new_offset)
{
    struct vigs_device *vigs_dev = user_data;
    struct vigs_gem_object *vigs_gem = bo_to_vigs_gem(bo);
    struct vigs_surface *vigs_sfc = vigs_gem_to_vigs_surface(vigs_gem);

    if (vigs_surface_need_vram_update(vigs_sfc)) {
        DRM_DEBUG_DRIVER("0x%llX\n", bo->addr_space_offset);
        vigs_comm_update_vram(vigs_dev->comm,
                              vigs_sfc->id,
                              new_offset);
    } else {
        DRM_DEBUG_DRIVER("0x%llX (no-op)\n", bo->addr_space_offset);
    }
}

static void vigs_device_mman_init_vma(void *user_data,
                                      void *vma_data_opaque,
                                      struct ttm_buffer_object *bo,
                                      bool track_access)
{
    struct vigs_vma_data *vma_data = vma_data_opaque;
    struct vigs_gem_object *vigs_gem = bo_to_vigs_gem(bo);

    if (vigs_gem->type != VIGS_GEM_TYPE_SURFACE) {
        vma_data->sfc = NULL;
        return;
    }

    vigs_vma_data_init(vma_data,
                       vigs_gem_to_vigs_surface(vigs_gem),
                       track_access);
}

static void vigs_device_mman_cleanup_vma(void *user_data,
                                         void *vma_data_opaque)
{
    struct vigs_vma_data *vma_data = vma_data_opaque;

    if (!vma_data->sfc) {
        return;
    }

    vigs_vma_data_cleanup(vma_data);
}

static struct vigs_mman_ops mman_ops =
{
    .vram_to_gpu = &vigs_device_mman_vram_to_gpu,
    .gpu_to_vram = &vigs_device_mman_gpu_to_vram,
    .init_vma = &vigs_device_mman_init_vma,
    .cleanup_vma = &vigs_device_mman_cleanup_vma
};

static struct vigs_surface
    *vigs_device_reference_surface_unlocked(struct vigs_device *vigs_dev,
                                            vigsp_surface_id sfc_id)
{
    struct vigs_surface *sfc;

    mutex_lock(&vigs_dev->drm_dev->struct_mutex);

    mutex_lock(&vigs_dev->surface_idr_mutex);

    sfc = idr_find(&vigs_dev->surface_idr, sfc_id);

    if (sfc) {
        if (vigs_gem_freed(&sfc->gem)) {
            sfc = NULL;
        } else {
            drm_gem_object_reference(&sfc->gem.base);
        }
    }

    mutex_unlock(&vigs_dev->surface_idr_mutex);

    mutex_unlock(&vigs_dev->drm_dev->struct_mutex);

    return sfc;
}

static bool vigs_gem_is_reserved(struct list_head *gem_list,
                                 struct vigs_gem_object *gem)
{
    struct vigs_gem_object *tmp;

    list_for_each_entry(tmp, gem_list, list) {
        if (tmp == gem) {
            return true;
        }
    }

    return false;
}

static struct vigs_surface
    *vigs_surface_reserve(struct vigs_device *vigs_dev,
                          struct list_head *gem_list,
                          vigsp_surface_id sfc_id)
{
    struct vigs_surface *sfc =
        vigs_device_reference_surface_unlocked(vigs_dev, sfc_id);

    if (!sfc) {
        DRM_ERROR("Surface %u not found\n", sfc_id);
        return NULL;
    }

    if (vigs_gem_is_reserved(gem_list, &sfc->gem)) {
        drm_gem_object_unreference_unlocked(&sfc->gem.base);
    } else {
        vigs_gem_reserve(&sfc->gem);
        list_add_tail(&sfc->gem.list, gem_list);
    }

    return sfc;
}

/*
 * 'gem_list' will hold a list of GEMs that should be
 * unreserved and unreferenced after execution.
 */
static int vigs_device_patch_commands(struct vigs_device *vigs_dev,
                                      void *data,
                                      u32 data_size,
                                      struct list_head* gem_list)
{
    struct vigsp_cmd_batch_header *batch_header = data;
    struct vigsp_cmd_request_header *request_header =
        (struct vigsp_cmd_request_header*)(batch_header + 1);
    union
    {
        struct vigsp_cmd_update_vram_request *update_vram;
        struct vigsp_cmd_update_gpu_request *update_gpu;
        struct vigsp_cmd_copy_request *copy;
        struct vigsp_cmd_solid_fill_request *solid_fill;
        void *data;
    } request;
    vigsp_u32 i;
    struct vigs_surface *sfc;
    int ret = 0;

    /*
     * GEM is always at least PAGE_SIZE long, so don't check
     * if batch header is out of bounds.
     */

    for (i = 0; i < batch_header->num_requests; ++i) {
        if (((void*)(request_header) + sizeof(*request_header)) >
            (data + data_size)) {
            DRM_ERROR("request header outside of GEM\n");
            ret = -EINVAL;
            break;
        }

        if (((void*)(request_header + 1) + request_header->size) >
            (data + data_size)) {
            DRM_ERROR("request data outside of GEM\n");
            ret = -EINVAL;
            break;
        }

        request.data = (request_header + 1);

        switch (request_header->cmd) {
        case vigsp_cmd_update_vram:
            sfc = vigs_surface_reserve(vigs_dev,
                                       gem_list,
                                       request.update_vram->sfc_id);
            if (!sfc) {
                ret = -EINVAL;
                break;
            }
            if (vigs_gem_in_vram(&sfc->gem)) {
                if (vigs_surface_need_vram_update(sfc)) {
                    request.update_vram->offset = vigs_gem_offset(&sfc->gem);
                    sfc->is_gpu_dirty = false;
                } else {
                    DRM_DEBUG_DRIVER("Surface %u doesn't need to be updated, ignoring update_vram\n",
                                     request.update_vram->sfc_id);
                    request.update_vram->sfc_id = 0;
                }
            } else {
                DRM_DEBUG_DRIVER("Surface %u not in VRAM, ignoring update_vram\n",
                                 request.update_vram->sfc_id);
                request.update_vram->sfc_id = 0;
            }
            break;
        case vigsp_cmd_update_gpu:
            sfc = vigs_surface_reserve(vigs_dev,
                                       gem_list,
                                       request.update_gpu->sfc_id);
            if (!sfc) {
                ret = -EINVAL;
                break;
            }
            if (vigs_gem_in_vram(&sfc->gem)) {
                if (vigs_surface_need_gpu_update(sfc)) {
                    request.update_gpu->offset = vigs_gem_offset(&sfc->gem);
                    sfc->is_gpu_dirty = false;
                } else {
                    DRM_DEBUG_DRIVER("Surface %u doesn't need to be updated, ignoring update_gpu\n",
                                     request.update_gpu->sfc_id);
                    request.update_gpu->sfc_id = 0;
                }
            } else {
                DRM_DEBUG_DRIVER("Surface %u not in VRAM, ignoring update_gpu\n",
                                 request.update_gpu->sfc_id);
                request.update_gpu->sfc_id = 0;
            }
            break;
        case vigsp_cmd_copy:
            sfc = vigs_surface_reserve(vigs_dev,
                                       gem_list,
                                       request.copy->dst_id);
            if (!sfc) {
                ret = -EINVAL;
                break;
            }
            if (vigs_gem_in_vram(&sfc->gem)) {
                sfc->is_gpu_dirty = true;
            }
            break;
        case vigsp_cmd_solid_fill:
            sfc = vigs_surface_reserve(vigs_dev,
                                       gem_list,
                                       request.solid_fill->sfc_id);
            if (!sfc) {
                ret = -EINVAL;
                break;
            }
            if (vigs_gem_in_vram(&sfc->gem)) {
                sfc->is_gpu_dirty = true;
            }
            break;
        default:
            break;
        }

        request_header =
            (struct vigsp_cmd_request_header*)(request.data +
                                               request_header->size);
    }

    return 0;
}

static void vigs_device_finish_patch_commands(struct list_head* gem_list)
{
    struct vigs_gem_object *gem, *gem_tmp;

    list_for_each_entry_safe(gem, gem_tmp, gem_list, list) {
        list_del(&gem->list);
        vigs_gem_unreserve(gem);
        drm_gem_object_unreference_unlocked(&gem->base);
    }
}

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

    INIT_LIST_HEAD(&vigs_dev->pageflip_event_list);

    vigs_dev->vram_base = pci_resource_start(pci_dev, 0);
    vigs_dev->vram_size = pci_resource_len(pci_dev, 0);

    vigs_dev->ram_base = pci_resource_start(pci_dev, 1);
    vigs_dev->ram_size = pci_resource_len(pci_dev, 1);

    vigs_dev->io_base = pci_resource_start(pci_dev, 2);
    vigs_dev->io_size = pci_resource_len(pci_dev, 2);

    idr_init(&vigs_dev->surface_idr);
    mutex_init(&vigs_dev->surface_idr_mutex);

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
                           sizeof(struct vigs_vma_data),
                           &mman_ops,
                           vigs_dev,
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

    ret = drm_vblank_init(drm_dev, 1);

    if (ret != 0) {
        goto fail4;
    }

    /*
     * We allow VBLANK interrupt disabling right from the start. There's
     * no point in "waiting until first modeset".
     */
    drm_dev->vblank_disable_allowed = 1;

    ret = drm_irq_install(drm_dev);

    if (ret != 0) {
        goto fail5;
    }

    ret = vigs_fbdev_create(vigs_dev, &vigs_dev->fbdev);

    if (ret != 0) {
        goto fail6;
    }

    return 0;

fail6:
    drm_irq_uninstall(drm_dev);
fail5:
    drm_vblank_cleanup(drm_dev);
fail4:
    drm_mode_config_cleanup(vigs_dev->drm_dev);
    vigs_comm_destroy(vigs_dev->comm);
fail3:
    vigs_mman_destroy(vigs_dev->mman);
fail2:
    drm_rmmap(vigs_dev->drm_dev, vigs_dev->io_map);
fail1:
    idr_destroy(&vigs_dev->surface_idr);
    mutex_destroy(&vigs_dev->surface_idr_mutex);

    return ret;
}

void vigs_device_cleanup(struct vigs_device *vigs_dev)
{
    DRM_DEBUG_DRIVER("enter\n");

    vigs_fbdev_destroy(vigs_dev->fbdev);
    drm_irq_uninstall(vigs_dev->drm_dev);
    drm_vblank_cleanup(vigs_dev->drm_dev);
    drm_mode_config_cleanup(vigs_dev->drm_dev);
    vigs_comm_destroy(vigs_dev->comm);
    vigs_mman_destroy(vigs_dev->mman);
    drm_rmmap(vigs_dev->drm_dev, vigs_dev->io_map);
    idr_destroy(&vigs_dev->surface_idr);
    mutex_destroy(&vigs_dev->surface_idr_mutex);
}

int vigs_device_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct drm_file *file_priv = filp->private_data;
    struct vigs_device *vigs_dev = file_priv->minor->dev->dev_private;

    if (vigs_dev == NULL) {
        DRM_ERROR("no device\n");
        return -EINVAL;
    }

    return vigs_mman_mmap(vigs_dev->mman,
                          filp,
                          vma,
                          vigs_dev->track_gem_access);
}

int vigs_device_add_surface(struct vigs_device *vigs_dev,
                            struct vigs_surface *sfc,
                            vigsp_surface_id* id)
{
    int ret, tmp_id = 0;

    mutex_lock(&vigs_dev->surface_idr_mutex);

    do {
        if (unlikely(idr_pre_get(&vigs_dev->surface_idr, GFP_KERNEL) == 0)) {
            mutex_unlock(&vigs_dev->surface_idr_mutex);
            return -ENOMEM;
        }

        ret = idr_get_new_above(&vigs_dev->surface_idr, sfc, 1, &tmp_id);
    } while (ret == -EAGAIN);

    *id = tmp_id;

    mutex_unlock(&vigs_dev->surface_idr_mutex);

    return ret;
}

void vigs_device_remove_surface(struct vigs_device *vigs_dev,
                                vigsp_surface_id sfc_id)
{
    mutex_lock(&vigs_dev->surface_idr_mutex);
    idr_remove(&vigs_dev->surface_idr, sfc_id);
    mutex_unlock(&vigs_dev->surface_idr_mutex);
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
    struct list_head gem_list;
    int ret;

    INIT_LIST_HEAD(&gem_list);

    gem = drm_gem_object_lookup(drm_dev, file_priv, args->handle);

    if (gem == NULL) {
        return -ENOENT;
    }

    vigs_gem = gem_to_vigs_gem(gem);

    if (vigs_gem->type != VIGS_GEM_TYPE_EXECBUFFER) {
        drm_gem_object_unreference_unlocked(gem);
        return -ENOENT;
    }

    execbuffer = vigs_gem_to_vigs_execbuffer(vigs_gem);

    vigs_gem_reserve(vigs_gem);

    /*
     * Never unmap for optimization, but we got to be careful,
     * worst case scenario is when whole RAM BAR is mapped into kernel.
     */
    ret = vigs_gem_kmap(vigs_gem);

    if (ret != 0) {
        vigs_gem_unreserve(vigs_gem);
        drm_gem_object_unreference_unlocked(gem);
        return ret;
    }

    vigs_gem_unreserve(vigs_gem);

    ret = vigs_device_patch_commands(vigs_dev,
                                     execbuffer->gem.kptr,
                                     vigs_gem_size(&execbuffer->gem),
                                     &gem_list);

    if (ret != 0) {
        vigs_device_finish_patch_commands(&gem_list);
        drm_gem_object_unreference_unlocked(gem);
        return ret;
    }

    vigs_comm_exec(vigs_dev->comm, execbuffer);

    vigs_device_finish_patch_commands(&gem_list);
    drm_gem_object_unreference_unlocked(gem);

    return 0;
}
