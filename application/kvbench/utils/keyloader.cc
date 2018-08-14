#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include "sys/mman.h"
#include "keyloader.h"
#include "memleak.h"

int keyloader_init(struct keyloader *handle,
                   char *filename,
                   struct keyloader_option *option)
{
    int r_flag, first_key_found;
    uint64_t i, begin_pos, last_pos;
    uint64_t segsize = 256;

    begin_pos = last_pos = 0;

    // file open
    handle->fd = open(filename, O_RDONLY);
    if (handle->fd < 0) {
        return -1;
    }
    handle->nkeys = 0;
    handle->avg_keysize = 0;
    handle->filesize = lseek(handle->fd, 0, SEEK_END);

    handle->map = (char*)mmap(0, handle->filesize, PROT_READ,
                              MAP_SHARED, handle->fd, 0);
    if (handle->map == MAP_FAILED) {
        close(handle->fd);
        return -2;
    }

    handle->arr = (struct keyloader_array*)
                  malloc(sizeof(struct keyloader_array) * segsize);

    r_flag = first_key_found = 0;
    for (i=0;i<handle->filesize;++i) {
        if (handle->map[i] == 0x0d || handle->map[i] == 0x0a) {
            // return character
            if (!r_flag && first_key_found) {
                // the end of key
                handle->arr[handle->nkeys].len = last_pos - begin_pos + 1;
                handle->avg_keysize += handle->arr[handle->nkeys].len;
                handle->nkeys++;
                if (option->max_nkeys &&
                    handle->nkeys >= option->max_nkeys) {
                    r_flag = 1;
                    break;
                }

                if (handle->nkeys >= segsize) {
                    segsize *= 2;
                    handle->arr = (struct keyloader_array*)
                        realloc(handle->arr, sizeof(struct keyloader_array) *
                                             segsize);
                }
            }
            r_flag = 1;
        } else {
            if (!first_key_found) {
                // the first key
                handle->arr[0].offset = i;
                begin_pos = i;
                first_key_found = 1;
            } else if (r_flag) {
                handle->arr[handle->nkeys].offset = i;
                begin_pos = i;
            }
            last_pos = i;
            r_flag = 0;
        }
    }

    if (!r_flag) {
        handle->arr[handle->nkeys].len = last_pos - begin_pos + 1;
        handle->avg_keysize += handle->arr[handle->nkeys].len;
        handle->nkeys++;
    }
    if (handle->nkeys) {
        handle->avg_keysize /= handle->nkeys;
    }

    return 0;
}

uint64_t keyloader_get_nkeys(struct keyloader *handle)
{
    return handle->nkeys;
}

size_t keyloader_get_avg_keylen(struct keyloader *handle)
{
    return handle->avg_keysize;
}

size_t keyloader_get_key(struct keyloader *handle, uint64_t idx, char *buf)
{
    size_t len;

    if (idx >= handle->nkeys) {
        return 0;
    }

    len = handle->arr[idx].len;
    memcpy(buf, handle->map + handle->arr[idx].offset, len);
    buf[len] = 0;

    return len;
}

void keyloader_free(struct keyloader *handle)
{
    munmap(handle->map, handle->filesize);
    free(handle->arr);
    close(handle->fd);
}

