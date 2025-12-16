#include <aerospike/aerospike.h>
#include <aerospike/aerospike_batch.h>
#include <aerospike/aerospike_info.h>
#include <aerospike/aerospike_key.h>
#include <aerospike/as_arraylist.h>
#include <aerospike/as_batch.h>
#include <aerospike/as_bin.h>
#include <aerospike/as_cdt_order.h>
#include <aerospike/as_cluster.h>
#include <aerospike/as_error.h>
#include <aerospike/as_exp.h>
#include <aerospike/as_lookup.h>
#include <aerospike/as_map_operations.h>
#include <aerospike/as_monitor.h>
#include <aerospike/as_node.h>
#include <aerospike/as_operations.h>
#include <aerospike/as_orderedmap.h>
#include <aerospike/as_pair.h>
#include <aerospike/as_record.h>
#include <aerospike/as_record_iterator.h>
#include <aerospike/as_status.h>
#include <aerospike/as_val.h>
#include <assert.h>
#include <erl_nif.h>
#include <time.h>

#include <chrono>
#include <format>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "aspike_nif.h"
#include "sync_methods.h"

// Async callback structure for cdt_put operations
struct cdt_put_callback_data {
    ErlNifPid caller_pid;  // Erlang process to send result to
    ErlNifEnv* msg_env;    // Environment for creating response message
    std::vector<as_cdt_ctx*> ctx_vec;
    as_record rec;

    // Constructor to properly initialize
    cdt_put_callback_data(ErlNifEnv* env) {
        msg_env = enif_alloc_env();
    }

    // Destructor for cleanup
    ~cdt_put_callback_data() {
        if (msg_env) {
            enif_free_env(msg_env);
        }
        // Cleanup Aerospike resources
        for (as_cdt_ctx* pctx : ctx_vec) {
            as_cdt_ctx_destroy(pctx);
        }
    }
};

// Callback function for async cdt_put operation
static void cdt_put_async_callback(as_error* err, as_record* record, void* udata, as_event_loop* event_loop) {
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM result_msg;

    cdt_put_callback_data* cb_data = (cdt_put_callback_data*)udata;

    if (err) {
        ERL_NIF_TERM error_msg;
        ERL_NIF_TERM error_code;

        if (err->code == AEROSPIKE_ERR_NO_MORE_CONNECTIONS) {
            // Special handling for connection pool exhaustion
            error_code = enif_make_atom(cb_data->msg_env, "connection_pool_exhausted");
            error_msg = enif_make_string(cb_data->msg_env, "connection_pool_exhausted", ERL_NIF_UTF8);
        } else {
            // Regular error message
            char int_buffer[32];
            snprintf(int_buffer, sizeof(int_buffer), "%d", err->code);
            error_code = enif_make_atom(cb_data->msg_env, int_buffer);
            error_msg = enif_make_string(cb_data->msg_env, err->message, ERL_NIF_UTF8);
        }
        ERL_NIF_TERM error_tuple = enif_make_tuple2(cb_data->msg_env, error_code, error_msg);

        result_msg = enif_make_tuple2(cb_data->msg_env, erl_error, error_tuple);
    } else {
        ERL_NIF_TERM aspike_ok = enif_make_atom(cb_data->msg_env, "aspike_ok");
        ERL_NIF_TERM response = enif_make_string(cb_data->msg_env, "put", ERL_NIF_UTF8);
        result_msg = enif_make_tuple2(cb_data->msg_env, aspike_ok, response);
    }

    // Send message to calling Erlang process
    enif_send(NULL, &cb_data->caller_pid, cb_data->msg_env, result_msg);

    // Clean up callback data
    delete cb_data;
}

ERL_NIF_TERM aspike_nif_cdt_put_async(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_key;
    unsigned int bins_amount;
    std::string name_space, aspk_set, aspk_key;
    long ttl;

    if (!enif_inspect_binary(env, argv[0], &bin_ns)) {
        return enif_make_badarg(env);
    }
    name_space.assign((const char*)bin_ns.data, bin_ns.size);

    if (!enif_inspect_binary(env, argv[1], &bin_set)) {
        return enif_make_badarg(env);
    }
    aspk_set.assign((const char*)bin_set.data, bin_set.size);

    if (!enif_inspect_binary(env, argv[2], &bin_key)) {
        return enif_make_badarg(env);
    }
    aspk_key.assign((const char*)bin_key.data, bin_key.size);

    ERL_NIF_TERM list = argv[3];
    if (!enif_is_list(env, list) || !enif_get_list_length(env, list, &bins_amount)) {
        return enif_make_badarg(env);
    }

    if (!enif_get_long(env, argv[4], &ttl)) {
        return enif_make_badarg(env);
    }

    // {max_retries, sleep_between_retries, socket_timeout, total_timeout}
    const ERL_NIF_TERM* cdt_put_policy = NULL;
    int policy_length;
    long max_retries = 0;
    long sleep_between_retries = 0;
    long socket_timeout = 30000;
    long total_timeout = 1000;
    int policyReadRC = enif_get_tuple(env, argv[5], &policy_length, &cdt_put_policy);
    if (!policyReadRC || policy_length != 4) {
        return enif_make_badarg(env);
    }
    enif_get_long(env, cdt_put_policy[0], &max_retries);
    enif_get_long(env, cdt_put_policy[1], &sleep_between_retries);
    enif_get_long(env, cdt_put_policy[2], &socket_timeout);
    enif_get_long(env, cdt_put_policy[3], &total_timeout);

    ERL_NIF_TERM rc, msg;
    if (bins_amount == 0) {
        rc = erl_ok;
        msg = enif_make_string(env, "put", ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    CHECK_ALL

    // Allocate callback data with proper initialization
    cdt_put_callback_data* cb_data = new cdt_put_callback_data(env);

    // Get caller PID and create callback data
    if (!enif_self(env, &cb_data->caller_pid)) {
        return enif_make_tuple2(env, erl_error, enif_make_string(env, "Failed to get caller PID", ERL_NIF_UTF8));
    }

    as_key key;
    as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());
    as_record_inita(&cb_data->rec, bins_amount);
    if (ttl != 0) {
        cb_data->rec.ttl = ttl;
    }

    as_map_policy put_mode;
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    as_operations operations;
    std::vector<as_bytes*> bin_vec;
    for (uint i = 0; i < bins_amount; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        ErlNifBinary bin_bin;
        std::string bin_str, bin_str_val;
        int t_length;
        const ERL_NIF_TERM* tuple = NULL;
        unsigned int ts_length;

        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if (!enif_get_tuple(env, head, &t_length, &tuple) || t_length != 2) {
            // Cleanup and return error
            delete cb_data;
            return enif_make_badarg(env);
        }

        if (!enif_inspect_binary(env, tuple[0], &bin_bin)) {
            delete cb_data;
            return enif_make_badarg(env);
        }
        bin_str.assign((const char*)bin_bin.data, bin_bin.size);

        if (!enif_is_list(env, tuple[1]) || !enif_get_list_length(env, tuple[1], &ts_length)) {
            delete cb_data;
            return enif_make_badarg(env);
        }
        auto ts_list = tuple[1];
        as_operations_inita(&operations, ts_length + 1);
        if (ttl != 0) {
            operations.ttl = ttl;
        } else {
            operations.ttl = -2;
        }
        uint opnum = 0;
        ErlNifBinary bin_key_local, bin_val;
        as_string key_str, subkey1, subkey2, subkey3;
        as_bytes subval1;
        as_integer subval2, subval3;
        std::string fcap_key, fcap_val, valuesk, valuesk1, valuesk2;
        long i64;
        for (uint ts_i = 0; ts_i < ts_length; ts_i++) {
            ERL_NIF_TERM ts_head;
            ERL_NIF_TERM ts_tail;
            if (!enif_get_list_cell(env, ts_list, &ts_head, &ts_tail)) {
                break;
            }

            if (opnum == 0) {
                // getting fcap key
                cb_data->ctx_vec.push_back(as_cdt_ctx_create(1));
                if (enif_inspect_binary(env, ts_head, &bin_key_local)) {
                    fcap_key.assign((const char*)bin_key_local.data, bin_key_local.size);
                    as_string_init(&key_str, (char*)fcap_key.c_str(), false);
                    as_cdt_ctx_add_map_key_create(cb_data->ctx_vec.back(), (as_val*)&key_str, AS_MAP_KEY_ORDERED);
                }
                opnum++;
            } else if (opnum == 1) {
                // getting fcap value
                if (enif_inspect_binary(env, ts_head, &bin_val)) {
                    valuesk = "value";
                    as_string_init(&subkey1, (char*)valuesk.c_str(), false);
                    as_bytes_inita(&subval1, bin_val.size);
                    as_bytes_set(&subval1, 0, bin_val.data, bin_val.size);
                    as_operations_map_put(&operations, bin_str.c_str(), cb_data->ctx_vec.back(), &put_mode, (as_val*)&subkey1, (as_val*)&subval1);
                }
                opnum++;
            } else if (opnum == 2) {
                // getting subkey ttl
                if (enif_get_int64(env, ts_head, &i64)) {
                    valuesk1 = "ttl";
                    as_string_init(&subkey2, (char*)valuesk1.c_str(), false);
                    as_integer_init(&subval2, i64);
                    as_operations_map_put(&operations, bin_str.c_str(), cb_data->ctx_vec.back(), &put_mode, (as_val*)&subkey2, (as_val*)&subval2);
                }
                opnum = 0;
                // subkey write time
                auto now = std::chrono::system_clock::now().time_since_epoch();
                long wt = std::chrono::duration_cast<std::chrono::seconds>(now).count();
                valuesk2 = "wt";
                as_string_init(&subkey3, (char*)valuesk2.c_str(), false);
                as_integer_init(&subval3, wt);
                as_operations_map_put(&operations, bin_str.c_str(), cb_data->ctx_vec.back(), &put_mode, (as_val*)&subkey3, (as_val*)&subval3);
            } else {
                break;
            }

            ts_list = ts_tail;
        }
        list = tail;
    }

    // Set up async policy
    as_policy_operate policy;
    as_policy_operate_init(&policy);
    policy.ttl = ttl;
    policy.base.max_retries = max_retries;
    policy.base.sleep_between_retries = sleep_between_retries;
    policy.base.socket_timeout = socket_timeout;
    policy.base.total_timeout = total_timeout;

    ERL_NIF_TERM return_data;
    as_error err;
    as_status status = aerospike_key_operate_async(as, &err, &policy, &key, &operations, cdt_put_async_callback, cb_data, NULL, NULL);

    if (status != AEROSPIKE_OK) {
        // Failed to initiate async operation

        // Cleanup callback data
        delete cb_data;

        char int_buffer[32];
        snprintf(int_buffer, sizeof(int_buffer), "%d", err.code);
        ERL_NIF_TERM error_code = enif_make_atom(env, int_buffer);
        ERL_NIF_TERM error_msg;
        if (err.message) {
            error_msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        } else {
            error_msg = enif_make_string(env, "Unknown error occurred", ERL_NIF_UTF8);
        }
        ERL_NIF_TERM error_tuple = enif_make_tuple2(env, error_code, error_msg);

        return_data = enif_make_tuple2(env, erl_error, error_tuple);
    } else {
        return_data = enif_make_tuple2(env, erl_ok, enif_make_atom(env, "in_progress"));
    }

    as_key_destroy(&key);
    as_operations_destroy(&operations);

    return return_data;
}
