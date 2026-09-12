#pragma once
#include "source_authority.h"
#include <psp2/types.h>

typedef struct {
    SceUID fd;
    int temp_owned;
    char target[VBE_SOURCE_PATH_MAX];
    char temp[VBE_SOURCE_PATH_MAX];
} VbePersistenceFile;

void vbe_persist_file_init(VbePersistenceFile *file);
int vbe_persist_file_set_target(VbePersistenceFile *file, const char *target);
int vbe_persist_file_prepare(void *context);
int vbe_persist_file_open(void *context);
int vbe_persist_file_sync(void *context);
int vbe_persist_file_close(void *context);
int vbe_persist_file_rename(void *context);
int vbe_persist_file_cleanup(void *context);
