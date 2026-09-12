#include "persistence_file.h"
#include <psp2kern/io/fcntl.h>

static int copy_path(char out[VBE_SOURCE_PATH_MAX], const char *path) {
    int i = 0;
    if (out == 0 || path == 0) return -1;
    while (i < VBE_SOURCE_PATH_MAX - 1 && path[i] != '\0') {
        out[i] = path[i];
        ++i;
    }
    if (path[i] != '\0') return -1;
    out[i] = '\0';
    return 0;
}

static int build_temp_path(char out[VBE_SOURCE_PATH_MAX], const char *path) {
    int i = 0;
    while (i < VBE_SOURCE_PATH_MAX - 5 && path[i] != '\0') {
        out[i] = path[i];
        ++i;
    }
    if (path[i] != '\0') return -1;
    out[i++] = '.';
    out[i++] = 't';
    out[i++] = 'm';
    out[i++] = 'p';
    out[i] = '\0';
    return 0;
}

void vbe_persist_file_init(VbePersistenceFile *file) {
    if (file == 0) return;
    file->fd = -1;
    vbe_persistence_state_init(&file->state);
    file->target[0] = '\0';
    file->temp[0] = '\0';
}

int vbe_persist_file_set_target(VbePersistenceFile *file, const char *target) {
    if (file == 0 || target == 0 || file->fd >= 0 ||
        !vbe_persistence_state_clean(&file->state))
        return -1;
    if (copy_path(file->target, target) < 0) return -1;
    if (build_temp_path(file->temp, target) < 0) {
        file->target[0] = '\0';
        return -1;
    }
    return 0;
}

int vbe_persist_file_prepare(void *context) {
    VbePersistenceFile *file = (VbePersistenceFile *)context;
    if (file == 0 || file->fd >= 0 ||
        !vbe_persistence_state_clean(&file->state) || file->temp[0] == '\0')
        return -1;
    int ret = ksceIoRemove(file->temp);
    if (ret >= 0 || ret == VBE_SCE_IO_ERROR_NOT_FOUND) return 0;
    return ret;
}

int vbe_persist_file_open(void *context) {
    VbePersistenceFile *file = (VbePersistenceFile *)context;
    if (file == 0 || file->fd >= 0 ||
        !vbe_persistence_state_can_open(&file->state))
        return -1;
    SceUID fd = ksceIoOpen(file->temp,
                           SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
    if (fd < 0) return (int)fd;
    file->fd = fd;
    vbe_persistence_state_opened(&file->state);
    return 0;
}

int vbe_persist_file_sync(void *context) {
    VbePersistenceFile *file = (VbePersistenceFile *)context;
    if (file == 0 || file->fd < 0 || !file->state.fd_owned) return -1;
    int status = 0;
    int ret = ksceIoSyncByFd(file->fd, &status);
    if (ret < 0) return ret;
    return status < 0 ? status : 0;
}

int vbe_persist_file_close(void *context) {
    VbePersistenceFile *file = (VbePersistenceFile *)context;
    if (file == 0) return -1;
    if (!file->state.fd_owned) return file->fd < 0 ? 0 : -1;
    if (file->fd < 0) return -1;
    int ret = ksceIoClose(file->fd);
    if (ret < 0) return ret;
    file->fd = -1;
    vbe_persistence_state_closed(&file->state);
    return 0;
}

int vbe_persist_file_rename(void *context) {
    VbePersistenceFile *file = (VbePersistenceFile *)context;
    if (file == 0 || file->fd >= 0 || file->state.fd_owned ||
        !file->state.temp_owned || file->target[0] == '\0' ||
        file->temp[0] == '\0')
        return -1;
    int ret = ksceIoRename(file->temp, file->target);
    if (ret < 0) return ret;
    vbe_persistence_state_renamed(&file->state);
    return 0;
}

int vbe_persist_file_cleanup(void *context) {
    VbePersistenceFile *file = (VbePersistenceFile *)context;
    if (file == 0) return -1;

    if (file->state.fd_owned) {
        if (file->fd < 0) return -1;
        int ret = ksceIoClose(file->fd);
        if (ret < 0) return ret;
        file->fd = -1;
        vbe_persistence_state_closed(&file->state);
    } else if (file->fd >= 0) {
        return -1;
    }

    if (file->state.temp_owned) {
        int ret = ksceIoRemove(file->temp);
        if (ret < 0 && ret != VBE_SCE_IO_ERROR_NOT_FOUND) return ret;
        vbe_persistence_state_temp_removed(&file->state);
    }
    return 0;
}
