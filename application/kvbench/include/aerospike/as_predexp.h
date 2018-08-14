/*
 * Copyright 2016-2017 Aerospike, Inc.
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

#ifdef __cplusplus
extern "C" {
#endif

struct as_predexp_base_s;

typedef void (*as_predexp_base_dtor_fn) (struct as_predexp_base_s *);
typedef size_t (*as_predexp_base_size_fn) (struct as_predexp_base_s *);
typedef uint8_t * (*as_predexp_base_write_fn) (struct as_predexp_base_s *, uint8_t *p);
	
/**
 *	Defines a predicate expression base.
 */
typedef struct as_predexp_base_s {

	/**
	 * Destructor for this object.
	 */
	as_predexp_base_dtor_fn dtor_fn;

	/**
	 * Returns serialization size of this object.
	 */
	as_predexp_base_size_fn size_fn;

	/**
	 * Serialize this object into a command.
	 */
	as_predexp_base_write_fn write_fn;

} as_predexp_base;

/**
 *	Create an AND logical predicate expression.
 *
 *  The AND predicate expression returns true if all of its children
 *  are true.
 *
 *  The nexpr parameter specifies how many children to pop off the
 *  expression stack.  These children must be "logical" expressions
 *  and not "value" expressions.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "c" is between 11 and 20
 *  inclusive:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 7);
 *	as_query_predexp_add(&q, as_predexp_integer_bin("c"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(11));
 *  as_query_predexp_add(&q, as_predexp_integer_greatereq());
 *	as_query_predexp_add(&q, as_predexp_integer_bin("c"));
 *  as_query_predexp_add(&q, as_predexp_integer_value(20));
 *	as_query_predexp_add(&q, as_predexp_integer_lesseq());
 *	as_query_predexp_add(&q, as_predexp_and(2));
 *	~~~~~~~~~~
 *
 *  @param nexpr	The number of child expressions to AND.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_and(uint16_t nexpr);
	
/**
 *	Create an OR logical predicate expression.
 *
 *  The OR predicate expression returns true if any of its children
 *  are true.
 *
 *  The nexpr parameter specifies how many children to pop off the
 *  expression stack.  These children must be "logical" expressions
 *  and not "value" expressions.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "pet" is "cat" or "dog".
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 7);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_string_value("dog"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_or(2));
 *	~~~~~~~~~~
 *
 *  @param nexpr	The number of child expressions to OR.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_or(uint16_t nexpr);
	
/**
 *	Create a NOT logical predicate expression.
 *
 *  The NOT predicate expression returns true if its child is false.
 *
 *  The NOT expression pops a single child off the expression stack.
 *  This child must be a "logical" expression and not a "value"
 *  expression.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "pet" is not "dog".
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 4);
 *	as_query_predexp_add(&q, as_predexp_string_value("dog"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_not());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_not();

/**
 *	Create a constant integer value predicate expression.
 *
 *  The integer value predicate expression pushes a single constant
 *  integer value onto the expression stack.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "c" is between 11 and 20
 *  inclusive:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 7);
 *	as_query_predexp_add(&q, as_predexp_integer_bin("c"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(11));
 *  as_query_predexp_add(&q, as_predexp_integer_greatereq());
 *	as_query_predexp_add(&q, as_predexp_integer_bin("c"));
 *  as_query_predexp_add(&q, as_predexp_integer_value(20));
 *	as_query_predexp_add(&q, as_predexp_integer_lesseq());
 *	as_query_predexp_add(&q, as_predexp_and(2));
 *	~~~~~~~~~~
 *
 *  @param value	The integer value.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_integer_value(int64_t value);

/**
 *	Create a constant string value predicate expression.
 *
 *  The string value predicate expression pushes a single constant
 *  string value onto the expression stack.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "pet" is "cat" or "dog":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 7);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_string_value("dog"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_or(2));
 *	~~~~~~~~~~
 *
 *  @param value	The string value.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_string_value(char const * value);

/**
 *	Create a constant GeoJSON value predicate expression.
 *
 *  The GeoJSON value predicate expression pushes a single constant
 *  GeoJSON value onto the expression stack.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where a point in bin "loc" is inside the
 *  specified polygon:
 *
 *	~~~~~~~~~~{.c}
 *	char const * region =
 *		"{ "
 *		"    \"type\": \"Polygon\", "
 *		"    \"coordinates\": [ "
 *		"        [[-122.500000, 37.000000],[-121.000000, 37.000000], "
 *		"         [-121.000000, 38.080000],[-122.500000, 38.080000], "
 *		"         [-122.500000, 37.000000]] "
 *		"    ] "
 *		" } ";
 *
 *	as_query_predexp_inita(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_geojson_bin("loc"));
 *	as_query_predexp_add(&query, as_predexp_geojson_value(region));
 *	as_query_predexp_add(&query, as_predexp_geojson_within());
 *	~~~~~~~~~~
 *
 *  @param value	The GeoJSON string value.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_geojson_value(char const * value);

/**
 *	Create an integer bin value predicate expression.
 *
 *  The integer bin predicate expression pushes a single integer bin
 *  value extractor onto the expression stack.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "c" is between 11 and 20
 *  inclusive:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 7);
 *	as_query_predexp_add(&q, as_predexp_integer_bin("c"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(11));
 *  as_query_predexp_add(&q, as_predexp_integer_greatereq());
 *	as_query_predexp_add(&q, as_predexp_integer_bin("c"));
 *  as_query_predexp_add(&q, as_predexp_integer_value(20));
 *	as_query_predexp_add(&q, as_predexp_integer_lesseq());
 *	as_query_predexp_add(&q, as_predexp_and(2));
 *	~~~~~~~~~~
 *
 *  @param binname	The name of the bin.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_integer_bin(char const * binname);

/**
 *	Create an string bin value predicate expression.
 *
 *  The string bin predicate expression pushes a single string bin
 *  value extractor onto the expression stack.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the value of bin "pet" is "cat" or "dog":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 7);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_string_value("dog"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *  as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_or(2));
 *	~~~~~~~~~~
 *
 *  @param binname	The name of the bin.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_string_bin(char const * binname);

/**
 *	Create an GeoJSON bin value predicate expression.
 *
 *  The GeoJSON bin predicate expression pushes a single GeoJSON bin
 *  value extractor onto the expression stack.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where a point in bin "loc" is inside the
 *  specified polygon:
 *
 *	~~~~~~~~~~{.c}
 *	char const * region =
 *		"{ "
 *		"    \"type\": \"Polygon\", "
 *		"    \"coordinates\": [ "
 *		"        [[-122.500000, 37.000000],[-121.000000, 37.000000], "
 *		"         [-121.000000, 38.080000],[-122.500000, 38.080000], "
 *		"         [-122.500000, 37.000000]] "
 *		"    ] "
 *		" } ";
 *
 *	as_query_predexp_inita(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_geojson_bin("loc"));
 *	as_query_predexp_add(&query, as_predexp_geojson_value(region));
 *	as_query_predexp_add(&query, as_predexp_geojson_within());
 *	~~~~~~~~~~
 *
 *  @param binname	The name of the bin.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_geojson_bin(char const * binname);

/**
 *	Create a list bin value predicate expression.
 *
 *  The list bin predicate expression pushes a single list bin value
 *  extractor onto the expression stack.  List bin values may be used
 *  with list iteration expressions to evaluate a subexpression for
 *  each of the elements of the list.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where one of the list items is "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("item"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_list_bin("pets"));
 *	as_query_predexp_add(&q, as_predexp_list_iterate_or("item"));
 *	~~~~~~~~~~
 *
 *  @param binname	The name of the bin.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_list_bin(char const * binname);

/**
 *	Create a map bin value predicate expression.
 *
 *  The map bin predicate expression pushes a single map bin value
 *  extractor onto the expression stack.  Map bin values may be used
 *  with map iteration expressions to evaluate a subexpression for
 *  each of the elements of the map.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the map contains a key of "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("key"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_map_bin("petcount"));
 *	as_query_predexp_add(&q, as_predexp_mapkey_iterate_or("key"));
 *	~~~~~~~~~~
 *
 *  @param binname	The name of the bin.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_map_bin(char const * binname);

/**
 *	Create an integer iteration variable (value) predicate expression.
 *
 *  The integer iteration variable is used in the subexpression child
 *  of a list or map iterator and takes the value of each element in
 *  the collection as it is traversed.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where the list contains a value of 42:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_integer_var("item"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(42));
 *	as_query_predexp_add(&q, as_predexp_integer_equal());
 *	as_query_predexp_add(&q, as_predexp_list_bin("numbers"));
 *	as_query_predexp_add(&q, as_predexp_list_iterate_or("item"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_integer_var(char const * varname);

/**
 *	Create an string iteration variable (value) predicate expression.
 *
 *  The string iteration variable is used in the subexpression child
 *  of a list or map iterator and takes the value of each element in
 *  the collection as it is traversed.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where one of the list items is "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("item"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_list_bin("pets"));
 *	as_query_predexp_add(&q, as_predexp_list_iterate_or("item"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_string_var(char const * varname);

/**
 *	Create an GeoJSON iteration variable (value) predicate expression.
 *
 *  The GeoJSON iteration variable is used in the subexpression child
 *  of a list or map iterator and takes the value of each element in
 *  the collection as it is traversed.
 *
 *  @param varname	The name of the iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_geojson_var(char const * varname);

/**
 *	Create a record device size metadata value predicate expression.
 *
 *  The record device size expression assumes the value of the size in
 *  bytes that the record occupies on device storage. For non-persisted
 *  records, this value is 0.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records whose device storage size is larger than 65K:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_rec_device_size());
 *	as_query_predexp_add(&q, as_predexp_integer_value(65 * 1024));
 *	as_query_predexp_add(&q, as_predexp_integer_greater());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_rec_device_size();

/**
 *	Create a last update record metadata value predicate expression.
 *
 *  The record last update expression assumes the value of the number
 *  of nanoseconds since the unix epoch that the record was last
 *  updated.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records that have been updated after an timestamp:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_rec_last_update());
 *	as_query_predexp_add(&q, as_predexp_integer_value(g_tstampns));
 *	as_query_predexp_add(&q, as_predexp_integer_greater());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_rec_last_update();

/**
 *	Create a void time record metadata value predicate expression.
 *
 *  The record void time expression assumes the value of the number of
 *  nanoseconds since the unix epoch when the record will expire.  The
 *  special value of 0 means the record will not expire.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records that have void time set to 0 (no expiration):
 *
 *	~~~~~~~~~~{.c}
 *  as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_rec_void_time());
 *	as_query_predexp_add(&q, as_predexp_integer_value(0));
 *	as_query_predexp_add(&q, as_predexp_integer_equal());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_rec_void_time();

/**
 *	Create a digest modulo record metadata value predicate expression.
 *
 *  The digest modulo expression assumes the value of 4 bytes of the
 *  record's key digest modulo it's modulus argument.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records that have digest(key) % 3 == 1):
 *
 *	~~~~~~~~~~{.c}
 *  as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_rec_digest_modulo(3));
 *	as_query_predexp_add(&q, as_predexp_integer_value(1));
 *	as_query_predexp_add(&q, as_predexp_integer_equal());
 *	~~~~~~~~~~
 *
 *  @param mod	The modulus argument.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_rec_digest_modulo(int32_t mod);

/**
 *	Create an integer comparison logical predicate expression.
 *
 *  The integer comparison expressions pop a pair of value expressions
 *  off the expression stack and compare them.  The deeper of the two
 *  child expressions (pushed earlier) is considered the left side of
 *  the expression and the shallower (pushed later) is considered the
 *  right side.
 *
 *  If the value of either of the child expressions is unknown because
 *  a specified bin does not exist or contains a value of the wrong
 *  type the result of the comparison is false.  If a true outcome is
 *  desirable in this situation use the complimentary comparison and
 *  enclose in a logical NOT.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records that have bin "foo" greater than 42:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_integer_bin("foo"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(42));
 *	as_query_predexp_add(&q, as_predexp_integer_greater());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_integer_equal();

as_predexp_base*
as_predexp_integer_unequal();

as_predexp_base*
as_predexp_integer_greater();

as_predexp_base*
as_predexp_integer_greatereq();

as_predexp_base*
as_predexp_integer_less();

as_predexp_base*
as_predexp_integer_lesseq();

/**
 *	Create an string comparison logical predicate expression.
 *
 *  The string comparison expressions pop a pair of value expressions
 *  off the expression stack and compare them.  The deeper of the two
 *  child expressions (pushed earlier) is considered the left side of
 *  the expression and the shallower (pushed later) is considered the
 *  right side.
 *
 *  If the value of either of the child expressions is unknown because
 *  a specified bin does not exist or contains a value of the wrong
 *  type the result of the comparison is false.  If a true outcome is
 *  desirable in this situation use the complimentary comparison and
 *  enclose in a logical NOT.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records that have bin "pet" equal to "cat":
 *
 *	~~~~~~~~~~{.c}
 *  as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_bin("pet"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_string_equal();

as_predexp_base*
as_predexp_string_unequal();

/**
 *	Create an string regular expression logical predicate expression.
 *
 *  The string regex expression pops two children off the expression
 *  stack and compares a child value expression to a regular
 *  expression.  The left child (pushed earlier) must contain the
 *  string value to be matched.  The right child (pushed later) must
 *  be a string value containing a valid regular expression..
 *
 *  If the value of the left child is unknown because a specified
 *  bin does not exist or contains a value of the wrong type the
 *  result of the regex match is false.
 *
 *  The cflags argument is passed to the regcomp library routine on
 *  the server.  Useful values include REG_EXTENDED, REG_ICASE and
 *  REG_NEWLINE.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records that have bin "hex" value ending in '1' or '2':
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 3);
 *	as_query_predexp_add(&q, as_predexp_string_bin("hex"));
 *	as_query_predexp_add(&q, as_predexp_string_value("0x00.[12]"));
 *	as_query_predexp_add(&q, as_predexp_string_regex(REG_ICASE));
 *	~~~~~~~~~~
 *
 *  @param cflags	POSIX regex cflags value.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_string_regex(uint32_t cflags);

/**
 *	Create an GeoJSON Points-in-Region logical predicate expression.
 *
 *  The Points-in-Region (within) expression pops two children off the
 *  expression stack and checks to see if a child GeoJSON point is
 *  inside a specified GeoJSON region.  The left child (pushed
 *  earlier) must contain a GeoJSON value specifying a point.  The
 *  right child (pushed later) must be a GeoJSON value containing a
 *  region.
 *
 *  If the value of the left child is unknown because a specified bin
 *  does not exist or contains a value of the wrong type the result of
 *  the within expression is false.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where a point in bin "loc" is inside the
 *  specified polygon:
 *
 *	~~~~~~~~~~{.c}
 *	char const * region =
 *		"{ "
 *		"    \"type\": \"Polygon\", "
 *		"    \"coordinates\": [ "
 *		"        [[-122.500000, 37.000000],[-121.000000, 37.000000], "
 *		"         [-121.000000, 38.080000],[-122.500000, 38.080000], "
 *		"         [-122.500000, 37.000000]] "
 *		"    ] "
 *		" } ";
 *
 *	as_query_predexp_inita(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_geojson_bin("loc"));
 *	as_query_predexp_add(&query, as_predexp_geojson_value(region));
 *	as_query_predexp_add(&query, as_predexp_geojson_within());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_geojson_within();

/**
 *	Create an GeoJSON Regions-Containing-Point logical predicate expression.
 *
 *  The Regions-Containing-Point (contains) expression pops two
 *  children off the expression stack and checks to see if a child
 *  GeoJSON region contains a specified GeoJSON point.  The left child
 *  (pushed earlier) must contain a GeoJSON value specifying a
 *  possibly enclosing region.  The right child (pushed later) must be
 *  a GeoJSON value containing a point.
 *
 *  If the value of the left child is unknown because a specified bin
 *  does not exist or contains a value of the wrong type the result of
 *  the contains expression is false.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where a region in bin "rgn" contains the specified
 *  query point:
 *
 *	~~~~~~~~~~{.c}
 *	char const * point =
 *		"{ "
 *		"    \"type\": \"Point\", "
 *		"    \"coordinates\": [ -122.0986857, 37.4214209 ] "
 *		"} ";
 *
 *	as_query_predexp_inita(&query, 3);
 *	as_query_predexp_add(&query, as_predexp_geojson_bin("rgn"));
 *	as_query_predexp_add(&query, as_predexp_geojson_value(point));
 *	as_query_predexp_add(&query, as_predexp_geojson_contains());
 *	~~~~~~~~~~
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_geojson_contains();

/**
 *	Create an list iteration OR logical predicate expression.
 *
 *  The list iteration expression pops two children off the expression
 *  stack.  The left child (pushed earlier) must contain a logical
 *  subexpression containing one or more matching iteration variable
 *  expressions.  The right child (pushed later) must specify a list
 *  bin.  The list iteration traverses the list and repeatedly
 *  evaluates the subexpression substituting each list element's value
 *  into the matching iteration variable.  The result of the iteration
 *  expression is a logical OR of all of the individual element
 *  evaluations.
 *
 *  If the list bin contains zero elements as_predexp_list_iterate_or
 *  will return false.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where one of the list items is "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("item"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_list_bin("pets"));
 *	as_query_predexp_add(&q, as_predexp_list_iterate_or("item"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the list item iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_list_iterate_or(char const * varname);

/**
 *	Create an list iteration AND logical predicate expression.
 *
 *  The list iteration expression pops two children off the expression
 *  stack.  The left child (pushed earlier) must contain a logical
 *  subexpression containing one or more matching iteration variable
 *  expressions.  The right child (pushed later) must specify a list
 *  bin.  The list iteration traverses the list and repeatedly
 *  evaluates the subexpression substituting each list element's value
 *  into the matching iteration variable.  The result of the iteration
 *  expression is a logical AND of all of the individual element
 *  evaluations.
 *
 *  If the list bin contains zero elements as_predexp_list_iterate_and
 *  will return true.  This is useful when testing for exclusion (see
 *  example).
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where none of the list items is "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 6);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("item"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_not());
 *	as_query_predexp_add(&q, as_predexp_list_bin("pets"));
 *	as_query_predexp_add(&q, as_predexp_list_iterate_and("item"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the list item iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_list_iterate_and(char const * varname);

/**
 *	Create an map key iteration OR logical predicate expression.
 *
 *  The mapkey iteration expression pops two children off the
 *  expression stack.  The left child (pushed earlier) must contain a
 *  logical subexpression containing one or more matching iteration
 *  variable expressions.  The right child (pushed later) must specify
 *  a map bin.  The mapkey iteration traverses the map and repeatedly
 *  evaluates the subexpression substituting each map key value into
 *  the matching iteration variable.  The result of the iteration
 *  expression is a logical OR of all of the individual element
 *  evaluations.
 *
 *  If the map bin contains zero elements as_predexp_mapkey_iterate_or
 *  will return false.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where one of the map keys is "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("item"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_map_bin("petcount"));
 *	as_query_predexp_add(&q, as_predexp_mapkey_iterate_or("item"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the map key iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_mapkey_iterate_or(char const * varname);

/**
 *	Create an map key iteration AND logical predicate expression.
 *
 *  The mapkey iteration expression pops two children off the
 *  expression stack.  The left child (pushed earlier) must contain a
 *  logical subexpression containing one or more matching iteration
 *  variable expressions.  The right child (pushed later) must specify
 *  a map bin.  The mapkey iteration traverses the map and repeatedly
 *  evaluates the subexpression substituting each map key value into
 *  the matching iteration variable.  The result of the iteration
 *  expression is a logical AND of all of the individual element
 *  evaluations.
 *
 *  If the map bin contains zero elements as_predexp_mapkey_iterate_and
 *  will return true.  This is useful when testing for exclusion (see
 *  example).
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where none of the map keys is "cat":
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 6);
 *	as_query_predexp_add(&q, as_predexp_string_value("cat"));
 *	as_query_predexp_add(&q, as_predexp_string_var("item"));
 *	as_query_predexp_add(&q, as_predexp_string_equal());
 *	as_query_predexp_add(&q, as_predexp_not());
 *	as_query_predexp_add(&q, as_predexp_map_bin("petcount"));
 *	as_query_predexp_add(&q, as_predexp_mapkey_iterate_or("item"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the map key iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_mapkey_iterate_and(char const * varname);

/**
 *	Create an map value iteration OR logical predicate expression.
 *
 *  The mapval iteration expression pops two children off the
 *  expression stack.  The left child (pushed earlier) must contain a
 *  logical subexpression containing one or more matching iteration
 *  variable expressions.  The right child (pushed later) must specify
 *  a map bin.  The mapval iteration traverses the map and repeatedly
 *  evaluates the subexpression substituting each map value into the
 *  matching iteration variable.  The result of the iteration
 *  expression is a logical OR of all of the individual element
 *  evaluations.
 *
 *  If the map bin contains zero elements as_predexp_mapval_iterate_or
 *  will return false.
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where one of the map values is 0:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 5);
 *	as_query_predexp_add(&q, as_predexp_integer_var("count"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(0));
 *	as_query_predexp_add(&q, as_predexp_integer_equal());
 *	as_query_predexp_add(&q, as_predexp_map_bin("petcount"));
 *	as_query_predexp_add(&q, as_predexp_mapval_iterate_or("count"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the map value iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_mapval_iterate_or(char const * varname);

/**
 *	Create an map value iteration AND logical predicate expression.
 *
 *  The mapval iteration expression pops two children off the
 *  expression stack.  The left child (pushed earlier) must contain a
 *  logical subexpression containing one or more matching iteration
 *  variable expressions.  The right child (pushed later) must specify
 *  a map bin.  The mapval iteration traverses the map and repeatedly
 *  evaluates the subexpression substituting each map value into the
 *  matching iteration variable.  The result of the iteration
 *  expression is a logical AND of all of the individual element
 *  evaluations.
 *
 *  If the map bin contains zero elements as_predexp_mapval_iterate_and
 *  will return true.  This is useful when testing for exclusion (see
 *  example).
 *
 *  For example, the following sequence of predicate expressions
 *  selects records where none of the map values is 0:
 *
 *	~~~~~~~~~~{.c}
 *	as_query_predexp_inita(&q, 6);
 *	as_query_predexp_add(&q, as_predexp_integer_var("count"));
 *	as_query_predexp_add(&q, as_predexp_integer_value(0));
 *	as_query_predexp_add(&q, as_predexp_integer_equal());
 *	as_query_predexp_add(&q, as_predexp_not());
 *	as_query_predexp_add(&q, as_predexp_map_bin("petcount"));
 *	as_query_predexp_add(&q, as_predexp_mapval_iterate_and("count"));
 *	~~~~~~~~~~
 *
 *  @param varname	The name of the map value iteration variable.
 *
 *  @returns a predicate expression suitable for adding to a query or
 *  scan.
 */
as_predexp_base*
as_predexp_mapval_iterate_and(char const * varname);

#ifdef __cplusplus
} // end extern "C"
#endif
