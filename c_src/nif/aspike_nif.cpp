#include <erl_nif.h>

#include <time.h>
#include <string>
#include <utility>
#include <iostream>
#include <vector>
#include <chrono>
#include <functional>
#include <assert.h>

#include <aerospike/aerospike.h>
#include <aerospike/aerospike_info.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_error.h>
#include <aerospike/as_record.h>
#include <aerospike/as_record_iterator.h>
#include <aerospike/as_status.h>
#include <aerospike/as_node.h>
#include <aerospike/as_cluster.h>
#include <aerospike/as_lookup.h>
#include <aerospike/as_bin.h>
#include <aerospike/as_val.h>
#include <aerospike/as_arraylist.h>
#include <aerospike/as_cdt_order.h>
#include <aerospike/as_operations.h>
#include <aerospike/as_map_operations.h>
#include <aerospike/as_orderedmap.h>
#include <aerospike/as_pair.h>
#include <aerospike/as_exp.h>
#include <aerospike/as_batch.h>
#include <aerospike/aerospike_batch.h>
#include <aerospike/as_arraylist.h>
#include <aerospike/as_monitor.h>

#include "aspike_nif.h"
#include "sync_methods.h"
#include "async_methods.h"

// #define CLOCK_REALTIME 0 
// #define CLOCK_MONOTONIC 6 
// #define CLOCK_PROCESS_CPUTIME_ID  12 
// #define CLOCK_THREAD_CPUTIME_ID  16 

static aerospike as;
static as_config config;
static as_monitor app_complete_monitor;
static bool is_aerospike_initialised = false;
static bool is_connected = false;
static ERL_NIF_TERM erl_error;
static ERL_NIF_TERM erl_ok;
static uint32_t CONNECTIONS_PER_NODE = 100;
static uint32_t EVENT_LOOPS_AMOUNT = 1;

aerospike* get_aerospike () { return &as; }
bool get_is_aerospike_initialised () { return is_aerospike_initialised; }
bool get_is_connected () { return is_connected; }
ERL_NIF_TERM get_erl_error () { return erl_error; }
ERL_NIF_TERM get_erl_ok () { return erl_ok; }

// ----------------------------------------------------------------------------

static int load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    as_config_init(&config);

    config.async_max_conns_per_node = CONNECTIONS_PER_NODE;

    aerospike_init(&as, &config);
    erl_error = enif_make_atom(env, "error");
    erl_ok = enif_make_atom(env, "ok");
    is_aerospike_initialised = true;
    return 0;
}

static ERL_NIF_TERM aspike_nif_as_init(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    if (!as_event_create_loops(EVENT_LOOPS_AMOUNT)) {
        ERL_NIF_TERM msg = enif_make_string(env, "Failed to create event loop", ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_error, msg);
    }

    ERL_NIF_TERM msg = enif_make_string(env, "initialised", ERL_NIF_UTF8);
    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM aspike_nif_host_add(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char host[MAX_HOST_SIZE];
    int port;
    if (!enif_get_string(env, argv[0], host, MAX_HOST_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_int(env, argv[1], &port)) {
	    return enif_make_badarg(env);
    }
    CHECK_INIT

    ERL_NIF_TERM rc, msg;

    if (! as_config_add_hosts(&(as.config), host, port)) {
        rc = erl_error;
        msg = enif_make_string(env, "failed to add host and port", ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "host and port added", ERL_NIF_UTF8);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_host_clear(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    CHECK_INIT

    as_config_clear_hosts(&(as.config));
    ERL_NIF_TERM rc = erl_ok;
    ERL_NIF_TERM msg = enif_make_string(env, "hosts list was cleared", ERL_NIF_UTF8);
    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_host_list(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    CHECK_INIT

    as_config  *config = &(as.config);
    as_vector  *hosts = config->hosts;
    uint32_t size = (hosts == NULL) ? 0 : hosts->size;

    ERL_NIF_TERM msg = enif_make_list(env, 0);

    for (uint32_t i = 0; i < size; i++) {
        as_host* host = (as_host*)as_vector_get(hosts, i);
		ERL_NIF_TERM cell = enif_make_tuple3(env,
            enif_make_string(env, host->name, ERL_NIF_UTF8),
            enif_make_string(env,  host->tls_name == NULL ? "" : host->tls_name, ERL_NIF_UTF8),
            enif_make_uint(env, host->port)
            );
        msg = enif_make_list_cell(env, cell, msg);
	}

    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM aspike_nif_connect_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char user[AS_USER_SIZE];
    char password[AS_PASSWORD_SIZE];

    if (!enif_get_string(env, argv[0], user, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], password, AS_PASSWORD_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    
    CHECK_AEROSPIKE_INIT

    ERL_NIF_TERM msg;
    as_config_set_user(&(as.config), user, password);
	as_error err;

    if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
        is_connected = false;
        as_event_close_loops();
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_error, msg);
    }

    as_monitor_init(&app_complete_monitor);

    is_connected = true;

    msg = enif_make_string(env, "connected", ERL_NIF_UTF8);
    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM aspike_nif_node_random(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    CHECK_ALL

    ERL_NIF_TERM rc, msg;
    as_node* node = as_node_get_random(as.cluster);
    if (! node) {
        rc = erl_error;
        msg = enif_make_string(env, "Failed to find server node.", ERL_NIF_UTF8);
	} else {
        rc = erl_ok;
        msg = enif_make_string(env, as_node_get_address_string(node), ERL_NIF_UTF8);
        as_node_release(node);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_node_names(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    CHECK_ALL

    ERL_NIF_TERM rc;
	as_nodes* nodes = as_nodes_reserve(as.cluster);
    uint32_t n_nodes = (nodes == NULL) ? 0 : nodes->size;

    ERL_NIF_TERM lst = enif_make_list(env, 0);

    for(uint32_t i = 0; i < n_nodes; i++){
        as_node* node = nodes->array[i];
        ERL_NIF_TERM cell = enif_make_tuple2(
            env,
            enif_make_string(env, node->name, ERL_NIF_UTF8),
            enif_make_string(env, as_node_get_address_string(node), ERL_NIF_UTF8)
            );
        lst = enif_make_list_cell(env, cell, lst);
    }

    rc = erl_ok;
    as_nodes_release(nodes);

    return enif_make_tuple2(env, rc, lst);
}

static ERL_NIF_TERM aspike_nif_node_get(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char node_name[AS_NODE_NAME_MAX_SIZE];
    if (!enif_get_string(env, argv[0], node_name, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL
    ERL_NIF_TERM rc, msg;

    as_node* node = as_node_get_by_name(as.cluster, node_name);
    if (! node) {
        rc = erl_error;
        msg = enif_make_string(env, "Failed to find server node.", ERL_NIF_UTF8);
	} else {
        rc = erl_ok;
        msg = enif_make_string(env, as_node_get_address_string(node), ERL_NIF_UTF8);
        as_node_release(node);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_node_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char node_name[AS_NODE_NAME_MAX_SIZE];
    char item[1024];
    if (!enif_get_string(env, argv[0], node_name, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], item, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL
    ERL_NIF_TERM rc, msg;

	as_cluster* cluster = as.cluster;
    as_node* node = as_node_get_by_name(cluster, node_name);
    if (! node) {
        rc = erl_error;
        msg = enif_make_string(env, "Failed to find server node.", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    char * info = NULL;
    as_error err;
    const as_policy_info* policy = &(as.config.policies.info);
	uint64_t deadline = as_socket_deadline(policy->timeout);

    as_status status = as_info_command_node(&err, node, (char*)item, policy->send_as_is, deadline, &info);
    if (status != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.in_doubt == true ? "unknown error" : err.message, ERL_NIF_UTF8);
    } else if (info == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "no data", ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, &info[0], ERL_NIF_UTF8);
        cf_free(info);
    }
    as_node_release(node);

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_help(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char item[1024];
    if (!enif_get_string(env, argv[0], item, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL
    ERL_NIF_TERM rc, msg;
    char * info = NULL;
    as_error err;

    if (aerospike_info_any(&as, &err, NULL, item, &info) != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.in_doubt == true ? "unknown error" : err.message, ERL_NIF_UTF8);
    } else if (info == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "no data", ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, info, ERL_NIF_UTF8);
        cf_free(info);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_host_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char hostname[AS_NODE_NAME_MAX_SIZE];
    long port;
    char item[1024];
    if (!enif_get_string(env, argv[0], hostname, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_long(env, argv[1], &port)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], item, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL
    ERL_NIF_TERM rc, msg;
    as_error err;
    as_address_iterator iter;

	as_status status = as_lookup_host(&iter, &err, hostname, port);
	if (status != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.in_doubt == true ? "unknown error" : err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
	}
    char * info = NULL;
    as_cluster* cluster = as.cluster;
    const as_policy_info* policy = &(as.config.policies.info);
	uint64_t deadline = as_socket_deadline(policy->timeout);
	struct sockaddr* addr;

	bool loop = true;
	while (loop && as_lookup_next(&iter, &addr)) {
		status = as_info_command_host(cluster, &err, addr, (char*)item, policy->send_as_is, deadline, &info, hostname);

		switch (status) {
			case AEROSPIKE_OK:
			case AEROSPIKE_ERR_TIMEOUT:
			case AEROSPIKE_ERR_INDEX_FOUND:
			case AEROSPIKE_ERR_INDEX_NOT_FOUND:
				loop = false;
				break;

			default:
				break;
		}
	}
	as_lookup_end(&iter);

    if (info == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "no data", ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, &info[0], ERL_NIF_UTF8);
        cf_free(info);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_get_connection_stats(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    if (!as.cluster || !as.cluster->nodes) {
        return enif_make_tuple2(env, erl_error, enif_make_string(env, "no_cluster", ERL_NIF_UTF8));
    }

    ERL_NIF_TERM result_list = enif_make_list(env, 0);

    for (uint32_t i = 0; i < as.cluster->nodes->size; i++) {
        as_node* node = as.cluster->nodes->array[i];

        for (uint32_t loop_idx = 0; loop_idx < EVENT_LOOPS_AMOUNT; loop_idx++) {
            as_async_conn_pool* pool = &node->async_conn_pools[loop_idx];

            uint32_t total = pool->queue.total;
            uint32_t available = as_queue_size(&pool->queue);
            uint32_t used = total - available;

            // Create tuple: {node_index, loop_index, total, used, available, limit}
            ERL_NIF_TERM pool_info = enif_make_tuple6(env,
                enif_make_int(env, i),
                enif_make_int(env, loop_idx),
                enif_make_int(env, total),
                enif_make_int(env, used),
                enif_make_int(env, available),
                enif_make_int(env, pool->limit)
            );

            result_list = enif_make_list_cell(env, pool_info, result_list);
        }
    }

    return enif_make_tuple2(env, erl_ok, result_list);
}

static ERL_NIF_TERM aspike_nif_get_lowest_available_connection(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    if (!as.cluster || !as.cluster->nodes) {
        return enif_make_tuple2(env, erl_error, enif_make_string(env, "no_cluster", ERL_NIF_UTF8));
    }

    int min_available = CONNECTIONS_PER_NODE;

    for (uint32_t i = 0; i < as.cluster->nodes->size; i++) {
        as_node* node = as.cluster->nodes->array[i];

        for (uint32_t loop_idx = 0; loop_idx < EVENT_LOOPS_AMOUNT; loop_idx++) {
            as_async_conn_pool* pool = &node->async_conn_pools[loop_idx];
            // to understand next formula you hav to understand units of connections to node:
            // |------------------*------------------*------------------|
            // 0                 used               total              limit
            // so the limit is the total amount of connections, basically defined as CONNECTIONS_PER_NODE.
            // The total is the current total connections created (established) to that node, and
            // the used is amount of connections being used.
            // Of course, usually available would equal to "total - used", but in aerospike C-client
            // there is another way to calculate available amount: you just call as_queue_size().
            // Now, that will give you the available from total connections. Now you have to add the difference
            // between limit and total, and this is what we do below:
            uint32_t total = pool->queue.total;
            uint32_t available_from_total = as_queue_size(&pool->queue);
            uint32_t available = (pool->limit - total) + available_from_total;
            min_available = (int)available < min_available ? (int)available : min_available;
        }
    }

    auto erl_min_available = enif_make_int(env, min_available);
    return enif_make_tuple2(env, erl_ok, erl_min_available);
}

#define NIF_DIRTY_FUN(A, B, C) {A, B, C, ERL_DIRTY_JOB_IO_BOUND}
static ErlNifFunc nif_funcs[] = {

    {"as_init", 0, aspike_nif_as_init},
    {"nif_host_add", 2, aspike_nif_host_add},
    {"host_clear", 0, aspike_nif_host_clear},
    {"nif_host_list", 0, aspike_nif_host_list},
    NIF_DIRTY_FUN("connect", 2, aspike_nif_connect_sync),
    NIF_DIRTY_FUN("nif_node_random", 0, aspike_nif_node_random),
    NIF_DIRTY_FUN("nif_node_names", 0, aspike_nif_node_names),
    NIF_DIRTY_FUN("nif_node_get", 1, aspike_nif_node_get),
    NIF_DIRTY_FUN("nif_node_info", 2, aspike_nif_node_info),
    NIF_DIRTY_FUN("nif_help", 1, aspike_nif_help),
    NIF_DIRTY_FUN("nif_host_info", 3, aspike_nif_host_info),
    {"get_connection_stats", 0, aspike_nif_get_connection_stats},
    {"get_lowest_available_connection", 0, aspike_nif_get_lowest_available_connection},

    NIF_DIRTY_FUN("cdt_put_sync", 6, aspike_nif_cdt_put_sync),
    {"cdt_put_async", 6, aspike_nif_cdt_put_async},
    NIF_DIRTY_FUN("cdt_get_sync", 4, aspike_nif_cdt_get_sync),
    {"cdt_get_async", 4, aspike_nif_cdt_get_async},
    NIF_DIRTY_FUN("cdt_delete_by_keys", 5, aspike_nif_cdt_delete_by_keys_sync),
    NIF_DIRTY_FUN("cdt_delete_by_keys_batch", 4, aspike_nif_cdt_delete_by_keys_batch_sync),
    NIF_DIRTY_FUN("segment_tag_get", 4, aspike_nif_segment_tag_get_sync),
    NIF_DIRTY_FUN("key_select", 4, aspike_nif_key_select_sync),
    NIF_DIRTY_FUN("binary_get", 3, aspike_nif_binary_get_sync),
    NIF_DIRTY_FUN("key_get", 3, aspike_nif_key_get_sync),
    NIF_DIRTY_FUN("key_exists", 3, aspike_nif_key_exists_sync),
    NIF_DIRTY_FUN("key_inc", 4, aspike_nif_key_inc_sync),
    NIF_DIRTY_FUN("key_generation", 3, aspike_nif_key_generation_sync),
    NIF_DIRTY_FUN("key_put", 4, aspike_nif_key_put_sync),
    NIF_DIRTY_FUN("binary_put", 5, aspike_nif_binary_put_sync),
    NIF_DIRTY_FUN("binary_remove", 5, aspike_nif_binary_remove_sync),
    NIF_DIRTY_FUN("cdt_expire", 4, aspike_nif_cdt_expire_sync),
    NIF_DIRTY_FUN("key_remove", 3, aspike_nif_key_remove_sync),
    NIF_DIRTY_FUN("a_key_put", 6, aspike_nif_a_key_put_sync)
};

ERL_NIF_INIT(aspike_nif, nif_funcs, load, NULL, NULL, NULL)
