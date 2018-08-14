#ifndef _FDBBENCH_KEYLOADER_H
#define _FDBBENCH_KEYLOADER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct keyloader_option {
    uint64_t max_nkeys;
};

struct keyloader_array {
    uint64_t offset;
    uint32_t len;
};

struct keyloader {
    uint64_t nkeys;
    uint64_t filesize;
    int fd;
    char *map;
    struct keyloader_array *arr;
    uint64_t avg_keysize;
};

int keyloader_init(struct keyloader *handle,
                   char *filename,
                   struct keyloader_option *option);

uint64_t keyloader_get_nkeys(struct keyloader *handle);
size_t keyloader_get_avg_keylen(struct keyloader *handle);

size_t keyloader_get_key(struct keyloader *handle, uint64_t idx, char *buf);
void keyloader_free(struct keyloader *handle);

#ifdef __cplusplus
}
#endif

#endif
