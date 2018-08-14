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
#pragma GCC diagnostic ignored "-Waddress"

#include <aerospike/as_bin.h>
#include <aerospike/as_key.h>
#include <aerospike/as_predexp.h>
#include <aerospike/as_udf.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 *	Default value for as_scan.priority
 */
#define AS_SCAN_PRIORITY_DEFAULT AS_SCAN_PRIORITY_AUTO

/**
 *	Default value for as_scan.percent
 */
#define AS_SCAN_PERCENT_DEFAULT 100

/**
 *	Default value for as_scan.no_bins
 */
#define AS_SCAN_NOBINS_DEFAULT false

/**
 *	Default value for as_scan.concurrent
 */
#define AS_SCAN_CONCURRENT_DEFAULT false

/**
 *	Default value for as_scan.deserialize_list_map
 */
#define AS_SCAN_DESERIALIZE_DEFAULT true

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	Priority levels for a scan operation.
 */
typedef enum as_scan_priority_e { 

	/**
	 *	The cluster will auto adjust the scan priority.
	 */
	AS_SCAN_PRIORITY_AUTO, 

	/**
	 *	Low priority scan.
	 */
	AS_SCAN_PRIORITY_LOW,

	/**
	 *	Medium priority scan.
	 */ 
	AS_SCAN_PRIORITY_MEDIUM,

	/**
	 *	High priority scan.
	 */ 
	AS_SCAN_PRIORITY_HIGH
	
} as_scan_priority;

/**
 *	The status of a particular background scan.
 */
typedef enum as_scan_status_e {

	/**
	 *	The scan status is undefined.
	 *	This is likely due to the status not being properly checked.
	 */
	AS_SCAN_STATUS_UNDEF,

	/**
	 *	The scan is currently running.
	 */
	AS_SCAN_STATUS_INPROGRESS,

	/**
	 *	The scan was aborted. Due to failure or the user.
	 */
	AS_SCAN_STATUS_ABORTED,

	/**
	 *	The scan completed successfully.
	 */
	AS_SCAN_STATUS_COMPLETED,

} as_scan_status;

/**
 *	Information about a particular background scan.
 *
 *	@ingroup as_scan_object 
 */
typedef struct as_scan_info_s {

	/**
	 *	Status of the scan.
	 */
	as_scan_status status;

	/**
	 *	Progress estimate for the scan, as percentage.
	 */
	uint32_t progress_pct;

	/**
	 *	How many records have been scanned.
	 */
	uint32_t records_scanned;

} as_scan_info;

/**
 *	Sequence of bins which should be selected during a scan.
 *
 *	Entries can either be initialized on the stack or on the heap.
 *
 *	Initialization should be performed via a query object, using:
 *	- as_scan_select_init()
 *	- as_scan_select_inita()
 */
typedef struct as_scan_bins_s {

	/**
	 *	@private
	 *	If true, then as_scan_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	Number of entries allocated
	 */
	uint16_t capacity;

	/**
	 *	Number of entries used
	 */
	uint16_t size;

	/**
	 *	Sequence of entries
	 */
	as_bin_name * entries;

} as_scan_bins;

/**
 *	Sequence of predicate expressions to be applied to a scan.
 *
 *	Entries can either be initialized on the stack or on the heap.
 *
 *	Initialization should be performed via a scan object, using:
 *	-	as_scan_predexp_init()
 *	-	as_scan_predexp_inita()
 */
typedef struct as_scan_predexp_s {

	/**
	 *	@private
	 *	If true, then as_scan_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	Number of entries allocated
	 */
	uint16_t capacity;

	/**
	 *	Number of entries used
	 */
	uint16_t size;

	/**
	 *	Sequence of entries
	 */
	as_predexp_base ** entries;

} as_scan_predexp;

/**
 *	In order to execute a scan using the Scan API, an as_scan object
 *	must be initialized and populated.
 *
 *	## Initialization
 *	
 *	Before using an as_scan, it must be initialized via either: 
 *	- as_scan_init()
 *	- as_scan_new()
 *	
 *	as_scan_init() should be used on a stack allocated as_scan. It will
 *	initialize the as_scan with the given namespace and set. On success,
 *	it will return a pointer to the initialized as_scan. Otherwise, NULL 
 *	is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan scan;
 *	as_scan_init(&scan, "namespace", "set");
 *	~~~~~~~~~~
 *
 *	as_scan_new() should be used to allocate and initialize a heap allocated
 *	as_scan. It will allocate the as_scan, then initialized it with the 
 *	given namespace and set. On success, it will return a pointer to the 
 *	initialized as_scan. Otherwise, NULL is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan * scan = as_scan_new("namespace", "set");
 *	~~~~~~~~~~
 *
 *	## Destruction
 *
 *	When you are finished with the as_scan, you can destroy it and associated
 *	resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_destroy(scan);
 *	~~~~~~~~~~
 *
 *	## Usage
 *
 *	An initialized as_scan can be populated with additional fields.
 *
 *	### Selecting Bins
 *
 *	as_scan_select() is used to specify the bins to be selected by the scan.
 *	If a scan specifies bins to be selected, then only those bins will be 
 *	returned. If no bins are selected, then all bins will be returned.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_select(query, "bin1");
 *	as_scan_select(query, "bin2");
 *	~~~~~~~~~~
 *
 *	Before adding bins to select, the select structure must be initialized via
 *	either:
 *	- as_scan_select_inita() - Initializes the structure on the stack.
 *	- as_scan_select_init() - Initializes the structure on the heap.
 *
 *	Both functions are given the number of bins to be selected.
 *
 *	A complete example using as_scan_select_inita()
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_select_inita(query, 2);
 *	as_scan_select(query, "bin1");
 *	as_scan_select(query, "bin2");
 *	~~~~~~~~~~
 *
 *	### Returning only meta data
 *
 *	A scan can return only record meta data, and exclude bins.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_set_nobins(scan, true);
 *	~~~~~~~~~~
 *
 *	### Scan nodes in parallel
 *
 *	A scan can be made to scan all the nodes in parallel
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_set_concurrent(scan, true);
 *	~~~~~~~~~~
 *
 *	### Scan a Percentage of Records
 *
 *	A scan can define the percentage of record in the cluster to be scaned.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_set_percent(scan, 100);
 *	~~~~~~~~~~
 *
 *	### Scan a Priority
 *
 *	To set the priority of the scan, the set as_scan.priority.
 *
 *	The priority of a scan can be defined as either:
 *	- `AS_SCAN_PRIORITY_AUTO`
 *	- `AS_SCAN_PRIORITY_LOW`
 *	- `AS_SCAN_PRIORITY_MEDIUM`
 * 	- `AS_SCAN_PRIORITY_HIGH`
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_set_priority(scan, AS_SCAN_PRIORITY_LOW);
 *	~~~~~~~~~~
 *
 *	### Applying a UDF to each Record Scanned
 *
 *	A UDF can be applied to each record scanned.
 *
 *	To define the UDF for the scan, use as_scan_apply_each().
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_apply_each(scan, "udf_module", "udf_function", arglist);
 *	~~~~~~~~~~
 *
 *	@ingroup client_objects
 */
typedef struct as_scan_s {

	/**
	 *	@private
	 *	If true, then as_scan_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	Priority of scan.
	 *
	 *	Default value is AS_SCAN_PRIORITY_DEFAULT.
	 */
	as_scan_priority priority;

	/**
	 *	Percentage of the data to scan.
	 *
	 *	Default value is AS_SCAN_PERCENT_DEFAULT.
	 */
	uint8_t percent;

	/**
	 *	Set to true if the scan should return only the metadata of the record.
	 *
	 *	Default value is AS_SCAN_NOBINS_DEFAULT.
	 */
	bool no_bins;

	/**
	 *	Set to true if the scan should scan all the nodes in parallel
	 *
	 *	Default value is AS_SCAN_CONCURRENT_DEFAULT.
	 */
	bool concurrent;

	/**
	 *	Set to true if the scan should deserialize list and map raw bytes.
	 *	Set to false for backup programs that just need access to raw bytes.
	 *
	 *	Default value is AS_SCAN_DESERIALIZE_DEFAULT.
	 */
	bool deserialize_list_map;
	
	/**
	 * 	@memberof as_scan
	 *	Namespace to be scanned.
	 *
	 *	Should be initialized via either:
	 *	-	as_scan_init() -	To initialize a stack allocated scan.
	 *	-	as_scan_new() -		To heap allocate and initialize a scan.
	 *
	 */
	as_namespace ns;

	/**
	 *	Set to be scanned.
	 *
	 *	Should be initialized via either:
	 *	-	as_scan_init() -	To initialize a stack allocated scan.
	 *	-	as_scan_new() -		To heap allocate and initialize a scan.
	 *
	 */
	as_set set;

	/**
	 *	Name of bins to select.
	 *	
	 *	Use either of the following function to initialize:
	 *	- as_scan_select_init() -	To initialize on the heap.
	 *	- as_scan_select_inita() -	To initialize on the stack.
	 *
	 *	Use as_scan_select() to populate.
	 */
	as_scan_bins select;
	
	/**
	 *	Predicate Expressions for filtering.
	 *	
	 *	Use either of the following function to initialize:
	 *	-	as_query_predexp_init() -	To initialize on the heap.
	 *	-	as_query_predexp_inita() -	To initialize on the stack.
	 *
	 *	Use as_query_predexp() to populate.
	 */
	as_scan_predexp predexp;

	/**	
	 *	Apply the UDF for each record scanned on the server.
	 *
	 *	Should be set via `as_scan_apply_each()`.
	 */
	as_udf_call apply_each;

} as_scan;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 *****************************************************************************/

/**
 *	Initializes a scan.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan scan;
 *	as_scan_init(&scan, "test", "demo");
 *	~~~~~~~~~~
 *
 *	When you no longer require the scan, you should release the scan and 
 *	related resources via `as_scan_destroy()`.
 *
 *	@param scan		The scan to initialize.
 *	@param ns 		The namespace to scan.
 *	@param set 		The set to scan.
 *
 *	@returns On succes, the initialized scan. Otherwise NULL.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
as_scan * as_scan_init(as_scan * scan, const as_namespace ns, const as_set set);

/**
 *	Create and initializes a new scan on the heap.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan * scan = as_scan_new("test","demo");
 *	~~~~~~~~~~
 *
 *	When you no longer require the scan, you should release the scan and 
 *	related resources via `as_scan_destroy()`.
 *
 *	@param ns 		The namespace to scan.
 *	@param set 		The set to scan.
 *
 *	@returns On success, a new scan. Otherwise NULL.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
as_scan * as_scan_new(const as_namespace ns, const as_set set);

/**
 *	Releases all resources allocated to the scan.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_destroy(scan);
 *	~~~~~~~~~~
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
void as_scan_destroy(as_scan * scan);

/******************************************************************************
 *	SELECT FUNCTIONS
 *****************************************************************************/

/** 
 *	Initializes `as_scan.select` with a capacity of `n` using `alloca`
 *
 *	For heap allocation, use `as_scan_select_init()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_select_inita(&scan, 2);
 *	as_scan_select(&scan, "bin1");
 *	as_scan_select(&scan, "bin2");
 *	~~~~~~~~~~
 *	
 *	@param __scan	The scan to initialize.
 *	@param __n		The number of bins to allocate.
 *
 *	@ingroup as_scan_object
 */
#define as_scan_select_inita(__scan, __n) \
	do { \
		if ( (__scan) != NULL && (__scan)->select.entries == NULL ) {\
			(__scan)->select.entries = (as_bin_name*) alloca(sizeof(as_bin_name) * (__n));\
			if ( (__scan)->select.entries ) { \
				(__scan)->select._free = false;\
				(__scan)->select.capacity = (__n);\
				(__scan)->select.size = 0;\
			}\
	 	} \
	} while(0)

/** 
 *	Initializes `as_scan.select` with a capacity of `n` using `malloc()`.
 *	
 *	For stack allocation, use `as_scan_select_inita()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_select_init(&scan, 2);
 *	as_scan_select(&scan, "bin1");
 *	as_scan_select(&scan, "bin2");
 *	~~~~~~~~~~
 *
 *	@param scan		The scan to initialize.
 *	@param n		The number of bins to allocate.
 *
 *	@return On success, the initialized. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_select_init(as_scan * scan, uint16_t n);

/**
 *	Select bins to be projected from matching records.
 *
 *	You have to ensure as_scan.select has sufficient capacity, prior to 
 *	adding a bin. If capacity is insufficient then false is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_select_init(&scan, 2);
 *	as_scan_select(&scan, "bin1");
 *	as_scan_select(&scan, "bin2");
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to modify.
 *	@param bin 			The name of the bin to select.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_select(as_scan * scan, const char * bin);


/******************************************************************************
 *	PREDEXP FUNCTIONS
 *****************************************************************************/

/** 
 *	Initializes `as_scan.predexp` with a capacity of `n` using `alloca`
 *
 *	For heap allocation, use `as_scan_predexp_init()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_predexp_inita(&scan, 3);
 *	as_scan_predexp_add(&scan, as_predexp_integer_value(90));
 *	as_scan_predexp_add(&scan, as_predexp_integer_bin("bin1"));
 *	as_scan_predexp_add(&scan, as_predexp_integer_greatereq());
 *	~~~~~~~~~~
 *	
 *	@param __scan	The scan to initialize.
 *	@param __n		The number of predicate expression slots to allocate.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
#define as_scan_predexp_inita(__scan, __n)							\
	if ( (__scan) != NULL && (__scan)->predexp.entries == NULL ) {	\
		(__scan)->predexp.entries =									\
			(as_predexp_base **)											\
			alloca(__n * sizeof(as_predexp_base *));					\
		if ( (__scan)->predexp.entries ) {								\
			(__scan)->predexp._free = false;							\
			(__scan)->predexp.capacity = __n;							\
			(__scan)->predexp.size = 0;								\
		}																\
	}

/** 
 *	Initializes `as_scan.predexp` with a capacity of `n` using `malloc()`.
 *	
 *	For stack allocation, use `as_scan_predexp_inita()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_predexp_init(&scan, 3);
 *	as_scan_predexp_add(&scan, as_predexp_integer_value(90));
 *	as_scan_predexp_add(&scan, as_predexp_integer_bin("bin1"));
 *	as_scan_predexp_add(&scan, as_predexp_integer_greatereq());
 *	~~~~~~~~~~
 *
 *	@param scan	    The scan to initialize.
 *	@param n		The number of predicate expression slots to allocate.
 *
 *	@return On success, the initialized. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_predexp_init(as_scan * scan, uint16_t n);

/**
 *	Adds predicate expressions to a scan.
 *
 *	You have to ensure as_scan.predexp has sufficient capacity, prior to 
 *	adding a predexp. If capacity is sufficient then false is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_scan_predexp_inita(&scan, 3);
 *	as_scan_predexp_add(&scan, as_predexp_integer_value(90));
 *	as_scan_predexp_add(&scan, as_predexp_integer_bin("bin1"));
 *	as_scan_predexp_add(&scan, as_predexp_integer_greatereq());
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to modify.
 *  @param predexp		Pointer to a constructed predicate expression.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_predexp_add(as_scan * scan, as_predexp_base * predexp);

/******************************************************************************
 *	MODIFIER FUNCTIONS
 *****************************************************************************/

/**
 *	The percentage of data to scan.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_set_percent(&q, 100);
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to set the priority on.
 *	@param percent		The percent to scan.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_set_percent(as_scan * scan, uint8_t percent);

/**
 *	Set the priority for the scan.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_set_priority(&q, AS_SCAN_PRIORITY_LOW);
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to set the priority on.
 *	@param priority		The priority for the scan.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_set_priority(as_scan * scan, as_scan_priority priority);

/**
 *	Do not return bins. This will only return the metadata for the records.
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_set_nobins(&q, true);
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to set the priority on.
 *	@param nobins		If true, then do not return bins.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_set_nobins(as_scan * scan, bool nobins);

/**
 *	Scan all the nodes in prallel
 *	
 *	~~~~~~~~~~{.c}
 *	as_scan_set_concurrent(&q, true);
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to set the concurrency on.
 *	@param concurrent	If true, scan all the nodes in parallel
 *
 *	@return On success, true. Otherwise an error occurred.
 */
bool as_scan_set_concurrent(as_scan * scan, bool concurrent);

/**
 *	Apply a UDF to each record scanned on the server.
 *	
 *	~~~~~~~~~~{.c}
 *	as_arraylist arglist;
 *	as_arraylist_init(&arglist, 2, 0);
 *	as_arraylist_append_int64(&arglist, 1);
 *	as_arraylist_append_int64(&arglist, 2);
 *	
 *	as_scan_apply_each(&q, "module", "func", (as_list *) &arglist);
 *
 *	as_arraylist_destroy(&arglist);
 *	~~~~~~~~~~
 *
 *	@param scan 		The scan to apply the UDF to.
 *	@param module 		The module containing the function to execute.
 *	@param function 	The function to execute.
 *	@param arglist 		The arguments for the function.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_scan
 *	@ingroup as_scan_object
 */
bool as_scan_apply_each(as_scan * scan, const char * module, const char * function, as_list * arglist);

#ifdef __cplusplus
} // end extern "C"
#endif
