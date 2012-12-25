#ifndef _VIGS_GEM_H_
#define _VIGS_GEM_H_

#include "drmP.h"

struct vigs_device;
struct vigs_buffer_object;

struct vigs_gem_object
{
    struct drm_gem_object base;

    struct vigs_buffer_object *bo;
};

static inline struct vigs_gem_object *gem_to_vigs_gem(struct drm_gem_object *gem)
{
    return container_of(gem, struct vigs_gem_object, base);
}

/*
 * Creates a gem object. 'size' is automatically rounded up to page size.
 */
int vigs_gem_create(struct vigs_device *vigs_dev,
                    unsigned long size,
                    bool kernel,
                    u32 domain,
                    struct vigs_gem_object **vigs_gem);

void vigs_gem_free_object(struct drm_gem_object *gem);

int vigs_gem_init_object(struct drm_gem_object *gem);

int vigs_gem_open_object(struct drm_gem_object *gem,
                         struct drm_file *file_priv);

void vigs_gem_close_object(struct drm_gem_object *gem,
                           struct drm_file *file_priv);

/*
 * Dumb
 * @{
 */

int vigs_gem_dumb_create(struct drm_file *file_priv,
                         struct drm_device *drm_dev,
                         struct drm_mode_create_dumb *args);

int vigs_gem_dumb_destroy(struct drm_file *file_priv,
                          struct drm_device *drm_dev,
                          uint32_t handle);

int vigs_gem_dumb_map_offset(struct drm_file *file_priv,
                             struct drm_device *drm_dev,
                             uint32_t handle, uint64_t *offset_p);

/*
 * @}
 */

/*
 * IOCTLs
 * @{
 */

int vigs_gem_create_ioctl(struct drm_device *drm_dev,
                          void *data,
                          struct drm_file *file_priv);

int vigs_gem_mmap_ioctl(struct drm_device *drm_dev,
                        void *data,
                        struct drm_file *file_priv);

int vigs_gem_info_ioctl(struct drm_device *drm_dev,
                        void *data,
                        struct drm_file *file_priv);

/*
 * @}
 */

#endif
