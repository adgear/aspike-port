#include <erl_nif.h>

#include <time.h>
#include <math.h>
#include <string>
#include <utility>
#include <iostream>
#include <vector>
#include <chrono>
#include <functional>
#include <assert.h>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <shared_mutex>

#include <aerospike/aerospike.h>
#include <aerospike/aerospike_info.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_error.h>
#include <aerospike/as_record.h>
#include <aerospike/as_record_iterator.h>
#include <aerospike/as_status.h>
#include <aerospike/as_node.h>
#include <aerospike/as_cluster.h>
#include <aerospike/as_partition.h>
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
#include "common_methods.h"

using namespace std;

static aerospike as;
static as_monitor app_complete_monitor;
static bool is_connected = false;
static ERL_NIF_TERM erl_error;
static ERL_NIF_TERM erl_ok;

static uint32_t event_loops_amount = 1;

atomic<uint32_t> sync_current_counter(0);
atomic<uint32_t> sync_peak_counter(0);
atomic<int64_t> sync_peak_ttl_counter(0);
atomic<uint32_t> async_current_counter(0);
atomic<uint32_t> async_peak_counter(0);
atomic<int64_t> async_peak_ttl_counter(0);

// Thread-safe storage using node name as key
static unordered_map<string, shared_ptr<NodeConnectionStats>> node_stats_map;
static shared_mutex node_stats_rw_mutex; // Reader-writer mutex for thread-safe map access

aerospike* get_aerospike () { return &as; }
bool get_is_connected () { return is_connected; }
bool statistics_enabled = false;
bool get_statistics_enabled () { return statistics_enabled; };

// Get target node for a key using proper Aerospike partition routing
string* get_target_node_for_key(const char* namespace_name, const char* set, const char* key_str) {
    if (!statistics_enabled || !namespace_name || !key_str || !is_connected || !as.cluster) {
        return nullptr;
    }

    // Use the cluster nodes API which handles reference counting internally
    as_nodes* nodes = as_nodes_reserve(as.cluster);
    if (!nodes) {
        return nullptr;
    }

    // Create a key and compute its digest
    as_key key;
    as_key_init_str(&key, namespace_name, set, key_str);

    as_error err;
    if (as_key_set_digest(&err, &key) != AEROSPIKE_OK) {
        as_key_destroy(&key);
        as_nodes_release(nodes);
        return nullptr;
    }

    // Get the digest and compute partition ID
    as_digest* digest = as_key_digest(&key);
    if (!digest) {
        as_key_destroy(&key);
        as_nodes_release(nodes);
        return nullptr;
    }

    // Get partition table for this namespace
    as_partition_tables* pt = &as.cluster->partition_tables;
    as_partition_table* table = nullptr;

    // Find the partition table for our namespace
    for (uint32_t i = 0; i < pt->size; i++) {
        if (pt->tables[i] && strcmp(pt->tables[i]->ns, namespace_name) == 0) {
            table = pt->tables[i];
            break;
        }
    }

    if (!table) {
        as_key_destroy(&key);
        as_nodes_release(nodes);
        return nullptr;
    }

    // Calculate partition ID
    uint32_t partition_id = as_partition_getid(digest->value, table->size);

    // Get partition and find master node
    as_partition* partition = &table->partitions[partition_id];

    // Get the master node (replica index 0) for writes, or any available node for reads
    uint8_t replica_index = 0;
    as_node* target_node = as_partition_get_node(as.cluster, namespace_name, partition,
                                                 nullptr, AS_POLICY_REPLICA_MASTER,
                                                 1, &replica_index);

    string* node_name = nullptr;
    if (target_node && strlen(target_node->name) > 0) {
        node_name = new string((char *)target_node->name);
    }

    as_key_destroy(&key);
    as_nodes_release(nodes);

    return node_name;
}

// Get or create stats for a node (thread-safe with reader-writer lock)
shared_ptr<NodeConnectionStats> get_or_create_node_stats(string* node_name) {
    // Fast path: shared lock for reading (multiple threads can access simultaneously)
    {
        shared_lock<shared_mutex> read_lock(node_stats_rw_mutex);
        auto it = node_stats_map.find(*node_name);
        if (it != node_stats_map.end()) {
            return it->second;  // Found it! Multiple readers can do this concurrently
        }
    } // Shared lock released here

    // Slow path: exclusive lock for writing (only one thread can do this)
    lock_guard<shared_mutex> write_lock(node_stats_rw_mutex);

    // Double-check: another thread might have created it while we waited for the lock
    auto it2 = node_stats_map.find(*node_name);
    if (it2 != node_stats_map.end()) {
        return it2->second;
    }

    // Create new stats entry
    auto stats = make_shared<NodeConnectionStats>();
    node_stats_map[*node_name] = stats;
    return stats;
}

// ----------------------------------------------------------------------------

static int load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    aerospike_init(&as, NULL);

    erl_error = enif_make_atom(env, "error");
    erl_ok = enif_make_atom(env, "ok");

    return 0;
}

static ERL_NIF_TERM aspike_nif_set_connections_per_node(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    int sync_min_amount;
    int sync_max_amount;
    int async_min_amount;
    int async_max_amount;
    if (!enif_get_int(env, argv[0], &sync_min_amount)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_int(env, argv[1], &sync_max_amount)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_int(env, argv[2], &async_min_amount)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_int(env, argv[3], &async_max_amount)) {
	    return enif_make_badarg(env);
    }

    as.config.min_conns_per_node = sync_min_amount;
    as.config.max_conns_per_node = sync_max_amount;
    as.config.async_min_conns_per_node = async_min_amount;
    as.config.async_max_conns_per_node = async_max_amount;

    ERL_NIF_TERM msg = enif_make_string(env, "set", ERL_NIF_UTF8);
    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM aspike_nif_set_event_loops_amount(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    int ev_amount;
    if (!enif_get_int(env, argv[0], &ev_amount)) {
	    return enif_make_badarg(env);
    }

    event_loops_amount = ev_amount;

    ERL_NIF_TERM msg = enif_make_string(env, "set", ERL_NIF_UTF8);
    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM aspike_nif_as_init(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    if (!as_event_create_loops(event_loops_amount)) {
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
    as_config_clear_hosts(&(as.config));
    ERL_NIF_TERM rc = erl_ok;
    ERL_NIF_TERM msg = enif_make_string(env, "hosts list was cleared", ERL_NIF_UTF8);
    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM aspike_nif_host_list(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    as_config *config = &(as.config);
    as_vector *hosts = config->hosts;
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

static ERL_NIF_TERM aspike_nif_get_connections_stats(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    
    if (!statistics_enabled) {
        ERL_NIF_TERM global_stats = enif_make_tuple6(env,
            enif_make_uint(env, 0),
            enif_make_uint(env, 0),
            enif_make_uint(env, 0),
            enif_make_uint(env, 0),
            enif_make_uint(env, 0),
            enif_make_uint(env, 0)
        );
        ERL_NIF_TERM worse_host_stats = enif_make_tuple2(env,
            enif_make_uint(env, 0),
            enif_make_uint(env, 0)
        );
        ERL_NIF_TERM combined_stats = enif_make_tuple2(env, global_stats, worse_host_stats);
        return enif_make_tuple2(env, erl_ok, combined_stats);
    }

    int64_t outdated_ts = unix_ts() - 2 * 15;

    // Get global statistics
    uint32_t sync_current = sync_current_counter.load();
    uint32_t sync_peak = sync_peak_counter.load();
    uint32_t total_sync_connections = 0;
    uint32_t async_current = async_current_counter.load();
    uint32_t async_peak = async_peak_counter.load();
    uint32_t total_async_connections = 0;
    if (as.cluster && as.cluster->nodes) {
        total_sync_connections = as.cluster->nodes->size * as.config.max_conns_per_node;
        total_async_connections = as.cluster->nodes->size * as.config.async_max_conns_per_node;
    }

    // Create global stats tuple
    ERL_NIF_TERM global_stats = enif_make_tuple6(env,
        enif_make_uint(env, sync_current),
        enif_make_uint(env, sync_peak),
        enif_make_uint(env, total_sync_connections),
        enif_make_uint(env, async_current),
        enif_make_uint(env, async_peak),
        enif_make_uint(env, total_async_connections)
    );

    uint32_t host_async_current_max = 0;
    uint32_t host_async_peak_max = 0;

    // Use shared lock for safe read-only iteration (allows concurrent readers)
    {
        shared_lock<shared_mutex> read_lock(node_stats_rw_mutex);
        for (const auto& pair : node_stats_map) {
            const auto& stats = pair.second;

            if (!stats) {
                continue; // Skip invalid entries
            }

            uint32_t node_async_peak = stats->async_peak.load();

            if (host_async_peak_max < node_async_peak) {
                host_async_current_max = stats->async_current.load();
                host_async_peak_max = node_async_peak;
            }

            // Check if the peak values are outdated for this node and reset them if needed
            if (node_async_peak > 0 && stats->async_peak_ttl.load() < outdated_ts) {
                stats->async_current.store(0);
                stats->async_peak.store(0);
            }
        }
    } // Shared lock released here

    ERL_NIF_TERM worse_host_stats = enif_make_tuple2(env,
        enif_make_uint(env, host_async_current_max),
        enif_make_uint(env, host_async_peak_max)
    );

    // Combined stats: {global_stats, worse_host_stats}
    ERL_NIF_TERM combined_stats = enif_make_tuple2(env, global_stats, worse_host_stats);

    // Check if the peak values are outdated and reset them if needed
    if (sync_peak > 0 && sync_peak_ttl_counter.load() < outdated_ts) {
        sync_current_counter.store(0);
        sync_peak_counter.store(0);
    }
    if (async_peak > 0 && async_peak_ttl_counter.load() < outdated_ts) {
        async_current_counter.store(0);
        async_peak_counter.store(0);
    }

    return enif_make_tuple2(env, erl_ok, combined_stats);
}

#define NIF_DIRTY_FUN(A, B, C) {A, B, C, ERL_DIRTY_JOB_IO_BOUND}
static ErlNifFunc nif_funcs[] = {

    {"set_connections_per_node", 4, aspike_nif_set_connections_per_node},
    {"set_event_loops_amount", 1, aspike_nif_set_event_loops_amount},
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
    {"get_connections_stats", 0, aspike_nif_get_connections_stats},

    NIF_DIRTY_FUN("cdt_put_sync", 6, aspike_nif_cdt_put_sync),
    {"cdt_put_async", 7, aspike_nif_cdt_put_async},
    NIF_DIRTY_FUN("cdt_get_sync", 4, aspike_nif_cdt_get_sync),
    {"cdt_get_async", 5, aspike_nif_cdt_get_async},
    NIF_DIRTY_FUN("cdt_delete_by_keys_sync", 5, aspike_nif_cdt_delete_by_keys_sync),
    {"cdt_delete_by_keys_async", 6, aspike_nif_cdt_delete_by_keys_async},
    NIF_DIRTY_FUN("cdt_delete_by_keys_batch_sync", 4, aspike_nif_cdt_delete_by_keys_batch_sync),
    {"cdt_delete_by_keys_batch_async", 5, aspike_nif_cdt_delete_by_keys_batch_async},
    NIF_DIRTY_FUN("cdt_get_bin_sync", 4, aspike_nif_cdt_get_bin_sync),
    {"cdt_get_bin_async", 5, aspike_nif_cdt_get_bin_async},
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
