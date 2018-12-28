// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// A database can be configured with a custom FilterPolicy object.
// This object is responsible for creating a small filter from a set
// of keys.  These filters are stored in insdb and are consulted
// automatically by insdb to decide whether or not to read some
// information from disk. In many cases, a filter can cut down the
// number of disk seeks form a handful to a single disk seek per
// DB::Get() call.
//
// Most people will want to use the builtin bloom filter support (see
// NewBloomFilterPolicy() below).

#ifndef STORAGE_INSDB_INCLUDE_FILTER_POLICY2_H_
#define STORAGE_INSDB_INCLUDE_FILTER_POLICY2_H_

#include <string>
#include "insdb/export.h"

namespace insdb {

    class Slice;

    class INSDB_EXPORT FilterPolicy2 {
        public:
            virtual ~FilterPolicy2();

            /**
              Add key to filter. For the successfully added key, filter must
              return true when it call KeyMayMatch().
              */
            virtual bool Add(const Slice &item) = 0;

            /**
              "filter" contains the data appended by a preceding call to
              Add() on this class.  This method must return true if
              the key was in the filter. This method may return true or false
              if the key was not on the filter, but it should aim to return false
              with a high probability.
              */
            virtual bool KeyMayMatch(const Slice &key) const = 0;

            /**
              Remove key from filter, to reduce false positive ratio, delete remove
              key from filter.
              */
            virtual void Delete(const Slice &key) = 0;

            // number of current inserted items;
            virtual size_t Count() const = 0;

            // size of the filter in bytes.
            virtual size_t MemUsage() const = 0;

            virtual std::string Info() const = 0;

    };

    /**
      Create New CuckooFilter Policy with given bit_per_key, and max_num_keys.
      */
    extern FilterPolicy2* NewCuckooFilterPolicy(size_t bits_per_key, size_t max_num_keys) ;

    /**
      Build CuckooFilter Policy with from saved image.
      num_items indicates current # of keys.
      bits_per_key indicates # of bits per key.
      max_num_key used to create bitmap buffer.
      bitmap indicates saved filter imagae.
      */
    extern FilterPolicy2* BuildCuckooFilterPolicy(size_t num_items, size_t bits_per_key, size_t max_num_keys, Slice &bitmap);

} // insdb namespace
#endif  // STORAGE_INSDB_INCLUDE_FILTER_POLICY2_H_
