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
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "aspike_nif.h"
#include "common_methods.h"
#include "async_methods.h"

// Async callback structure for async operations
struct callback_data {
    ErlNifPid caller_pid;  // Erlang process to send result to
    ErlNifEnv* msg_env;    // Environment for creating response message
    std::vector<as_cdt_ctx*> cdt_contexts;

    // Constructor to properly initialize
    callback_data(ErlNifEnv* env) {
        msg_env = enif_alloc_env();
    }

    // Destructor for cleanup
    ~callback_data() {
        if (msg_env) {
            enif_free_env(msg_env);
        }
        for (auto cdt_ctx : cdt_contexts) {
            as_cdt_ctx_destroy(cdt_ctx);
        }
    }
};

// Callback function for async cdt_put operation
static void cdt_put_async_callback(as_error* err, as_record* record, void* udata, as_event_loop* event_loop) {
    ERL_NIF_TERM erl_ok = get_erl_ok();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM result_msg;

    callback_data* cb_data = (callback_data*)udata;

    if (err) {
        ERL_NIF_TERM error_msg;

        if (err->code == AEROSPIKE_ERR_NO_MORE_CONNECTIONS) {
            // Special handling for connection pool exhaustion
            error_msg = enif_make_string(cb_data->msg_env, "connection_pool_exhausted", ERL_NIF_UTF8);
        } else {
            // Regular error message
            if (strlen(err->message) != 0) {
                error_msg = enif_make_string(cb_data->msg_env, err->message, ERL_NIF_UTF8);
            } else {
                error_msg = enif_make_string(cb_data->msg_env, "Unknown error occurred", ERL_NIF_UTF8);
            }
        }
        auto nifErrorCode = enif_make_int(cb_data->msg_env, ASPIKE_NIF_OK);
        auto aspikeErrorCode = enif_make_int(cb_data->msg_env, err->code);
        ERL_NIF_TERM error_tuple = enif_make_tuple3(cb_data->msg_env, nifErrorCode, aspikeErrorCode, error_msg);

        result_msg = enif_make_tuple2(cb_data->msg_env, erl_error, error_tuple);
    } else {
        ERL_NIF_TERM response = enif_make_string(cb_data->msg_env, "done", ERL_NIF_UTF8);
        result_msg = enif_make_tuple2(cb_data->msg_env, erl_ok, response);
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

    ErlNifBinary erl_namespace;
    if (!enif_inspect_binary(env, argv[0], &erl_namespace)) {
        return enif_make_badarg(env);
    }
    std::string name_space;
    name_space.assign((const char*)erl_namespace.data, erl_namespace.size);

    ErlNifBinary erl_set_name;
    if (!enif_inspect_binary(env, argv[1], &erl_set_name)) {
        return enif_make_badarg(env);
    }
    std::string set_name;
    set_name.assign((const char*)erl_set_name.data, erl_set_name.size);

    ErlNifBinary erl_primary_key;
    if (!enif_inspect_binary(env, argv[2], &erl_primary_key)) {
        return enif_make_badarg(env);
    }
    std::string record_primary_key;
    record_primary_key.assign((const char*)erl_primary_key.data, erl_primary_key.size);

    unsigned int bins_amount;
    ERL_NIF_TERM bins = argv[3];
    if (!enif_is_list(env, bins) || !enif_get_list_length(env, bins, &bins_amount)) {
        return enif_make_badarg(env);
    }
    // now the list points to structure like
    // [{<<"fcap_map">>, [<<"map_key_1">>, <<"map_value_1">>, 123, <<"map_key_2">>, <<"map_value_2">>, 456]}]
    if (bins_amount == 0) {
        // just a check if there is something we should do at all, and if not - complete this call
        auto msg = enif_make_string(env, "put", ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_ok, msg);
    }

    long ttl;
    if (!enif_get_long(env, argv[4], &ttl)) {
        return enif_make_badarg(env);
    }

    const ERL_NIF_TERM* erl_policy = NULL;
    int policy_length;
    long max_retries = 0;
    long socket_timeout = 0;
    long total_timeout = 0;
    int policyReadRC = enif_get_tuple(env, argv[5], &policy_length, &erl_policy);
    if (!policyReadRC || policy_length != 4) {
        return enif_make_badarg(env);
    }
    // note: sleep_between_retries is not used in async operations by c-client
    enif_get_long(env, erl_policy[0], &max_retries);
    enif_get_long(env, erl_policy[2], &socket_timeout);
    enif_get_long(env, erl_policy[3], &total_timeout);
    as_policy_operate policy;
    as_policy_operate_init(&policy);
    policy.ttl = ttl;
    policy.base.max_retries = max_retries;
    policy.base.socket_timeout = socket_timeout;
    policy.base.total_timeout = total_timeout;

    // We need to know upfront how many operations we are going to send to
    // aerospike, and we can get that amount by walking thr provided bins
    // and checking the lengths of data of bins.
    // At the same time this is a good place to conduct some input validation
    // so we don't have to do that later once we start memory allocations
    uint total_operations = 0;
    ERL_NIF_TERM bins_copy = bins; // Keep original for second pass
    for (uint i = 0; i < bins_amount; i++) {
        ERL_NIF_TERM bins_head, bins_tail;
        if (!enif_get_list_cell(env, bins_copy, &bins_head, &bins_tail)) {
            return enif_make_badarg(env);
        }

        int tuple_length;
        const ERL_NIF_TERM* bin_tuple = NULL;
        if (!enif_get_tuple(env, bins_head, &tuple_length, &bin_tuple) || tuple_length != 2) {
            return enif_make_badarg(env);
        }

        ErlNifBinary erl_bin_name;
        if (!enif_inspect_binary(env, bin_tuple[0], &erl_bin_name)) {
            return enif_make_badarg(env);
        }

        unsigned int bin_data_len;
        if (!enif_is_list(env, bin_tuple[1]) || !enif_get_list_length(env, bin_tuple[1], &bin_data_len)) {
            return enif_make_badarg(env);
        }
        total_operations += bin_data_len;
        bins_copy = bins_tail;
    }
    as_operations operations;
    as_operations_inita(&operations, total_operations);
    if (ttl != 0) {
        operations.ttl = ttl;
    } else {
        operations.ttl = -2;
    }

    // by this time all checks are completed, as we assume the incoming data are clean,
    // so let's check if we are connected to Aerospike
    CHECK_ALL

    as_map_policy put_mode;
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    // Allocate callback data with proper initialization on heap
    callback_data* cb_data = new callback_data(env);

    // Get caller PID and create callback data
    if (!enif_self(env, &cb_data->caller_pid)) {
        delete cb_data;
        auto nifErrorCode = enif_make_int(env, ASPIKE_NIF_NO_CALLER_ID);
        auto aspikeErrorCode = enif_make_int(env, AEROSPIKE_OK);
        auto message = enif_make_string(env, "Failed to get caller PID", ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_error, enif_make_tuple3(env, nifErrorCode, aspikeErrorCode, message));
    }

    as_key record_key;
    as_key_init_str(&record_key, name_space.c_str(), set_name.c_str(), record_primary_key.c_str());

    for (uint i = 0; i < bins_amount; i++) {
        // each bin of
        // {<<"fcap_map">>, [<<"map_key_1">>, <<"map_value_1">>, 123, <<"map_key_2">>, <<"map_value_2">>, 456]}
        // will form a next map:
        // KEY_ORDERED_MAP('{"map_key_1":{"ttl":123, "value":"map_value_1", "wt":1766794574}, "map_key_2":{"ttl":456, "value":"map_value_2", "wt":1766794574}}')
        // which will be stored under a bin name "fcap_map". 

        ERL_NIF_TERM bins_head, bins_tail;
        enif_get_list_cell(env, bins, &bins_head, &bins_tail);
        // now, the bins_head points to something like {<<"fcap_map">>, [<<"map_key_1">>, <<"map_value_1">>, 123, <<"map_key_2">>, <<"map_value_2">>, 456]}

        int tuple_length;
        const ERL_NIF_TERM* bin_tuple = NULL;
        enif_get_tuple(env, bins_head, &tuple_length, &bin_tuple);
        // now bin_tuple points to something like {<<"fcap_map">>, [<<"map_key_1">>, <<"map_value_1">>, 123, <<"map_key_2">>, <<"map_value_2">>, 456]}

        ErlNifBinary erl_bin_name;
        enif_inspect_binary(env, bin_tuple[0], &erl_bin_name);
        std::string bin_name;
        bin_name.assign((const char*)erl_bin_name.data, erl_bin_name.size);
        // now bin_name has a value like "fcap_map"

        unsigned int bin_data_len;
        enif_get_list_length(env, bin_tuple[1], &bin_data_len);
        auto bin_data = bin_tuple[1];
        // bin_data points to something like [<<"map_key_1">>, <<"map_value_1">>, 123, <<"map_key_2">>, <<"map_value_2">>, 456]

        uint opnum = 0;
        for (uint k = 0; k < bin_data_len; k++) {
            ERL_NIF_TERM data_head;
            ERL_NIF_TERM data_tail;
            if (!enif_get_list_cell(env, bin_data, &data_head, &data_tail)) {
                break;
            }

            // the following code forms a map with structure like:
            // KEY_ORDERED_MAP(
            //     "map_key_1": {
            //         "ttl":123,
            //         "value":"map_value_1",
            //         "wt":1766794574
            //     },
            //     "map_key_2": {
            //         "ttl":456,
            //         "value":"map_value_2",
            //         "wt":1766794574
            //     }
            //     ...
            // )
            // the first-level map key name and the values of second-level 'value' and 'ttl'
            // keys are read one by one from 'bin_data' array with 'opnum' variable indicating
            // which value we are reading now

            if (opnum == 0) {
                // with optnum == 0 it's a first-level key name.
                // The value of this key will be another map, so let's create a context for
                // this map
                as_cdt_ctx* context = as_cdt_ctx_create(1);
                // save context for later removal
                cb_data->cdt_contexts.push_back(context);
                // getting first level key name
                ErlNifBinary erl_key_name;
                if (enif_inspect_binary(env, data_head, &erl_key_name)) {
                    // erl_key_name points to something like <<"map_key_1">>.
                    // Now, make a copy of the string on heap (because the erl_key_name localed on stack)
                    // so Aerospike will be able to free its memory once the 'key_name' variable will be destroyed.
                    uint8_t * copy_on_heap = (uint8_t *)malloc(sizeof(uint8_t) * erl_key_name.size);
                    memcpy(copy_on_heap, erl_key_name.data, erl_key_name.size);
                    if (!copy_on_heap) {
                        as_key_destroy(&record_key);
                        as_operations_destroy(&operations);
                        delete cb_data;
                        auto nifErrorCode = enif_make_int(env, ASPIKE_NIF_MEMORY_ALLOC_ERR);
                        auto aspikeErrorCode = enif_make_int(env, AEROSPIKE_OK);
                        auto message = enif_make_string(env, "Failed to allocate memory for erl_key_name", ERL_NIF_UTF8);
                        return enif_make_tuple2(env, erl_error, enif_make_tuple3(env, nifErrorCode, aspikeErrorCode, message));
                    }
                    // create aerospike string which will be freed by context on its removal
                    as_string* key_name = as_string_new((char *)copy_on_heap, true);
                    as_cdt_ctx_add_map_key_create(context, (as_val*)key_name, AS_MAP_KEY_ORDERED);
                }
                opnum++;
            } else if (opnum == 1) {
                // with optnum == 1 it's a value for 'value' key on second-level map
                ErlNifBinary erl_value_data;
                if (enif_inspect_binary(env, data_head, &erl_value_data)) {
                    // erl_value_data points to something like <<"map_value_1">>.
                    // Create aerospike string (a second level key name) which will be freed by context on its removal.
                    as_string* key_name = as_string_new_strdup("value");
                    // Make a copy of the data on heap (because the erl_value_data localed on stack)
                    // so Aerospike will be able to free its memory once the 'value_data' variable will be destroyed.
                    uint8_t * copy_on_heap = (uint8_t *)malloc(sizeof(uint8_t) * erl_value_data.size);
                    memcpy(copy_on_heap, erl_value_data.data, erl_value_data.size);
                    if (!copy_on_heap) {
                        as_key_destroy(&record_key);
                        as_operations_destroy(&operations);
                        delete cb_data;
                        auto nifErrorCode = enif_make_int(env, ASPIKE_NIF_MEMORY_ALLOC_ERR);
                        auto aspikeErrorCode = enif_make_int(env, AEROSPIKE_OK);
                        auto message = enif_make_string(env, "Failed to allocate memory for erl_value_data", ERL_NIF_UTF8);
                        return enif_make_tuple2(env, erl_error, enif_make_tuple3(env, nifErrorCode, aspikeErrorCode, message));
                    }
                    as_bytes* value_data = as_bytes_new_wrap(copy_on_heap, erl_value_data.size, true);
                    // next line creates a key 'value' in the map we created above in 'opnum == 1'
                    as_operations_map_put(&operations, bin_name.c_str(), cb_data->cdt_contexts.back(), &put_mode, (as_val*)key_name, (as_val*)value_data);
                }
                opnum++;
            } else if (opnum == 2) {
                // with optnum == 2 it's a value for 'ttl' key on second-level map
                long i64;
                if (enif_get_int64(env, data_head, &i64)) {
                    // i64 points to something like 123.
                    // Create aerospike string (a second level key name) which will be freed by context on its removal.
                    as_string* key_name = as_string_new_strdup("ttl");
                    as_integer* ttl_value = as_integer_new(i64);
                    // next line creates a key 'ttl' in the map we created above in 'opnum == 1'
                    as_operations_map_put(&operations, bin_name.c_str(), cb_data->cdt_contexts.back(), &put_mode, (as_val*)key_name, (as_val*)ttl_value);
                }

                // let's create a 'wt' key on second-level map
                // Create aerospike string (a second level key name) which will be freed by context on its removal.
                as_string* key_name = as_string_new_strdup("wt");
                auto now = std::chrono::system_clock::now().time_since_epoch();
                long timestamp = std::chrono::duration_cast<std::chrono::seconds>(now).count();
                as_integer* wt_value = as_integer_new(timestamp);
                // next line creates a key 'wt' in the map we created above in 'opnum == 1'
                as_operations_map_put(&operations, bin_name.c_str(), cb_data->cdt_contexts.back(), &put_mode, (as_val*)key_name, (as_val*)wt_value);

                opnum = 0;
            } else {
                break;
            }

            bin_data = data_tail;
        }
        bins = bins_tail;
    }

    as_error err;
    as_status status = aerospike_key_operate_async(as, &err, &policy, &record_key, &operations, cdt_put_async_callback, cb_data, NULL, NULL);

    ERL_NIF_TERM return_data;
    if (status != AEROSPIKE_OK) {
        // Failed to initiate async operation
        // Cleanup callback data
        delete cb_data;
        ERL_NIF_TERM error_msg;
        if (strlen(err.message) != 0) {
            error_msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        } else {
            error_msg = enif_make_string(env, "Unknown error occurred", ERL_NIF_UTF8);
        }
        auto nifErrorCode = enif_make_int(env, ASPIKE_NIF_OK);
        auto aspikeErrorCode = enif_make_int(env, err.code);
        ERL_NIF_TERM error_tuple = enif_make_tuple3(env, nifErrorCode, aspikeErrorCode, error_msg);

        return_data = enif_make_tuple2(env, erl_error, error_tuple);
    } else {
        return_data = enif_make_tuple2(env, erl_ok, enif_make_atom(env, "in_progress"));
    }

    as_key_destroy(&record_key);
    as_operations_destroy(&operations);

    return return_data;
}

// Callback function for async cdt_get operation
static void cdt_get_async_callback(as_error* err, as_record* record, void* udata, as_event_loop* event_loop) {
    ERL_NIF_TERM erl_ok = get_erl_ok();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM result_msg;

    callback_data* cb_data = (callback_data*)udata;

    if (err) {
        ERL_NIF_TERM error_msg;

        if (err->code == AEROSPIKE_ERR_NO_MORE_CONNECTIONS) {
            // Special handling for connection pool exhaustion
            error_msg = enif_make_string(cb_data->msg_env, "connection_pool_exhausted", ERL_NIF_UTF8);
        } else {
            // Regular error message
            if (strlen(err->message) != 0) {
                error_msg = enif_make_string(cb_data->msg_env, err->message, ERL_NIF_UTF8);
            } else {
                error_msg = enif_make_string(cb_data->msg_env, "Unknown error occurred", ERL_NIF_UTF8);
            }
        }
        auto nifErrorCode = enif_make_int(cb_data->msg_env, ASPIKE_NIF_OK);
        auto aspikeErrorCode = enif_make_int(cb_data->msg_env, err->code);
        ERL_NIF_TERM error_tuple = enif_make_tuple3(cb_data->msg_env, nifErrorCode, aspikeErrorCode, error_msg);

        result_msg = enif_make_tuple2(cb_data->msg_env, erl_error, error_tuple);
    } else {
        ERL_NIF_TERM response = aspike_dump_cdt_records(cb_data->msg_env, record);
        result_msg = enif_make_tuple2(cb_data->msg_env, erl_ok, response);
    }

    // Send message to calling Erlang process
    enif_send(NULL, &cb_data->caller_pid, cb_data->msg_env, result_msg);

    // Clean up callback data
    delete cb_data;
}

ERL_NIF_TERM aspike_nif_cdt_get_async(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary erl_namespace;
    if (!enif_inspect_binary(env, argv[0], &erl_namespace)) {
        return enif_make_badarg(env);
    }
    std::string name_space;
    name_space.assign((const char*)erl_namespace.data, erl_namespace.size);

    ErlNifBinary erl_set_name;
    if (!enif_inspect_binary(env, argv[1], &erl_set_name)) {
        return enif_make_badarg(env);
    }
    std::string set_name;
    set_name.assign((const char*)erl_set_name.data, erl_set_name.size);

    ErlNifBinary erl_primary_key;
    if (!enif_inspect_binary(env, argv[2], &erl_primary_key)) {
        return enif_make_badarg(env);
    }
    std::string record_primary_key;
    record_primary_key.assign((const char*)erl_primary_key.data, erl_primary_key.size);

    const ERL_NIF_TERM* erl_policy = NULL;
    int policy_length;
    long max_retries = 0;
    long socket_timeout = 0;
    long total_timeout = 0;
    int policyReadRC = enif_get_tuple(env, argv[3], &policy_length, &erl_policy);
    if (!policyReadRC || policy_length != 4) {
        return enif_make_badarg(env);
    }
    // note: sleep_between_retries is not used in async operations by c-client
    enif_get_long(env, erl_policy[0], &max_retries);
    enif_get_long(env, erl_policy[2], &socket_timeout);
    enif_get_long(env, erl_policy[3], &total_timeout);
    as_policy_read policy;
    as_policy_read_init(&policy);
    policy.base.max_retries = max_retries;
    policy.base.socket_timeout = socket_timeout;
    policy.base.total_timeout = total_timeout;

    CHECK_ALL

    // Allocate callback data with proper initialization on heap
    callback_data* cb_data = new callback_data(env);

    // Get caller PID and create callback data
    if (!enif_self(env, &cb_data->caller_pid)) {
        delete cb_data;
        auto nifErrorCode = enif_make_int(env, ASPIKE_NIF_NO_CALLER_ID);
        auto aspikeErrorCode = enif_make_int(env, AEROSPIKE_OK);
        auto message = enif_make_string(env, "Failed to get caller PID", ERL_NIF_UTF8);
        return enif_make_tuple2(env, erl_error, enif_make_tuple3(env, nifErrorCode, aspikeErrorCode, message));
    }

    as_key record_key;
    as_key_init_str(&record_key, name_space.c_str(), set_name.c_str(), record_primary_key.c_str());

    as_error err;
    as_status status = aerospike_key_get_async(as, &err, &policy, &record_key, cdt_get_async_callback, cb_data, NULL, NULL);
    
    ERL_NIF_TERM return_data;
    if (status != AEROSPIKE_OK) {
        // Failed to initiate async operation
        // Cleanup callback data
        delete cb_data;
        ERL_NIF_TERM error_msg;
        if (strlen(err.message) != 0) {
            error_msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        } else {
            error_msg = enif_make_string(env, "Unknown error occurred", ERL_NIF_UTF8);
        }
        auto nifErrorCode = enif_make_int(env, ASPIKE_NIF_OK);
        auto aspikeErrorCode = enif_make_int(env, err.code);
        ERL_NIF_TERM error_tuple = enif_make_tuple3(env, nifErrorCode, aspikeErrorCode, error_msg);

        return_data = enif_make_tuple2(env, erl_error, error_tuple);
    } else {
        return_data = enif_make_tuple2(env, erl_ok, enif_make_atom(env, "in_progress"));
    }

    as_key_destroy(&record_key);

    return return_data;
}
