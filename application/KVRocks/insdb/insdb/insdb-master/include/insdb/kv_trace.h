/*
 * kv_trace.h 
 * Author : Heekwon Park
 * E-mail : heekwon.p@samsung.com
 *
 * LOG & Elapsed time measurement funcionts
 */
#ifndef KV_TRACE_H
#define KV_TRACE_H
#include <time.h>
#include <stdio.h>
#include "db/global_stats.h"
#include "db/insdb_internal.h"
//////////////////// TIME //////////////////

#ifdef KV_TIME_MEASURE
#define TRACE_BUF_SIZE 2048
#endif
#if 0
#ifdef KV_TIME_MEASURE
        cycles_t st, e;
        st = get_cycles();
#endif
#ifdef KV_TIME_MEASURE
        e = get_cycles();
        p[__LINE__].fetch_add(time_cycle_measure(st,e));
        st = get_cycles();
#endif
#endif
namespace insdb {
    typedef unsigned long long cycles_t;

#define DECLARE_ARGS(val, low, high)	unsigned low, high
#define EAX_EDX_VAL(val, low, high)	((low) | ((unsigned long)(high) << 32))
#define EAX_EDX_ARGS(val, low, high)	"a" (low), "d" (high)
#define EAX_EDX_RET(val, low, high)	"=a" (low), "=d" (high)

    static unsigned long long __native_read_tsc(void)
    {
        DECLARE_ARGS(val, low, high);

        asm volatile("rdtsc" : EAX_EDX_RET(val, low, high));

        return EAX_EDX_VAL(val, low, high);
    }

    static inline cycles_t get_cycles(void)
    {
        unsigned long long ret = 0;

        ret=__native_read_tsc();

        return ret;
    }

    extern cycles_t msec_value;
    extern cycles_t usec_value;
    extern cycles_t nsec_value;
    extern cycles_t cycle_value;

#ifdef KV_TIME_MEASURE
    extern std::atomic<uint64_t> p[TRACE_BUF_SIZE];
#endif


#ifndef DEBUG
#define DEBUG 0
#endif

#define KVLOG(fmt, args...)      {time_t r; struct tm * t; time(&r); t=localtime(&r); printf("{%2d:%2d:%2d}[%s:%s():%d] :: " fmt, t->tm_hour, t->tm_min, t->tm_sec, __FILE__, __func__, __LINE__, ##args);}
#define ERRLOG(fmt, args...)      {time_t r; struct tm * t; time(&r); t=localtime(&r); fprintf(stderr, "{%2d:%2d:%2d}[%s:%s():%d] :: " fmt, t->tm_hour, t->tm_min, t->tm_sec, __FILE__, __func__, __LINE__, ##args); exit(0);}

#if DEBUG
#define DEBUGLOG(fmt, args...)      {time_t r; struct tm * t; time(&r); t=localtime(&r); printf("{%2d:%2d:%2d}[%s:%s():%d] :: " fmt, t->tm_hour, t->tm_min, t->tm_sec, __FILE__, __func__, __LINE__, ##args);}
#else
#define DEBUGLOG(fmt, args...)
#endif

#define time_sec_measure(START,END) ((END-START)/cycle_value)
#define time_msec_measure(START,END) ((END-START)/msec_value)
#define time_usec_measure(START,END) ((END-START)/usec_value)
#define time_nsec_measure(START,END) ((END-START)/nsec_value)
#define time_cycle_measure(START,END) (END-START)
#define cycle_to_msec(T) ((T)/msec_value)
#define cycle_to_usec(T) ((T)/usec_value)
#define cycle_to_nsec(T) ((T)/nsec_value)

    static inline void time_init(cycles_t *msec, cycles_t *usec, cycles_t *nsec, cycles_t *cycle){

        cycles_t s_time, e_time;
        /* Time Value initialization*/
        s_time = get_cycles();
        sleep(1);
        e_time = get_cycles();
        *cycle = time_cycle_measure(s_time, e_time);
        *msec  = *cycle / 1000; //for cycle to msec
        *usec  = *cycle / 1000 / 1000; //for cycle to usec
        *nsec  = *cycle / 1000 / 1000 / 1000; //for cycle to usec
        *msec = ((*msec + 99999) / 100000) * 100000;
        *usec = ((*usec + 99) / 100) * 100;
    }
}
#endif

