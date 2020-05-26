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


#ifndef INCLUDE_KVS_UTILS_H_
#define INCLUDE_KVS_UTILS_H_

#include <algorithm>
#include <stdio.h>
#include <kvs_api.h>
#include <stdarg.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <mutex>
#include <dirent.h>
#include <unistd.h>
#include <numa.h>
//#include <private_types.h>

inline void yprintf(std::ostream &stream, const char* fmt, ...)
{
    static char buffer[1024] = "";

    va_list argptr;
    int written = 0;
    va_start(argptr,fmt);

    //vsprintf(buffer, fmt, argptr);
    written = vsprintf(buffer, fmt, argptr);


    va_end(argptr);
    buffer[written] = '\0';
    stream << buffer;
}

inline void logprintf(const char* fmt, ...)
{
	static std::mutex mutex;
	static std::ofstream out("/tmp/libsirius.log", std::ios::out | std::ios::app);

	if (out.is_open()) {
		static char buffer[1024] = "";

		va_list argptr;
		int written = 0;
		va_start(argptr,fmt);

		written = vsprintf(buffer, fmt, argptr);

		va_end(argptr);
		buffer[written] = '\0';// just in case if the buffer overruns
		{
			std::unique_lock<std::mutex> lck(mutex);
			out<< "[INFO] ";
			out<< buffer;
			out.flush();
		}
	} else {
		yprintf(std::cout, "can't open /tmp/libsirius.log\n");
	}
}

inline void write_err(const char* format, ... ) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[ERROR] ");
    vfprintf(stderr, format, args);
    va_end(args);
}

inline void write_warn(FILE * out, const char* format, ... ) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[WARN] ");
    vfprintf(stderr, format, args);
    va_end(args);
}

inline void write_info(FILE * out, const char* format, ... ) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "[INFO] ");
    vfprintf(stderr, format, args);
    va_end(args);
}


#ifdef ENABLE_LOGGING
	#define WRITE_ERR(...) do { write_err(__VA_ARGS__); } while (0)
	#define WRITE_WARNING(...) write_warn(stderr, __VA_ARGS__)
	#define WRITE_INFO(...) write_info(stdout, __VA_ARGS__)
	#define WRITE_LOG(...) logprintf(__VA_ARGS__)
#else
	#define WRITE_ERR(...) do { write_err(__VA_ARGS__); } while (0)
	#define WRITE_WARNING(...) write_warn(stderr, __VA_ARGS__)
	#define WRITE_INFO(...) write_info(stdout, __VA_ARGS__)
	#define WRITE_LOG(...)
#endif



inline unsigned long get_curcpu(int *chip, int *core)
{
    unsigned long a,d,c;
    __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));
    *chip = (c & 0xFFF000)>>12;
    *core = c & 0xFFF;
    return ((unsigned long)a) | (((unsigned long)d) << 32);;
}
/*
inline void kvs_print_env_opts(kvs_init_options* options) {
  fprintf(stderr, "kvs_init_options: memsize %7ldMB, cachesize %7ldMB, use DPDK = %s { mastercore = %d%s}, use AIO = %s",
	  
			options->memory.max_memorysize_mb,
			options->memory.max_cachesize_mb,
			(options->memory.use_dpdk == 1)? "true":"false",
			options->memory.dpdk_mastercoreid,
			(options->memory.dpdk_mastercoreid == -1)? "(current core)":"",
			(options->aio.iocomplete_fn != 0)? "true":"false"
	);

	if ((options->aio.iocomplete_fn != 0))
		fprintf(stderr, " { iocores 0x%lx qdepth %d io_fn %p }",
				options->aio.iocoremask,
				options->aio.queuedepth,
				options->aio.iocomplete_fn);

	fprintf(stderr, "\n");
}
*/
inline int run_shellcmd(const char *buf)
{
    int status = system(buf);
    return WEXITSTATUS(status);
}

inline std::string run_shellcmd_with_output(const char *cmd) {
	FILE *in;
	std::stringstream ss;
	char buff[512];
	if (!(in = popen(cmd, "r"))) {
		return "";
	}

	while (fgets(buff, sizeof(buff), in) != NULL) {
		ss << buff;
	}

	pclose(in);
	return ss.str();
}


inline std::string do_readlink(std::string const& path) {
	char buff[PATH_MAX];
	ssize_t len = ::readlink(path.c_str(), buff, sizeof(buff) - 1);
	if (len != -1) {
		buff[len] = '\0';
		return std::string(buff);
	} else
		return "";
	/* handle error condition */
}





inline bool is_directory_empty_or_notexist(const char *path) {
	DIR *dp;
	int i = 0;
	struct dirent *ep;
	dp = opendir(path);

	if (dp != NULL) {
		while ((ep = readdir(dp)) != NULL) {
			i++;
			if (i > 2)
				return false;
		}
		(void) closedir(dp);
	}

	return true;
}


inline void *numa_aligned_alloc(const int socketid, int alignment, uint64_t size) {
    void *base;
    void **aligned;
    uint64_t total = size + alignment - 1 + sizeof(void*) + sizeof(uint64_t);
    if (socketid != -1)
        base = numa_alloc_onnode(total, socketid);
    else
        base= numa_alloc_local(total);

    aligned = (void**)((((uint64_t)base) + total) & ~(alignment - 1));
    aligned[-1]  = base;
    aligned[-2]  = (void*)total;
    return aligned;
}

inline void numa_aligned_free(void *p) {
    void *ptr = (((void**)p)[-1]);
    uint64_t size = (uint64_t)(((void**)p)[-2]);
    numa_free(ptr, size);
}

// trim from start (in place)
static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
        return !std::isspace(ch);
    }));
}

// trim from end (in place)
static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

// trim from both ends (in place)
static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
}

static inline int32_t validate_kv_pair_(
  const kvs_key *key, const kvs_value *value, uint32_t max_valid_val_len){
  if (key) {
    if(key->length < KVS_MIN_KEY_LENGTH || key->length > KVS_MAX_KEY_LENGTH) {
      WRITE_WARNING("key size is out of range, key size = %d\n", key->length);
      return KVS_ERR_KEY_LENGTH_INVALID;
    }
    if(key->key == NULL){
      WRITE_WARNING("key buffer inputted is NULL\n");
      return KVS_ERR_PARAM_INVALID;
    }
  }
  if(value) {
    if(value->length < KVS_MIN_VALUE_LENGTH || value->length > max_valid_val_len) {
      WRITE_WARNING("value size is out of range, value size = %d\n", value->length);
      return KVS_ERR_VALUE_LENGTH_INVALID;
    }
    if(value->offset & (KVS_ALIGNMENT_UNIT - 1)) {
      return KVS_ERR_VALUE_OFFSET_MISALIGNED;
    }
    if(value->value == NULL && value->length > 0){
      WRITE_WARNING("value buffer inputted is NULL\n");
      return KVS_ERR_PARAM_INVALID;
    }
  }
  return KVS_SUCCESS;
}
static inline int32_t validate_request(const kvs_key *key, const kvs_value *value) {
  return validate_kv_pair_(key, value, KVS_MAX_VALUE_LENGTH);
}
#endif /* INCLUDE_KVS_UTILS_H_ */
