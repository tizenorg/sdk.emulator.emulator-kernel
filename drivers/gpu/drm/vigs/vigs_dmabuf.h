#ifndef _VIGS_DMABUF_H_
#define _VIGS_DMABUF_H_

#include <linux/types.h>

struct drm_device;
struct drm_file;
struct dma_buf;
struct drm_gem_object;

int vigs_prime_handle_to_fd(struct drm_device *dev,
                            struct drm_file *file_priv,
                            uint32_t handle,
                            uint32_t flags,
                            int *prime_fd);

int vigs_prime_fd_to_handle(struct drm_device *dev,
                            struct drm_file *file_priv,
                            int fd,
                            uint32_t *handle);

struct dma_buf *vigs_dmabuf_prime_export(struct drm_device *dev,
                                         struct drm_gem_object *gem_obj,
                                         int flags);

struct drm_gem_object *vigs_dmabuf_prime_import(struct drm_device *dev,
                                                struct dma_buf *dma_buf);

#endif /* _VIGS_DMABUF_H_ */
