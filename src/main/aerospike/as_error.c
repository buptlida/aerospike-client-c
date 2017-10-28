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
#include <aerospike/as_error.h>

/******************************************************************************
 *	FUNCTIONS
 *****************************************************************************/

#define ERR_ASSIGN(__enum) return #__enum

#define CASE_ASSIGN(__enum) \
case __enum : \
return #__enum; \

char*
as_error_string(as_status status)
{
	switch (status) {
		CASE_ASSIGN(AEROSPIKE_OK);
		CASE_ASSIGN(AEROSPIKE_QUERY_END);

		CASE_ASSIGN(AEROSPIKE_ERR_CONNECTION);
		CASE_ASSIGN(AEROSPIKE_ERR_TLS_ERROR);
		CASE_ASSIGN(AEROSPIKE_ERR_INVALID_NODE);
		CASE_ASSIGN(AEROSPIKE_ERR_NO_MORE_CONNECTIONS);
		CASE_ASSIGN(AEROSPIKE_ERR_ASYNC_CONNECTION);
		CASE_ASSIGN(AEROSPIKE_ERR_CLIENT_ABORT);
		CASE_ASSIGN(AEROSPIKE_ERR_INVALID_HOST);
		CASE_ASSIGN(AEROSPIKE_NO_MORE_RECORDS);
		CASE_ASSIGN(AEROSPIKE_ERR_PARAM);
		CASE_ASSIGN(AEROSPIKE_ERR_CLIENT);
		CASE_ASSIGN(AEROSPIKE_ERR_SERVER);
		CASE_ASSIGN(AEROSPIKE_ERR_RECORD_NOT_FOUND);
		CASE_ASSIGN(AEROSPIKE_ERR_RECORD_GENERATION);
		CASE_ASSIGN(AEROSPIKE_ERR_REQUEST_INVALID);
		CASE_ASSIGN(AEROSPIKE_ERR_RECORD_EXISTS);
		CASE_ASSIGN(AEROSPIKE_ERR_BIN_EXISTS);
		CASE_ASSIGN(AEROSPIKE_ERR_CLUSTER_CHANGE);
		CASE_ASSIGN(AEROSPIKE_ERR_SERVER_FULL);
		CASE_ASSIGN(AEROSPIKE_ERR_TIMEOUT);
		CASE_ASSIGN(AEROSPIKE_ERR_ALWAYS_FORBIDDEN);
		CASE_ASSIGN(AEROSPIKE_ERR_CLUSTER);
		CASE_ASSIGN(AEROSPIKE_ERR_BIN_INCOMPATIBLE_TYPE);
		CASE_ASSIGN(AEROSPIKE_ERR_RECORD_TOO_BIG);
		CASE_ASSIGN(AEROSPIKE_ERR_RECORD_BUSY);
		CASE_ASSIGN(AEROSPIKE_ERR_SCAN_ABORTED);
		CASE_ASSIGN(AEROSPIKE_ERR_UNSUPPORTED_FEATURE);
		CASE_ASSIGN(AEROSPIKE_ERR_BIN_NOT_FOUND);
		CASE_ASSIGN(AEROSPIKE_ERR_DEVICE_OVERLOAD);
		CASE_ASSIGN(AEROSPIKE_ERR_RECORD_KEY_MISMATCH);
		CASE_ASSIGN(AEROSPIKE_ERR_NAMESPACE_NOT_FOUND);
		CASE_ASSIGN(AEROSPIKE_ERR_BIN_NAME);
		CASE_ASSIGN(AEROSPIKE_ERR_FAIL_FORBIDDEN);
		CASE_ASSIGN(AEROSPIKE_SECURITY_NOT_SUPPORTED);
		CASE_ASSIGN(AEROSPIKE_SECURITY_NOT_ENABLED);
		CASE_ASSIGN(AEROSPIKE_SECURITY_SCHEME_NOT_SUPPORTED);
		CASE_ASSIGN(AEROSPIKE_INVALID_COMMAND);
		CASE_ASSIGN(AEROSPIKE_INVALID_FIELD);
		CASE_ASSIGN(AEROSPIKE_ILLEGAL_STATE);
		CASE_ASSIGN(AEROSPIKE_INVALID_USER);
		CASE_ASSIGN(AEROSPIKE_USER_ALREADY_EXISTS);
		CASE_ASSIGN(AEROSPIKE_INVALID_PASSWORD);
		CASE_ASSIGN(AEROSPIKE_EXPIRED_PASSWORD);
		CASE_ASSIGN(AEROSPIKE_FORBIDDEN_PASSWORD);
		CASE_ASSIGN(AEROSPIKE_INVALID_CREDENTIAL);
		CASE_ASSIGN(AEROSPIKE_INVALID_ROLE);
		CASE_ASSIGN(AEROSPIKE_ROLE_ALREADY_EXISTS);
		CASE_ASSIGN(AEROSPIKE_INVALID_PRIVILEGE);
		CASE_ASSIGN(AEROSPIKE_NOT_AUTHENTICATED);
		CASE_ASSIGN(AEROSPIKE_ROLE_VIOLATION);
		CASE_ASSIGN(AEROSPIKE_ERR_UDF);
		CASE_ASSIGN(AEROSPIKE_ERR_LARGE_ITEM_NOT_FOUND);
		CASE_ASSIGN(AEROSPIKE_ERR_BATCH_DISABLED);
		CASE_ASSIGN(AEROSPIKE_ERR_BATCH_MAX_REQUESTS_EXCEEDED);
		CASE_ASSIGN(AEROSPIKE_ERR_BATCH_QUEUES_FULL);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX_FOUND);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX_NOT_FOUND);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX_OOM);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX_NOT_READABLE);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX_NAME_MAXLEN);
		CASE_ASSIGN(AEROSPIKE_ERR_INDEX_MAXCOUNT);
		CASE_ASSIGN(AEROSPIKE_ERR_QUERY_ABORTED);
		CASE_ASSIGN(AEROSPIKE_ERR_QUERY_QUEUE_FULL);
		CASE_ASSIGN(AEROSPIKE_ERR_QUERY_TIMEOUT);
		CASE_ASSIGN(AEROSPIKE_ERR_QUERY);
			
		default:
			if (status < 0) {
				ERR_ASSIGN(AEROSPIKE_ERR_CLIENT);
			}
			else {
				ERR_ASSIGN(AEROSPIKE_ERR_SERVER);
			}
	}
}
