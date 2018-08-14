#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "adv_random.h"
#include "zipfian_random.h"

#include "memleak.h"

double _get_den_sum(struct zipf_rnd *zipf)
{
    uint64_t i;
    double s, sum;

    s = zipf->s;
    sum = 0;

    for (i=0;i<zipf->n;++i){
        sum += 1/pow((i+1), s);
    }
    return sum;
}

double _get_zeta(struct zipf_rnd *zipf, uint64_t k, double s, uint64_t n)
{
    return (1/pow(k,s)) / zipf->sum;
}

void zipf_rnd_init(struct zipf_rnd *zipf, uint64_t n, double s, uint32_t resolution)
{
    uint64_t i, j, temp, a, b;
    uint32_t *table;
    double cum, zeta;
    BDR_RNG_VARS;

    zipf->n = n;
    zipf->s = s;
    zipf->resolution = resolution;
    zipf->turn = 0;
    zipf->table = (uint32_t*)malloc(sizeof(uint32_t) * zipf->n);
    zipf->map = (uint32_t*)malloc(sizeof(uint32_t) * resolution);
    zipf->sum = _get_den_sum(zipf);

    memset(zipf->map, 0, sizeof(uint32_t) * resolution);
    table = zipf->table;
    for (i=0; i<zipf->n; ++i){
        table[i] = i;
    }

    // shuffle zipfian table
    for (i=0; i<zipf->n; ++i){
        BDR_RNG_NEXTPAIR;
        a = rngz % zipf->n;
        b = rngz2 % zipf->n;

        temp = table[a];
        table[a] = table[b];
        table[b] = temp;
    }

    // create map table
    cum = 0;
    for (i=0;i<zipf->n;++i){
        zeta = _get_zeta(zipf, i+1, zipf->s, zipf->n);

        a = (uint64_t)(cum * zipf->resolution);
        b = (uint64_t)((cum+zeta) * zipf->resolution);
        if (b >= zipf->resolution) b = zipf->resolution-1;
        cum += zeta;

        for (j = a; j <= b; ++j){
            zipf->map[j] = i;
        }
    }
}

uint32_t zipf_rnd_get(struct zipf_rnd *zipf)
{
    uint32_t idx, r;
    r = rand()  % zipf->resolution;
    idx = (zipf->map[r] + zipf->turn) % zipf->n;

    return zipf->table[idx];
}

void zipf_rnd_shift(struct zipf_rnd *zipf, uint32_t shift)
{
    zipf->turn += shift;
    zipf->turn = zipf->turn % zipf->n;
}

void zipf_rnd_free(struct zipf_rnd *zipf)
{
    free(zipf->table);
    free(zipf->map);
}

