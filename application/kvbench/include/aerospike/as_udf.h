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

#include <aerospike/as_bytes.h>
#include <aerospike/as_list.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 *	Maximum number of bytes in UDF module name.
 */
#define AS_UDF_MODULE_MAX_SIZE 64

/**
 *	Maximum number of chars allows in UDF module name.	
 */
#define AS_UDF_MODULE_MAX_LEN (AS_UDF_MODULE_MAX_SIZE - 1)

/**
 *	Maximum number of bytes in UDF module name.
 */
#define AS_UDF_FUNCTION_MAX_SIZE 64

/**
 *	Maximum number of chars allows in UDF function name.	
 */
#define AS_UDF_FUNCTION_MAX_LEN (AS_UDF_FUNCTION_MAX_SIZE - 1)

/**
 *	The size of a UDF file name
 */
#define AS_UDF_FILE_NAME_SIZE 128

/**
 *	The size of a UDF file name
 */
#define AS_UDF_FILE_NAME_SIZE 128

/**
 * The maxium string length of the UDF file name
 */
#define AS_UDF_FILE_NAME_LEN AS_UDF_FILE_NAME_SIZE - 1

/**
 *	The size of a UDF hash value
 */
#define AS_UDF_FILE_HASH_SIZE (20 * 2)

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	UDF Module Name
 */
typedef char as_udf_module_name[AS_UDF_MODULE_MAX_SIZE];

/**
 *	UDF Function Name
 */
typedef char as_udf_function_name[AS_UDF_FUNCTION_MAX_SIZE];

/**
 *	Defines a call to a UDF
 */
typedef struct as_udf_call_s {

	/**
	 *	@private
	 *	If true, then as_udf_call_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	UDF Module containing the function to be called.
	 */
	as_udf_module_name module;

	/**
	 *	UDF Function to be called
	 */
	as_udf_function_name function;

	/**
	 *	Argument List
	 */
	as_list * arglist;
	
} as_udf_call;

/**
 *	Enumeration of UDF types
 */
typedef enum as_udf_type_e {

	/**
	 *	Lua
	 */
	AS_UDF_TYPE_LUA

} as_udf_type;

/**
 *	UDF File
 */
typedef struct as_udf_file_s {

	/**
	 *	@private
	 *	If true, then as_udf_file_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	Name of the UDF file
	 */
	char name[AS_UDF_FILE_NAME_SIZE];

	/** 
	 *	Hash value of the file contents
	 */
	uint8_t hash[AS_UDF_FILE_HASH_SIZE+1];

	/**
	 *	The type of UDF
	 */
	as_udf_type type;

	/**
	 *	UDF File contents
	 */
	struct {

		/**
	 	 *	@private
		 *	If true, then as_udf_file_destroy() will free bytes()
		 */
		bool _free;

		/**
		 *	Number of bytes allocated to bytes.
		 */
		uint32_t capacity;

		/**
		 *	Number of bytes used in bytes.
		 */
		uint32_t size;

		/**
		 *	Sequence of bytes
		 */
		uint8_t * bytes;

	} content;

} as_udf_file;

/**
 *	Sequence of UDF Files
 */
typedef struct as_udf_files_s {

	/**
	 *	@private
	 *	If true, then as_udf_list_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	Number of file entries allocated to files.
	 */
	uint32_t capacity;

	/**
	 *	Number of used file entries in files.
	 */
	uint32_t size;

	/**
	 *	Sequence of files.
	 */
	as_udf_file * entries;

} as_udf_files;

/******************************************************************************
 *	UDF CALL FUNCTIONS
 *****************************************************************************/

/**
 *	Initialize a stack allocated as_udf_call.
 *
 *	@param call 		The call to initialize.
 *	@param module 		The UDF module.
 *	@param function 	The UDF function.
 *	@param arglist 		The UDF argument list.
 *
 *	@return The initialized call on success. Otherwise NULL.
 */
as_udf_call * as_udf_call_init(as_udf_call * call, const as_udf_module_name module, const as_udf_function_name function, as_list * arglist);

/**
 *	Creates a new heap allocated as_udf_call.
 *	@param module 		The UDF module.
 *	@param function 	The UDF function.
 *	@param arglist 		The UDF argument list.
 *
 *	@return The newly allocated call on success. Otherwise NULL.
 */
as_udf_call * as_udf_call_new(const as_udf_module_name module, const as_udf_function_name function, as_list * arglist);

/**
 *	Destroy an as_udf_call.
 */
void as_udf_call_destroy(as_udf_call * call);

/******************************************************************************
 *	UDF FILE FUNCTIONS
 *****************************************************************************/

/**
 *	Initialize a stack allocated as_udf_file.
 *
 *	@returns The initialized udf file on success. Otherwise NULL.
 */
as_udf_file * as_udf_file_init(as_udf_file * file);

/**
 *	Creates a new heap allocated as_udf_file.
 *
 *	@returns The newly allocated udf file on success. Otherwise NULL.
 */
as_udf_file * as_udf_file_new();

/**
 *	Destroy an as_udf_file.
 */
void as_udf_file_destroy(as_udf_file * file);

/******************************************************************************
 *	UDF LIST FUNCTIONS
 *****************************************************************************/

/**
 *	Initialize a stack allocated as_udf_files.
 *
 *	@returns The initialized udf list on success. Otherwise NULL.
 */
as_udf_files * as_udf_files_init(as_udf_files * files, uint32_t capacity);

/**
 *	Creates a new heap allocated as_udf_files.
 *
 *	@returns The newly allocated udf list on success. Otherwise NULL.
 */
as_udf_files * as_udf_files_new(uint32_t capacity);

/**
 *	Destroy an as_udf_files.
 */
void as_udf_files_destroy(as_udf_files * files);

#ifdef __cplusplus
} // end extern "C"
#endif
