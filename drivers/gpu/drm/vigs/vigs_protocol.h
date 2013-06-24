#ifndef _VIGS_PROTOCOL_H_
#define _VIGS_PROTOCOL_H_

/*
 * VIGS protocol is a multiple request-single response protocol.
 *
 * + Requests come batched.
 * + The response is written after the request batch.
 *
 * Not all commands can be batched, only commands that don't have response
 * data can be batched.
 */

/*
 * Bump this whenever protocol changes.
 */
#define VIGS_PROTOCOL_VERSION 14

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
typedef vigsp_u32 vigsp_offset;
typedef vigsp_u32 vigsp_color;

typedef enum
{
    vigsp_cmd_init = 0x0,
    vigsp_cmd_reset = 0x1,
    vigsp_cmd_exit = 0x2,
    vigsp_cmd_create_surface = 0x3,
    vigsp_cmd_destroy_surface = 0x4,
    vigsp_cmd_set_root_surface = 0x5,
    vigsp_cmd_update_vram = 0x6,
    vigsp_cmd_update_gpu = 0x7,
    vigsp_cmd_copy = 0x8,
    vigsp_cmd_solid_fill = 0x9,
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

#pragma pack(1)

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

struct vigsp_cmd_batch_header
{
    vigsp_u32 num_requests;
};

struct vigsp_cmd_request_header
{
    vigsp_cmd cmd;

    /*
     * Request size starting from request header.
     */
    vigsp_u32 size;
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
 * Called for each surface created. Client passes 'id' of the surface,
 * all further operations must be carried out using this is. 'id' is
 * unique across whole target system.
 *
 * @{
 */

struct vigsp_cmd_create_surface_request
{
    vigsp_u32 width;
    vigsp_u32 height;
    vigsp_u32 stride;
    vigsp_surface_format format;
    vigsp_surface_id id;
};

/*
 * @}
 */

/*
 * cmd_destroy_surface
 *
 * Destroys the surface identified by 'id'. Surface 'id' may not be used
 * after this call and its id can be assigned to some other surface right
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
 * one that's displayed on screen. Root surface must reside in VRAM
 * all the time, pass 'offset' in VRAM here.
 *
 * Pass 0 as id in order to reset the root surface.
 *
 * @{
 */

struct vigsp_cmd_set_root_surface_request
{
    vigsp_surface_id id;
    vigsp_offset offset;
};

/*
 * @}
 */

/*
 * cmd_update_vram
 *
 * Updates 'sfc_id' in vram.
 *
 * @{
 */

struct vigsp_cmd_update_vram_request
{
    vigsp_surface_id sfc_id;
    vigsp_offset offset;
};

/*
 * @}
 */

/*
 * cmd_update_gpu
 *
 * Updates 'sfc_id' in GPU.
 *
 * @{
 */

struct vigsp_cmd_update_gpu_request
{
    vigsp_surface_id sfc_id;
    vigsp_offset offset;
    vigsp_u32 num_entries;
    struct vigsp_rect entries[0];
};

/*
 * @}
 */

/*
 * cmd_copy
 *
 * Copies parts of surface 'src_id' to
 * surface 'dst_id'.
 *
 * @{
 */

struct vigsp_cmd_copy_request
{
    vigsp_surface_id src_id;
    vigsp_surface_id dst_id;
    vigsp_u32 num_entries;
    struct vigsp_copy entries[0];
};

/*
 * @}
 */

/*
 * cmd_solid_fill
 *
 * Fills surface 'sfc_id' with color 'color' at 'entries'.
 *
 * @{
 */

struct vigsp_cmd_solid_fill_request
{
    vigsp_surface_id sfc_id;
    vigsp_color color;
    vigsp_u32 num_entries;
    struct vigsp_rect entries[0];
};

/*
 * @}
 */

#pragma pack()

#endif
