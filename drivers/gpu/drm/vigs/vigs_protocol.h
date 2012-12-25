#ifndef _VIGS_PROTOCOL_H_
#define _VIGS_PROTOCOL_H_

/*
 * VIGS protocol is a request-response protocol.
 *
 * + Requests come one by one.
 * + The response is written after the request.
 */

/*
 * Bump this whenever protocol changes.
 */
#define VIGS_PROTOCOL_VERSION 9

typedef signed char vigsp_s8;
typedef signed short vigsp_s16;
typedef signed int vigsp_s32;
typedef signed long long vigsp_s64;
typedef unsigned char vigsp_u8;
typedef unsigned short vigsp_u16;
typedef unsigned int vigsp_u32;
typedef unsigned long long vigsp_u64;

typedef vigsp_u32 vigsp_bool;
typedef vigsp_u32 vigsp_surface_id;
typedef vigsp_s32 vigsp_offset;
typedef vigsp_u32 vigsp_color;
typedef vigsp_u64 vigsp_va;
typedef vigsp_u32 vigsp_resource_id;

typedef enum
{
    vigsp_cmd_init = 0x0,
    vigsp_cmd_reset = 0x1,
    vigsp_cmd_exit = 0x2,
    vigsp_cmd_create_surface = 0x3,
    vigsp_cmd_destroy_surface = 0x4,
    vigsp_cmd_set_root_surface = 0x5,
    vigsp_cmd_copy = 0x6,
    vigsp_cmd_solid_fill = 0x7,
    vigsp_cmd_update_vram = 0x8,
    vigsp_cmd_put_image = 0x9,
    vigsp_cmd_get_image = 0xA,
    vigsp_cmd_assign_resource = 0xB,
    vigsp_cmd_destroy_resource = 0xC,
} vigsp_cmd;

typedef enum
{
    /*
     * Start from 0x1 to detect host failures on target.
     */
    vigsp_status_success = 0x1,
    vigsp_status_bad_call = 0x2,
    vigsp_status_exec_error = 0x3,
} vigsp_status;

typedef enum
{
    vigsp_surface_bgrx8888 = 0x0,
    vigsp_surface_bgra8888 = 0x1,
} vigsp_surface_format;

typedef enum
{
    vigsp_resource_window = 0x0,
    vigsp_resource_pixmap = 0x1,
} vigsp_resource_type;

#pragma pack(1)

/*
 * 'vram_offset' is both surface data offset
 * and dirty flag. when it's < 0 it means surface data
 * is not allocated on target or surface is not dirty.
 * When it's >= 0 it means either surface data has been allocated
 * or surface is dirty in case if data has been allocated before.
 */
struct vigsp_surface
{
    vigsp_surface_id id;
    vigsp_offset vram_offset;
};

struct vigsp_point
{
    vigsp_u32 x;
    vigsp_u32 y;
};

struct vigsp_size
{
    vigsp_u32 w;
    vigsp_u32 h;
};

struct vigsp_rect
{
    struct vigsp_point pos;
    struct vigsp_size size;
};

struct vigsp_copy
{
    struct vigsp_point from;
    struct vigsp_point to;
    struct vigsp_size size;
};

struct vigsp_cmd_request_header
{
    vigsp_cmd cmd;

    /*
     * Response offset counting after request header.
     */
    vigsp_u32 response_offset;
};

struct vigsp_cmd_response_header
{
    vigsp_status status;
};

/*
 * cmd_init
 *
 * First command to be sent, client passes its protocol version
 * and receives server's in response. If 'client_version' doesn't match
 * 'server_version' then initialization is considered failed. This
 * is typically called on target's DRM driver load.
 *
 * @{
 */

struct vigsp_cmd_init_request
{
    vigsp_u32 client_version;
};

struct vigsp_cmd_init_response
{
    vigsp_u32 server_version;
};

/*
 * @}
 */

/*
 * cmd_reset
 *
 * Destroys all surfaces but root surface, this typically happens
 * or DRM's lastclose.
 *
 * @{
 * @}
 */

/*
 * cmd_exit
 *
 * Destroys all surfaces and transitions into uninitialized state, this
 * typically happens when target's DRM driver gets unloaded.
 *
 * @{
 * @}
 */

/*
 * cmd_create_surface
 *
 * Called for each surface created. Server returns 'id' of the surface,
 * all further operations must be carried out using this is. 'id' is
 * unique across whole target system, because there can be only one
 * DRM master (like X.Org) on target and this master typically wants to
 * share the surfaces with other processes.
 *
 * 'vram_offset' points to the surface data in VRAM, if any. If no surface data
 * is provided then 'vram_surface' must be < 0.
 *
 * @{
 */

struct vigsp_cmd_create_surface_request
{
    vigsp_u32 width;
    vigsp_u32 height;
    vigsp_u32 stride;
    vigsp_surface_format format;
    vigsp_offset vram_offset;
};

struct vigsp_cmd_create_surface_response
{
    vigsp_surface_id id;
};

/*
 * @}
 */

/*
 * cmd_destroy_surface
 *
 * Destroys the surface identified by 'id'. Surface 'id' may not be used
 * after this call and its data can be assigned to some other surface right
 * after this call.
 *
 * @{
 */

struct vigsp_cmd_destroy_surface_request
{
    vigsp_surface_id id;
};

/*
 * @}
 */

/*
 * cmd_set_root_surface
 *
 * Sets surface identified by 'id' as new root surface. Root surface is the
 * one that's displayed on screen. Root surface must have data.
 *
 * Pass 0 as id in order to reset the root surface.
 *
 * @{
 */

struct vigsp_cmd_set_root_surface_request
{
    vigsp_surface_id id;
};

/*
 * @}
 */

/*
 * cmd_copy
 *
 * Copies parts of surface 'src' to
 * surface 'dst'.
 *
 * @{
 */

struct vigsp_cmd_copy_request
{
    struct vigsp_surface src;
    struct vigsp_surface dst;
    vigsp_u32 num_entries;
    struct vigsp_copy entries[0];
};

/*
 * @}
 */

/*
 * cmd_solid_fill
 *
 * Fills surface 'sfc' with color 'color' at 'entries'.
 *
 * @{
 */

struct vigsp_cmd_solid_fill_request
{
    struct vigsp_surface sfc;
    vigsp_color color;
    vigsp_u32 num_entries;
    struct vigsp_rect entries[0];
};

/*
 * @}
 */

/*
 * cmd_update_vram
 *
 * Updates 'sfc' data in vram.
 *
 * @{
 */

struct vigsp_cmd_update_vram_request
{
    struct vigsp_surface sfc;
};

/*
 * @}
 */

/*
 * cmd_put_image
 *
 * Puts image 'src_va' on surface 'sfc'.
 * Host may detect page fault condition, in that case it'll
 * set 'is_pf' to 1 in response, target then must fault in 'src_va'
 * memory and repeat this command.
 *
 * @{
 */

struct vigsp_cmd_put_image_request
{
    struct vigsp_surface sfc;
    vigsp_va src_va;
    vigsp_u32 src_stride;
    struct vigsp_rect rect;
};

struct vigsp_cmd_put_image_response
{
    vigsp_bool is_pf;
};

/*
 * @}
 */

/*
 * cmd_get_image
 *
 * Gets image 'dst_va' from surface 'sfc_id'.
 * Host may detect page fault condition, in that case it'll
 * set 'is_pf' to 1 in response, target then must fault in 'dst_va'
 * memory and repeat this command.
 *
 * @{
 */

struct vigsp_cmd_get_image_request
{
    vigsp_surface_id sfc_id;
    vigsp_va dst_va;
    vigsp_u32 dst_stride;
    struct vigsp_rect rect;
};

struct vigsp_cmd_get_image_response
{
    vigsp_bool is_pf;
};

/*
 * @}
 */

/*
 * cmd_assign_resource
 *
 * Assign resource 'res_id' to refer to surface 'sfc_id'.
 *
 * @{
 */

struct vigsp_cmd_assign_resource_request
{
    vigsp_resource_id res_id;
    vigsp_resource_type res_type;
    vigsp_surface_id sfc_id;
};

/*
 * @}
 */

/*
 * cmd_destroy_resource
 *
 * Destroys resource 'id'.
 *
 * @{
 */

struct vigsp_cmd_destroy_resource_request
{
    vigsp_resource_id id;
};

/*
 * @}
 */

#pragma pack()

#endif
