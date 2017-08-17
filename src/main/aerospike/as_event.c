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
#include <aerospike/as_event.h>
#include <aerospike/as_event_internal.h>
#include <aerospike/as_admin.h>
#include <aerospike/as_command.h>
#include <aerospike/as_log_macros.h>
#include <aerospike/as_monitor.h>
#include <aerospike/as_pipe.h>
#include <aerospike/as_proto.h>
#include <aerospike/as_shm_cluster.h>
#include <citrusleaf/alloc.h>
#include <errno.h>
#include <pthread.h>

/******************************************************************************
 * GLOBALS
 *****************************************************************************/

as_event_loop* as_event_loops = 0;
as_event_loop* as_event_loop_current = 0;
uint32_t as_event_loop_capacity = 0;
uint32_t as_event_loop_size = 0;
int as_event_send_buffer_size = 0;
int as_event_recv_buffer_size = 0;
bool as_event_threads_created = false;

/******************************************************************************
 * PUBLIC FUNCTIONS
 *****************************************************************************/

// Force link error on event initialization when event library not defined.
#if AS_EVENT_LIB_DEFINED

static bool
as_event_initialize_loops(uint32_t capacity)
{
	if (capacity == 0) {
		return false;
	}
	
	as_event_send_buffer_size = as_pipe_get_send_buffer_size();
	as_event_recv_buffer_size = as_pipe_get_recv_buffer_size();

	as_event_loops = cf_calloc(capacity, sizeof(as_event_loop));
	
	if (! as_event_loops) {
		return false;
	}

	as_event_loop_capacity = capacity;
	as_event_loop_current = as_event_loops;
	
	// Initialize first loop to circular linked list for efficient round-robin
	// event loop distribution.
	as_event_loops->next = as_event_loops;
	return true;
}

as_event_loop*
as_event_create_loops(uint32_t capacity)
{
	if (! as_event_initialize_loops(capacity)) {
		return 0;
	}
	
	as_event_threads_created = true;
	
	for (uint32_t i = 0; i < capacity; i++) {
		as_event_loop* event_loop = &as_event_loops[i];

		event_loop->loop = 0;
		pthread_mutex_init(&event_loop->lock, 0);
		event_loop->thread = 0;
		event_loop->index = i;
		event_loop->errors = 0;
		as_queue_init(&event_loop->queue, sizeof(as_event_commander), AS_EVENT_QUEUE_INITIAL_CAPACITY);
		as_queue_init(&event_loop->pipe_cb_queue, sizeof(as_queued_pipe_cb), AS_EVENT_QUEUE_INITIAL_CAPACITY);
		event_loop->pipe_cb_calling = false;

		if (! as_event_create_loop(event_loop)) {
			as_event_close_loops();
			return 0;
		}
		
		if (i > 0) {
			// This loop points to first loop to create circular round-robin linked list.
			event_loop->next = as_event_loops;
			
			// Adjust previous loop to point to this loop.
			as_event_loops[i - 1].next = event_loop;
		}
		as_event_loop_size++;
	}
	return as_event_loops;
}

bool
as_event_set_external_loop_capacity(uint32_t capacity)
{
	if (! as_event_initialize_loops(capacity)) {
		return 0;
	}
	
	as_event_threads_created = false;
	return true;
}

#endif

as_event_loop*
as_event_set_external_loop(void* loop)
{
	uint32_t current = ck_pr_faa_32(&as_event_loop_size, 1);
	
	if (current >= as_event_loop_capacity) {
		as_log_error("Failed to add external loop. Capacity is %u", as_event_loop_capacity);
		return 0;
	}
	
	as_event_loop* event_loop = &as_event_loops[current];
	event_loop->loop = loop;
	pthread_mutex_init(&event_loop->lock, 0);
	event_loop->thread = pthread_self();  // Current thread must be same as event loop thread!
	event_loop->index = current;
	event_loop->errors = 0;
	as_queue_init(&event_loop->queue, sizeof(as_event_commander), AS_EVENT_QUEUE_INITIAL_CAPACITY);
	as_queue_init(&event_loop->pipe_cb_queue, sizeof(as_queued_pipe_cb), AS_EVENT_QUEUE_INITIAL_CAPACITY);
	event_loop->pipe_cb_calling = false;
	as_event_register_external_loop(event_loop);

	if (current > 0) {
		// This loop points to first loop to create circular round-robin linked list.
		event_loop->next = as_event_loops;
		
		// Adjust previous loop to point to this loop.
		// Warning: not synchronized with as_event_loop_get()
		as_event_loops[current - 1].next = event_loop;
	}
	return event_loop;
}

as_event_loop*
as_event_loop_find(void* loop)
{
	for (uint32_t i = 0; i < as_event_loop_size; i++) {
		as_event_loop* event_loop = &as_event_loops[i];
		
		if (event_loop->loop == loop) {
			return event_loop;
		}
	}
	return NULL;
}

bool
as_event_close_loops()
{
	if (! as_event_loops) {
		return false;
	}
	
	bool status = true;
	
	// Close or send close signal to all event loops.
	// This will eventually release resources associated with each event loop.
	for (uint32_t i = 0; i < as_event_loop_size; i++) {
		as_event_loop* event_loop = &as_event_loops[i];
	
		// Calling close directly can cause previously queued commands to be dropped.
		// Therefore, always queue close command to event loop.
		if (! as_event_execute(event_loop, NULL, NULL)) {
			as_log_error("Failed to send stop command to event loop");
			status = false;
		}
	}

	// Only join threads if event loops were created internally.
	// It is not possible to join on externally created event loop threads.
	if (as_event_threads_created && status) {
		for (uint32_t i = 0; i < as_event_loop_size; i++) {
			as_event_loop* event_loop = &as_event_loops[i];
			pthread_join(event_loop->thread, NULL);
		}
		as_event_destroy_loops();
	}
	return status;
}

void
as_event_destroy_loops()
{
	if (as_event_loops) {
		cf_free(as_event_loops);
		as_event_loops = NULL;
		as_event_loop_size = 0;
	}
}

/******************************************************************************
 * PRIVATE FUNCTIONS
 *****************************************************************************/

static void as_event_command_execute_in_loop(as_event_command* cmd);
static void as_event_command_begin(as_event_command* cmd);

as_status
as_event_command_execute(as_event_command* cmd, as_error* err)
{
	// Initialize read buffer (buf) to be located after write buffer.
	cmd->write_offset = (uint32_t)(cmd->buf - (uint8_t*)cmd);
	cmd->buf += cmd->write_len;

	as_event_loop* event_loop = cmd->event_loop;

	// Use pointer comparison for performance.  If portability becomes an issue, use
	// "pthread_equal(event_loop->thread, pthread_self())" instead.
	// Also, avoid recursive error death spiral by forcing command to be queued to
	// event loop when consecutive recursive errors reaches an approximate limit.
	if (event_loop->thread == pthread_self() && event_loop->errors < 5) {
		// We are already in event loop thread, so start processing.
		as_event_command_execute_in_loop(cmd);
	}
	else {
		// Send command through queue so it can be executed in event loop thread.
		if (cmd->total_deadline > 0) {
			// Convert total timeout to deadline.
			cmd->total_deadline += cf_getms();
		}
		cmd->state = AS_ASYNC_STATE_REGISTERED;

		if (! as_event_execute(cmd->event_loop, (as_event_executable)as_event_command_execute_in_loop, cmd)) {
			event_loop->errors++;  // Not in event loop thread, so not exactly accurate.
			if (cmd->node) {
				as_node_release(cmd->node);
			}
			cf_free(cmd);
			return as_error_set_message(err, AEROSPIKE_ERR_CLIENT, "Failed to queue command");
		}
	}
	return AEROSPIKE_OK;
}

static void
as_event_command_execute_in_loop(as_event_command* cmd)
{
	as_event_loop* event_loop = cmd->event_loop;

	if (cmd->cluster->pending[event_loop->index]++ == -1) {
		event_loop->errors++;
		cmd->state = AS_ASYNC_STATE_COMPLETE;

		as_error err;
		as_error_set_message(&err, AEROSPIKE_ERR_CLIENT, "Cluster has been closed");
		as_event_error_callback(cmd, &err);
		return;
	}

	if (cmd->total_deadline > 0) {
		uint64_t now = cf_getms();
		uint64_t total_timeout;

		if (cmd->state == AS_ASYNC_STATE_REGISTERED) {
			// Command was queued to event loop thread.
			if (now >= cmd->total_deadline) {
				// Command already timed out.
				event_loop->errors++;
				cmd->state = AS_ASYNC_STATE_COMPLETE;

				as_error err;
				as_error_set_message(&err, AEROSPIKE_ERR_TIMEOUT, "Register timeout");
				as_event_error_callback(cmd, &err);
				return;
			}
			total_timeout = cmd->total_deadline - now;
		}
		else {
			// Convert total timeout to deadline.
			total_timeout = cmd->total_deadline;
			cmd->total_deadline += now;
		}

		if (cmd->socket_timeout > 0 && cmd->socket_timeout < total_timeout) {
			// Use socket timer.
			as_event_init_socket_timer(cmd);
			cmd->flags |= AS_ASYNC_FLAGS_HAS_TIMER | AS_ASYNC_FLAGS_USING_SOCKET_TIMER;
		}
		else {
			// Use total timer.
			as_event_init_total_timer(cmd, total_timeout);
			cmd->flags |= AS_ASYNC_FLAGS_HAS_TIMER;
		}
	}
	else if (cmd->socket_timeout > 0) {
		// Use socket timer.
		as_event_init_socket_timer(cmd);
		cmd->flags |= AS_ASYNC_FLAGS_HAS_TIMER | AS_ASYNC_FLAGS_USING_SOCKET_TIMER;
	}

	// Start processing.
	as_event_command_begin(cmd);
}

static void
as_event_command_begin(as_event_command* cmd)
{
	if (cmd->partition) {
		// If in retry, need to release node from prior attempt.
		if (cmd->node) {
			as_node_release(cmd->node);
		}

		if (cmd->cluster->shm_info) {
			cmd->node = as_partition_shm_get_node(cmd->cluster, cmd->partition, cmd->replica, cmd->flags & AS_ASYNC_FLAGS_MASTER);
		}
		else {
			cmd->node = as_partition_get_node(cmd->cluster, cmd->partition, cmd->replica, cmd->flags & AS_ASYNC_FLAGS_MASTER);
		}

		if (! cmd->node) {
			as_error err;
			as_error_set_message(&err, AEROSPIKE_ERR_CLIENT, "Cluster is empty");

			if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
				as_event_stop_timer(cmd);
			}
			as_event_error_callback(cmd, &err);
			return;
		}
	}

	if (cmd->pipe_listener) {
		as_pipe_get_connection(cmd);
		return;
	}

	as_conn_pool* pool = &cmd->node->async_conn_pools[cmd->event_loop->index];
	as_async_connection* conn;

	// Find connection.
	while (as_conn_pool_get(pool, &conn)) {
		// Verify that socket is active and receive buffer is empty.
		int len = as_event_validate_connection(&conn->base);

		if (len == 0) {
			conn->cmd = cmd;
			cmd->conn = (as_event_connection*)conn;
			cmd->event_loop->errors = 0;  // Reset errors on valid connection.
			as_event_command_write_start(cmd);
			return;
		}

		as_log_debug("Invalid async socket from pool: %d", len);
		as_event_release_connection(&conn->base, pool);
	}

	// Create connection structure only when node connection count within queue limit.
	if (as_conn_pool_inc(pool)) {
		conn = cf_malloc(sizeof(as_async_connection));
		conn->base.pipeline = false;
		conn->base.watching = 0;
		conn->cmd = cmd;
		cmd->conn = &conn->base;
		as_event_connect(cmd);
		return;
	}

	cmd->event_loop->errors++;

	if (! as_event_command_retry(cmd, true)) {
		as_error err;
		as_error_update(&err, AEROSPIKE_ERR_NO_MORE_CONNECTIONS,
						"Max node/event loop %s async connections would be exceeded: %u",
						cmd->node->name, pool->limit);

		if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
			as_event_stop_timer(cmd);
		}
		as_event_error_callback(cmd, &err);
	}
}

void
as_event_socket_timeout(as_event_command* cmd)
{
	if (cmd->flags & AS_ASYNC_FLAGS_EVENT_RECEIVED) {
		// Event(s) received within socket timeout period.
		cmd->flags &= ~AS_ASYNC_FLAGS_EVENT_RECEIVED;

		if (cmd->total_deadline > 0) {
			// Check total timeout.
			uint64_t now = cf_getms();

			if (now >= cmd->total_deadline) {
				cmd->iteration++;
				as_event_stop_timer(cmd);
				as_event_total_timeout(cmd);
				return;
			}

			uint64_t remaining = cmd->total_deadline - now;

			if (remaining <= cmd->socket_timeout) {
				// Transition to total timer.
				cmd->flags &= ~AS_ASYNC_FLAGS_USING_SOCKET_TIMER;
				as_event_stop_timer(cmd);
				as_event_set_total_timer(cmd, remaining);
			}
			else {
				as_event_repeat_socket_timer(cmd);
			}
		}
		else {
			as_event_repeat_socket_timer(cmd);
		}
		return;
	}

	if (cmd->pipe_listener) {
		as_pipe_timeout(cmd, true);
		return;
	}

	// Close connection.
	as_conn_pool* pool = &cmd->node->async_conn_pools[cmd->event_loop->index];
	as_event_connection_timeout(cmd, pool);

	// Attempt retry.
	// Read commands shift to prole node on timeout.
	if (! as_event_command_retry(cmd, cmd->flags & AS_ASYNC_FLAGS_READ)) {
		as_event_stop_timer(cmd);

		as_error err;
		const char* node_string = cmd->node ? as_node_get_address_string(cmd->node) : "null";
		as_error_update(&err, AEROSPIKE_ERR_TIMEOUT, "Timeout: iterations=%u lastNode=%s",
						cmd->iteration, node_string);

		as_event_error_callback(cmd, &err);
	}
}

void
as_event_total_timeout(as_event_command* cmd)
{
	if (cmd->pipe_listener) {
		as_pipe_timeout(cmd, false);
		return;
	}

	as_error err;
	const char* node_string = cmd->node ? as_node_get_address_string(cmd->node) : "null";
	as_error_update(&err, AEROSPIKE_ERR_TIMEOUT, "Timeout: iterations=%u lastNode=%s",
					cmd->iteration, node_string);

	as_conn_pool* pool = &cmd->node->async_conn_pools[cmd->event_loop->index];
	as_event_connection_timeout(cmd, pool);

	as_event_error_callback(cmd, &err);
}

bool
as_event_command_retry(as_event_command* cmd, bool alternate)
{
	// Check max retries.
	if (++(cmd->iteration) > cmd->max_retries) {
		return false;
	}

	if (cmd->total_deadline > 0) {
		// Check total timeout.
		uint64_t now = cf_getms();

		if (now >= cmd->total_deadline) {
			return false;
		}

		if (cmd->flags & AS_ASYNC_FLAGS_USING_SOCKET_TIMER) {
			uint64_t remaining = cmd->total_deadline - now;

			if (remaining <= cmd->socket_timeout) {
				// Transition to total timer.
				cmd->flags &= ~AS_ASYNC_FLAGS_USING_SOCKET_TIMER;
				as_event_stop_timer(cmd);
				as_event_set_total_timer(cmd, remaining);
			}
			else {
				as_event_repeat_socket_timer(cmd);
			}
		}
	}
	else if (cmd->flags & AS_ASYNC_FLAGS_USING_SOCKET_TIMER) {
		as_event_repeat_socket_timer(cmd);
	}

	if (alternate) {
		cmd->flags ^= AS_ASYNC_FLAGS_MASTER;  // Alternate between master and prole.
	}

	// Retry command at the end of the queue so other commands have a chance to run first.
	return as_event_execute(cmd->event_loop, (as_event_executable)as_event_command_begin, cmd);
}

static inline void
as_event_put_connection(as_event_command* cmd, as_conn_pool* pool)
{
	as_event_set_conn_last_used(cmd->conn, cmd->cluster->max_socket_idle);

	if (! as_conn_pool_put(pool, &cmd->conn)) {
		as_event_release_connection(cmd->conn, pool);
	}
}

static inline void
as_event_response_complete(as_event_command* cmd)
{
	if (cmd->pipe_listener != NULL) {
		as_pipe_response_complete(cmd);
		return;
	}
	
	if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
		as_event_stop_timer(cmd);
	}
	as_event_stop_watcher(cmd, cmd->conn);

	as_conn_pool* pool = &cmd->node->async_conn_pools[cmd->event_loop->index];
	as_event_put_connection(cmd, pool);
}

static inline void
as_event_executor_destroy(as_event_executor* executor)
{
	pthread_mutex_destroy(&executor->lock);
	
	if (executor->commands) {
		cf_free(executor->commands);
	}

	if (executor->err) {
		cf_free(executor->err);
	}
	
	cf_free(executor);
}

static void
as_event_executor_error(as_event_executor* executor, as_error* err, int queued_count)
{
	pthread_mutex_lock(&executor->lock);
	bool first_error = executor->valid;
	executor->valid = false;

	if (queued_count >= 0) {
		// Add tasks that were never queued.
		executor->count += (executor->max - queued_count);
	}
	else {
		executor->count++;
	}

	bool complete = executor->count == executor->max;
	pthread_mutex_unlock(&executor->lock);

	if (complete) {
		// All commands have completed.
		// If scan or query user callback already returned false,
		// do not re-notify user that an error occurred.
		if (executor->notify) {
			if (first_error) {
				// Original error can be used directly.
				executor->err = err;
				executor->complete_fn(executor);
				executor->err = NULL;
			}
			else {
				// Use saved error.
				executor->complete_fn(executor);
			}
		}
		as_event_executor_destroy(executor);
	}
	else if (first_error)
	{
		// Save first error only.
		executor->err = cf_malloc(sizeof(as_error));
		as_error_copy(executor->err, err);
	}
}

void
as_event_executor_cancel(as_event_executor* executor, int queued_count)
{
	// Cancel group of commands that already have been queued.
	// We are cancelling commands running in the event loop thread when this method
	// is NOT running in the event loop thread.  Enforce thread-safety.
	pthread_mutex_lock(&executor->lock);
	executor->valid = false;
	
	// Add tasks that were never queued.
	executor->count += (executor->max - queued_count);
	
	bool complete = executor->count == executor->max;
	pthread_mutex_unlock(&executor->lock);

	if (complete) {
		// Do not call user listener because an error will be returned
		// on initial batch, scan or query call.
		as_event_executor_destroy(executor);
	}
}

void
as_event_executor_complete(as_event_command* cmd)
{
	as_event_response_complete(cmd);
	
	as_event_executor* executor = cmd->udata;
	pthread_mutex_lock(&executor->lock);
	executor->count++;
	bool complete = executor->count == executor->max;
	int next = executor->count + executor->max_concurrent - 1;
	bool start_new_command = next < executor->max && executor->valid;
	pthread_mutex_unlock(&executor->lock);

	if (complete) {
		// All commands completed.
		// If scan or query user callback already returned false,
		// do not re-notify user that an error occurred.
		if (executor->notify) {
			executor->complete_fn(executor);
		}
		as_event_executor_destroy(executor);
	}
	else {
		// Determine if a new command needs to be started.
		if (start_new_command) {
			as_error err;
			as_status status = as_event_command_execute(executor->commands[next], &err);
			
			if (status != AEROSPIKE_OK) {
				as_event_executor_error(executor, &err, next);
			}
		}
	}
	as_event_command_release(cmd);
}

void
as_event_error_callback(as_event_command* cmd, as_error* err)
{
	switch (cmd->type) {
		case AS_ASYNC_TYPE_WRITE:
			((as_async_write_command*)cmd)->listener(err, cmd->udata, cmd->event_loop);
			break;
		case AS_ASYNC_TYPE_RECORD:
			((as_async_record_command*)cmd)->listener(err, 0, cmd->udata, cmd->event_loop);
			break;
		case AS_ASYNC_TYPE_VALUE:
			((as_async_value_command*)cmd)->listener(err, 0, cmd->udata, cmd->event_loop);
			break;
			
		default:
			// Handle command that is part of a group (batch, scan, query).
			as_event_executor_error(cmd->udata, err, -1);
			break;
	}

	as_event_command_release(cmd);
}

void
as_event_parse_error(as_event_command* cmd, as_error* err)
{
	if (cmd->pipe_listener) {
		as_pipe_socket_error(cmd, err, false);
		return;
	}

	// Close connection.
	as_event_stop_watcher(cmd, cmd->conn);
	as_event_release_async_connection(cmd);

	// Stop timer.
	if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
		as_event_stop_timer(cmd);
	}

	as_event_error_callback(cmd, err);
}

void
as_event_socket_error(as_event_command* cmd, as_error* err)
{
	if (cmd->pipe_listener) {
		// Retry pipeline commands.
		as_pipe_socket_error(cmd, err, true);
		return;
	}

	// Connection should already have been closed before calling this function.
	// Stop timer.
	if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
		as_event_stop_timer(cmd);
	}

	as_event_error_callback(cmd, err);
}

void
as_event_response_error(as_event_command* cmd, as_error* err)
{
	if (cmd->pipe_listener != NULL) {
		as_pipe_response_error(cmd, err);
		return;
	}
	
	// Server sent back error.
	// Release resources, make callback and free command.
	if (cmd->flags & AS_ASYNC_FLAGS_HAS_TIMER) {
		as_event_stop_timer(cmd);
	}
	as_event_stop_watcher(cmd, cmd->conn);
	
	as_conn_pool* pool = &cmd->node->async_conn_pools[cmd->event_loop->index];

	// Close socket on errors that can leave unread data in socket.
	switch (err->code) {
		case AEROSPIKE_ERR_QUERY_ABORTED:
		case AEROSPIKE_ERR_SCAN_ABORTED:
		case AEROSPIKE_ERR_ASYNC_CONNECTION:
		case AEROSPIKE_ERR_TLS_ERROR:
		case AEROSPIKE_ERR_CLIENT_ABORT:
		case AEROSPIKE_ERR_CLIENT:
		case AEROSPIKE_NOT_AUTHENTICATED: {
			as_event_release_connection(cmd->conn, pool);
			break;
		}
			
		default:
			as_event_put_connection(cmd, pool);
			break;
	}
	as_event_error_callback(cmd, err);
}

bool
as_event_command_parse_header(as_event_command* cmd)
{
	as_msg* msg = (as_msg*)cmd->buf;
	
	if (msg->result_code == AEROSPIKE_OK) {
		as_event_response_complete(cmd);
		((as_async_write_command*)cmd)->listener(0, cmd->udata, cmd->event_loop);
		as_event_command_release(cmd);
	}
	else {
		as_error err;
		as_error_set_message(&err, msg->result_code, as_error_string(msg->result_code));
		as_event_response_error(cmd, &err);
	}
	return true;
}

bool
as_event_command_parse_result(as_event_command* cmd)
{
	as_error err;
	as_msg* msg = (as_msg*)cmd->buf;
	as_msg_swap_header_from_be(msg);
	uint8_t* p = cmd->buf + sizeof(as_msg);
	as_status status = msg->result_code;
	
	switch (status) {
		case AEROSPIKE_OK: {
			as_record rec;
			
			if (msg->n_ops < 1000) {
				as_record_inita(&rec, msg->n_ops);
			}
			else {
				as_record_init(&rec, msg->n_ops);
			}
			
			rec.gen = msg->generation;
			rec.ttl = cf_server_void_time_to_ttl(msg->record_ttl);
			
			p = as_command_ignore_fields(p, msg->n_fields);
			status = as_command_parse_bins(&p, &err, &rec, msg->n_ops, cmd->deserialize);

			if (status == AEROSPIKE_OK) {
				as_event_response_complete(cmd);
				((as_async_record_command*)cmd)->listener(0, &rec, cmd->udata, cmd->event_loop);
				as_event_command_release(cmd);
			}
			else {
				as_event_response_error(cmd, &err);
			}
			as_record_destroy(&rec);
			break;
		}
			
		case AEROSPIKE_ERR_UDF: {
			as_command_parse_udf_failure(p, &err, msg, status);
			as_event_response_error(cmd, &err);
			break;
		}
			
		default: {
			as_error_set_message(&err, status, as_error_string(status));
			as_event_response_error(cmd, &err);
			break;
		}
	}
	return true;
}

bool
as_event_command_parse_success_failure(as_event_command* cmd)
{
	as_msg* msg = (as_msg*)cmd->buf;
	as_msg_swap_header_from_be(msg);
	uint8_t* p = cmd->buf + sizeof(as_msg);
	as_status status = msg->result_code;
	
	switch (status) {
		case AEROSPIKE_OK: {
			as_error err;
			as_val* val = 0;
			status = as_command_parse_success_failure_bins(&p, &err, msg, &val);
			
			if (status == AEROSPIKE_OK) {
				as_event_response_complete(cmd);
				((as_async_value_command*)cmd)->listener(0, val, cmd->udata, cmd->event_loop);
				as_event_command_release(cmd);
				as_val_destroy(val);
			}
			else {
				as_event_response_error(cmd, &err);
			}
			break;
		}
			
		case AEROSPIKE_ERR_UDF: {
			as_error err;
			as_command_parse_udf_failure(p, &err, msg, status);
			as_event_response_error(cmd, &err);
			break;
		}
			
		default: {
			as_error err;
			as_error_set_message(&err, status, as_error_string(status));
			as_event_response_error(cmd, &err);
			break;
		}
	}
	return true;
}

void
as_event_command_free(as_event_command* cmd)
{
	cmd->cluster->pending[cmd->event_loop->index]--;

	if (cmd->node) {
		as_node_release(cmd->node);
	}

	if (cmd->flags & AS_ASYNC_FLAGS_FREE_BUF) {
		cf_free(cmd->buf);
	}
	cf_free(cmd);
}

/******************************************************************************
 * CLUSTER CLOSE FUNCTIONS
 *****************************************************************************/

typedef struct {
	as_monitor* monitor;
	as_cluster* cluster;
	as_event_loop* event_loop;
	uint32_t* event_loop_count;
} as_event_close_state;

static void
as_event_close_cluster_event_loop(as_event_close_state* state)
{
	state->cluster->pending[state->event_loop->index] = -1;

	bool destroy;
	ck_pr_dec_32_zero(state->event_loop_count, &destroy);

	if (destroy) {
		as_cluster_destroy(state->cluster);
		cf_free(state->event_loop_count);

		if (state->monitor) {
			as_monitor_notify(state->monitor);
		}
	}
	cf_free(state);
}

static void
as_event_close_cluster_cb(as_event_close_state* state)
{
	int pending = state->cluster->pending[state->event_loop->index];

	if (pending < 0) {
		// Cluster's event loop connections are already closed.
		return;
	}

	if (pending > 0) {
		// Cluster has pending commands.
		// Check again after all other commands run.
		if (as_event_execute(state->event_loop, (as_event_executable)as_event_close_cluster_cb, state)) {
			return;
		}
		as_log_error("Failed to queue cluster close command");
	}

	as_event_close_cluster_event_loop(state);
}

void
as_event_close_cluster(as_cluster* cluster)
{
	// Determine if current thread is an event loop thread.
	bool in_event_loop = false;

	for (uint32_t i = 0; i < as_event_loop_size; i++) {
		as_event_loop* event_loop = &as_event_loops[i];

		if (event_loop->thread == pthread_self()) {
			in_event_loop = true;
			break;
		}
	}

	as_monitor* monitor = NULL;

	if (! in_event_loop) {
		monitor = cf_malloc(sizeof(as_monitor));
		as_monitor_init(monitor);
	}

	uint32_t* event_loop_count = cf_malloc(sizeof(uint32_t));
	*event_loop_count = as_event_loop_size;

	// Send cluster close notification to async event loops.
	for (uint32_t i = 0; i < as_event_loop_size; i++) {
		as_event_loop* event_loop = &as_event_loops[i];

		as_event_close_state* state = cf_malloc(sizeof(as_event_close_state));
		state->monitor = monitor;
		state->cluster = cluster;
		state->event_loop = event_loop;
		state->event_loop_count = event_loop_count;

		if (! as_event_execute(event_loop, (as_event_executable)as_event_close_cluster_cb, state)) {
			as_log_error("Failed to queue cluster close command");
			as_event_close_cluster_event_loop(state);
		}
	}

	// Deadlock would occur if we wait from an event loop thread.
	// Only wait when not in event loop thread.
	if (monitor) {
		as_monitor_wait(monitor);
		as_monitor_destroy(monitor);
		cf_free(monitor);
	}
}
