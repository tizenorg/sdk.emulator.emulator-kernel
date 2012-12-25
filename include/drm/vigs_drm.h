/*
 * vigs_drm.h
 */

#ifndef _VIGS_DRM_H_
#define _VIGS_DRM_H_

/*
 * Bump this whenever driver interface changes.
 */
#define DRM_VIGS_DRIVER_VERSION 3

#define DRM_VIGS_GEM_DOMAIN_VRAM 0
#define DRM_VIGS_GEM_DOMAIN_RAM 1

struct drm_vigs_get_protocol_version
{
    uint32_t version;
};

struct drm_vigs_gem_create
{
    uint32_t domain;
    uint32_t size;
    uint32_t handle;
    uint32_t domain_offset;
};

struct drm_vigs_gem_mmap
{
    uint32_t handle;
    uint64_t offset;
};

struct drm_vigs_gem_info
{
    uint32_t handle;
    uint32_t domain;
    uint32_t domain_offset;
};

struct drm_vigs_user_enter
{
    uint32_t index;
};

struct drm_vigs_user_leave
{
    uint32_t index;
};

struct drm_vigs_fb_info
{
    uint32_t fb_id;
    uint32_t sfc_id;
};

#define DRM_VIGS_GET_PROTOCOL_VERSION 0x00
#define DRM_VIGS_GEM_CREATE 0x01
#define DRM_VIGS_GEM_MMAP 0x02
#define DRM_VIGS_GEM_INFO 0x03
#define DRM_VIGS_USER_ENTER 0x04
#define DRM_VIGS_USER_LEAVE 0x05
#define DRM_VIGS_FB_INFO 0x06

#define DRM_IOCTL_VIGS_GET_PROTOCOL_VERSION DRM_IOR(DRM_COMMAND_BASE + \
            DRM_VIGS_GET_PROTOCOL_VERSION, struct drm_vigs_get_protocol_version)
#define DRM_IOCTL_VIGS_GEM_CREATE DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_GEM_CREATE, struct drm_vigs_gem_create)
#define DRM_IOCTL_VIGS_GEM_MMAP DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_GEM_MMAP, struct drm_vigs_gem_mmap)
#define DRM_IOCTL_VIGS_GEM_INFO DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_GEM_INFO, struct drm_vigs_gem_info)
#define DRM_IOCTL_VIGS_USER_ENTER DRM_IOR(DRM_COMMAND_BASE + \
            DRM_VIGS_USER_ENTER, struct drm_vigs_user_enter)
#define DRM_IOCTL_VIGS_USER_LEAVE DRM_IOW(DRM_COMMAND_BASE + \
            DRM_VIGS_USER_LEAVE, struct drm_vigs_user_leave)
#define DRM_IOCTL_VIGS_FB_INFO DRM_IOWR(DRM_COMMAND_BASE + \
            DRM_VIGS_FB_INFO, struct drm_vigs_fb_info)

#endif
