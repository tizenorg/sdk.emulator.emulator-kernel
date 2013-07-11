/*
 * vigs_drm.h
 */

#ifndef _VIGS_DRM_H_
#define _VIGS_DRM_H_

/*
 * Bump this whenever driver interface changes.
 */
#define DRM_VIGS_DRIVER_VERSION 9

/*
 * Surface access flags.
 */
#define DRM_VIGS_SAF_READ 1
#define DRM_VIGS_SAF_WRITE 2
#define DRM_VIGS_SAF_MASK 3

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
    uint32_t id;
};

struct drm_vigs_create_execbuffer
{
    uint32_t size;
    uint32_t handle;
};

struct drm_vigs_gem_map
{
    uint32_t handle;
    int track_access;
    unsigned long address;
};

struct drm_vigs_surface_info
{
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t size;
    uint32_t id;
};

struct drm_vigs_exec
{
    uint32_t handle;
};

struct drm_vigs_surface_set_gpu_dirty
{
    uint32_t handle;
};

struct drm_vigs_surface_start_access
{
    unsigned long address;
    uint32_t saf;
};

struct drm_vigs_surface_end_access
{
    unsigned long address;
    int sync;
};

#define DRM_VIGS_GET_PROTOCOL_VERSION 0x00
#define DRM_VIGS_CREATE_SURFACE 0x01
#define DRM_VIGS_CREATE_EXECBUFFER 0x02
#define DRM_VIGS_GEM_MAP 0x03
#define DRM_VIGS_SURFACE_INFO 0x04
#define DRM_VIGS_EXEC 0x05
#define DRM_VIGS_SURFACE_SET_GPU_DIRTY 0x06
#define DRM_VIGS_SURFACE_START_ACCESS 0x07
#define DRM_VIGS_SURFACE_END_ACCESS 0x08

#define DRM_IOCTL_VIGS_GET_PROTOCOL_VERSION DRM_IOR(DRM_COMMAND_BASE + \
            DRM_VIGS_GET_PROTOCOL_VERSION, struct drm_vigs_get_protocol_version)
#define DRM_IOCTL_VIGS_CREATE_SURFACE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_SURFACE, struct drm_vigs_create_surface)
#define DRM_IOCTL_VIGS_CREATE_EXECBUFFER DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_EXECBUFFER, struct drm_vigs_create_execbuffer)
#define DRM_IOCTL_VIGS_GEM_MAP DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_GEM_MAP, struct drm_vigs_gem_map)
#define DRM_IOCTL_VIGS_SURFACE_INFO DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_SURFACE_INFO, struct drm_vigs_surface_info)
#define DRM_IOCTL_VIGS_EXEC DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_EXEC, struct drm_vigs_exec)
#define DRM_IOCTL_VIGS_SURFACE_SET_GPU_DIRTY DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_SURFACE_SET_GPU_DIRTY, struct drm_vigs_surface_set_gpu_dirty)
#define DRM_IOCTL_VIGS_SURFACE_START_ACCESS DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_SURFACE_START_ACCESS, struct drm_vigs_surface_start_access)
#define DRM_IOCTL_VIGS_SURFACE_END_ACCESS DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_SURFACE_END_ACCESS, struct drm_vigs_surface_end_access)

#endif
