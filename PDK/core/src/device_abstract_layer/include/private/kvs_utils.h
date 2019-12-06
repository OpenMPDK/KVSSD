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
#include <stdarg.h>
#include <stdlib.h>
#include <iostream>
#include <sstream>
#include <fstream>
#include <mutex>
#include <dirent.h>
#include <unistd.h>
#include "kvs_adi.h"

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
	static std::ofstream out("/tmp/libkvadi.log", std::ios::out | std::ios::app);

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
		yprintf(std::cout, "can't open /tmp/libkvadi.log\n");
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
	#define WRITE_ERR(...) do { write_err(__VA_ARGS__);} while (0)
	#define WRITE_WARN(...) write_warn(stderr, __VA_ARGS__)
	#define WRITE_INFO(...) write_info(stdout, __VA_ARGS__)
	#define WRITE_LOG(...) logprintf(__VA_ARGS__)
#else
	#define WRITE_ERR(...) do { write_err(__VA_ARGS__);} while (0)
	#define WRITE_WARN(...) write_warn(stderr, __VA_ARGS__)
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

// validate key and value
static inline kv_result validate_key_value(const kv_key *key, const kv_value *value) {
    if (key->key == NULL) {
        return KV_ERR_KEY_INVALID;
    }
    if (key->length > SAMSUNG_KV_MAX_KEY_LEN || key->length < SAMSUNG_KV_MIN_KEY_LEN) {
        return KV_ERR_KEY_LENGTH_INVALID;
    }

    if (value != NULL) {
        if (value->length < SAMSUNG_KV_MIN_VALUE_LEN || value->length > SAMSUNG_KV_MAX_VALUE_LEN) {
            return KV_ERR_VALUE_LENGTH_INVALID;
        }
        if(value->offset & (KV_ALIGNMENT_UNIT - 1)) {
          return KV_ERR_MISALIGNED_VALUE_OFFSET;
        }
    }

    return KV_SUCCESS;
}

#endif /* INCLUDE_KVS_UTILS_H_ */
