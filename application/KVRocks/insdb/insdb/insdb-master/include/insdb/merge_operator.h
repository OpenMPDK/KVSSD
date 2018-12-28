// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.

#ifndef STORAGE_INSDB_INCLUDE_MERGE_OPERATOR_H_
#define STORAGE_INSDB_INCLUDE_MERGE_OPERATOR_H_

#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "insdb/slice.h"

namespace insdb {

class Slice;
class Logger;

// The Merge Operator
//
// Essentially, a MergeOperator specifies the SEMANTICS of a merge, which only
// client knows. It could be numeric addition, list append, string
// concatenation, edit data structure, ... , anything.
// The library, on the other hand, is concerned with the exercise of this
// interface, at the right time (during get, iteration, compaction...)
//
// To use merge, the client needs to provide an object implementing one of
// the following interfaces:
//  a) AssociativeMergeOperator - for most simple semantics (always take
//    two values, and merge them into one value, which is then put back
//    into insdb); numeric addition and string concatenation are examples;
//
//  b) MergeOperator - the generic class for all the more abstract / complex
//    operations; one method (FullMergeV2) to merge a Put/Delete value with a
//    merge operand; and another method (PartialMerge) that merges multiple
//    operands together. this is especially useful if your key values have
//    complex structures but you would still like to support client-specific
//    incremental updates.
//
// AssociativeMergeOperator is simpler to implement. MergeOperator is simply
// more powerful.
//
// INSDB does not support complex MergeOperator
//


// The simpler, associative merge operator.
class AssociativeMergeOperator {
 public:
  virtual ~AssociativeMergeOperator() {}

  // Gives the client a way to express the read -> modify -> write semantics
  // key:           (IN) The key that's associated with this merge operation.
  // existing_value:(IN) null indicates the key does not exist before this op
  // value:         (IN) the value to update/merge the existing_value with
  // new_value:    (OUT) Client is responsible for filling the merge result
  // here. The string that new_value is pointing to will be empty.
  // logger:        (IN) Client could use this to log errors during merge.
  //
  // Return true on success.
  // All values passed in will be client-specific values. So if this method
  // returns false, it is because client specified bad data or there was
  // internal corruption. The client should assume that this will be treated
  // as an error by the library.
  virtual bool Merge(const Slice& key,
                     const Slice* existing_value,
                     const Slice& value,
                     std::string* new_value,
                     Logger* logger) const = 0;


  // The name of the MergeOperator. Used to check for MergeOperator
  // mismatches (i.e., a DB created with one MergeOperator is
  // accessed using a different MergeOperator)
  // TODO: the name is currently not stored persistently and thus
  //       no checking is enforced. Client is responsible for providing
  //       consistent MergeOperator between DB opens.
  virtual const char* Name() const = 0;
};

}  // namespace insdb

#endif  // STORAGE_INSDB_INCLUDE_MERGE_OPERATOR_H_
