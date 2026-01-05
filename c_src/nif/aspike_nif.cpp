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

aerospike* get_aerospike () { return &as; }
bool get_is_aerospike_initialised () { return is_aerospike_initialised; }
bool get_is_connected () { return is_connected; }
ERL_NIF_TERM get_erl_error () { return erl_error; }
ERL_NIF_TERM get_erl_ok () { return erl_ok; }

// ----------------------------------------------------------------------------

static int load(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    as_config_init(&config);

    // now, set the values we'd like to have
    // TODO: find out the best value of this variable.
    config.async_max_conns_per_node = 300;

    aerospike_init(&as, &config);
    erl_error = enif_make_atom(env, "error");
    erl_ok = enif_make_atom(env, "ok");
    is_aerospike_initialised = true;
    return 0;
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

#define NIF_DIRTY_FUN(A, B, C) {A, B, C, ERL_DIRTY_JOB_IO_BOUND}

static ErlNifFunc nif_funcs[] = {
    {"as_init", 0, aspike_nif_as_init_sync},
    {"nif_host_add", 2, aspike_nif_host_add_sync},
    {"host_clear", 0, aspike_nif_host_clear_sync},
    {"nif_host_list", 0, aspike_nif_host_list_sync},
    NIF_DIRTY_FUN("connect", 2, aspike_nif_connect_sync),

    NIF_DIRTY_FUN("cdt_put_sync", 6, aspike_nif_cdt_put_sync),
    {"cdt_put_async", 6, aspike_nif_cdt_put_async},
    NIF_DIRTY_FUN("cdt_get", 4, aspike_nif_cdt_get_sync),
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
    NIF_DIRTY_FUN("nif_node_random", 0, aspike_nif_node_random_sync),
    NIF_DIRTY_FUN("nif_node_names", 0, aspike_nif_node_names_sync),
    NIF_DIRTY_FUN("nif_node_get", 1, aspike_nif_node_get_sync),
    NIF_DIRTY_FUN("nif_node_info", 2, aspike_nif_node_info_sync),
    NIF_DIRTY_FUN("nif_help", 1, aspike_nif_help_sync),
    NIF_DIRTY_FUN("nif_host_info", 3, aspike_nif_host_info_sync),
    NIF_DIRTY_FUN("a_key_put", 6, aspike_nif_a_key_put_sync)
};

ERL_NIF_INIT(aspike_nif, nif_funcs, load, NULL, NULL, NULL)
