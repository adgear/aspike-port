#include "sync_methods.h"

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

// helper functions declarations
static ERL_NIF_TERM dump_records(ErlNifEnv* env, const as_record* p_rec);
static ERL_NIF_TERM dump_binary_records(ErlNifEnv* env, const as_record* p_rec);
static ERL_NIF_TERM format_value_out(ErlNifEnv* env, as_val_t type, as_bin_value* val);
static ERL_NIF_TERM dump_cdt_records(ErlNifEnv* env, const as_record* p_rec);
static ERL_NIF_TERM get_binary_asval(ErlNifEnv* env, const as_val * val);
static ERL_NIF_TERM get_binaryb_asval(ErlNifEnv* env, const as_val * val);

ERL_NIF_TERM aspike_nif_cdt_put_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_key;
    unsigned int length;
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
    if (!enif_get_tuple(env, argv[5], &policy_length, &policy) || policy_length != 4) {
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
    as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());

    std::vector<as_cdt_ctx*> ctx_vec;
    as_operations ops;
    as_map_policy put_mode;
    // as_map_policy_set(&put_mode, AS_MAP_UNORDERED, AS_MAP_UPDATE);
    as_map_policy_set(&put_mode, AS_MAP_KEY_ORDERED, AS_MAP_UPDATE);

    std::vector<as_bytes*> bin_vec;
    for (uint i = 0; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        ErlNifBinary bin_bin;
        std::string bin_str, bin_str_val;
        int t_length;
        const ERL_NIF_TERM* tuple = NULL;
        // as_bytes as_bytes_val;
        unsigned int ts_length;

        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if (!enif_get_tuple(env, head, &t_length, &tuple) || t_length != 2) {
            return enif_make_badarg(env);
        }

        if (!enif_inspect_binary(env, tuple[0], &bin_bin)) {
            return enif_make_badarg(env);
        }
        bin_str.assign((const char*)bin_bin.data, bin_bin.size);

        if (!enif_is_list(env, tuple[1]) || !enif_get_list_length(env, tuple[1], &ts_length)) {
            return enif_make_badarg(env);
        }
        auto ts_list = tuple[1];
        as_operations_inita(&ops, ts_length + 1);
        if (ttl != 0) {
            ops.ttl = ttl;
        } else {
            ops.ttl = -2;
        }
        uint opnum = 0;
        ErlNifBinary bin_key, bin_val;
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
                ctx_vec.push_back(as_cdt_ctx_create(1));
                if (enif_inspect_binary(env, ts_head, &bin_key)) {
                    fcap_key.assign((const char*)bin_key.data, bin_key.size);
                    as_string_init(&key_str, (char*)fcap_key.c_str(), false);
                    as_cdt_ctx_add_map_key_create(ctx_vec.back(), (as_val*)&key_str, AS_MAP_KEY_ORDERED);
                }
                opnum++;
            } else if (opnum == 1) {
                // getting fcap value
                if (enif_inspect_binary(env, ts_head, &bin_val)) {
                    valuesk = "value";
                    as_string_init(&subkey1, (char*)valuesk.c_str(), false);
                    as_bytes_inita(&subval1, bin_val.size);
                    as_bytes_set(&subval1, 0, bin_val.data, bin_val.size);
                    as_operations_map_put(&ops, bin_str.c_str(), ctx_vec.back(), &put_mode, (as_val*)&subkey1, (as_val*)&subval1);
                }
                opnum++;

            } else if (opnum == 2) {
                // getting subkey ttl
                if (enif_get_int64(env, ts_head, &i64)) {
                    valuesk1 = "ttl";
                    as_string_init(&subkey2, (char*)valuesk1.c_str(), false);
                    as_integer_init(&subval2, i64);
                    as_operations_map_put(&ops, bin_str.c_str(), ctx_vec.back(), &put_mode, (as_val*)&subkey2, (as_val*)&subval2);
                }
                opnum = 0;
                // subkey write time
                auto now = std::chrono::system_clock::now().time_since_epoch();
                long wt = std::chrono::duration_cast<std::chrono::seconds>(now).count();
                valuesk2 = "wt";
                as_string_init(&subkey3, (char*)valuesk2.c_str(), false);
                as_integer_init(&subval3, wt);
                as_operations_map_put(&ops, bin_str.c_str(), ctx_vec.back(), &put_mode, (as_val*)&subkey3, (as_val*)&subval3);
            } else {
                break;
            }

            ts_list = ts_tail;
        }
    }

    // const as_policy_operate * policy;
    as_policy_operate p;
    as_policy_operate_init(&p);
    p.ttl = ttl;
    p.base.max_retries = max_retries;
    p.base.sleep_between_retries = sleep_between_retries;
    p.base.socket_timeout = socket_timeout;
    p.base.total_timeout = total_timeout;

    if (aerospike_key_operate(as, &err, &p, &key, &ops, NULL) != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "put", ERL_NIF_UTF8);
    }
    as_operations_destroy(&ops);
    as_key_destroy(&key);

    // destroy all contexts
    for (as_cdt_ctx* pctx : ctx_vec) {
        as_cdt_ctx_destroy(pctx);
    }

    return enif_make_tuple2(env, rc, msg);
}

ERL_NIF_TERM aspike_nif_cdt_get_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_key;
    std::string name_space, aspk_set, aspk_key;

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

    // {max_retries, sleep_between_retries, socket_timeout, total_timeout}
    const ERL_NIF_TERM* policy = NULL;
    int policy_length;
    long max_retries = 0;
    long sleep_between_retries = 0;
    long socket_timeout = 30000;
    long total_timeout = 1000;
    if (!enif_get_tuple(env, argv[3], &policy_length, &policy) || policy_length != 4) {
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

    if (aerospike_key_get(as, &err, NULL, &key, &p_rec) != AEROSPIKE_OK) {
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
}

ERL_NIF_TERM aspike_nif_cdt_delete_by_keys_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_key, bin_name;
    std::string name_space, aspk_set, aspk_key, bin_str;
    unsigned int length;

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

    if (!enif_inspect_binary(env, argv[3], &bin_name)) {
        return enif_make_badarg(env);
    }
    bin_str.assign((const char*)bin_name.data, bin_name.size);

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
    ops.ttl = AS_RECORD_NO_CHANGE_TTL;  // Preserve existing record TTL (-2)
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
            subkey_str.assign((const char*)subkey_term.data, subkey_term.size);
            as_arraylist_append_str(&remove_list, (char*)subkey_str.c_str());
            subkeys_num++;
        }
        list = tail;
    }
    if (subkeys_num == length) {
        as_operations_add_map_remove_by_key_list(&ops, bin_str.c_str(), (as_list*)&remove_list, AS_MAP_RETURN_NONE);
    }
    as_arraylist_destroy(&remove_list);

    if (aerospike_key_operate(as, &err, NULL, &key, &ops, NULL) != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
    } else {
        rc = erl_ok;
        msg = enif_make_string(env, "keys_deleted", ERL_NIF_UTF8);
    }
    as_operations_destroy(&ops);

    return enif_make_tuple2(env, rc, msg);
}

ERL_NIF_TERM aspike_nif_cdt_delete_by_keys_batch_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_name;
    std::string name_space, aspk_set, aspk_key, bin_str;
    unsigned int length;

    if (!enif_inspect_binary(env, argv[0], &bin_ns)) {
        return enif_make_badarg(env);
    }
    name_space.assign((const char*)bin_ns.data, bin_ns.size);

    if (!enif_inspect_binary(env, argv[1], &bin_set)) {
        return enif_make_badarg(env);
    }
    aspk_set.assign((const char*)bin_set.data, bin_set.size);

    if (!enif_inspect_binary(env, argv[2], &bin_name)) {
        return enif_make_badarg(env);
    }
    bin_str.assign((const char*)bin_name.data, bin_name.size);

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
        if (!enif_get_tuple(env, head, &ksk_length, &ksk_tuple) || ksk_length != 2) {
            return enif_make_badarg(env);
        }

        if (!enif_inspect_binary(env, ksk_tuple[0], &bin_bin)) {
            return enif_make_badarg(env);
        }
        bin_str_list[i].assign((const char*)bin_bin.data, bin_bin.size);

        unsigned int ts_length;
        if (!enif_is_list(env, ksk_tuple[1]) || !enif_get_list_length(env, ksk_tuple[1], &ts_length)) {
            return enif_make_badarg(env);
        }

        abwrs[i] = as_batch_write_reserve(&recs);
        as_key_init_str(&(abwrs[i]->key), name_space.c_str(), aspk_set.c_str(), bin_str_list[i].c_str());

        auto ts_list = ksk_tuple[1];
        std::vector<std::string> bin_str_sk_list(ts_length);
        as_arraylist_init(&(rval[i]), ts_length, ts_length);
        for (uint j = 0; j < ts_length; j++) {
            ERL_NIF_TERM skl_head;
            ERL_NIF_TERM skl_tail;
            ErlNifBinary skl_bin_bin;
            if (!enif_get_list_cell(env, ts_list, &skl_head, &skl_tail)) {
                break;
            }

            if (!enif_inspect_binary(env, skl_head, &skl_bin_bin)) {
                return enif_make_badarg(env);
            }
            bin_str_sk_list[j].assign((const char*)skl_bin_bin.data, skl_bin_bin.size);
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

    as->config.policies.batch_write.ttl = 1000;
    as_error err;
    as_status status = aerospike_batch_write(as, &err, NULL, &recs);

    std::vector<ERL_NIF_TERM> erl_list;
    for (auto aitr : abwrs) {
        erl_list.push_back(enif_make_int(env, aitr->result));
        /*if(aitr->result == AEROSPIKE_OK){
            std::cout << "WOPOK! \r\n";
        }else{
            std::cout << "WOPNOK!: " << std::to_string(aitr->result) << "\r\n";
        }*/
    }
    auto opsl = enif_make_list_from_array(env, erl_list.data(), erl_list.size());

    for (auto vitr : wopsl) {
        as_operations_destroy(&vitr);
    }

    as_batch_records_destroy(&recs);
    if (status != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    } else {
        rc = erl_ok;
        return enif_make_tuple2(env, rc, opsl);
    }
}

ERL_NIF_TERM aspike_nif_segment_tag_get_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_key, bin_columns;
    std::string name_space, aspk_set, aspk_key, aspk_columns;

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

    if (!enif_inspect_binary(env, argv[3], &bin_columns)) {
        return enif_make_badarg(env);
    }
    aspk_columns.assign((const char*)bin_columns.data, bin_columns.size);

    CHECK_ALL

    ERL_NIF_TERM rc, code, msg;
    as_error err;
    as_key key;
    as_record* p_rec = NULL;

    const char* bins[] = {aspk_columns.c_str(), NULL};

    as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());

    RETURN_ERROR_WITH_MSG_IF(
        (aerospike_key_select(as, &err, NULL, &key, bins, &p_rec) != AEROSPIKE_OK),
        p_rec,
        int(err.code),
        err.message)

    RETURN_ERROR_WITH_MSG_IF(
        (p_rec == NULL),
        p_rec,
        int(AEROSPIKE_ERR),
        "NULL p_rec")

    as_bin_value* val = as_record_get(p_rec, bins[0]);

    RETURN_ERROR_WITH_MSG_IF(
        (val == NULL),
        p_rec,
        int(AEROSPIKE_ERR),
        "NULL val - internal error")

    RETURN_ERROR_WITH_MSG_IF(
        (as_val_type(val) != AS_STRING && as_val_type(val) != AS_MAP),
        p_rec,
        int(AEROSPIKE_ERR),
        "Non-string or non-map bin - internal error")

    ERL_NIF_TERM res;

    if (as_val_type(val) == AS_STRING) {
        uint8_t* bin_as_str = (uint8_t*)val->string.value;
        auto len = val->string.len;

        unsigned char* val_data;
        val_data = enif_make_new_binary(env, len, &res);
        memcpy(val_data, bin_as_str, len);
    } else if (as_val_type(val) == AS_MAP) {
        as_map* amap = (as_map*)val;
        uint32_t size = as_map_size(amap);
        ERL_NIF_TERM keys[size];
        ERL_NIF_TERM vals[size];

        as_orderedmap_iterator it;
        as_orderedmap_iterator_init(&it, (as_orderedmap*)amap);

        uint32_t idx = 0;
        while (as_orderedmap_iterator_has_next(&it)) {
            as_pair* pair = as_pair_fromval(as_orderedmap_iterator_next(&it));
            keys[idx] = get_binary_asval(env, as_pair_1(pair));
            vals[idx] = get_binary_asval(env, as_pair_2(pair));
            idx++;
        }
        as_orderedmap_iterator_destroy(&it);

        ERL_NIF_TERM map_term;
        enif_make_map_from_arrays(env, keys, vals, size, &map_term);
        res = map_term;
    }

    rc = erl_ok;
    code = enif_make_int(env, int(AEROSPIKE_OK));
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }
    return enif_make_tuple3(env, rc, code, res);
}

ERL_NIF_TERM aspike_nif_key_select_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

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

    const char* bins[MAX_BINS_NUMBER];
    for (; i < length; i++) {
        ERL_NIF_TERM head;
        ERL_NIF_TERM tail;
        char bin[AS_BIN_NAME_MAX_SIZE] = {0};
        if (!enif_get_list_cell(env, list, &head, &tail)) {
            break;
        }
        if (!enif_get_string(env, head, bin, MAX_KEY_STR_SIZE, ERL_NIF_UTF8)) {
            break;
        }
        bins[i] = (const char*)cf_strdup(bin);
        list = tail;
    }
    bins[i] = NULL;

    as_error err;
    as_key key;
    as_key_init_str(&key, name_space, set, key_str);

    if (aerospike_key_select(as, &err, NULL, &key, bins, &p_rec) != AEROSPIKE_OK) {
        rc = erl_error;
        msg = enif_make_string(env, err.message, ERL_NIF_UTF8);
        return enif_make_tuple2(env, rc, msg);
    }

    msg = dump_records(env, p_rec);
    rc = erl_ok;
    for (uint j = 0; j < i; j++) {
        cf_free((void*)bins[j]);
    }
    if (p_rec != NULL) {
        as_record_destroy(p_rec);
    }

    return enif_make_tuple2(env, rc, msg);
}

ERL_NIF_TERM aspike_nif_binary_get_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

    ErlNifBinary bin_ns, bin_set, bin_key;
    std::string name_space, aspk_set, aspk_key;

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

    CHECK_ALL

    ERL_NIF_TERM rc, msg;
    as_error err;
    as_key key;
    as_record* p_rec = NULL;

    as_key_init_str(&key, name_space.c_str(), aspk_set.c_str(), aspk_key.c_str());

    if (aerospike_key_get(as, &err, NULL, &key, &p_rec) != AEROSPIKE_OK) {
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
}

ERL_NIF_TERM aspike_nif_key_get_sync(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]) {
    static aerospike* as = get_aerospike();
    bool is_aerospike_initialised = get_is_aerospike_initialised();
    bool is_connected = get_is_connected();
    ERL_NIF_TERM erl_error = get_erl_error();
    ERL_NIF_TERM erl_ok = get_erl_ok();

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

    if (aerospike_key_get(as, &err, NULL, &key, &p_rec) != AEROSPIKE_OK) {
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

static ERL_NIF_TERM dump_records(ErlNifEnv* env, const as_record* p_rec) {
    ERL_NIF_TERM res;

    if (p_rec->key.valuep) {
        char* key_val_as_str = as_val_tostring(p_rec->key.valuep);
        res = enif_make_string(env, key_val_as_str, ERL_NIF_UTF8);
        cf_free(key_val_as_str);
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
                                             format_value_out(env, type, as_bin_get_value(p_bin)));
        res = enif_make_list_cell(env, cell, res);
    }

    as_record_iterator_destroy(&it);

    return res;
}

static ERL_NIF_TERM dump_binary_records(ErlNifEnv* env, const as_record* p_rec) {
    ERL_NIF_TERM res;

    if (p_rec->key.valuep) {
        unsigned char* key_data;
        char* key_val_as_str = as_val_tostring(p_rec->key.valuep);
        auto len = strlen(key_val_as_str);

        key_data = enif_make_new_binary(env, len, &res);
        memcpy(key_data, key_val_as_str, len);
        // res = enif_make_string(env, key_val_as_str, ERL_NIF_UTF8);
        cf_free(key_val_as_str);
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
                                             format_value_out(env, type, as_bin_get_value(p_bin)));
        res = enif_make_list_cell(env, cell, res);
    }

    as_record_iterator_destroy(&it);

    return res;
}

static ERL_NIF_TERM format_value_out(ErlNifEnv* env, as_val_t type, as_bin_value* val) {
    switch (type) {
        case AS_INTEGER:
            return enif_make_int64(env, val->integer.value);
        case AS_STRING:
        case AS_BYTES: {
            as_bytes asbval = val->bytes;
            uint8_t* bin_as_str = as_bytes_get(&asbval);
            auto len = asbval.size;

            unsigned char* val_data;
            ERL_NIF_TERM res;
            val_data = enif_make_new_binary(env, len, &res);
            memcpy(val_data, bin_as_str, len);
            return res;
        } break;
        case AS_LIST: {
            auto len = as_list_size(&val->list);
            std::vector<ERL_NIF_TERM> erl_list;
            erl_list.reserve(len);

            using Callback = std::function<bool(as_val * val)>;

            Callback lambda = [&erl_list, env](as_val* val) -> bool {
                if (!val) return false;
                as_integer* intval = as_integer_fromval(val);
                assert(intval);
                erl_list.push_back(enif_make_int64(env, as_integer_get(intval)));
                return true;
            };

            as_list_foreach(&val->list, [](as_val* val, void* ctx) -> bool { return (*(reinterpret_cast<Callback*>(ctx)))(val); }, &lambda);

            return enif_make_list_from_array(env, erl_list.data(), len);
        } break;
        case AS_MAP: {
            auto len = as_map_size((as_map*)(&val->map));
            std::vector<ERL_NIF_TERM> erl_list;
            erl_list.reserve(len * 2);

            const as_orderedmap* amap = (const as_orderedmap*)&val->map;
            as_orderedmap_iterator it;
            as_orderedmap_iterator_init(&it, amap);
            while (as_orderedmap_iterator_has_next(&it)) {
                long fccount = 0;
                const as_val* val = as_orderedmap_iterator_next(&it);
                as_pair* apr = as_pair_fromval(val);
                erl_list.push_back(get_binary_asval(env, as_pair_1(apr)));

                const as_orderedmap* vmap = (const as_orderedmap*)as_map_fromval(as_pair_2(apr));
                as_orderedmap_iterator iti_int;
                as_orderedmap_iterator_init(&iti_int, vmap);
                ERL_NIF_TERM vnt = enif_make_atom(env, "undefined");
                ERL_NIF_TERM ttlsm = enif_make_int64(env, 0);
                ERL_NIF_TERM writetime = enif_make_int64(env, 0);
                while (as_orderedmap_iterator_has_next(&iti_int)) {
                    const as_val* valsm = as_orderedmap_iterator_next(&iti_int);
                    as_pair* aprsm = as_pair_fromval(valsm);
                    if (as_pair_2(aprsm)->type == 9) {
                        vnt = get_binaryb_asval(env, as_pair_2(aprsm));
                        fccount++;
                    } else if (as_pair_2(aprsm)->type == 3) {
                        auto smkey = as_string_get((as_string*)as_pair_1(aprsm));
                        if (strcmp(smkey, "ttl") == 0) {
                            ttlsm = enif_make_int64(env, as_integer_get((as_integer*)as_pair_2(aprsm)));
                        } else if (strcmp(smkey, "wt") == 0) {
                            writetime = enif_make_int64(env, as_integer_get((as_integer*)as_pair_2(aprsm)));
                        }
                        fccount++;
                    } else if (as_pair_2(aprsm)->type == 4) {
                        vnt = get_binary_asval(env, as_pair_2(aprsm));
                        fccount++;
                    }
                }
                as_orderedmap_iterator_destroy(&iti_int);
                if ((fccount == 2) || (fccount == 3)) {
                    erl_list.push_back(enif_make_tuple3(env, vnt, ttlsm, writetime));
                }
            }
            as_orderedmap_iterator_destroy(&it);
            if (erl_list.size() == 0) {
                return enif_make_list(env, 0);
            } else {
                return enif_make_list_from_array(env, erl_list.data(), erl_list.size());
            }
        } break;
        default:
            char* val_as_str = as_val_tostring(val);
            ERL_NIF_TERM res = enif_make_string(env, as_val_tostring(val), ERL_NIF_UTF8);
            cf_free(val_as_str);
            return res;
    }
}

static ERL_NIF_TERM dump_cdt_records(ErlNifEnv* env, const as_record* p_rec) {
    ERL_NIF_TERM res;
    if (p_rec->key.valuep) {
        unsigned char* key_data;
        char* key_val_as_str = as_val_tostring(p_rec->key.valuep);
        auto len = strlen(key_val_as_str);

        key_data = enif_make_new_binary(env, len, &res);
        memcpy(key_data, key_val_as_str, len);
        // res = enif_make_string(env, key_val_as_str, ERL_NIF_UTF8);
        cf_free(key_val_as_str);
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

        ERL_NIF_TERM typeTerm = format_value_out(env, type, as_bin_get_value(p_bin));
        ERL_NIF_TERM cell = enif_make_tuple2(env, name_term, typeTerm);
        res = enif_make_list_cell(env, cell, res);
    }

    as_record_iterator_destroy(&it);
    return res;
}

static ERL_NIF_TERM get_binary_asval(ErlNifEnv* env, const as_val * val) {
    ERL_NIF_TERM fcap_key;
    as_string *keystr = as_string_fromval(val);
    auto len = as_string_len(keystr);
    unsigned char * val_data;
    val_data = enif_make_new_binary(env, len, &fcap_key);
    memcpy(val_data, as_string_get(keystr), len);
    return fcap_key;
}

static ERL_NIF_TERM get_binaryb_asval(ErlNifEnv* env, const as_val * val) {
    ERL_NIF_TERM fcap_key;
    as_bytes *keystr = as_bytes_fromval(val);
    auto len = as_bytes_size(keystr);
    unsigned char * val_data;
    val_data = enif_make_new_binary(env, len, &fcap_key);
    memcpy(val_data, as_bytes_get(keystr), len);
    return fcap_key;
}
