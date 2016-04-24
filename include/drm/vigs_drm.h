/*
 * vigs_drm.h
 */

#ifndef _VIGS_DRM_H_
#define _VIGS_DRM_H_

/*
 * Bump this whenever driver interface changes.
 */
#define DRM_VIGS_DRIVER_VERSION 14

/*
 * Surface access flags.
 */
#define DRM_VIGS_SAF_READ 1
#define DRM_VIGS_SAF_WRITE 2
#define DRM_VIGS_SAF_MASK 3

/*
 * Number of DP framebuffers.
 */
#define DRM_VIGS_NUM_DP_FB_BUF 4

/*
 * DP memory types.
 */
#define DRM_VIGS_DP_FB_Y 2
#define DRM_VIGS_DP_FB_C 3

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
    int scanout;
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

struct drm_vigs_gem_wait
{
    uint32_t handle;
};

struct drm_vigs_surface_info
{
    uint32_t handle;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    int scanout;
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

struct drm_vigs_create_fence
{
    int send;
    uint32_t handle;
    uint32_t seq;
};

struct drm_vigs_fence_wait
{
    uint32_t handle;
};

struct drm_vigs_fence_signaled
{
    uint32_t handle;
    int signaled;
};

struct drm_vigs_fence_unref
{
    uint32_t handle;
};

struct drm_vigs_plane_set_zpos
{
    uint32_t plane_id;
    int zpos;
};

struct drm_vigs_plane_set_transform
{
    uint32_t plane_id;
    int hflip;
    int vflip;
    int rotation;
};

struct drm_vigs_dp_create_surface
{
    uint32_t dp_plane;
    uint32_t dp_fb_buf;
    uint32_t dp_mem_flag;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t handle;
    uint32_t size;
    uint32_t id;
};

struct drm_vigs_dp_open_surface
{
    uint32_t dp_plane;
    uint32_t dp_fb_buf;
    uint32_t dp_mem_flag;
    uint32_t handle;
};

#define DRM_VIGS_GET_PROTOCOL_VERSION 0x00
#define DRM_VIGS_CREATE_SURFACE 0x01
#define DRM_VIGS_CREATE_EXECBUFFER 0x02
#define DRM_VIGS_GEM_MAP 0x03
#define DRM_VIGS_GEM_WAIT 0x04
#define DRM_VIGS_SURFACE_INFO 0x05
#define DRM_VIGS_EXEC 0x06
#define DRM_VIGS_SURFACE_SET_GPU_DIRTY 0x07
#define DRM_VIGS_SURFACE_START_ACCESS 0x08
#define DRM_VIGS_SURFACE_END_ACCESS 0x09
#define DRM_VIGS_CREATE_FENCE 0x0A
#define DRM_VIGS_FENCE_WAIT 0x0B
#define DRM_VIGS_FENCE_SIGNALED 0x0C
#define DRM_VIGS_FENCE_UNREF 0x0D
#define DRM_VIGS_PLANE_SET_ZPOS 0x0E
#define DRM_VIGS_PLANE_SET_TRANSFORM 0x0F

#define DRM_VIGS_DP_CREATE_SURFACE 0x20
#define DRM_VIGS_DP_OPEN_SURFACE 0x21

#define DRM_IOCTL_VIGS_GET_PROTOCOL_VERSION DRM_IOR(DRM_COMMAND_BASE + \
            DRM_VIGS_GET_PROTOCOL_VERSION, struct drm_vigs_get_protocol_version)
#define DRM_IOCTL_VIGS_CREATE_SURFACE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_SURFACE, struct drm_vigs_create_surface)
#define DRM_IOCTL_VIGS_CREATE_EXECBUFFER DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_EXECBUFFER, struct drm_vigs_create_execbuffer)
#define DRM_IOCTL_VIGS_GEM_MAP DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_GEM_MAP, struct drm_vigs_gem_map)
#define DRM_IOCTL_VIGS_GEM_WAIT DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_GEM_WAIT, struct drm_vigs_gem_wait)
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
#define DRM_IOCTL_VIGS_CREATE_FENCE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_CREATE_FENCE, struct drm_vigs_create_fence)
#define DRM_IOCTL_VIGS_FENCE_WAIT DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_FENCE_WAIT, struct drm_vigs_fence_wait)
#define DRM_IOCTL_VIGS_FENCE_SIGNALED DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_FENCE_SIGNALED, struct drm_vigs_fence_signaled)
#define DRM_IOCTL_VIGS_FENCE_UNREF DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_FENCE_UNREF, struct drm_vigs_fence_unref)
#define DRM_IOCTL_VIGS_PLANE_SET_ZPOS DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_PLANE_SET_ZPOS, struct drm_vigs_plane_set_zpos)
#define DRM_IOCTL_VIGS_PLANE_SET_TRANSFORM DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_PLANE_SET_TRANSFORM, struct drm_vigs_plane_set_transform)

#define DRM_IOCTL_VIGS_DP_CREATE_SURFACE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_DP_CREATE_SURFACE, struct drm_vigs_dp_create_surface)
#define DRM_IOCTL_VIGS_DP_OPEN_SURFACE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_DP_OPEN_SURFACE, struct drm_vigs_dp_open_surface)

#endif
