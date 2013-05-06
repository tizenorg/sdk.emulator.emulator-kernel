#include "vigs_comm.h"
#include "vigs_device.h"
#include "vigs_execbuffer.h"
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
    struct vigsp_cmd_batch_header *batch_header;
    struct vigsp_cmd_request_header *request_header;
    unsigned long total_size = sizeof(*batch_header) +
                               sizeof(*request_header) +
                               request_size +
                               sizeof(struct vigsp_cmd_response_header) +
                               response_size;

    if (!comm->execbuffer || (vigs_gem_size(&comm->execbuffer->gem) < total_size)) {
        if (comm->execbuffer) {
            drm_gem_object_unreference_unlocked(&comm->execbuffer->gem.base);
            comm->execbuffer = NULL;
        }

        ret = vigs_execbuffer_create(comm->vigs_dev,
                                     total_size,
                                     true,
                                     &comm->execbuffer);

        if (ret != 0) {
            DRM_ERROR("unable to create execbuffer\n");
            return ret;
        }

        vigs_gem_reserve(&comm->execbuffer->gem);

        ret = vigs_gem_kmap(&comm->execbuffer->gem);

        vigs_gem_unreserve(&comm->execbuffer->gem);

        if (ret != 0) {
            DRM_ERROR("unable to kmap execbuffer\n");

            drm_gem_object_unreference_unlocked(&comm->execbuffer->gem.base);
            comm->execbuffer = NULL;

            return ret;
        }
    }

    ptr = comm->execbuffer->gem.kptr;

    memset(ptr, 0, vigs_gem_size(&comm->execbuffer->gem));

    batch_header = ptr;
    request_header = (struct vigsp_cmd_request_header*)(batch_header + 1);

    batch_header->num_requests = 1;

    request_header->cmd = cmd;
    request_header->size = request_size;

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

static void vigs_comm_exec_locked(struct vigs_comm *comm,
                                  struct vigs_execbuffer *execbuffer)
{
    writel(vigs_gem_offset(&execbuffer->gem), comm->io_ptr);
}

static int vigs_comm_exec_internal(struct vigs_comm *comm)
{
    struct vigsp_cmd_batch_header *batch_header = comm->execbuffer->gem.kptr;
    struct vigsp_cmd_request_header *request_header =
        (struct vigsp_cmd_request_header*)(batch_header + 1);
    struct vigsp_cmd_response_header *response_header =
        (struct vigsp_cmd_response_header*)((u8*)(request_header + 1) +
                                            request_header->size);

    vigs_comm_exec_locked(comm, comm->execbuffer);

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

    ret = vigs_comm_exec_internal(comm);

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

    vigs_comm_exec_internal(comm);
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

    mutex_init(&(*comm)->mutex);

    return 0;

fail2:
    if ((*comm)->execbuffer) {
        drm_gem_object_unreference_unlocked(&(*comm)->execbuffer->gem.base);
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
    if (comm->execbuffer) {
        drm_gem_object_unreference_unlocked(&comm->execbuffer->gem.base);
    }
    kfree(comm);
}

void vigs_comm_exec(struct vigs_comm *comm,
                    struct vigs_execbuffer *execbuffer)
{
    mutex_lock(&comm->mutex);
    vigs_comm_exec_locked(comm, execbuffer);
    mutex_unlock(&comm->mutex);
}

int vigs_comm_reset(struct vigs_comm *comm)
{
    int ret;

    mutex_lock(&comm->mutex);

    ret = vigs_comm_prepare(comm, vigsp_cmd_reset, 0, 0, NULL, NULL);

    if (ret == 0) {
        ret = vigs_comm_exec_internal(comm);
    }

    mutex_unlock(&comm->mutex);

    return ret;
}

int vigs_comm_create_surface(struct vigs_comm *comm,
                             u32 width,
                             u32 height,
                             u32 stride,
                             vigsp_surface_format format,
                             vigsp_surface_id id)
{
    int ret;
    struct vigsp_cmd_create_surface_request *request;

    DRM_DEBUG_DRIVER("width = %u, height = %u, stride = %u, fmt = %d, id = %u\n",
                     width,
                     height,
                     stride,
                     format,
                     id);

    mutex_lock(&comm->mutex);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_create_surface,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret == 0) {
        request->width = width;
        request->height = height;
        request->stride = stride;
        request->format = format;
        request->id = id;

        ret = vigs_comm_exec_internal(comm);
    }

    mutex_unlock(&comm->mutex);

    return ret;
}

int vigs_comm_destroy_surface(struct vigs_comm *comm, vigsp_surface_id id)
{
    int ret;
    struct vigsp_cmd_destroy_surface_request *request;

    DRM_DEBUG_DRIVER("id = %u\n", id);

    mutex_lock(&comm->mutex);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_destroy_surface,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret == 0) {
        request->id = id;

        ret = vigs_comm_exec_internal(comm);
    }

    mutex_unlock(&comm->mutex);

    return ret;
}

int vigs_comm_set_root_surface(struct vigs_comm *comm,
                               vigsp_surface_id id,
                               vigsp_offset offset)
{
    int ret;
    struct vigsp_cmd_set_root_surface_request *request;

    DRM_DEBUG_DRIVER("id = %u, offset = %u\n", id, offset);

    mutex_lock(&comm->mutex);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_set_root_surface,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret == 0) {
        request->id = id;
        request->offset = offset;

        ret = vigs_comm_exec_internal(comm);
    }

    mutex_unlock(&comm->mutex);

    return ret;
}

int vigs_comm_update_vram(struct vigs_comm *comm,
                          vigsp_surface_id id,
                          vigsp_offset offset)
{
    int ret;
    struct vigsp_cmd_update_vram_request *request;

    DRM_DEBUG_DRIVER("id = %u, offset = %u\n", id, offset);

    mutex_lock(&comm->mutex);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_update_vram,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret == 0) {
        request->sfc_id = id;
        request->offset = offset;

        ret = vigs_comm_exec_internal(comm);
    }

    mutex_unlock(&comm->mutex);

    return ret;
}

int vigs_comm_update_gpu(struct vigs_comm *comm,
                         vigsp_surface_id id,
                         vigsp_offset offset)
{
    int ret;
    struct vigsp_cmd_update_gpu_request *request;

    DRM_DEBUG_DRIVER("id = %u, offset = %u\n", id, offset);

    mutex_lock(&comm->mutex);

    ret = vigs_comm_prepare(comm,
                            vigsp_cmd_update_gpu,
                            sizeof(*request),
                            0,
                            (void**)&request,
                            NULL);

    if (ret == 0) {
        request->sfc_id = id;
        request->offset = offset;

        ret = vigs_comm_exec_internal(comm);
    }

    mutex_unlock(&comm->mutex);

    return ret;
}

int vigs_comm_get_protocol_version_ioctl(struct drm_device *drm_dev,
                                         void *data,
                                         struct drm_file *file_priv)
{
    struct drm_vigs_get_protocol_version *args = data;

    args->version = VIGS_PROTOCOL_VERSION;

    return 0;
}
