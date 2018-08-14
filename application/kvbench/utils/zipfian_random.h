#ifndef __JSAHN_ZIPFIAN_RANDOM
#define __JSAHN_ZIPFIAN_RANDOM

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zipf_rnd{
    uint64_t n;
    uint32_t resolution;
    uint32_t *table;
    // map from [0, resolution) -> table
    uint32_t *map;
    uint64_t turn;
    double s;
    double sum;
};

void zipf_rnd_init(struct zipf_rnd *zipf, uint64_t n, double s, uint32_t resolution);
uint32_t zipf_rnd_get(struct zipf_rnd *zipf);
void zipf_rnd_shift(struct zipf_rnd *zipf, uint32_t shift);
void zipf_rnd_free(struct zipf_rnd *zipf);

#ifdef __cplusplus
}
#endif

#endif
