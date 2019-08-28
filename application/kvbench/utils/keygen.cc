#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "keygen.h"
#include "crc32.h"

static char *abt_array =
  //(char*)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_";
  (char*)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";

void keygen_init(
    struct keygen *keygen,
    size_t nprefix,
    struct rndinfo *prefix_len,
    struct rndinfo *prefix_dist,
    struct keygen_option *opt)
{
    keygen->nprefix = nprefix;
    keygen->prefix_len = (struct rndinfo *)malloc(sizeof(struct rndinfo) * nprefix);
    keygen->prefix_dist = (struct rndinfo *)malloc(sizeof(struct rndinfo) * nprefix);
    keygen->opt = *opt;
    keygen->abt_array_size = strlen(abt_array);

    memcpy(keygen->prefix_len, prefix_len, sizeof(struct rndinfo) * nprefix);
    memcpy(keygen->prefix_dist, prefix_dist, sizeof(struct rndinfo) * nprefix);
}

void keygen_free(struct keygen *keygen)
{
    free(keygen->prefix_len);
    free(keygen->prefix_dist);
}

void _crc2key(struct keygen *keygen, uint64_t crc, char *buf, size_t len, uint8_t abt_only)
{
    size_t i;
    BDR_RNG_VARS_SET(crc);
    BDR_RNG_NEXTPAIR;
    BDR_RNG_NEXT;

    for (i=0;i<len;i+=1){
        BDR_RNG_NEXTPAIR;
        if (abt_only) {
            buf[i] = abt_array[(rngz%(keygen->abt_array_size))];
            //buf[i] = 'a' + (rngz%('z'-'a'));
        } else {
            buf[i] = rngz & 0xff;
        }
    }
}

size_t _crc2keylen(struct rndinfo *prefix_len, uint64_t crc)
{
    size_t r;
    BDR_RNG_VARS_SET(crc);
    BDR_RNG_NEXTPAIR;
    BDR_RNG_NEXT;

    r = get_random(prefix_len, rngz, rngz2);
    return r;
}

uint32_t keygen_idx2crc(uint64_t idx, uint32_t seed)
{
    uint32_t crc;
    uint64_t idx64 = idx;
    crc = crc32_8(&idx64, sizeof(idx64), seed);
    crc = crc32_8(&idx64, sizeof(idx64), crc);
    return crc;
}

uint64_t MurmurHash64A ( const void * key, int len, unsigned int seed )
{
    const uint64_t m = 0xc6a4a7935bd1e995;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t * data = (const uint64_t *)key;
    const uint64_t * end = data + (len/8);

    while(data != end)
    {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char * data2 = (const unsigned char*)data;

    switch(len & 7)
    {
        case 7: h ^= uint64_t(data2[6]) << 48;
        case 6: h ^= uint64_t(data2[5]) << 40;
        case 5: h ^= uint64_t(data2[4]) << 32;
        case 4: h ^= uint64_t(data2[3]) << 24;
        case 3: h ^= uint64_t(data2[2]) << 16;
        case 2: h ^= uint64_t(data2[1]) << 8;
        case 1: h ^= uint64_t(data2[0]);
        h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

size_t keygen_seed2key(struct keygen *keygen, uint64_t seed, char *buf, int keylen)
{
    uint64_t i;
    size_t len, cursor;
    uint64_t seed_local, seed64, rnd_sel;

    seed64 = MurmurHash64A(&seed, sizeof(uint64_t), 0);
    BDR_RNG_VARS_SET(seed64);
    BDR_RNG_NEXTPAIR;
    BDR_RNG_NEXT;

    cursor = 0;

    for (i=0;i<keygen->nprefix;++i){
        if (i+1 == keygen->nprefix) {
	  if(keylen > 0)
	    len = keylen - 1;
	  else
	    len = _crc2keylen(&keygen->prefix_len[i], seed64);
	    //  a workaround for key length overflow check in uniform distribution
	    //if (keygen->prefix_len[i].type == RND_UNIFORM && len > keygen->prefix_len[i].b)
	    //  len = (keygen->prefix_len[i].a + keygen->prefix_len[i].b) / 2;

            _crc2key(keygen, seed64, buf + cursor, len, keygen->opt.abt_only);
        } else {
            BDR_RNG_NEXTPAIR;
            BDR_RNG_NEXT;
            rnd_sel = get_random(&keygen->prefix_dist[i], rngz, rngz2);
            seed_local = MurmurHash64A(&rnd_sel, sizeof(rnd_sel), 0);

            len = _crc2keylen(&keygen->prefix_len[i], seed_local);
            _crc2key(keygen, seed_local, buf + cursor, len, keygen->opt.abt_only);
        }

        cursor += len;
	
        if (keygen->opt.delimiter && i != keygen->nprefix-1) {
            buf[cursor] = '/';
            cursor++;
        }
	
    }

    buf[cursor] = 0;
    return cursor;
}

size_t keygen_seqfill(uint64_t idx, char *buf, int len){

  snprintf(buf, len, "%0*ld", len -1 , idx);

  return len;
}
