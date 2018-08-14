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

#include <aerospike/as_operations.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/
	
/**
 *  Add an as_val element to end of list.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param val			Value to append. Consumes a reference of this as_val.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_append(as_operations* ops, const as_bin_name name, as_val* val);

/**
 *  Add an integer to end of list. Convenience function of as_operations_add_list_append()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param value		An integer value.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_append_int64(as_operations* ops, const as_bin_name name, int64_t value);

/**
 *  Add a double to end of list. Convenience function of as_operations_add_list_append()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param value		A double value.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_append_double(as_operations* ops, const as_bin_name name, double value);

/**
 *  Add a string to end of list. Convenience function of as_operations_add_list_append()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param value		A c-string.
 *	@param free			If true, then the value will be freed when the operations is destroyed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_append_strp(as_operations* ops, const as_bin_name name, const char* value, bool free);

/**
 *  Add a string to end of list. Convenience function of as_operations_add_list_append()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param value		A c-string.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
static inline bool
as_operations_add_list_append_str(as_operations* ops, const as_bin_name name, const char* value)
{
	return as_operations_add_list_append_strp(ops, name, value, false);
}

/**
 *  Add a blob to end of list. Convenience function of as_operations_add_list_append()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param value		A blob.
 *	@param size 		Size of the blob.
 *	@param free			If true, then the value will be freed when the operations is destroyed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_append_rawp(as_operations* ops, const as_bin_name name, const uint8_t* value, uint32_t size, bool free);

/**
 *  Add a blob to end of list. Convenience function of as_operations_add_list_append()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param value		A blob.
 *	@param size 		Size of the blob.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
static inline bool
as_operations_add_list_append_raw(as_operations* ops, const as_bin_name name, const uint8_t* value, uint32_t size)
{
	return as_operations_add_list_append_rawp(ops, name, value, size, false);
}

/**
 *  Add multiple values to end of list.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	@param list			List of values to append. Consumes a reference of this as_list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_append_items(as_operations* ops, const as_bin_name name, as_list *list);

/**
 *  Insert an as_val element to list at index position.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the as_val will be inserted at. Negative index counts from end of list.
 *	@param val			Value to insert. Consumes a reference of this as_list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_insert(as_operations* ops, const as_bin_name name, int64_t index, as_val* val);

/**
 *  Insert integer to list at index position. Convenience function of as_operations_add_list_insert()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the integer will be inserted at. Negative index counts from end of list.
 *	@param value 		An integer value.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_insert_int64(as_operations* ops, const as_bin_name name, int64_t index, int64_t value);

/**
 *  Insert double to list at index position. Convenience function of as_operations_add_list_insert()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the double will be inserted at. Negative index counts from end of list.
 *	@param value 		A double value.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_insert_double(as_operations* ops, const as_bin_name name, int64_t index, double value);

/**
 *  Insert string to list at index position. Convenience function of as_operations_add_list_insert()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the string will be inserted at. Negative index counts from end of list.
 *	@param value 		A c-string.
 *	@param free			If true, then the value will be freed when the operations is destroyed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_insert_strp(as_operations* ops, const as_bin_name name, int64_t index, const char* value, bool free);

/**
 *  Insert string to list at index position. Convenience function of as_operations_add_list_insert()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the string will be inserted at. Negative index counts from end of list.
 *	@param value 		A c-string.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
static inline bool
as_operations_add_list_insert_str(as_operations* ops, const as_bin_name name, int64_t index, const char* value)
{
	return as_operations_add_list_insert_strp(ops, name, index, value, false);
}

/**
 *  Insert blob to list at index position. Convenience function of as_operations_add_list_insert()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the blob will be inserted at. Negative index counts from end of list.
 *	@param value 		A blob.
 *	@param size 		Size of the blob.
 *	@param free			If true, then the value will be freed when the operations is destroyed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_insert_rawp(as_operations* ops, const as_bin_name name, int64_t index, const uint8_t* value, uint32_t size, bool free);

/**
 *  Insert blob to list at index position. Convenience function of as_operations_add_list_insert()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the blob will be inserted at. Negative index counts from end of list.
 *	@param value 		A blob.
 *	@param size 		Size of the blob.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
static inline bool
as_operations_add_list_insert_raw(as_operations* ops, const as_bin_name name, int64_t index, const uint8_t* value, uint32_t size)
{
	return as_operations_add_list_insert_rawp(ops, name, index, value, size, false);
}

/**
 *  Insert multiple values to list at index position.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position which the blob will be inserted at. Negative index counts from end of list.
 *	@param list 		List of values to insert. Consumes reference of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_insert_items(as_operations* ops, const as_bin_name name, int64_t index, as_list *list);

/**
 *  Remove and return a value at index.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which the value will be removed and returned.  Negative index counts from end of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_pop(as_operations* ops, const as_bin_name name, int64_t index);

/**
 *  Remove and return N values from index.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start the removal. Negative index counts from end of list.
 *  @param count 		Number of values to remove. If not enough values in list, will remove to list end.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_pop_range(as_operations* ops, const as_bin_name name, int64_t index, uint64_t count);

/**
 *  Remove and return all values from index to the end of list.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start the removal. Negative index counts from end of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_pop_range_from(as_operations* ops, const as_bin_name name, int64_t index);

/**
 *  Remove value at index.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start the removal. Negative index counts from end of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_remove(as_operations* ops, const as_bin_name name, int64_t index);

/**
 *  Remove N values from index.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start the removal. Negative index counts from end of list.
 *  @param count 		Number of values to remove. If not enough values in list, will remove to list end.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_remove_range(as_operations* ops, const as_bin_name name, int64_t index, uint64_t count);

/**
 *  Remove all values from index until end of list.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start the removal. Negative index counts from end of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_remove_range_from(as_operations* ops, const as_bin_name name, int64_t index);

//-----------------------------------------------------------------------------
// Other list modifies

/**
 *  Remove all values. Will leave empty list in bin.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_clear(as_operations* ops, const as_bin_name name);

/**
 *  Set an as_val element of the list at the index position.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param val			Consumes a reference of this as_val.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_set(as_operations* ops, const as_bin_name name, int64_t index, as_val* val);

/**
 *  Set value at index as integer. Convenience function of as_operations_add_list_set()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param value		An integer value.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_set_int64(as_operations* ops, const as_bin_name name, int64_t index, int64_t value);

/**
 *  Set value at index as double. Convenience function of as_operations_add_list_set()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param value		A double value.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_set_double(as_operations* ops, const as_bin_name name, int64_t index, double value);

/**
 *  Set value at index as string. Convenience function of as_operations_add_list_set()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param value		A c-string.
 *	@param free			If true, then the value will be freed when the operations is destroyed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_set_strp(as_operations* ops, const as_bin_name name, int64_t index, const char* value, bool free);

/**
 *  Set value at index as string. Convenience function of as_operations_add_list_set()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param value		A c-string.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
static inline bool
as_operations_add_list_set_str(as_operations* ops, const as_bin_name name, int64_t index, const char* value)
{
	return as_operations_add_list_set_strp(ops, name, index, value, false);
}

/**
 *  Set value at index as blob. Convenience function of as_operations_add_list_set()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param value		A blob.
 *	@param size 		Size of the blob.
 *	@param free			If true, then the value will be freed when the operations is destroyed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_set_rawp(as_operations* ops, const as_bin_name name, int64_t index, const uint8_t* value, uint32_t size, bool free);

/**
 *  Set value at index as blob. Convenience function of as_operations_add_list_set()
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to set. Negative index counts from end of list.
 *  @param value		A blob.
 *	@param size 		Size of the blob.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
static inline bool
as_operations_add_list_set_raw(as_operations* ops, const as_bin_name name, int64_t index, const uint8_t* value, uint32_t size)
{
	return as_operations_add_list_set_rawp(ops, name, index, value, size, false);
}

/**
 * 	Remove values NOT within range(index, count).
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Values from 0-index position are removed. Negative index counts from end of list.
 *  @param count 		Number of values to keep. All other values beyond count are removed.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_trim(as_operations* ops, const as_bin_name name, int64_t index, uint64_t count);

/**
 *	Create list increment operation.
 *	Server increments value at index by incr and returns final result.
 *	Valid only for numbers.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_increment(as_operations *ops, const as_bin_name name, int64_t index, as_val *incr);

//-----------------------------------------------------------------------------
// Read operations

/**
 *  Get value of list at index.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position to get. Negative index counts from end of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_get(as_operations* ops, const as_bin_name name, int64_t index);

/**
 *  Get multiple values of list starting at index.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start. Negative index counts from end of list.
 * 	@param count 		Number of values to get. If not enough in list, will return all remaining.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_get_range(as_operations* ops, const as_bin_name name, int64_t index, uint64_t count);

/**
 * Get multiple values of list starting at index until end of list.
 *
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *  @param index 		Index position at which to start. Negative index counts from end of list.
 *
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_get_range_from(as_operations* ops, const as_bin_name name, int64_t index);

/**
 * Get number of values in list.
 *  
 *	@param ops			The `as_operations` to append the operation to.
 *	@param name 		The name of the bin to perform the operation on.
 *	
 *  @return true on success. Otherwise an error occurred.
 *
 *	@relates as_operations
 *	@ingroup as_operations_object
 */
bool
as_operations_add_list_size(as_operations* ops, const as_bin_name name);

#ifdef __cplusplus
} // end extern "C"
#endif
