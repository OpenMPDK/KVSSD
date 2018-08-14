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

#include <aerospike/aerospike.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************
 *	TYPES
 *****************************************************************************/

/**
 *	The status of a particular background scan.
 */
typedef enum as_job_status_e {
	/**
	 *	The job status is undefined.
	 *	This is likely due to the status not being properly checked.
	 */
	AS_JOB_STATUS_UNDEF,

	/**
	 *	The job is currently running.
	 */
	AS_JOB_STATUS_INPROGRESS,

	/**
	 *	The job completed successfully.
	 */
	AS_JOB_STATUS_COMPLETED,
} as_job_status;

/**
 *	Information about a particular background job.
 */
typedef struct as_job_info_s {
	/**
	 *	Status of the job.
	 */
	as_job_status status;

	/**
	 *	Progress estimate for the job, as percentage.
	 */
	uint32_t progress_pct;

	/**
	 *	How many records have been scanned.
	 */
	uint32_t records_read;
} as_job_info;

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

/**
 *	Wait for a background job to be completed by servers.
 *
 *	~~~~~~~~~~{.c}
 *	uint64_t job_id = 1234;
 *	aerospike_job_wait(&as, &err, NULL, "scan", job_id, 0);
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param module		Background module. Values: scan | query
 *	@param job_id		Job ID.
 *	@param interval_ms	Polling interval in milliseconds. If zero, 1000 ms is used.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error occurred.
 */
as_status
aerospike_job_wait(
	aerospike* as, as_error* err, const as_policy_info* policy, const char* module, uint64_t job_id,
	uint32_t interval_ms
	);
	
/**
 *	Check the progress of a background job running on the database. The status
 *	of the job running on the datatabse will be populated in as_job_info.
 *
 *	~~~~~~~~~~{.c}
 *	uint64_t job_id = 1234;
 *	as_job_info job_info;
 *
 *	if (aerospike_scan_info(&as, &err, NULL, "scan", job_id, &job_info) != AEROSPIKE_OK) {
 *		fprintf(stderr, "error(%d) %s at [%s:%d]", err.code, err.message, err.file, err.line);
 *	}
 *	else {
 *		printf("Scan id=%ll, status=%d percent=%d", job_id, job_info.status, job_info.progress_pct);
 *	}
 *	~~~~~~~~~~
 *
 *	@param as			The aerospike instance to use for this operation.
 *	@param err			The as_error to be populated if an error occurs.
 *	@param policy		The policy to use for this operation. If NULL, then the default policy will be used.
 *	@param module		Background module. Values: scan | query
 *	@param job_id		Job ID.
 *	@param stop_if_in_progress	Stop querying nodes if background job is still running.
 *	@param info			Information about this background job, to be populated by this operation.
 *
 *	@return AEROSPIKE_OK on success. Otherwise an error occurred.
 */
as_status
aerospike_job_info(
	aerospike* as, as_error* err, const as_policy_info* policy, const char* module, uint64_t job_id,
	bool stop_if_in_progress, as_job_info * info
	);

#ifdef __cplusplus
} // end extern "C"
#endif
