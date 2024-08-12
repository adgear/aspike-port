#include <erl_nif.h>

#include <time.h>
#include <string>
#include <utility>
#include <iostream>
#include <vector>

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


// ----------------------------------------------------------------------------

#define MAX_HOST_SIZE 1024
#define MAX_KEY_STR_SIZE 1024
#define MAX_NAMESPACE_SIZE 32	// based on current server limit
#define MAX_SET_SIZE 64			// based on current server limit
#define AS_BIN_NAME_MAX_SIZE 16
#define MAX_BINS_NUMBER 1024

#define USE_DIRTY 1
#ifdef USE_DIRTY
#define NIF_FUN(A, B, C) {A, B, C, ERL_DIRTY_JOB_IO_BOUND}
#else
#define NIF_FUN(A, B, C) {A, B, C}
#endif

// ----------------------------------------------------------------------------

static aerospike as;
static bool is_aerospike_initialised = false;
static bool is_connected = false;
static ERL_NIF_TERM erl_error;
static ERL_NIF_TERM erl_ok;

// ----------------------------------------------------------------------------

typedef struct {
    ErlNifEnv* env;
    uint32_t count;
    void *udata;
} conversion_data;

// ----------------------------------------------------------------------------

#define CHECK_AEROSPIKE_INIT \
    if (!is_aerospike_initialised) {\
        return enif_make_tuple2(env,\
            enif_make_atom(env, "error"),\
            enif_make_string(env, "aerospike not initialised", ERL_NIF_UTF8));\
    }

#define CHECK_IS_CONNECTED \
    if (!is_connected) {\
        return enif_make_tuple2(env,\
            enif_make_atom(env, "error"),\
            enif_make_string(env, "not connected", ERL_NIF_UTF8));\
    }

#define CHECK_INIT CHECK_AEROSPIKE_INIT

#define CHECK_ALL\
    CHECK_INIT\
    CHECK_IS_CONNECTED

// ----------------------------------------------------------------------------

static ERL_NIF_TERM as_init(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    if (!is_aerospike_initialised) {
        as_config config;
        as_config_init(&config);
        aerospike_init(&as, &config);
        is_aerospike_initialised = true;
    }
    erl_error = enif_make_atom(env, "error");
    erl_ok = enif_make_atom(env, "ok");
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

    ERL_NIF_TERM rc, msg;
    as_config_set_user(&as.config, user, password);
	as_error err;

    if (aerospike_connect(&as, &err) != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        is_connected = false;
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "connected", ERL_NIF_UTF8);
        is_connected = true;
    }

    return enif_make_tuple2(env, rc, msg);
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

static ERL_NIF_TERM cdt_put(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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

    // {max_retries, sleep_between_retries, socket_timeout, total_timeout}
    const ERL_NIF_TERM* policy = NULL;
    int policy_length;
    long max_retries = 0;
    long sleep_between_retries = 0;
    long socket_timeout = 30000;
    long total_timeout = 1000;
    if(!enif_get_tuple(env, argv[5], &policy_length, &policy) || policy_length != 4){
        return enif_make_badarg(env);
    }
    enif_get_long(env, policy[0], &max_retries);
    enif_get_long(env, policy[1], &sleep_between_retries);
    enif_get_long(env, policy[2], &socket_timeout);
    enif_get_long(env, policy[3], &total_timeout);
    
    ERL_NIF_TERM rc, msg;
    if (length == 0) {
        rc = erl_ok;
        msg = enif_make_string(env, "put", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    CHECK_ALL

	as_error err;
    as_key key;
	as_record rec;
	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
	as_record_inita(&rec, length);
    if(ttl != 0){
        rec.ttl = ttl;
    }
        
    as_cdt_ctx ctx;
    as_cdt_ctx_inita(&ctx, 1);
    as_operations ops;
    as_map_policy put_mode;
    //as_map_policy_set(&put_mode, AS_MAP_UNORDERED, AS_MAP_UPDATE);
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    std::vector<as_bytes*> bin_vec; 
    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        ErlNifBinary bin_bin;
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

        if (!enif_is_list(env, tuple[1]) || !enif_get_list_length(env, tuple[1], &ts_length)) {
            return enif_make_badarg(env);
        }
        auto ts_list = tuple[1];
        as_operations_inita(&ops, 2);
        if(ttl != 0){
            ops.ttl = ttl;
        } else {
            ops.ttl = -2;
        }
        uint opnum = 0;
        ErlNifBinary bin_key, bin_val;
        as_string key_str, subkey1, subkey2;
        as_bytes subval1;
        as_integer subval2;
        std::string fcap_key, fcap_val, valuesk, valuesk1;
        long i64;
        for (uint ts_i = 0; ts_i < ts_length; ts_i++) {
            ERL_NIF_TERM ts_head;
            ERL_NIF_TERM ts_tail;
            if (!enif_get_list_cell(env, ts_list, &ts_head, &ts_tail)) {
                break;
            }
            
            if(opnum == 0){
                //getting fcap key
                if (enif_inspect_binary(env, ts_head, &bin_key)) {
                    fcap_key.assign((const char*) bin_key.data, bin_key.size);
                    as_string_init(&key_str, (char*)fcap_key.c_str(), false);
                    as_cdt_ctx_add_map_key_create(&ctx, (as_val*)&key_str, AS_MAP_KEY_ORDERED);
                }
                opnum++;
            }else if(opnum == 1){
                //getting fcap value
                if (enif_inspect_binary(env, ts_head, &bin_val)) {
                    //fcap_val.assign((const char*) bin_val.data, bin_val.size);
                    valuesk = "value";
                    as_string_init(&subkey1, (char*)valuesk.c_str(), false);
                    //as_string_init(&subval1, (char*)fcap_val.c_str(), false);
                    as_bytes_inita(&subval1, bin_val.size);
                    as_bytes_set(&subval1, 0, bin_val.data, bin_val.size);
                    as_operations_map_put(&ops, bin_str.c_str(), &ctx, &put_mode, (as_val*)&subkey1, (as_val*)&subval1);
                }
                opnum++;

            }else if(opnum == 2){
                //getting subkey ttl
                if(enif_get_int64(env, ts_head, &i64)){
                    valuesk1 = "ttl";
                    as_string_init(&subkey2, (char*)valuesk1.c_str(), false);
                    as_integer_init(&subval2, i64);
                    as_operations_map_put(&ops, bin_str.c_str(), &ctx, &put_mode, (as_val*)&subkey2, (as_val*)&subval2);
                }
                opnum=0;
                break;
            }else{
                break;
            }
            

            ts_list = ts_tail;
        }
/*
        as_string str;
        std::string uidsk = "flight.id.99999.5657667";
        as_string_init(&str, (char*)uidsk.c_str(), false);
        as_cdt_ctx_add_map_key_create(&ctx, (as_val*)&str, AS_MAP_UNORDERED);

        as_string subkey;
        std::string valuesk = "value";
        as_string_init(&subkey, (char*)valuesk.c_str(), false);
        //as_integer subval;
        //as_integer_init(&subval, 222);
        as_string subval;
        std::string valuesv = "200,212,242,174,6,144,8,6";
        as_string_init(&subval, (char*)valuesv.c_str(), false);
        as_operations_map_put(&ops, bin_str.c_str(), &ctx, &put_mode, (as_val*)&subkey, (as_val*)&subval);
        
        as_string subkey1;
        std::string valuesk1 = "ttl";
        as_string_init(&subkey1, (char*)valuesk1.c_str(), false);
        as_integer subval1;
        as_integer_init(&subval1, 600);
        as_operations_map_put(&ops, bin_str.c_str(), &ctx, &put_mode, (as_val*)&subkey1, (as_val*)&subval1);
*/

    }

    //const as_policy_operate * policy;
    as_record * rec1 = &rec;
    as_policy_operate p;
	as_policy_operate_init(&p);
    p.ttl = ttl;
    p.base.max_retries = max_retries;
    p.base.sleep_between_retries = sleep_between_retries;
    p.base.socket_timeout = socket_timeout;
    p.base.total_timeout = total_timeout;

    if(aerospike_key_operate(&as, &err, &p, &key, &ops, &rec1) != AEROSPIKE_OK){
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "put", ERL_NIF_UTF8);
        as_record_destroy(&rec);
    }
    as_operations_destroy(&ops);
    as_key_destroy(&key);



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
        rc = erl_error;;
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
        rc = erl_error;;
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


static bool list_to_termlist_each(as_val *val, void *udata){
    if (!val) {
        return false;
    }

    conversion_data *convd = (conversion_data *)udata;
    std::vector<ERL_NIF_TERM> *erl_list = (std::vector<ERL_NIF_TERM> *)convd->udata;
    erl_list->push_back( enif_make_int64(convd->env, as_integer_get((as_integer*)val)) );

    convd->count++;
    return true;
}

ERL_NIF_TERM get_binary_asval(ErlNifEnv* env, const as_val * val) {
    ERL_NIF_TERM fcap_key;
    as_string *keystr = as_string_fromval(val);
    auto len = as_string_len(keystr);
    unsigned char * val_data;
    val_data = enif_make_new_binary(env, len, &fcap_key);
    memcpy(val_data, as_string_get(keystr), len);
    return fcap_key;
}

ERL_NIF_TERM get_binaryb_asval(ErlNifEnv* env, const as_val * val) {
    ERL_NIF_TERM fcap_key;
    as_bytes *keystr = as_bytes_fromval(val);
    auto len = as_bytes_size(keystr);
    unsigned char * val_data;
    val_data = enif_make_new_binary(env, len, &fcap_key);
    memcpy(val_data, as_bytes_get(keystr), len);
    return fcap_key;
}

static ERL_NIF_TERM format_value_out(ErlNifEnv* env, as_val_t type, as_bin_value *val) {
    switch(type) {
        case AS_INTEGER:
            return enif_make_int64(env, val->integer.value);
        case AS_STRING:
        case AS_BYTES: {
            as_bytes asbval = val->bytes;
            uint8_t * bin_as_str = as_bytes_get(&asbval);
            auto len = asbval.size;

            unsigned char * val_data;
            ERL_NIF_TERM res;
            val_data = enif_make_new_binary(env, len, &res);
            memcpy(val_data, bin_as_str, len);
            return res;
        }break;
        case AS_LIST: {
            auto len = as_list_size((as_list *)(&val->list));
	        std::vector<ERL_NIF_TERM> * erl_list = new std::vector<ERL_NIF_TERM>();
	        erl_list->reserve(len);

            conversion_data convd = {
                .env = env, .count = 0, .udata = erl_list};

            as_list_foreach((as_list *)(&val->list), list_to_termlist_each, &convd);
	        return enif_make_list_from_array(env, erl_list->data(), len);
        }break;
        case AS_MAP: {
            auto len = as_map_size((as_map *)(&val->map));
	        std::vector<ERL_NIF_TERM> * erl_list = new std::vector<ERL_NIF_TERM>();
	        erl_list->reserve(len*2);
            
            const as_orderedmap *amap = (const as_orderedmap*)&val->map;
            as_orderedmap_iterator it;
            as_orderedmap_iterator_init(&it, amap);
            while ( as_orderedmap_iterator_has_next(&it) ) {
                long fccount = 0;
                const as_val* val = as_orderedmap_iterator_next(&it);
                as_pair * apr = as_pair_fromval(val);
                erl_list->push_back(get_binary_asval(env, as_pair_1(apr)));

                const as_orderedmap *vmap = (const as_orderedmap*)as_map_fromval(as_pair_2(apr));
                as_orderedmap_iterator iti_int;
                as_orderedmap_iterator_init(&iti_int, vmap);
                ERL_NIF_TERM vnt, ttlsm;
                while ( as_orderedmap_iterator_has_next(&iti_int) ) {
                    const as_val* valsm = as_orderedmap_iterator_next(&iti_int);
                    as_pair * aprsm = as_pair_fromval(valsm);
                    if(as_pair_2(aprsm)->type == 9){
                        vnt = get_binaryb_asval(env, as_pair_2(aprsm));
                        fccount++;
                    }else if(as_pair_2(aprsm)->type == 3){
                        ttlsm = enif_make_int64(env, as_integer_get((as_integer*)as_pair_2(aprsm)));
                        fccount++;
                    }else if(as_pair_2(aprsm)->type == 4){
                        vnt = get_binary_asval(env, as_pair_2(aprsm));
                        fccount++;
                    }
                }
                as_orderedmap_iterator_destroy(&iti_int);
                if(fccount == 2){
                    erl_list->push_back(enif_make_tuple2(env, vnt, ttlsm));
                }
            }
            as_orderedmap_iterator_destroy(&it);
            if(erl_list->size() == 0){
                return enif_make_list(env, 0);
            } else {
	            auto dlret = enif_make_list_from_array(env, erl_list->data(), erl_list->size());
                delete erl_list;
                return dlret;
            }
        }break;
        default:
            char * val_as_str = as_val_tostring(val);
            ERL_NIF_TERM res =  enif_make_string(env, as_val_tostring(val), ERL_NIF_UTF8);
            free(val_as_str);
            return res;
    }
}



static ERL_NIF_TERM dump_records(ErlNifEnv* env, const as_record *p_rec) {
    ERL_NIF_TERM res;

	if (p_rec->key.valuep) {
		char* key_val_as_str = as_val_tostring(p_rec->key.valuep);
        res = enif_make_string(env, key_val_as_str, ERL_NIF_UTF8);
		free(key_val_as_str);
        return res;
	}

	as_record_iterator it;
	as_record_iterator_init(&it, p_rec);
    
    res = enif_make_list(env, 0);

    while (as_record_iterator_has_next(&it)) {
        const as_bin* p_bin = as_record_iterator_next(&it);
        char* name = as_bin_get_name(p_bin);
        uint type = as_bin_get_type(p_bin);
		ERL_NIF_TERM cell = enif_make_tuple2(env,
            enif_make_string(env, name, ERL_NIF_UTF8),
            format_value_out(env, type, as_bin_get_value(p_bin))
            );
        res = enif_make_list_cell(env, cell, res);
	}

	as_record_iterator_destroy(&it);

    return res;
}

static ERL_NIF_TERM key_get(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
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
        if (p_rec != NULL) {
            as_record_destroy(p_rec);
        }
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }
    if (p_rec == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "NULL p_rec - internal error", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    msg = dump_records(env, p_rec);
    rc = erl_ok;
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }
    return enif_make_tuple2(env, rc, msg);
}


static ERL_NIF_TERM dump_binary_records(ErlNifEnv* env, const as_record *p_rec) {
    ERL_NIF_TERM res;
	
    if (p_rec->key.valuep) {
        unsigned char* key_data;
		char* key_val_as_str = as_val_tostring(p_rec->key.valuep);
        auto len = strlen(key_val_as_str);

        key_data = enif_make_new_binary(env, len, &res);
        memcpy(key_data, key_val_as_str, len);
        //res = enif_make_string(env, key_val_as_str, ERL_NIF_UTF8);
		free(key_val_as_str);
        return res;
	}

	as_record_iterator it;
	as_record_iterator_init(&it, p_rec);
    
    res = enif_make_list(env, 0);

    while (as_record_iterator_has_next(&it)) {
        const as_bin* p_bin = as_record_iterator_next(&it);
        char* name = as_bin_get_name(p_bin);
        auto namelen = strlen(name);
        uint type = as_bin_get_type(p_bin);

        unsigned char* name_data;
        ERL_NIF_TERM name_term;
        name_data = enif_make_new_binary(env, namelen, &name_term);
        memcpy(name_data, name, namelen);

		ERL_NIF_TERM cell = enif_make_tuple2(env,
            name_term,
            format_value_out(env, type, as_bin_get_value(p_bin))
            );
        res = enif_make_list_cell(env, cell, res);
	}

	as_record_iterator_destroy(&it);

    return res;

}

static ERL_NIF_TERM dump_cdt_records(ErlNifEnv* env, const as_record *p_rec) {
    ERL_NIF_TERM res;
    if (p_rec->key.valuep) {
        unsigned char* key_data;
		char* key_val_as_str = as_val_tostring(p_rec->key.valuep);
        auto len = strlen(key_val_as_str);

        key_data = enif_make_new_binary(env, len, &res);
        memcpy(key_data, key_val_as_str, len);
        //res = enif_make_string(env, key_val_as_str, ERL_NIF_UTF8);
		free(key_val_as_str);
        return res;
	}

	as_record_iterator it;
	as_record_iterator_init(&it, p_rec);
    res = enif_make_list(env, 0);

    while (as_record_iterator_has_next(&it)) {
        const as_bin* p_bin = as_record_iterator_next(&it);
        char* name = as_bin_get_name(p_bin);
        auto namelen = strlen(name);
        uint type = as_bin_get_type(p_bin);


        unsigned char* name_data;
        ERL_NIF_TERM name_term;
        name_data = enif_make_new_binary(env, namelen, &name_term);
        memcpy(name_data, name, namelen);

		ERL_NIF_TERM cell = enif_make_tuple2(env,
            name_term,
            format_value_out(env, type, as_bin_get_value(p_bin))
            );
        res = enif_make_list_cell(env, cell, res);
	}

	as_record_iterator_destroy(&it);
    return res;
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

static ERL_NIF_TERM cdt_delete_by_keys_batch(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set,  bin_name;
    std::string name_space, aspk_set, aspk_key, bin_str;
    unsigned int length;

    
    if (!enif_inspect_binary(env, argv[0], &bin_ns)) {
	    return enif_make_badarg(env);
    }
    name_space.assign((const char*) bin_ns.data, bin_ns.size);

    if (!enif_inspect_binary(env, argv[1], &bin_set)) {
	    return enif_make_badarg(env);
    }
    aspk_set.assign((const char*) bin_set.data, bin_set.size);
    
    if (!enif_inspect_binary(env, argv[2], &bin_name)) {
	    return enif_make_badarg(env);
    }
    bin_str.assign((const char*) bin_name.data, bin_name.size);
    
    ERL_NIF_TERM key_subkeys_list = argv[3];
    if (!enif_is_list(env, key_subkeys_list) || !enif_get_list_length(env, key_subkeys_list, &length)) {
	    return enif_make_badarg(env);
    }

    CHECK_ALL
    ERL_NIF_TERM rc, msg;
    std::vector<std::string> bin_str_list(length);
    std::vector<std::vector<std::string>> skeys_lst; 
    std::vector<as_batch_write_record*> abwrs(length);
    std::vector<as_operations> wopsl(length);
    std::vector<as_arraylist> rval(length);

    as_batch_records recs;
	as_batch_records_inita(&recs, length);

    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        ErlNifBinary bin_bin;

        if (!enif_get_list_cell(env, key_subkeys_list, &head, &tail)) {
            break;
        }
        const ERL_NIF_TERM* ksk_tuple = NULL;
        int ksk_length;
        if(!enif_get_tuple(env, head, &ksk_length, &ksk_tuple) || ksk_length != 2){
            return enif_make_badarg(env);
        }
        
        if (!enif_inspect_binary(env, ksk_tuple[0], &bin_bin)) {
            return enif_make_badarg(env);
        }
        bin_str_list[i].assign((const char*) bin_bin.data, bin_bin.size);
        
        unsigned int ts_length;
        if (!enif_is_list(env, ksk_tuple[1]) || !enif_get_list_length(env, ksk_tuple[1], &ts_length)) {
            return enif_make_badarg(env);
        }

        abwrs[i] = as_batch_write_reserve(&recs);
	    as_key_init_str(&(abwrs[i]->key), name_space.c_str(), aspk_set.c_str(), bin_str_list[i].c_str());

        auto ts_list = ksk_tuple[1];
        std::vector<std::string> bin_str_sk_list(ts_length);
	    as_arraylist_init(&(rval[i]), ts_length, ts_length);
        for(uint j = 0; j < ts_length; j++){
            ERL_NIF_TERM skl_head;
            ERL_NIF_TERM skl_tail;
            ErlNifBinary skl_bin_bin;
            if (!enif_get_list_cell(env, ts_list, &skl_head, &skl_tail)) {
                break;
            }
            
            if (!enif_inspect_binary(env, skl_head, &skl_bin_bin)) {
                return enif_make_badarg(env);
            }
            bin_str_sk_list[j].assign((const char*) skl_bin_bin.data, skl_bin_bin.size);
	        as_arraylist_append_str(&(rval[i]), (char*)bin_str_sk_list[j].c_str());

            ts_list = skl_tail;
        }
        skeys_lst.push_back(bin_str_sk_list);
        as_operations_inita(&(wopsl[i]), 1);
        as_operations_add_map_remove_by_key_list(&(wopsl[i]), bin_str.c_str(), (as_list*)&(rval[i]), AS_MAP_RETURN_NONE);
        wopsl[i].ttl = AS_RECORD_CLIENT_DEFAULT_TTL;
        abwrs[i]->ops = &(wopsl[i]);

        key_subkeys_list = tail;
    }

    as.config.policies.batch_write.ttl = 1000;
    as_error err;
	as_status status = aerospike_batch_write(&as, &err, NULL, &recs);

	std::vector<ERL_NIF_TERM> * erl_list = new std::vector<ERL_NIF_TERM>();
    for(auto aitr : abwrs){
        erl_list->push_back(enif_make_int(env, aitr->result));
        /*if(aitr->result == AEROSPIKE_OK){
            std::cout << "WOPOK! \r\n";
        }else{
            std::cout << "WOPNOK!: " << std::to_string(aitr->result) << "\r\n";
        }*/
    }
    auto opsl = enif_make_list_from_array(env, erl_list->data(), erl_list->size());
    delete erl_list;

    for(auto vitr : wopsl){
        as_operations_destroy(&vitr);
    }

    as_batch_records_destroy(&recs);
    if(status != AEROSPIKE_OK){
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    } else {
        rc = erl_ok; 
        return enif_make_tuple2(env, rc, opsl);
    }
}

static ERL_NIF_TERM cdt_delete_by_keys(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set, bin_key, bin_name;
    std::string name_space, aspk_set, aspk_key, bin_str;
    unsigned int length;
    
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
    
    if (!enif_inspect_binary(env, argv[3], &bin_name)) {
	    return enif_make_badarg(env);
    }
    bin_str.assign((const char*) bin_name.data, bin_name.size);
    
    ERL_NIF_TERM list = argv[4];
    if (!enif_is_list(env, list) || !enif_get_list_length(env, list, &length)) {
	    return enif_make_badarg(env);
    }

    CHECK_ALL
    
    as_arraylist remove_list;
	as_arraylist_init(&remove_list, length, length);

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;

	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
    
    as_operations ops;
    as_operations_inita(&ops, 1);
    as_map_policy put_mode;
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    ErlNifBinary subkey_term;
    std::string subkey_str;
    unsigned int subkeys_num = 0;
    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if (enif_inspect_binary(env, head, &subkey_term)) {
            subkey_str.assign((const char*) subkey_term.data, subkey_term.size);
	        as_arraylist_append_str(&remove_list, (char*)subkey_str.c_str());
            subkeys_num++;
        }
        list = tail;
    }
    if(subkeys_num == length){
        as_operations_add_map_remove_by_key_list(&ops, bin_str.c_str(), (as_list*)&remove_list, AS_MAP_RETURN_NONE);
    }
    as_arraylist_destroy(&remove_list);

    if(aerospike_key_operate(&as, &err, NULL, &key, &ops, NULL) != AEROSPIKE_OK){
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "keys_deleted", ERL_NIF_UTF8);
    }
    as_operations_destroy(&ops);

    return enif_make_tuple2(env, rc, msg);
}

static ERL_NIF_TERM cdt_get(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set, bin_key;
    std::string name_space, aspk_set, aspk_key;
    
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

    // {max_retries, sleep_between_retries, socket_timeout, total_timeout}
    const ERL_NIF_TERM* policy = NULL;
    int policy_length;
    long max_retries = 0;
    long sleep_between_retries = 0;
    long socket_timeout = 30000;
    long total_timeout = 1000;
    if(!enif_get_tuple(env, argv[3], &policy_length, &policy) || policy_length != 4){
        return enif_make_badarg(env);
    }
    enif_get_long(env, policy[0], &max_retries);
    enif_get_long(env, policy[1], &sleep_between_retries);
    enif_get_long(env, policy[2], &socket_timeout);
    enif_get_long(env, policy[3], &total_timeout);

    CHECK_ALL

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;
    as_record* p_rec = NULL;    

	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
    as_policy_read p;
	as_policy_read_init(&p);
    p.base.max_retries = max_retries;
    p.base.sleep_between_retries = sleep_between_retries;
    p.base.socket_timeout = socket_timeout;
    p.base.total_timeout = total_timeout;

    if (aerospike_key_get(&as, &err, NULL, &key, &p_rec)  != AEROSPIKE_OK) {
        if (p_rec != NULL) {
            as_record_destroy(p_rec);
        }
        rc = erl_error;
        as_key_destroy(&key);
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    as_key_destroy(&key);
    if (p_rec == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "NULL p_rec - internal error", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    msg = dump_cdt_records(env, p_rec);
    rc = erl_ok;
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }
    return enif_make_tuple2(env, rc, msg);

//
}

static ERL_NIF_TERM binary_get(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifBinary bin_ns, bin_set, bin_key;
    std::string name_space, aspk_set, aspk_key;
    
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

    CHECK_ALL

    ERL_NIF_TERM rc, msg;
	as_error err;
    as_key key;
    as_record* p_rec = NULL;    

	as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());

    if (aerospike_key_get(&as, &err, NULL, &key, &p_rec)  != AEROSPIKE_OK) {
        if (p_rec != NULL) {
            as_record_destroy(p_rec);
        }
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }
    if (p_rec == NULL) {
        rc = erl_error;
        msg = enif_make_string(env, "NULL p_rec - internal error", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    msg = dump_binary_records(env, p_rec);
    rc = erl_ok;
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }
    return enif_make_tuple2(env, rc, msg);

//
}

static ERL_NIF_TERM key_select(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    char name_space[MAX_NAMESPACE_SIZE];
    char set[MAX_SET_SIZE];
    char key_str[MAX_KEY_STR_SIZE];
    unsigned int length = 0;
    unsigned int i = 0;
    as_record* p_rec = NULL;

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
        msg = enif_make_list(env, 0);  // empty list
        rc = erl_ok;
        return enif_make_tuple2(env, rc, msg);
    }

    const char *bins[MAX_BINS_NUMBER];
    for (; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        char bin[AS_BIN_NAME_MAX_SIZE] = {0};
        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if(!enif_get_string(env, head, bin, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)){
            break;
        }
        bins[i] = strdup(bin);
        list = tail;
    }
    bins[i] = NULL;

	as_error err;
    as_key key;
	as_key_init_str(&key, name_space, set, key_str);

    if (aerospike_key_select(&as, &err, NULL, &key, bins, &p_rec)  != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    msg = dump_records(env, p_rec);
    rc = erl_ok;
    for (uint j = 0; j < i; j++) {
        delete(bins[j]);
    }
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }

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
        free(info);
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
        free(info);
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
        free(info);
    }

    return enif_make_tuple2(env, rc, msg);   
}


// ------------------------------------------------------------------------------------------------

extern int foo(int x);
extern int bar(int y);

static ERL_NIF_TERM foo_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    int x, ret;
    if (!enif_get_int(env, argv[0], &x)) {
	    return enif_make_badarg(env);
    }
    ret = foo(x);
    return enif_make_int(env, ret);
}

static ERL_NIF_TERM bar_nif(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    int y, ret;
    if (!enif_get_int(env, argv[0], &y)) {
	    return enif_make_badarg(env);
    }
    ret = bar(y);
    return enif_make_int(env, ret);
}

static ErlNifFunc nif_funcs[] = {
    {"as_init", 0, as_init},
    NIF_FUN("connect", 2, connect),
    NIF_FUN("nif_host_add", 2, host_add),
    NIF_FUN("host_clear", 0, host_clear),
    NIF_FUN("nif_host_list", 0, host_list),
    NIF_FUN("key_exists", 3, key_exists),
    NIF_FUN("key_inc", 4, key_inc),
    NIF_FUN("key_get", 3, key_get),
    NIF_FUN("key_generation", 3, key_generation),
    NIF_FUN("key_put", 4, key_put),
    NIF_FUN("binary_put", 5, binary_put),
    NIF_FUN("cdt_put", 6, cdt_put),
    NIF_FUN("binary_remove", 5, binary_remove),
    NIF_FUN("binary_get", 3, binary_get),
    NIF_FUN("cdt_get", 4, cdt_get),
    NIF_FUN("cdt_expire", 4, cdt_expire),
    NIF_FUN("cdt_delete_by_keys", 5, cdt_delete_by_keys),
    NIF_FUN("cdt_delete_by_keys_batch", 4, cdt_delete_by_keys_batch),
    NIF_FUN("key_remove", 3, key_remove),
    NIF_FUN("key_select", 4, key_select),
    NIF_FUN("nif_node_random", 0, node_random),
    NIF_FUN("nif_node_names", 0, node_names),
    NIF_FUN("nif_node_get", 1, node_get),
    NIF_FUN("nif_node_info", 2, nif_node_info),
    NIF_FUN("nif_help", 1, nif_help),
    NIF_FUN("nif_host_info", 3, nif_host_info),
    // ----------------------------------------------------
    NIF_FUN("a_key_put", 6, a_key_put),
    {"foo", 1, foo_nif},
    {"bar", 1, bar_nif}
};

ERL_NIF_INIT(aspike_nif, nif_funcs, NULL, NULL, NULL, NULL)
