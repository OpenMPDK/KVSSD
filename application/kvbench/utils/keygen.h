#ifndef _JSAHN_KEYGEN_H
#define _JSAHN_KEYGEN_H

#include "adv_random.h"

#ifdef __cplusplus
extern "C" {
#endif

struct keygen_option {
    uint8_t delimiter;
    uint8_t abt_only;
};

struct keygen {
    size_t nprefix;
    size_t abt_array_size;
    struct rndinfo *prefix_len;
    struct rndinfo *prefix_dist;
    struct keygen_option opt;
};

void keygen_init(
    struct keygen *keygen,
    size_t nprefix,
    struct rndinfo *prefix_len,
    struct rndinfo *prefix_dist,
    struct keygen_option *opt);

uint64_t MurmurHash64A( const void * key, int len, unsigned int seed );

void keygen_free(struct keygen *keygen);
uint32_t keygen_idx2crc(uint64_t idx, uint32_t seed);
size_t keygen_seed2key(struct keygen *keygen, uint64_t seed, char *buf, int keylen);
size_t keygen_seqfill(uint64_t idx, char *buf, int len);

#ifdef __cplusplus
}
#endif

#endif
