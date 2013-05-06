/*
 * vigs_drm.h
 */

#ifndef _VIGS_DRM_H_
#define _VIGS_DRM_H_

/*
 * Bump this whenever driver interface changes.
 */
#define DRM_VIGS_DRIVER_VERSION 5

struct drm_vigs_get_protocol_version
{
    uint32_t version;
};

struct drm_vigs_create_surface
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t handle;
    uint32_t size;
    uint64_t mmap_offset;
    uint32_t id;
};

struct drm_vigs_create_execbuffer
{
    uint32_t size;
    uint32_t handle;
    uint64_t mmap_offset;
};

struct drm_vigs_surface_info
{
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t size;
    uint64_t mmap_offset;
    uint32_t id;
};

struct drm_vigs_exec
{
    uint32_t handle;
};

struct drm_vigs_surface_set_dirty
{
    uint32_t handle;
};

#define DRM_VIGS_GET_PROTOCOL_VERSION 0x00
#define DRM_VIGS_CREATE_SURFACE 0x01
#define DRM_VIGS_CREATE_EXECBUFFER 0x02
#define DRM_VIGS_SURFACE_INFO 0x03
#define DRM_VIGS_EXEC 0x04
#define DRM_VIGS_SURFACE_SET_DIRTY 0x05

#define DRM_IOCTL_VIGS_GET_PROTOCOL_VERSION DRM_IOR(DRM_COMMAND_BASE + \
            DRM_VIGS_GET_PROTOCOL_VERSION, struct drm_vigs_get_protocol_version)
#define DRM_IOCTL_VIGS_CREATE_SURFACE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_SURFACE, struct drm_vigs_create_surface)
#define DRM_IOCTL_VIGS_CREATE_EXECBUFFER DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_EXECBUFFER, struct drm_vigs_create_execbuffer)
#define DRM_IOCTL_VIGS_SURFACE_INFO DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_SURFACE_INFO, struct drm_vigs_surface_info)
#define DRM_IOCTL_VIGS_EXEC DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_EXEC, struct drm_vigs_exec)
#define DRM_IOCTL_VIGS_SURFACE_SET_DIRTY DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_SURFACE_SET_DIRTY, struct drm_vigs_surface_set_dirty)

#endif
