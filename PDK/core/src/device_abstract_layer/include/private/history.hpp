/**
 *   BSD LICENSE
 *
 *   Copyright (c) 2018 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Samsung Electronics Co., Ltd. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ADI_HISTORY_HPP
#define ADI_HISTORY_HPP

#include "stdio.h"
#include "stdlib.h"
#include "math.h"
#include "stdint.h"
#include <vector>
#include <cmath>

enum op_type {
    STAT_FIRST  =0,
    STAT_READ   =0,
    STAT_UPDATE =1,
    STAT_INSERT =2,
    STAT_DELETE =3,
    STAT_LAST   =STAT_DELETE,
};
template <int size>
class op_history {

    int buffersize = size+1;
    int buffer[size+1];
    int front;
    int tail;
    int counters[4] = { 0, 0, 0,0 };
    double length = 0;
public:
    op_history():front(0),tail(0) {
    }

    void add (int value) {
        buffer[front] = value;
        counters[value]++;
        front = (front+1) % buffersize;
        length +=1 ;

        if (front == tail) {    // full
            // drop the old one
            counters[buffer[tail]]--;
            tail = (tail+1) % buffersize;
            length --;
        }
    }

    double get_readp() {
        //fprintf(stderr, "read = %d", counters[STAT_READ]);
        return counters[STAT_READ] / length;
    }

    double get_updatep() {
        //fprintf(stderr, "update = %d", counters[STAT_UPDATE]);
        return counters[STAT_UPDATE] / length;
    }

    double get_insertp() {
        //fprintf(stderr, "insert = %d", counters[STAT_INSERT]);
        return counters[STAT_INSERT] / length;
    }
    double get_deletep() {
        return counters[STAT_DELETE] / length;
    }

};

template <int size>
class value_history {
    uint32_t sum = 0;
    int buffersize = size+1;
    int buffer[size+1];
    int front;
    int tail;
    int length;
public:
    value_history():front(0),tail(0), length(0) {}

    void add (int value) {
        buffer[front] = value;
        sum  += value;
        front = (front+1) % buffersize;
        length++;

        if (front == tail) {    // full
            // drop the old one
            sum -= buffer[tail];
            tail = (tail+1) % buffersize;
            length--;
        }
    }

    int mean_value() {
        return std::round((double)sum / length);
    }

};

#define MAX_FEATURES 35

class latency_model {
public:

    // default constructor using hardcoded model
    latency_model() {}

    // using configuration file parameters for model
    // first one is intercept
    latency_model(std::vector<double> iops_model_coefficients) {
        int size = iops_model_coefficients.size();

        // validate basics and only use when # of parameters matches 
        if (size > 0) {
            if (size != (MAX_FEATURES + 1)) {
                fprintf(stderr, "WARNING: total number of IOPS model paramters should be %d\n", MAX_FEATURES + 1); 
                fprintf(stderr, "Please validate device configuration file. Fall back to use default IOPS model\n");
                return;
            }
            intercept = iops_model_coefficients[0];

            for (int i = 1; i <= MAX_FEATURES; i++) {
                coefficient[i - 1] = iops_model_coefficients[i];
            }
        }
    }

    // default linear model parameters
    double intercept = 93.55536664;
    double coefficient[MAX_FEATURES] = {

        9.73333650e-03,  -7.48332780e-04,  -1.02151073e+01,  -3.25414892e+01,
        4.27551652e+01,  -1.37097247e-03,   5.35734352e-04,   6.63934171e-05,
       -5.98786540e-03,   6.59822790e+01,  -9.08029926e+01,   1.46061898e+01,
        1.26562873e+01,   4.56136757e+01,  -1.74634825e+01,   3.45057316e-12,
        1.37099315e-03,   1.37105773e-03,   1.37092722e-03,  -7.66785198e-03,
        1.28200988e-02,  -4.76358522e-03,  -5.57416593e-03,  -4.75173882e-03,
        3.58063105e-03,  -9.87217038e+00,   6.72385443e+01,   8.61728612e+00,
       -1.39270618e+02,  -1.87756101e+01,   2.47645398e+01,   7.51414174e+01,
        7.67858478e+01,  -1.23967843e+01,  -2.98320336e+01
    };

    /*
     * 3 degree polynomial model feature list
     *
     * Where x0 �~@~S vlen, x1 �~@~S insert, x2 �~@~S read, x3 �~@~S update.
     *
    ['1', 'x0', 'x1', 'x2', 'x3', 'x0^2', 'x0 x1', 'x0 x2', 'x0 x3', 'x1^2', 'x1 x2', 'x1 x3', 'x2^2', 'x2 x3', 'x3^2', 'x0^3', 'x0^2 x1', 'x0^2 x2', 'x0^2 x3', 'x0 x1^2', 'x0 x1 x2', 'x0 x1 x3', 'x0 x2^2', 'x0 x2 x3', 'x0 x3^2', 'x1^3', 'x1^2 x2', 'x1^2 x3', 'x1 x2^2', 'x1 x2 x3', 'x1 x3^2', 'x2^3', 'x2^2 x3', 'x2 x3^2', 'x3^3']
     *
     * return 0 on success if the input features array is populated successfully.
     * return -1 on error when feature count doesn't match expected number of
     * features
     */

    int calculate_features(double *features, uint32_t feature_count, double vlen, double insert, double read, double update) {
        if (feature_count != MAX_FEATURES) {
            printf("model feature count doesn't match\n");
            return -1;
        }

        uint32_t x0 = vlen;
        double x1 = insert;
        double x2 = read;
        double x3 = update;

        features[0] = 1;
        features[1] = x0;
        features[2] = x1;
        features[3] = x2;
        features[4] = x3;

        features[5] = x0 * x0;
        features[6] = x0 * x1;
        features[7] = x0 * x2;
        features[8] = x0 * x3;
        features[9] = x1 * x1;
        features[10] = x1 * x2;
        features[11] = x1 * x3;
        features[12] = x2 * x2;
        features[13] = x2 * x3;
        features[14] = x3 * x3;

        features[15] = x0 * x0 * x0;
        features[16] = x0 * x0 * x1;
        features[17] = x0 * x0 * x2;
        features[18] = x0 * x0 * x3;
        features[19] = x0 * x1 * x1;
        features[20] = x0 * x1 * x2;
        features[21] = x0 * x1 * x3;
        features[22] = x0 * x2 * x2;
        features[23] = x0 * x2 * x3;
        features[24] = x0 * x3 * x3;

        features[25] = x1 * x1 * x1;
        features[26] = x1 * x1 * x2;
        features[27] = x1 * x1 * x3;
        features[28] = x1 * x2 * x2;
        features[29] = x1 * x2 * x3;
        features[30] = x1 * x3 * x3;
        features[31] = x2 * x2 * x2;
        features[32] = x2 * x2 * x3;
        features[33] = x2 * x3 * x3;
        features[34] = x3 * x3 * x3;

        return 0;
    }


    /*
     * Given a vlen and percentage of insert/read/update
     * output an iops (in kIOPS) from a trained model
     *
     * return -1 on error
     */
    inline double model_iops(double vlen, double insert, double read, double update) {
        double features[MAX_FEATURES];

        if (calculate_features(features, MAX_FEATURES, vlen, insert, read, update) < 0) {
            return -1;
        }

        double iops = 0;
        for (int i = 0; i < MAX_FEATURES; i++) {
            iops += coefficient[i] * features[i];
        }

        return iops + intercept;
    }

public:
    int64_t calc_latency(double insertp, double readp, double updatep, double deletep, int valuesize) {

        const double iops = (model_iops(valuesize, insertp, readp, updatep) * 1000);
        //fprintf(stderr, "ip = %f, rp = %f, latency = %f\n", insertp, readp, 1000000000l / iops);
        return 1000000000ULL / iops;
    }
};

class kv_history {
public:
    op_history<16>    op_window;
    value_history<16> val_window;
    latency_model     model;

    // separately measured
    // using default model
    kv_history() {}

    kv_history(std::vector<double> iops_model_coefficients) : model(iops_model_coefficients) {}

    void collect(const op_type type, const int valuesize) {
        op_window.add(type);
        val_window.add(valuesize);
        //op_window.print();
        //printf("mean value = %d\n", val_window.mean_value());
    }

    int64_t get_expected_latency_ns() {
        return model.calc_latency(op_window.get_insertp(), op_window.get_readp(), op_window.get_updatep(), op_window.get_deletep(), val_window.mean_value());
    }

};

class kv_timer {
    double g_TicksPerNanoSec;
    const uint64_t NANO_SECONDS_IN_SEC = 1000000000LL;
    int64_t clock_latency, diff_latency;
    uint64_t samples[10];
    uint64_t samples2[10];
public:
    inline uint64_t RDTSC() {
        //return __rdtsc();
        unsigned int lo,hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return ((uint64_t)hi << 32) | lo;
    }

    kv_timer() {
        uint64_t cycles_in_us = 0;
        int ret = calc_num_cycles_per_us(&cycles_in_us);
        if (ret != -1) {
            g_TicksPerNanoSec = cycles_in_us / 1000.0;
        }
        else
            g_TicksPerNanoSec = get_cycles_in_milli() / 1000000.0;

        struct timespec ts1;
        struct timespec ts2;
        struct timespec ts3;

        clock_gettime(CLOCK_MONOTONIC, &ts1);
        for (int i =0 ; i < 10; i++) {
            clock_gettime(CLOCK_MONOTONIC, &ts3);
            samples[i] = ts3.tv_sec;
        }
        clock_gettime(CLOCK_MONOTONIC, &ts2);
        clock_latency = TimeSpecDiff2(&ts2, &ts1) / 12;

        clock_gettime(CLOCK_MONOTONIC, &ts1);
        for (int i =0 ; i < 10; i++) {
            samples2[i] = TimeSpecDiff2(&ts2, &ts1);
        }
        clock_gettime(CLOCK_MONOTONIC, &ts2);

        diff_latency = (TimeSpecDiff2(&ts2, &ts1)) / 10;

        // fprintf(stderr, "clock latency = %ld\n", clock_latency);

    }

    void start2(struct timespec *begints) {

        clock_gettime(CLOCK_MONOTONIC, begints);
    }

    void wait_until2 (struct timespec *begints, int64_t expected_latency_ns) {
        if (expected_latency_ns <= 0) return;
        struct timespec endts;

        clock_gettime(CLOCK_MONOTONIC, &endts);

        expected_latency_ns -= (clock_latency) *2;
        int64_t nsecElapsed = TimeSpecDiff2(&endts, begints);
        while (nsecElapsed < expected_latency_ns) {
            clock_gettime(CLOCK_MONOTONIC, &endts);
            nsecElapsed = TimeSpecDiff2(&endts, begints);
            nsecElapsed += diff_latency + clock_latency;
        }

        //fprintf(stderr, "clock = %ld, diff = %ld, exepcted = %ld, queue = %ld\n", clock_latency,diff_latency, expected_latency_ns, nsecElapsed);
    }

    inline uint64_t TimeSpecDiff2(struct timespec *ts1, struct timespec *ts2)
    {
      struct timespec ts;
      ts.tv_sec = ts1->tv_sec - ts2->tv_sec;
      ts.tv_nsec = ts1->tv_nsec - ts2->tv_nsec;
      if (ts.tv_nsec < 0) {
        ts.tv_sec--;
        ts.tv_nsec += NANO_SECONDS_IN_SEC;
      }
      return ts.tv_sec * 1000000000LL + ts.tv_nsec;
    }

    uint64_t start() {
        return RDTSC();
    }

    void wait_until (uint64_t start, int64_t expected_latency_ns, uint64_t kv_emul_queue_latency) {
        if (expected_latency_ns <= 0) return;
        //fprintf(stderr, "exepcted = %ld, queue = %ld\n", expected_latency_ns, kv_emul_queue_latency);
        //kv_emul_queue_latency = 2000;
        uint64_t expected = (expected_latency_ns - kv_emul_queue_latency) * g_TicksPerNanoSec;
        while (RDTSC() - start < expected) {
            expected -= 100;
        }
    }

private:
#pragma GCC push_options
#pragma GCC optimize ("O0")

    void loop() {
        uint64_t i;
        for (i = 0; i < 1000000; i++); /* must be CPU intensive */
    }
#pragma GCC pop_options


    void x86timer() {

        uint64_t bench1=0;
        uint64_t bench2=0;

        bench1=RDTSC();

        usleep(250000); // 1/4 second

        bench2=RDTSC();

        g_TicksPerNanoSec=bench2-bench1;
        g_TicksPerNanoSec*=4.0e-9;

        fprintf(stderr, "clocks per second = %f\n", g_TicksPerNanoSec);
    }

    int calc_num_cycles_per_us(uint64_t *cycles_per_us) {
       struct timespec sleeptime = { 0, 0 };
       struct timespec ts_start, ts_end;

       /* Set the sleep-time to 0.5 s in nanoseconds                        */
       sleeptime.tv_nsec = 1000000000ULL / 2;

       if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts_start) == 0) {
          uint64_t tsc_start, tsc_end, ns;
          double usecs;

          tsc_start = RDTSC();
          nanosleep(&sleeptime, NULL); /* Sleep for 0.5 seconds             */
          clock_gettime(CLOCK_MONOTONIC_RAW, &ts_end);
          tsc_end = RDTSC();
          ns = ((ts_end.tv_sec  - ts_start.tv_sec) * 1000000000ULL);
          ns += (ts_end.tv_nsec - ts_start.tv_nsec);

          usecs = (double)ns / 1000;
          *cycles_per_us = (uint64_t)((tsc_end - tsc_start)/usecs);
          return 0;
       }
       return -1;
    }

    uint64_t get_cycles_in_milli() {
        int khz;
        FILE *fp =
            fopen("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "r");
        if (!fp) {
            fclose(fp);
            perror("cannot open cpu max frequency file");
            return 0;
        }
        int ret = fscanf(fp, "%d\n", &khz);
        if (!ret) {
            return -1;
        }
        fclose(fp);
        return khz;
    }

    void CalibrateTicks()
    {
      struct timespec begints, endts;
      uint64_t begin = 0, end = 0;
      clock_gettime(CLOCK_MONOTONIC, &begints);
      begin = RDTSC();
      loop();
      //uint64_t i;
      //for (i = 0; i < 1000000; i++); /* must be CPU intensive */
      end = RDTSC();
      clock_gettime(CLOCK_MONOTONIC, &endts);
      struct timespec *tmpts = TimeSpecDiff(&endts, &begints);
      uint64_t nsecElapsed = tmpts->tv_sec * 1000000000LL + tmpts->tv_nsec;
      g_TicksPerNanoSec = (double)(end - begin)/(double)nsecElapsed;

      //fprintf(stderr, "clocks per second = %f\n", (double)CLOCKS_PER_SEC / 1000000000LL);
      //fprintf(stderr, "clocks per second = %f\n", g_TicksPerNanoSec);
      //exit(1);

    }


    struct timespec *TimeSpecDiff(struct timespec *ts1, struct timespec *ts2)
    {
      static struct timespec ts;
      ts.tv_sec = ts1->tv_sec - ts2->tv_sec;
      ts.tv_nsec = ts1->tv_nsec - ts2->tv_nsec;
      if (ts.tv_nsec < 0) {
        ts.tv_sec--;
        ts.tv_nsec += NANO_SECONDS_IN_SEC;
      }
      return &ts;
    }

};

#endif
