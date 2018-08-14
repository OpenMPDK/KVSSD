/* 
 * Copyright 2008-2017 Aerospike, Inc.
 *
 * Portions may be licensed to Aerospike, Inc. under one or more contributor
 * license agreements.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License. You may obtain a copy of
 * the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations under
 * the License.
 */
#pragma once

#include <citrusleaf/cf_atomic.h>

#ifdef __linux__
#include <time.h>
#endif
    
#ifdef __APPLE__
#include <sys/time.h>
#endif
    
#ifdef CF_WINDOWS
#include <citrusleaf/cf_clock_win.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 * TYPES & CONSTANTS
 ******************************************************************************/

typedef uint64_t cf_clock;
typedef cf_atomic64 cf_atomic_clock;

#define CITRUSLEAF_EPOCH 1262304000
#define CITRUSLEAF_EPOCH_MS (CITRUSLEAF_EPOCH * 1000UL)
#define CITRUSLEAF_EPOCH_US (CITRUSLEAF_EPOCH * 1000000UL)
#define CITRUSLEAF_EPOCH_NS (CITRUSLEAF_EPOCH * 1000000000UL)

/******************************************************************************
 * FUNCTIONS
 ******************************************************************************/

cf_clock cf_getms();
cf_clock cf_getmicros();
cf_clock cf_getus();
cf_clock cf_getns();
cf_clock cf_clock_getabsolute();
cf_clock cf_get_seconds();
cf_clock cf_secs_since_clepoch();
void cf_set_wait_timespec(int ms_wait, struct timespec* out);
void cf_clock_current_add(struct timespec* delta, struct timespec* out);

/******************************************************************************
 * INLINE FUNCTIONS
 ******************************************************************************/

static inline void cf_clock_set_timespec_ms(int ms, struct timespec* out)
{
	out->tv_sec = ms / 1000;
	out->tv_nsec = (ms % 1000) * 1000 * 1000;
}

static inline cf_clock CF_TIMESPEC_TO_MS_P( struct timespec *ts ) {
    uint64_t r1 = ts->tv_nsec;
    r1 /= 1000000;
    uint64_t r2 = ts->tv_sec;
    r2 *= 1000;
    return( r1 + r2 );
}

static inline cf_clock CF_TIMESPEC_TO_MS( struct timespec ts ) {
    uint64_t r1 = ts.tv_nsec;
    r1 /= 1000000;
    uint64_t r2 = ts.tv_sec;
    r2 *= 1000;
    return ( r1 + r2 );
}

static inline cf_clock CF_TIMESPEC_TO_US( struct timespec ts ) {
    uint64_t r1 = ts.tv_nsec;
    r1 /= 1000;
    uint64_t r2 = ts.tv_sec;
    r2 *= 1000000;
    return ( r1 + r2 );
}

static inline cf_clock CF_TIMESPEC_TO_NS( struct timespec ts ) {
    return (uint64_t)ts.tv_nsec + ((uint64_t)ts.tv_sec * 1000000000);
}

static inline void CF_TIMESPEC_ADD_MS(struct timespec *ts, uint32_t ms) {
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (ms % 1000) * 1000000;
    if (ts->tv_nsec > 1000000000) {
        ts->tv_sec ++;
        ts->tv_nsec -= 1000000000;
    }
}

static inline uint32_t cf_clepoch_seconds() {
#ifdef __APPLE__
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec - CITRUSLEAF_EPOCH);
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint32_t)(ts.tv_sec - CITRUSLEAF_EPOCH);
#endif
}

static inline uint64_t cf_clepoch_milliseconds() {
#ifdef __APPLE__
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000) + (tv.tv_usec / 1000) - CITRUSLEAF_EPOCH_MS;
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return CF_TIMESPEC_TO_MS(ts) - CITRUSLEAF_EPOCH_MS;
#endif
}

// Convert from UTC nanosecond times to Citrusleaf epoch times.
// UTC nanosecond times before the Citrusleaf epoch are floored to return 0.

static inline uint64_t cf_clepoch_ns_from_utc_ns(uint64_t utc_ns) {
	return utc_ns > CITRUSLEAF_EPOCH_NS ? utc_ns - CITRUSLEAF_EPOCH_NS : 0;
}

static inline uint64_t cf_clepoch_us_from_utc_ns(uint64_t utc_ns) {
	return cf_clepoch_ns_from_utc_ns(utc_ns) / 1000;
}

static inline uint64_t cf_clepoch_ms_from_utc_ns(uint64_t utc_ns) {
	return cf_clepoch_ns_from_utc_ns(utc_ns) / 1000000;
}

static inline uint64_t cf_clepoch_sec_from_utc_ns(uint64_t utc_ns) {
	return cf_clepoch_ns_from_utc_ns(utc_ns) / 1000000000;
}

// Convert from Citrusleaf epoch times to UTC nanosecond times.
// Citrusleaf epoch times that cause overflow of uint64_t are not detected.

static inline uint64_t cf_utc_ns_from_clepoch_ns(uint64_t clepoch_ns) {
	return CITRUSLEAF_EPOCH_NS + clepoch_ns;
}

static inline uint64_t cf_utc_ns_from_clepoch_us(uint64_t clepoch_us) {
	return CITRUSLEAF_EPOCH_NS + (clepoch_us * 1000);
}

static inline uint64_t cf_utc_ns_from_clepoch_ms(uint64_t clepoch_ms) {
	return CITRUSLEAF_EPOCH_NS + (clepoch_ms * 1000000);
}

static inline uint64_t cf_utc_ns_from_clepoch_sec(uint64_t clepoch_sec) {
	return CITRUSLEAF_EPOCH_NS + (clepoch_sec * 1000000000);
}

// Special client-only conversion utility.
static inline uint32_t cf_server_void_time_to_ttl(uint32_t server_void_time) {
	// This is the server's flag indicating the record never expires...
	if (server_void_time == 0) {
		// ... converted to the new client-side convention for "never expires":
		return (uint32_t)-1;
	}

	uint32_t now = cf_clepoch_seconds();

	// Record may not have expired on server, but delay or clock differences may
	// cause it to look expired on client. (We give the record to the app anyway
	// to avoid internal cleanup complications.) Floor at 1, not 0, to avoid old
	// "never expires" interpretation.
	return server_void_time > now ? server_void_time - now : 1;
}

/******************************************************************************/

#ifdef __cplusplus
} // end extern "C"
#endif
