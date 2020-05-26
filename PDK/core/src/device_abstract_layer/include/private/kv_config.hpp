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

#ifndef _KV_CONFIG_INCLUDE_H_
#define _KV_CONFIG_INCLUDE_H_
#include <unordered_map>
#include <random>

// define basic configuration
namespace kvadi {

class kv_config {
public:
    kv_config(std::string configfile);
    ~kv_config() {}

    // load from config file
    bool load_config(std::string configfile);
     
    // based on configuration. generate a random latency in nanoseconds in normal
    // distribution
    uint32_t generate_sample_latency(uint16_t opcode);

    // return empty string usually means not configured
    std::string getkv(std::string const& section, std::string const& key) const;
        
    // static function to trim white space
    static std::string trim(std::string const& source, char const* delims);

    // default section name for mean
    static const std::string mean_section_name;
    // default section name for standard deviation
    static const std::string stdev_section_name;

private:
    // get configured mean latency for an IO operations
    uint32_t get_mean_latency(uint16_t opcode);
    // get configured standard deviation for an IO operations
    uint32_t get_stdev_latency(uint16_t opcode);

    // record configured mean IO latency in nanoseconds for
    // different operations
    // unsigned int seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine m_generator;
    // std::normal_distribution<float> m_distribution(mean, stdev);

    // configured key value pairs in the form of
    // [section]
    //      key = value
    // stored as:
    // "section/key" ==> "value"
    //
    // default section is global
    std::unordered_map<std::string, std::string> m_config_kv;
};

} // end of name space
#endif // end of include
