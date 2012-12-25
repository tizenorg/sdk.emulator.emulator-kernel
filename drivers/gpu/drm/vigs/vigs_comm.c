#include "vigs_comm.h"
#include "vigs_device.h"
#include "vigs_gem.h"
#include "vigs_buffer.h"
#include <drm/vigs_drm.h>

static int vigs_comm_prepare(struct vigs_comm *comm,
                             vigsp_cmd cmd,
                             unsigned long request_size,
                             unsigned long response_size,
                             void **request,
                             void **response)
{
    int ret;
    void *ptr;
    struct vigsp_cmd_request_header *request_header;
    unsigned long total_size = sizeof(struct vigsp_cmd_request_header) +
                               request_size +
                               sizeof(struct vigsp_cmd_response_header) +
                               response_size;

    if (!comm->cmd_gem || (vigs_buffer_size(comm->cmd_gem->bo) < total_size)) {
        if (comm->cmd_gem) {
            drm_gem_object_unreference_unlocked(&comm->cmd_gem->base);
            comm->cmd_gem = NULL;
        }

        ret = vigs_gem_create(comm->vigs_dev,
                              total_size,
                              true,
                              DRM_VIGS_GEM_DOMAIN_RAM,
                              &comm->cmd_gem);

        if (ret != 0) {
            DRM_ERROR("unable to create command GEM\n");
            return ret;
        }

        ret = vigs_buffer_kmap(comm->cmd_gem->bo);

        if (ret != 0) {
            DRM_ERROR("unable to kmap command GEM\n");

            drm_gem_object_unreference_unlocked(&comm->cmd_gem->base);
            comm->cmd_gem = NULL;

            return ret;
        }
    }

    ptr = comm->cmd_gem->bo->kptr;

    memset(ptr, 0, vigs_buffer_size(comm->cmd_gem->bo));

    request_header = ptr;

    request_header->cmd = cmd;
    request_header->response_offset = request_size;

    if (request) {
        *request = (request_header + 1);
    }

    if (response) {
        *response = (void*)(request_header + 1) +
                    request_size +
                    sizeof(struct vigsp_cmd_response_header);
    }

    return 0;
}

static int vigs_comm_exec(struct vigs_comm *comm)
{
    struct vigsp_cmd_request_header *request_header = comm->cmd_gem->bo->kptr;
    struct vigsp_cmd_response_header *response_header =
        (void*)(request_header + 1) + request_header->response_offset;

    /*
     * 'writel' already has the mem barrier, so it's ok to just access the
     * response data afterwards.
     */

    writel(vigs_buffer_offset(comm->cmd_gem->bo),
           VIGS_USER_PTR(comm->io_ptr, 0) + VIGS_REG_RAM_OFFSET);

    switch (response_header->status) {
    case vigsp_status_success:
        return 0;
    case vigsp_status_bad_call:
        DRM_ERROR("bad host call\n");
        return -EINVAL;
    case vigsp_status_exec_error:
        DRM_ERROR("host exec error\n");
        return -EIO;
    default:
        DRM_ERROR("fatal host error\n");
        return -ENXIO;
    }
}

static int vigs_comm_init(struct vigs_comm *comm)
{
    int ret;
    struct vigsp_cmd_init_request *request;
    struct vigsp_cmd_init_response *response;

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_init,
                            sizeof(*request),
                            sizeof(*response),
                            (void**)&request,
                            (void**)&response);

    if (ret != 0) {
        return ret;
    }

    request->client_version = VIGS_PROTOCOL_VERSION;

    ret = vigs_comm_exec(comm);

    if (ret != 0) {
        return ret;
    }

    if (response->server_version != VIGS_PROTOCOL_VERSION) {
        DRM_ERROR("protocol version mismatch, expected %u, actual %u\n",
                  VIGS_PROTOCOL_VERSION,
                  response->server_version);
        return -ENODEV;
    }

    return 0;
}

static void vigs_comm_exit(struct vigs_comm *comm)
{
    int ret;

    ret = vigs_comm_prepare(comm, vigsp_cmd_exit, 0, 0, NULL, NULL);

    if (ret != 0) {
        return;
    }

    vigs_comm_exec(comm);
}

int vigs_comm_create(struct vigs_device *vigs_dev,
                     struct vigs_comm **comm)
{
    int ret = 0;

    DRM_DEBUG_DRIVER("enter\n");

    *comm = kzalloc(sizeof(**comm), GFP_KERNEL);

    if (!*comm) {
        ret = -ENOMEM;
        goto fail1;
    }

    (*comm)->vigs_dev = vigs_dev;
    (*comm)->io_ptr = vigs_dev->io_map->handle;

    ret = vigs_comm_init(*comm);

    if (ret != 0) {
        goto fail2;
    }

    /*
     * We're always guaranteed that 'user_map' has at least one element
     * and we should use it, just stuff in 'this' pointer in order
     * not to loose this slot.
     */
    vigs_dev->user_map[0] = (struct drm_file*)(*comm);

    return 0;

fail2:
    if ((*comm)->cmd_gem) {
        drm_gem_object_unreference_unlocked(&(*comm)->cmd_gem->base);
    }
    kfree(*comm);
fail1:
    *comm = NULL;

    return ret;
}

void vigs_comm_destroy(struct vigs_comm *comm)
{
    DRM_DEBUG_DRIVER("enter\n");

    vigs_comm_exit(comm);
    comm->vigs_dev->user_map[0] = NULL;
    if (comm->cmd_gem) {
        drm_gem_object_unreference_unlocked(&comm->cmd_gem->base);
    }
    kfree(comm);
}

int vigs_comm_reset(struct vigs_comm *comm)
{
    int ret;

    ret = vigs_comm_prepare(comm, vigsp_cmd_reset, 0, 0, NULL, NULL);

    if (ret != 0) {
        return ret;
    }

    return vigs_comm_exec(comm);
}

int vigs_comm_create_surface(struct vigs_comm *comm,
                             unsigned int width,
                             unsigned int height,
                             unsigned int stride,
                             vigsp_surface_format format,
                             struct vigs_gem_object *sfc_gem,
                             vigsp_surface_id *id)
{
    int ret;
    struct vigsp_cmd_create_surface_request *request;
    struct vigsp_cmd_create_surface_response *response;

    DRM_DEBUG_DRIVER("width = %u, height = %u, stride = %u, fmt = %d\n",
                     width,
                     height,
                     stride,
                     format);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_create_surface,
                            sizeof(*request),
                            sizeof(*response),
                            (void**)&request,
                            (void**)&response);

    if (ret != 0) {
        return ret;
    }

    request->width = width;
    request->height = height;
    request->stride = stride;
    request->format = format;
    request->vram_offset = vigs_buffer_offset(sfc_gem->bo);

    ret = vigs_comm_exec(comm);

    if (ret != 0) {
        return ret;
    }

    DRM_DEBUG_DRIVER("created = %u\n", response->id);

    if (id) {
        *id = response->id;
    }

    return 0;
}

int vigs_comm_destroy_surface(struct vigs_comm *comm, vigsp_surface_id id)
{
    int ret;
    struct vigsp_cmd_destroy_surface_request *request;

    DRM_DEBUG_DRIVER("id = %u\n", id);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_destroy_surface,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret != 0) {
        return ret;
    }

    request->id = id;

    return vigs_comm_exec(comm);
}

int vigs_comm_set_root_surface(struct vigs_comm *comm, vigsp_surface_id id)
{
    int ret;
    struct vigsp_cmd_set_root_surface_request *request;

    DRM_DEBUG_DRIVER("id = %u\n", id);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_set_root_surface,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret != 0) {
        return ret;
    }

    request->id = id;

    return vigs_comm_exec(comm);
}

int vigs_comm_get_protocol_version_ioctl(struct drm_device *drm_dev,
                                         void *data,
                                         struct drm_file *file_priv)
{
    struct drm_vigs_get_protocol_version *args = data;

    args->version = VIGS_PROTOCOL_VERSION;

    return 0;
}
