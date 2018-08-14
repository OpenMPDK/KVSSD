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

#include <aerospike/aerospike_index.h>
#include <aerospike/as_bin.h>
#include <aerospike/as_key.h>
#include <aerospike/as_list.h>
#include <aerospike/as_predexp.h>
#include <aerospike/as_udf.h>

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	MACROS
 *****************************************************************************/

/**
 *	Macro for setting setting the STRING_EQUAL predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_string_equals("abc"));
 *	~~~~~~~~~~
 *
 *	@relates as_query
 */
#define as_string_equals(__val) AS_PREDICATE_EQUAL, AS_INDEX_TYPE_DEFAULT, AS_INDEX_STRING, __val

/**
 *	Macro for setting setting the INTEGER_EQUAL predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_integer_equals(123));
 *	~~~~~~~~~~
 *
 *	@relates as_query
 */
#define as_integer_equals(__val) AS_PREDICATE_EQUAL, AS_INDEX_TYPE_DEFAULT, AS_INDEX_NUMERIC, (int64_t)__val

/**
 *	Macro for setting setting the INTEGER_RANGE predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_integer_range(1,100));
 *	~~~~~~~~~~
 *	
 *	@relates as_query
 *	@ingroup query_object
 */
#define as_integer_range(__min, __max) AS_PREDICATE_RANGE, AS_INDEX_TYPE_DEFAULT, AS_INDEX_NUMERIC, (int64_t)__min, (int64_t)__max

/**
 *	Macro for setting setting the RANGE predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_range(LIST,NUMERIC,1,100));
 *	~~~~~~~~~~
 *	
 *	@relates as_query
 *	@ingroup query_object
 */
#define as_range(indextype, datatype, __min, __max) AS_PREDICATE_RANGE, AS_INDEX_TYPE_ ##indextype, AS_INDEX_ ##datatype, __min, __max

/**
 *	Macro for setting setting the CONTAINS predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_contains(LIST,STRING,"val"));
 *	~~~~~~~~~~
 *	
 *	@relates as_query
 *	@ingroup query_object
 */
#define as_contains(indextype, datatype, __val) AS_PREDICATE_EQUAL, AS_INDEX_TYPE_ ##indextype, AS_INDEX_ ##datatype, __val

/**
 *	Macro for setting setting the EQUALS predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_equals(NUMERIC,5));
 *	~~~~~~~~~~
 *	
 *	@relates as_query
 *	@ingroup query_object
 */
#define as_equals(datatype, __val) AS_PREDICATE_EQUAL, AS_INDEX_TYPE_DEFAULT, AS_INDEX_ ##datatype, __val

#define as_geo_within(__val) AS_PREDICATE_RANGE, AS_INDEX_TYPE_DEFAULT, AS_INDEX_GEO2DSPHERE, __val

#define as_geo_contains(__val) AS_PREDICATE_RANGE, AS_INDEX_TYPE_DEFAULT, AS_INDEX_GEO2DSPHERE, __val


/******************************************************************************
 *	TYPES 	
 *****************************************************************************/

/**
 *	Union of supported predicates
 */
typedef union as_predicate_value_u {
	
	/**
	 *	String Value
	 */
	char * string;

	/**
	 *	Integer Value
	 */
	int64_t integer;

	/**
	 *	Integer Range Value
	 */
	struct {

		/**
		 *	Minimum value
		 */
		int64_t min;

		/**
		 *	Maximum value
		 */
		int64_t max;

	} integer_range;

} as_predicate_value;

/**
 *	The types of predicates supported.
 */
typedef enum as_predicate_type_e {

	/**
	 *	String Equality Predicate. 
	 *	Requires as_predicate_value.string to be set.
	 */
	AS_PREDICATE_EQUAL,

	AS_PREDICATE_RANGE
} as_predicate_type;

/**
 *	Defines a predicate, including the bin, type of predicate and the value
 *	for the predicate.
 */
typedef struct as_predicate_s {

	/**
	 *	Bin to apply the predicate to
	 */
	as_bin_name bin;

	/**
	 *	The predicate type, dictates which values to use from the union
	 */
	as_predicate_type type;

	/**
	 *	The value for the predicate.
	 */
	as_predicate_value value;

	/*
	 * The type of data user wants to query
	 */

	as_index_datatype dtype;

	/*
	 * The type of index predicate is on
	 */
	as_index_type itype;
} as_predicate;

/**
 *	Enumerations defining the direction of an ordering.
 */
typedef enum as_order_e {

	/**
	 *	Ascending order
	 */
	AS_ORDER_ASCENDING = 0,

	/**
	 *	bin should be in ascending order
	 */
	AS_ORDER_DESCENDING = 1

} as_order;


/**
 *	Defines the direction a bin should be ordered by.
 */
typedef struct as_ordering_s {

	/**
	 *	Name of the bin to sort by
	 */
	as_bin_name bin;

	/**
	 *	Direction of the sort
	 */
	as_order order;

} as_ordering;

/**
 *	Sequence of bins which should be selected during a query.
 *
 *	Entries can either be initialized on the stack or on the heap.
 *
 *	Initialization should be performed via a query object, using:
 *	-	as_query_select_init()
 *	-	as_query_select_inita()
 */
typedef struct as_query_bins_s {

	/**
	 *	@private
	 *	If true, then as_query_destroy() will free this instance.
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

} as_query_bins;

/**
 *	Sequence of predicates to be applied to a query.
 *
 *	Entries can either be initialized on the stack or on the heap.
 *
 *	Initialization should be performed via a query object, using:
 *	-	as_query_where_init()
 *	-	as_query_where_inita()
 */
typedef struct as_query_predicates_s {

	/**
	 *	@private
	 *	If true, then as_query_destroy() will free this instance.
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
	as_predicate * 	entries;

} as_query_predicates;

/**
 *	Sequence of predicate expressions to be applied to a query.
 *
 *	Entries can either be initialized on the stack or on the heap.
 *
 *	Initialization should be performed via a query object, using:
 *	-	as_query_predexp_init()
 *	-	as_query_predexp_inita()
 */
typedef struct as_query_predexp_s {

	/**
	 *	@private
	 *	If true, then as_query_destroy() will free this instance.
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

} as_query_predexp;

/**
 *	Sequence of ordering to be applied to a query results.
 *
 *	Entries can either be initialized on the stack or on the heap.
 *	
 *	Initialization should be performed via a query object, using:
 *	-	as_query_orderby_init()
 *	-	as_query_orderby_inita()
 */
typedef struct as_query_sort_s {

	/**
	 *	@private
	 *	If true, then as_query_destroy() will free this instance.
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
	as_ordering * entries;

} as_query_ordering;


/** 
 *	The as_query object is used define a query to be executed in the datasbase.
 *
 *	## Initialization
 *	
 *	Before using an as_query, it must be initialized via either: 
 *	- as_query_init()
 *	- as_query_new()
 *	
 *	as_query_init() should be used on a stack allocated as_query. It will
 *	initialize the as_query with the given namespace and set. On success,
 *	it will return a pointer to the initialized as_query. Otherwise, NULL 
 *	is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_query query;
 *	as_query_init(&query, "namespace", "set");
 *	~~~~~~~~~~
 *
 *	as_query_new() should be used to allocate and initialize a heap allocated
 *	as_query. It will allocate the as_query, then initialized it with the 
 *	given namespace and set. On success, it will return a pointer to the 
 *	initialized as_query. Otherwise, NULL is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_query * query = as_query_new("namespace", "set");
 *	~~~~~~~~~~
 *
 *	## Destruction
 *
 *	When you are finished with the as_query, you can destroy it and associated
 *	resources:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_destroy(query);
 *	~~~~~~~~~~
 *
 *	## Usage
 *
 *	The following explains how to use an as_query to build a query.
 *
 *	### Selecting Bins
 *
 *	as_query_select() is used to specify the bins to be selected by the query.
 *	
 *	~~~~~~~~~~{.c}
 *	as_query_select(query, "bin1");
 *	as_query_select(query, "bin2");
 *	~~~~~~~~~~
 *
 *	Before adding bins to select, the select structure must be initialized via
 *	either:
 *	- as_query_select_inita() - Initializes the structure on the stack.
 *	- as_query_select_init() - Initializes the structure on the heap.
 *
 *	Both functions are given the number of bins to be selected.
 *
 *	A complete example using as_query_select_inita()
 *
 *	~~~~~~~~~~{.c}
 *	as_query_select_inita(query, 2);
 *	as_query_select(query, "bin1");
 *	as_query_select(query, "bin2");
 *	~~~~~~~~~~
 *
 *
 *	### Predicates on Bins
 *
 *	as_query_where() is used to specify predicates to be added to the the query.
 *
 *	**Note:** Currently, a single where predicate is supported. To do more advanced filtering,
 *	you will want to use a UDF to process the result set on the server.
 *	
 *	~~~~~~~~~~{.c}
 *	as_query_where(query, "bin1", as_string_equals("abc"));
 *	~~~~~~~~~~
 *
 *	The predicates that you can apply to a bin include:
 *	- as_string_equals() - Test for string equality.
 *	- as_integer_equals() - Test for integer equality.
 *	- as_integer_range() - Test for integer within a range.
 *
 *	Before adding predicates, the where structure must be initialized. To
 *	initialize the where structure, you can choose to use one of the following:
 *	- as_query_where_inita() - Initializes the structure on the stack.
 *	- as_query_where_init() - Initializes the structure on the heap.
 *	
 *	Both functions are given the number of predicates to be added.
 *
 *	A complete example using as_query_where_inita():
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where_inita(query, 1);
 *	as_query_where(query, "bin1", as_string_equals("abc"));
 *	~~~~~~~~~~
 *
 *
 *	### Sorting Results
 *
 *	as_query_orderby() is used to specify ordering of results of a query.
 *	
 *	~~~~~~~~~~{.c}
 *	as_query_orderby(query, "bin1", AS_ORDER_ASCENDING);
 *	~~~~~~~~~~
 *
 *	The sort order can be:
 *	- `AS_ORDER_ASCENDING`
 *	- `AS_ORDER_DESCENDING`
 *
 *	Before adding ordering, the orderby structure must be initialized via 
 *	either:
 *	- as_query_orderby_inita() - Initializes the structure on the stack.
 *	- as_query_orderby_init() - Initializes the structure on the heap.
 *	
 *	Both functions are given the number of orderings to be added.
 *
 *	A complete example using as_query_orderby_inita():
 *	
 *	~~~~~~~~~~{.c}
 *	as_query_orderby_inita(query, 2);
 *	as_query_orderby(query, "bin1", AS_ORDER_ASCENDING);
 *	as_query_orderby(query, "bin2", AS_ORDER_ASCENDING);
 *	~~~~~~~~~~
 *
 *	### Applying a UDF to Query Results
 *
 *	A UDF can be applied to the results of a query.
 *
 *	To define the UDF for the query, use as_query_apply().
 *
 *	~~~~~~~~~~{.c}
 *	as_query_apply(query, "udf_module", "udf_function", arglist);
 *	~~~~~~~~~~
 *
 *	@ingroup client_objects
 */
typedef struct as_query_s {

	/**
	 *	@private
	 *	If true, then as_query_destroy() will free this instance.
	 */
	bool _free;

	/**
	 *	Namespace to be queried.
	 *
	 *	Should be initialized via either:
	 *	-	as_query_init() -	To initialize a stack allocated query.
	 *	-	as_query_new() -	To heap allocate and initialize a query.
	 */
	as_namespace ns;

	/**
	 *	Set to be queried.
	 *
	 *	Should be initialized via either:
	 *	-	as_query_init() -	To initialize a stack allocated query.
	 *	-	as_query_new() -	To heap allocate and initialize a query.
	 */
	as_set set;

	/**
	 *	Name of bins to select.
	 *	
	 *	Use either of the following function to initialize:
	 *	-	as_query_select_init() -	To initialize on the heap.
	 *	-	as_query_select_inita() -	To initialize on the stack.
	 *
	 *	Use as_query_select() to populate.
	 */
	as_query_bins select;

	/**
	 *	Predicates for filtering.
	 *	
	 *	Use either of the following function to initialize:
	 *	-	as_query_where_init() -		To initialize on the heap.
	 *	-	as_query_where_inita() -	To initialize on the stack.
	 *
	 *	Use as_query_where() to populate.
	 */
	as_query_predicates where;

	/**
	 *	Predicate Expressions for filtering.
	 *	
	 *	Use either of the following function to initialize:
	 *	-	as_query_predexp_init() -	To initialize on the heap.
	 *	-	as_query_predexp_inita() -	To initialize on the stack.
	 *
	 *	Use as_query_predexp() to populate.
	 */
	as_query_predexp predexp;

	/**
	 *	UDF to apply to results of the query
	 *
	 *	Should be set via `as_query_apply()`.
	 */
	as_udf_call apply;

	/**
	 *	Set to true if query should only return keys and no bin data.
	 *
	 *	Default value is false.
	 */
	bool no_bins;

} as_query;

/******************************************************************************
 *	INSTANCE FUNCTIONS
 *****************************************************************************/

/**
 *	Initialize a stack allocated as_query.
 *
 *	~~~~~~~~~~{.c}
 *	as_query query;
 *	as_query_init(&query, "test", "demo");
 *	~~~~~~~~~~
 *
 *	@param query 	The query to initialize.
 *	@param ns 		The namespace to query.
 *	@param set 		The set to query.
 *
 *	@return On success, the initialized query. Otherwise NULL.
 *
 *	@relates as_query
 */
as_query * as_query_init(as_query * query, const as_namespace ns, const as_set set);

/**
 *	Create and initialize a new heap allocated as_query.
 *
 *	~~~~~~~~~~{.c}
 *	as_query * query = as_query_new("test", "demo");
 *	~~~~~~~~~~
 *	
 *	@param ns 		The namespace to query.
 *	@param set 		The set to query.
 *
 *	@return On success, the new query. Otherwise NULL.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
as_query * as_query_new(const as_namespace ns, const as_set set);

/**
 *	Destroy the query and associated resources.
 *	
 *	~~~~~~~~~~{.c}
 *	as_query_destroy(scan);
 *	~~~~~~~~~~
 *
 *	@param query 	The query to destroy.
 *
 *	@relates as_query
 */
void as_query_destroy(as_query * query);

/******************************************************************************
 *	SELECT FUNCTIONS
 *****************************************************************************/

/** 
 *	Initializes `as_query.select` with a capacity of `n` using `alloca`
 *
 *	For heap allocation, use `as_query_select_init()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_select_inita(&query, 2);
 *	as_query_select(&query, "bin1");
 *	as_query_select(&query, "bin2");
 *	~~~~~~~~~~
 *	
 *	@param __query	The query to initialize.
 *	@param __n		The number of bins to allocate.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
#define as_query_select_inita(__query, __n) \
	do { \
		if ( (__query) != NULL && (__query)->select.entries == NULL ) {\
			(__query)->select.entries = (as_bin_name*) alloca(sizeof(as_bin_name) * (__n));\
			if ( (__query)->select.entries ) { \
				(__query)->select._free = false;\
				(__query)->select.capacity = (__n);\
				(__query)->select.size = 0;\
			}\
	 	} \
	} while(0)

/** 
 *	Initializes `as_query.select` with a capacity of `n` using `malloc()`.
 *	
 *	For stack allocation, use `as_query_select_inita()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_select_init(&query, 2);
 *	as_query_select(&query, "bin1");
 *	as_query_select(&query, "bin2");
 *	~~~~~~~~~~
 *
 *	@param query	The query to initialize.
 *	@param n		The number of bins to allocate.
 *
 *	@return On success, the initialized. Otherwise an error occurred.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
bool as_query_select_init(as_query * query, uint16_t n);

/**
 *	Select bins to be projected from matching records.
 *
 *	You have to ensure as_query.select has sufficient capacity, prior to 
 *	adding a bin. If capacity is sufficient then false is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_select_init(&query, 2);
 *	as_query_select(&query, "bin1");
 *	as_query_select(&query, "bin2");
 *	~~~~~~~~~~
 *
 *	@param query 		The query to modify.
 *	@param bin 			The name of the bin to select.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
bool as_query_select(as_query * query, const char * bin);

/******************************************************************************
 *	WHERE FUNCTIONS
 *****************************************************************************/

/** 
 *	Initializes `as_query.where` with a capacity of `n` using `alloca()`.
 *
 *	For heap allocation, use `as_query_where_init()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where_inita(&query, 3);
 *	as_query_where(&query, "bin1", as_string_equals("abc"));
 *	as_query_where(&query, "bin2", as_integer_equals(123));
 *	as_query_where(&query, "bin3", as_integer_range(0,123));
 *	~~~~~~~~~~
 *
 *	@param __query	The query to initialize.
 *	@param __n		The number of as_predicate to allocate.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_query
 */
#define as_query_where_inita(__query, __n) \
	do { \
		if ( (__query)  != NULL && (__query)->where.entries == NULL ) {\
			(__query)->where.entries = (as_predicate*) alloca(sizeof(as_predicate) * (__n));\
			if ( (__query)->where.entries ) { \
				(__query)->where._free = false;\
				(__query)->where.capacity = (__n);\
				(__query)->where.size = 0;\
			}\
	 	} \
	} while(0)

/** 
 *	Initializes `as_query.where` with a capacity of `n` using `malloc()`.
 *
 *	For stack allocation, use `as_query_where_inita()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where_init(&query, 3);
 *	as_query_where(&query, "bin1", as_string_equals("abc"));
 *	as_query_where(&query, "bin1", as_integer_equals(123));
 *	as_query_where(&query, "bin1", as_integer_range(0,123));
 *	~~~~~~~~~~
 *
 *	@param query	The query to initialize.
 *	@param n		The number of as_predicate to allocate.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_query
 */
bool as_query_where_init(as_query * query, uint16_t n);

/**
 *	Add a predicate to the query.
 *
 *	You have to ensure as_query.where has sufficient capacity, prior to
 *	adding a predicate. If capacity is insufficient then false is returned.
 *
 *	String predicates are not owned by as_query.  If the string is allocated
 *	on the heap, the caller is responsible for freeing the string after the query
 *	has been executed.  as_query_destroy() will not free this string predicate.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_where_init(&query, 3);
 *	as_query_where(&query, "bin1", as_string_equals("abc"));
 *	as_query_where(&query, "bin1", as_integer_equals(123));
 *	as_query_where(&query, "bin1", as_integer_range(0,123));
 *	~~~~~~~~~~
 *
 *	@param query		The query add the predicate to.
 *	@param bin			The name of the bin the predicate will apply to.
 *	@param type			The type of predicate.
 *	@param itype		The type of index.
 *	@param dtype		The underlying data type that the index is based on.
 *	@param ... 			The values for the predicate.
 *	
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_query
 */
bool as_query_where(as_query * query, const char * bin, as_predicate_type type, as_index_type itype, as_index_datatype dtype, ... );

/******************************************************************************
 *	PREDEXP FUNCTIONS
 *****************************************************************************/

/** 
 *	Initializes `as_query.predexp` with a capacity of `n` using `alloca`
 *
 *	For heap allocation, use `as_query_predexp_init()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_string_value("apple"));
 *	as_query_predexp_add(&query, as_predexp_string_bin("fruit"));
 *	as_query_predexp_add(&query, as_predexp_string_equal());
 *	~~~~~~~~~~
 *	
 *	@param __query	The query to initialize.
 *	@param __n		The number of predicate expression slots to allocate.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
#define as_query_predexp_inita(__query, __n)							\
	if ( (__query) != NULL && (__query)->predexp.entries == NULL ) {	\
		(__query)->predexp.entries =									\
			(as_predexp_base **)											\
			alloca(__n * sizeof(as_predexp_base *));					\
		if ( (__query)->predexp.entries ) {								\
			(__query)->predexp._free = false;							\
			(__query)->predexp.capacity = __n;							\
			(__query)->predexp.size = 0;								\
		}																\
	}

/** 
 *	Initializes `as_query.predexp` with a capacity of `n` using `malloc()`.
 *	
 *	For stack allocation, use `as_query_predexp_inita()`.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_init(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_string_value("apple"));
 *	as_query_predexp_add(&query, as_predexp_string_bin("fruit"));
 *	as_query_predexp_add(&query, as_predexp_string_equal());
 *	~~~~~~~~~~
 *
 *	@param query	The query to initialize.
 *	@param n		The number of predicate expression slots to allocate.
 *
 *	@return On success, the initialized. Otherwise an error occurred.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
bool as_query_predexp_init(as_query * query, uint16_t n);

/**
 *	Adds predicate expressions to a query.
 *
 *	You have to ensure as_query.predexp has sufficient capacity, prior to 
 *	adding a predexp. If capacity is sufficient then false is returned.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_string_value("apple"));
 *	as_query_predexp_add(&query, as_predexp_string_bin("fruit"));
 *	as_query_predexp_add(&query, as_predexp_string_equal());
 *	~~~~~~~~~~
 *
 *	@param query 		The query to modify.
 *  @param predexp		Pointer to a constructed predicate expression.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_query
 *	@ingroup query_object
 */
bool as_query_predexp_add(as_query * query, as_predexp_base * predexp);

/******************************************************************************
 *	QUERY MODIFIER FUNCTIONS
 *****************************************************************************/

/**
 *	Apply a function to the results of the query.
 *
 *	~~~~~~~~~~{.c}
 *	as_query_apply(&query, "my_module", "my_function", NULL);
 *	~~~~~~~~~~
 *
 *	@param query		The query to apply the function to.
 *	@param module		The module containing the function to invoke.
 *	@param function		The function in the module to invoke.
 *	@param arglist		The arguments to use when calling the function.
 *
 *	@return On success, true. Otherwise an error occurred.
 *
 *	@relates as_query
 */
bool as_query_apply(as_query * query, const char * module, const char * function, const as_list * arglist);

#ifdef __cplusplus
} // end extern "C"
#endif
