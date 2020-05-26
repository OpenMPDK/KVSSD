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

#include <string>
#include <fstream>
#include "kvs_adi.h"
#include "kvs_adi_internal.h"
#include "kv_config.hpp"

namespace kvadi {

const std::string kv_config::mean_section_name = "latency";
const std::string kv_config::stdev_section_name = "latency";

// return a random latency based on uniform distribution of latency
// measurement. This number will be read from a config file
// latency in nanoseconds
uint32_t kv_config::generate_sample_latency(uint16_t opcode) {

    uint32_t mean_latency = get_mean_latency(opcode);
    uint32_t stdev_latency = get_stdev_latency(opcode);
    
    // if not configured, default to 0
    if (mean_latency == 0) {
        return 0;
    }

    // WRITE_INFO("mean %d stdev %d\n", mean_latency, stdev_latency);
    std::normal_distribution<float> distribution(mean_latency, stdev_latency);

    uint32_t nanosec = uint32_t(distribution(m_generator));

    // limit 50% swing for standard deviation
    // avoid negative number
    uint32_t lower_bound = uint32_t(mean_latency - stdev_latency *.5);
    if (lower_bound < 0) {
        lower_bound = 0;
    }
    uint32_t upper_bound = uint32_t(mean_latency + stdev_latency *.5);

    if (nanosec < lower_bound) {
        nanosec = lower_bound;
    } else if (nanosec > upper_bound) {
        nanosec = upper_bound;
    }

    return nanosec;
}

std::string kv_config::getkv(std::string const& section, std::string const& key) const {
    std::unordered_map<std::string, std::string>::const_iterator it = m_config_kv.find(section + '/' + key);
    if (it == m_config_kv.end()) {
        return "";
    }

    return it->second;
}

static std::string opcode_to_string(uint16_t opcode) {
    std::string str("");
    switch (opcode) {
        case KV_OPC_GET:
            str = "GET";
            break;
        case KV_OPC_STORE:
            str = "STORE";
            break;
        case KV_OPC_DELETE:
            str = "DELETE";
            break;
        case KV_OPC_PURGE:
            str = "PURGE";
            break;
        case KV_OPC_CHECK_KEY_EXIST:
            str = "CHECK_KEY_EXIST";
            break;
        case KV_OPC_OPEN_ITERATOR:
            str = "OPEN_ITERATOR";
            break;
        case KV_OPC_CLOSE_ITERATOR:
            str = "CLOSE_ITERATOR";
            break;
        case KV_OPC_ITERATE_NEXT:
            str = "ITERATE_NEXT";
            break;
        case KV_OPC_SANITIZE_DEVICE:
            str = "SANITIZE_DEVICE";
            break;
    }
    return str;
}


// get configured mean latency for an IO operations
uint32_t kv_config::get_mean_latency(uint16_t opcode) {
    std::string keyname = opcode_to_string(opcode); 
    auto it = m_config_kv.find(kv_config::mean_section_name + "/" + keyname + ".mean");
    // default is 0
    std::string latency = "0";
    if (it != m_config_kv.end()) {
        latency  = it->second;
    }
    return std::stoi(latency, NULL, 10);
}

// get configured standard deviation for an IO operations
uint32_t kv_config::get_stdev_latency(uint16_t opcode) {

    std::string keyname = opcode_to_string(opcode); 
    auto it = m_config_kv.find(kv_config::stdev_section_name + "/" + keyname + ".stdev");
    // default is 0
    std::string latency = "0";
    if (it != m_config_kv.end()) {
        latency  = it->second;
    }
    return std::stoi(latency, NULL, 10);
}


// static function to trim white space
std::string kv_config::trim(std::string const& source, char const* delims = " \t\r\n") {

    std::string result(source);
    std::string::size_type index = result.find_last_not_of(delims);
    if(index != std::string::npos)
      result.erase(++index);
    
    index = result.find_first_not_of(delims);
    if(index != std::string::npos)
      result.erase(0, index);
    else
      result.erase();
    return result;
}

kv_config::kv_config(const std::string configfile) {
    load_config(configfile);
}

bool kv_config::load_config(const std::string configfile) {

    // load from default config file
    std::ifstream file(configfile.c_str());
    
    std::string line;
    std::string key;
    std::string value;
    std::string section_name;
    int equal_pos;

    while (std::getline(file,line)) {
      line = trim(line);
      if (! line.length()) continue;
      if (line[0] == '#') continue;
      if (line[0] == ';') continue;
    
      if (line[0] == '[') {
        section_name = trim(line.substr(1,line.find(']')-1));
        continue;
      }
    
      equal_pos = line.find('=');
      key = trim(line.substr(0, equal_pos));
      value = trim(line.substr(equal_pos + 1));
    
      // WRITE_WARN("found [%s] %s = %s\n", section_name.c_str(), key.c_str(), value.c_str());
      m_config_kv.insert(std::make_pair(section_name + "/" + key, value));
    }

    return true;
}


} // end of namespace
