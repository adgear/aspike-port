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

// ----------------------------------------------------------------------------

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
    // TODO: find out the best value of this variable.
    config.async_max_conns_per_node = 200;
    as_config_init(&config);
    aerospike_init(&as, &config);
    erl_error = enif_make_atom(env, "error");
    erl_ok = enif_make_atom(env, "ok");
    is_aerospike_initialised = true;
    return 0;
}

static ERL_NIF_TERM as_init(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    if (!as_event_create_loops(1)) {
        ERL_NIF_TERM msg = enif_make_string(env, "Failed to create event loop", ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_error, msg);
    }
    
    ERL_NIF_TERM msg = enif_make_string(env, "initialised", ERL_NIF_UTF8);
    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM host_add(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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

    if (! as_config_add_hosts(&as.config, host, port)) {
        rc = erl_error;
        msg = enif_make_string(env, "failed to add host and port", ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "host and port added", ERL_NIF_UTF8);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM host_clear(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    CHECK_INIT
    as_config_clear_hosts(&as.config);
    ERL_NIF_TERM rc = erl_ok;
    ERL_NIF_TERM msg = enif_make_string(env, "hosts list was cleared", ERL_NIF_UTF8);
    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM host_list(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    CHECK_INIT
    as_config  *config = &as.config;   
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

static ERL_NIF_TERM connect(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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
    as_config_set_user(&as.config, user, password);
	as_error err;

    if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
        is_connected = false;
        as_event_close_loops();
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_error, msg);
    }

    // Initialize monitor.
    as_monitor_init(&app_complete_monitor);

    is_connected = true;

    msg = enif_make_string(env, "connected", ERL_NIF_UTF8);
    return enif_make_tuple2(env, erl_ok, msg);
}

static ERL_NIF_TERM binary_remove(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set, bin_key;
    unsigned int length;
    std::string name_space, aspk_set, aspk_key;
    long ttl;

    if (!enif_inspect_binary(env, argv[0], &bin_ns)) {
	    return enif_make_badarg(env);
    }
    name_space.assign((const char*) bin_ns.data, bin_ns.size);

    if (!enif_inspect_binary(env, argv[1], &bin_set)) {
	    return enif_make_badarg(env);
    }
    aspk_set.assign((const char*) bin_set.data, bin_set.size);

    if (!enif_inspect_binary(env, argv[2], &bin_key)) {
	    return enif_make_badarg(env);
    }
    aspk_key.assign((const char*) bin_key.data, bin_key.size);

    ERL_NIF_TERM list = argv[3];
    if (!enif_is_list(env, list) || !enif_get_list_length(env, list, &length)) {
	    return enif_make_badarg(env);
    }

    if (!enif_get_long(env, argv[4], &ttl)) {
        return enif_make_badarg(env);
    }

    ERL_NIF_TERM rc, msg;
    if (length == 0) {
        rc = erl_ok;
        msg = enif_make_string(env, "key_put", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    CHECK_ALL

	as_error err;
    as_key key;
	as_record rec;

	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
	as_record_inita(&rec, length);
    rec.ttl = ttl;
    long ret_val = 0;
   
    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        ErlNifBinary bin_bin;
        std::string bin_str;

        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if (!enif_inspect_binary(env, head, &bin_bin)) {
            return enif_make_badarg(env);
        }
        bin_str.assign((const char*) bin_bin.data, bin_bin.size);

	    if(!as_record_set_nil(&rec, bin_str.c_str())){
		    ret_val = 1;
        }
    }

    if(!ret_val){
	// destroy heap record
    }
	
    if (aerospike_key_put(&as, &err, NULL, &key, &rec)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "key_put", ERL_NIF_UTF8);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM binary_put(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set, bin_key;
    unsigned int length;
    std::string name_space, aspk_set, aspk_key;
    long ttl;

    if (!enif_inspect_binary(env, argv[0], &bin_ns)) {
	    return enif_make_badarg(env);
    }
    name_space.assign((const char*) bin_ns.data, bin_ns.size);

    if (!enif_inspect_binary(env, argv[1], &bin_set)) {
	    return enif_make_badarg(env);
    }
    aspk_set.assign((const char*) bin_set.data, bin_set.size);

    if (!enif_inspect_binary(env, argv[2], &bin_key)) {
	    return enif_make_badarg(env);
    }
    aspk_key.assign((const char*) bin_key.data, bin_key.size);

    ERL_NIF_TERM list = argv[3];
    if (!enif_is_list(env, list) || !enif_get_list_length(env, list, &length)) {
	    return enif_make_badarg(env);
    }

    if (!enif_get_long(env, argv[4], &ttl)) {
        return enif_make_badarg(env);
    }

    ERL_NIF_TERM rc, msg;
    if (length == 0) {
        rc = erl_ok;
        msg = enif_make_string(env, "key_put", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    CHECK_ALL

	as_error err;
    as_key key;
	as_record rec;

	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
	as_record_inita(&rec, length);
    rec.ttl = ttl;
    long ret_val = 0;
   
    std::vector<as_bytes*> bin_vec; 
    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        ErlNifBinary bin_bin, bin_val;
        std::string bin_str, bin_str_val;
        int t_length;
        const ERL_NIF_TERM* tuple = NULL;
        //as_bytes as_bytes_val;
        unsigned int ts_length;

        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if(!enif_get_tuple(env, head, &t_length, &tuple) || t_length != 2){
            return enif_make_badarg(env);
        }

        if (!enif_inspect_binary(env, tuple[0], &bin_bin)) {
            return enif_make_badarg(env);
        }
        bin_str.assign((const char*) bin_bin.data, bin_bin.size);

        if (!enif_inspect_binary(env, tuple[1], &bin_val)) {
            if(enif_is_number(env, tuple[1])){
                long i64;
                if(enif_get_int64(env, tuple[1], &i64)){
                    if(!as_record_set_int64(&rec, bin_str.c_str(), i64)){
                        ret_val = 1;
                    }
                }
            } else if(enif_is_atom(env, tuple[1])) { // every atom is considering as undefined to delete this binary
                if(!as_record_set_nil(&rec, bin_str.c_str())){
                    ret_val = 1;
                }
            } else {
                if (!enif_is_list(env, tuple[1]) || !enif_get_list_length(env, tuple[1], &ts_length)) {
                    return enif_make_badarg(env);
                }
                // expecting list of integers
                auto ts_list = tuple[1];
                //as_list* as_list_ofints = (as_list *)as_arraylist_new((uint32_t)ts_length, 0);
                as_arraylist* as_list_ofints = as_arraylist_new((uint32_t)ts_length, 0);
                for (uint ts_i = 0; ts_i < ts_length; ts_i++) {
                    ERL_NIF_TERM ts_head;
                    ERL_NIF_TERM ts_tail;
                    long i64;
                    if (!enif_get_list_cell(env, ts_list, &ts_head, &ts_tail)) {
                        break;
                    }
                    if(enif_get_int64(env, ts_head, &i64)){
                        as_arraylist_append_int64(as_list_ofints, i64);
                    }
                    ts_list = ts_tail;
                }
                ((as_val *)as_list_ofints)->type = AS_LIST;
                if(!as_record_set_list(&rec, bin_str.c_str(), (as_list*)as_list_ofints)){
                    as_list_destroy((as_list*)as_list_ofints);
                    ret_val = 1;
                };
            }
        }else{
	    bin_vec.push_back(as_bytes_new(bin_val.size));
	    as_bytes * bytes_v = bin_vec.back();
	    as_bytes_set(bytes_v, 0, (const uint8_t *)bin_val.data, bin_val.size);
	    if(!as_record_set_bytes(&rec, bin_str.c_str(), bytes_v)){
		as_bytes_destroy(bytes_v);
		ret_val = 1;
	    }
        }
        list = tail;
    }
    if(!ret_val){
	// destroy heap record
    }
	
    if (aerospike_key_put(&as, &err, NULL, &key, &rec)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "key_put", ERL_NIF_UTF8);
    }
    for(unsigned int i = 0; i < bin_vec.size(); i++){
	as_bytes_destroy(bin_vec[i]);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM key_put(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];
    unsigned int length;

    if (!enif_get_string(env, argv[0], name_space, MAX_NAMESPACE_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], set, MAX_SET_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], key_str, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    ERL_NIF_TERM list = argv[3];
    if (!enif_is_list(env, list) || !enif_get_list_length(env, list, &length)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL
                                        // enif_get_list_length(env, *val, &len);
    ERL_NIF_TERM rc, msg;
    if (length == 0) {
        rc = erl_ok;
        msg = enif_make_string(env, "key_put", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

	as_error err;
    as_key key;
	as_record rec;

	as_key_init_str(&key, name_space, set, key_str);
	as_record_inita(&rec, length);

    
    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        char bin[AS_BIN_NAME_MAX_SIZE] = {0};
        long val = 0;
        int t_length;
        const ERL_NIF_TERM* tuple = NULL;

        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if(!enif_get_tuple(env, head, &t_length, &tuple) || t_length != 2){
            return enif_make_badarg(env);
        }
        if (!enif_get_string(env, tuple[0], bin, MAX_SET_SIZE, ERL_NIF_UTF8)) {
    	    return enif_make_badarg(env);
        }
        if (!enif_get_long(env, tuple[1], &val)) {
            return enif_make_badarg(env);
        }
        as_record_set_int64(&rec, bin, val);
        list = tail;
    }

    if (aerospike_key_put(&as, &err, NULL, &key, &rec)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "key_put", ERL_NIF_UTF8);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM key_inc(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];
    unsigned int length;

    if (!enif_get_string(env, argv[0], name_space, MAX_NAMESPACE_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], set, MAX_SET_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], key_str, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    ERL_NIF_TERM list = argv[3];
    if (!enif_is_list(env, list) || !enif_get_list_length(env, list, &length)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL

    ERL_NIF_TERM rc, msg;
    if (length == 0) {
        rc = erl_ok;
        msg = enif_make_string(env, "key_inc", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

	as_error err;
    as_key key;
	as_key_init_str(&key, name_space, set, key_str);
	
    as_operations ops;
	as_operations_inita(&ops, length);

    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        char bin[AS_BIN_NAME_MAX_SIZE] = {0};
        long val = 0;
        int t_length;
        const ERL_NIF_TERM* tuple = NULL;

        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if(!enif_get_tuple(env, head, &t_length, &tuple) || t_length != 2){
            return enif_make_badarg(env);
        }
        if (!enif_get_string(env, tuple[0], bin, MAX_SET_SIZE, ERL_NIF_UTF8)) {
    	    return enif_make_badarg(env);
        }
        if (!enif_get_long(env, tuple[1], &val)) {
            return enif_make_badarg(env);
        }
        as_operations_add_incr(&ops, bin, val);
        list = tail;
    }

    if (aerospike_key_operate(&as, &err, NULL, &key, &ops, NULL)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "key_inc", ERL_NIF_UTF8);
    }

    return enif_make_tuple2(env, rc, msg);
}

// #define CLOCK_REALTIME 0 
// #define CLOCK_MONOTONIC 6 
// #define CLOCK_PROCESS_CPUTIME_ID  12 
// #define CLOCK_THREAD_CPUTIME_ID  16 

static ERL_NIF_TERM a_key_put(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char bin[AS_BIN_NAME_MAX_SIZE];
    long val;
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];
    long n;

    if (!enif_get_string(env, argv[0], bin, AS_USER_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_long(env, argv[1], &val)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], name_space, MAX_NAMESPACE_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[3], set, MAX_SET_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[4], key_str, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_long(env, argv[5], &n)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;
	as_record rec;

	as_key_init_str(&key, name_space, set, key_str);
	as_record_inita(&rec, 1);
	as_record_set_int64(&rec, bin, val);

    struct timespec thread_start, thread_done;
    struct timespec real_start, real_done;
    
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_start);
    clock_gettime(CLOCK_REALTIME, &real_start);

    for (uint i=0; i < n; i++) {
        if (aerospike_key_put(&as, &err, NULL, &key, &rec)  != AEROSPIKE_OK) {
            rc = erl_error;
            msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
            return enif_make_tuple2(env, rc, msg);
        }
    }
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &thread_done);
    clock_gettime(CLOCK_REALTIME, &real_done);
    // Convert to microseconds
    ErlNifSInt64 thread_tspent = (thread_done.tv_sec - thread_start.tv_sec) * 1000000 + (thread_done.tv_nsec - thread_start.tv_nsec) / 1000;
    ErlNifSInt64 real_tspent = (real_done.tv_sec - real_start.tv_sec) * 1000000 + (real_done.tv_nsec - real_start.tv_nsec) / 1000;

    rc = erl_ok;

    ERL_NIF_TERM keys[3];
    ERL_NIF_TERM vals[3];
    keys[0] = enif_make_string(env, "repetitions", ERL_NIF_UTF8);
    vals[0] = enif_make_uint64(env, n);
    keys[1] = enif_make_string(env, "CLOCK_THREAD_CPUTIME_ID", ERL_NIF_UTF8);
    vals[1] = enif_make_double(env, thread_tspent/n);    // from micro to mille seconds
    keys[2] = enif_make_string(env, "CLOCK_REALTIME", ERL_NIF_UTF8);
    vals[2] = enif_make_double(env, real_tspent/n);       // from micro to mille seconds
    enif_make_map_from_arrays(env, keys, vals, 3, &msg);
    return enif_make_tuple2(env, rc, msg);

}


static ERL_NIF_TERM key_remove(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];

    if (!enif_get_string(env, argv[0], name_space, MAX_NAMESPACE_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], set, MAX_SET_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], key_str, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;

	as_key_init_str(&key, name_space, set, key_str);

    if (aerospike_key_remove(&as, &err, NULL, &key)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc =erl_ok;
        msg = enif_make_string(env, "key_remove", ERL_NIF_UTF8);
    }

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM cdt_expire(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set, bin_key;
    std::string name_space, aspk_set, aspk_key;
    long ttl;
    
    if (!enif_inspect_binary(env, argv[0], &bin_ns)) {
	    return enif_make_badarg(env);
    }
    name_space.assign((const char*) bin_ns.data, bin_ns.size);

    if (!enif_inspect_binary(env, argv[1], &bin_set)) {
	    return enif_make_badarg(env);
    }
    aspk_set.assign((const char*) bin_set.data, bin_set.size);

    if (!enif_inspect_binary(env, argv[2], &bin_key)) {
	    return enif_make_badarg(env);
    }
    aspk_key.assign((const char*) bin_key.data, bin_key.size);
    
    if (!enif_get_long(env, argv[3], &ttl)) {
        return enif_make_badarg(env);
    }

    CHECK_ALL

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;

	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
    
    as_cdt_ctx ctx;
    as_cdt_ctx_inita(&ctx, 1);
    as_operations ops;
    as_operations_inita(&ops, 1);
    as_map_policy put_mode;
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    as_cdt_ctx_add_map_key(&ctx, (as_val*)&as_cmp_wildcard);
    //as_string key_main;
    //std::string main_key = "campaign.333";
    //as_string_init(&key_main, (char*)main_key.c_str(), false);
    //as_cdt_ctx_add_map_key(&ctx, (as_val*)&key_main);

    as_string key_ttl;
    std::string ttl_key = "3333";
    std::string bin_str = "fcap_map";
    as_string_init(&key_ttl, (char*)ttl_key.c_str(), false);
    //as_cdt_ctx_add_map_key(&ctx, (as_val*)&key_ttl);
    as_integer asv_begin, asv_end;
    as_integer_init(&asv_begin, 0);
    as_integer_init(&asv_end, ttl);
    as_operations_map_remove_by_value_range(&ops, bin_str.c_str(), &ctx, (as_val*)&asv_begin, (as_val*)&asv_end, AS_MAP_RETURN_COUNT);
    //as_operations_map_remove_by_value(&ops, bin_str.c_str(), &ctx, (as_val*)&asv_begin, AS_MAP_RETURN_COUNT);
    //as_operations_map_remove_by_key(&ops, bin_str.c_str(), &ctx, (as_val*)&key_ttl, AS_MAP_RETURN_NONE);
    //as_operations_map_get_by_value_range(&ops, bin_str.c_str(), &ctx, (as_val*)&asv_begin, (as_val*)&asv_end, AS_MAP_RETURN_NONE);
    //as_operations_map_remove_by_value(&ops, bin_str.c_str(), &ctx, (as_val*)&key_ttl, AS_MAP_RETURN_COUNT);

    // working code delete by key lists
    /*std::string bin_str = "fcap_map";
    as_operations ops;
    as_operations_inita(&ops, 1);
    as_map_policy put_mode;
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    as_arraylist remove_list;
	as_arraylist_init(&remove_list, 2, 2);
	as_arraylist_append_str(&remove_list, "campaign.222");
	as_arraylist_append_str(&remove_list, "campaign.444");

    //as_operations_add_map_remove_by_key_list(&ops, bin_str.c_str(), (as_list*)&remove_list, AS_MAP_RETURN_COUNT);
    as_operations_add_map_remove_by_key_list(&ops, bin_str.c_str(), (as_list*)&remove_list, AS_MAP_RETURN_NONE);
    as_arraylist_destroy(&remove_list);*/

    if(aerospike_key_operate(&as, &err, NULL, &key, &ops, NULL) != AEROSPIKE_OK){
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "muhahah", ERL_NIF_UTF8);
    }
    as_operations_destroy(&ops);

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM key_generation(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];

    if (!enif_get_string(env, argv[0], name_space, MAX_NAMESPACE_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], set, MAX_SET_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], key_str, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;
    as_record* p_rec = NULL;    

	as_key_init_str(&key, name_space, set, key_str);

    if (aerospike_key_get(&as, &err, NULL, &key, &p_rec)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }
    if (p_rec == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "NULL p_rec - internal error", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }
    ERL_NIF_TERM keys[2];
    ERL_NIF_TERM vals[2];
    keys[0] = enif_make_string(env, "gen", ERL_NIF_UTF8);
    vals[0] = enif_make_uint64(env, p_rec->gen);
    keys[1] = enif_make_string(env, "ttl", ERL_NIF_UTF8);
    vals[1] = enif_make_uint64(env, p_rec->ttl);
    enif_make_map_from_arrays(env, keys, vals, 2, &msg);
    rc = erl_ok;
    as_record_destroy(p_rec);
    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM key_exists(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];

    if (!enif_get_string(env, argv[0], name_space, MAX_NAMESPACE_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[1], set, MAX_SET_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    if (!enif_get_string(env, argv[2], key_str, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
	    return enif_make_badarg(env);
    }
    CHECK_ALL

    ERL_NIF_TERM rc, msg;
    as_error err;
    as_key key;
    as_record* p_rec = NULL;    

	as_key_init_str(&key, name_space, set, key_str);
    int as_rc = aerospike_key_exists(&as, &err, NULL, &key, &p_rec);

    if (as_rc != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }
    rc = erl_ok;
    static ERL_NIF_TERM tr = enif_make_atom(env, "true");
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }
    return enif_make_tuple2(env, rc, tr);
}

static ERL_NIF_TERM node_random(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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

static ERL_NIF_TERM node_names(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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

static ERL_NIF_TERM node_get(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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

static ERL_NIF_TERM nif_node_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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
    const as_policy_info* policy = &as.config.policies.info;
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


static ERL_NIF_TERM nif_help(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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

static ERL_NIF_TERM nif_host_info(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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
    const as_policy_info* policy = &as.config.policies.info;
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


// ------------------------------------------------------------------------------------------------

#define NIF_DIRTY_FUN(A, B, C) {A, B, C, ERL_DIRTY_JOB_IO_BOUND}

static ErlNifFunc nif_funcs[] = {
    {"as_init", 0, as_init},
    NIF_DIRTY_FUN("connect", 2, connect),
    {"nif_host_add", 2, host_add},
    {"host_clear", 0, host_clear},
    {"nif_host_list", 0, host_list},

    NIF_DIRTY_FUN("cdt_put_sync", 6, aspike_nif_cdt_put_sync),
    {"cdt_put_async", 6, aspike_nif_cdt_put_async},
    NIF_DIRTY_FUN("cdt_get", 4, aspike_nif_cdt_get_sync),
    NIF_DIRTY_FUN("cdt_delete_by_keys", 5, aspike_nif_cdt_delete_by_keys_sync),
    NIF_DIRTY_FUN("cdt_delete_by_keys_batch", 4, aspike_nif_cdt_delete_by_keys_batch_sync),
    NIF_DIRTY_FUN("segment_tag_get", 4, aspike_nif_segment_tag_get_sync),

    NIF_DIRTY_FUN("key_exists", 3, key_exists),
    NIF_DIRTY_FUN("key_inc", 4, key_inc),
    NIF_DIRTY_FUN("key_get", 3, aspike_nif_key_get_sync),
    NIF_DIRTY_FUN("key_generation", 3, key_generation),
    NIF_DIRTY_FUN("key_put", 4, key_put),
    NIF_DIRTY_FUN("binary_put", 5, binary_put),
    NIF_DIRTY_FUN("binary_remove", 5, binary_remove),
    NIF_DIRTY_FUN("binary_get", 3, aspike_nif_binary_get_sync),
    NIF_DIRTY_FUN("cdt_expire", 4, cdt_expire),
    NIF_DIRTY_FUN("key_remove", 3, key_remove),
    NIF_DIRTY_FUN("key_select", 4, aspike_nif_key_select_sync),
    NIF_DIRTY_FUN("nif_node_random", 0, node_random),
    NIF_DIRTY_FUN("nif_node_names", 0, node_names),
    NIF_DIRTY_FUN("nif_node_get", 1, node_get),
    NIF_DIRTY_FUN("nif_node_info", 2, nif_node_info),
    NIF_DIRTY_FUN("nif_help", 1, nif_help),
    NIF_DIRTY_FUN("nif_host_info", 3, nif_host_info),
    NIF_DIRTY_FUN("a_key_put", 6, a_key_put)
};

ERL_NIF_INIT(aspike_nif, nif_funcs, load, NULL, NULL, NULL)
