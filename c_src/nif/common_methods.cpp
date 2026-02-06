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

using namespace std;

int64_t unix_ts() {
    return chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()
    ).count();
}

ERL_NIF_TERM aspike_dump_records(ErlNifEnv* env, const as_record* p_rec) {
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
                                             aspike_format_value_out(env, type, as_bin_get_value(p_bin)));
        res = enif_make_list_cell(env, cell, res);
    }

    as_record_iterator_destroy(&it);

    return res;
}

ERL_NIF_TERM aspike_dump_binary_records(ErlNifEnv* env, const as_record* p_rec) {
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
                                             aspike_format_value_out(env, type, as_bin_get_value(p_bin)));
        res = enif_make_list_cell(env, cell, res);
    }

    as_record_iterator_destroy(&it);

    return res;
}

ERL_NIF_TERM aspike_format_value_out(ErlNifEnv* env, as_val_t type, as_bin_value* val) {
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
            vector<ERL_NIF_TERM> erl_list;
            erl_list.reserve(len);

            using Callback = function<bool(as_val * val)>;

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
            vector<ERL_NIF_TERM> erl_list;
            erl_list.reserve(len * 2);

            const as_orderedmap* amap = (const as_orderedmap*)&val->map;
            as_orderedmap_iterator it;
            as_orderedmap_iterator_init(&it, amap);
            while (as_orderedmap_iterator_has_next(&it)) {
                long fccount = 0;
                const as_val* val = as_orderedmap_iterator_next(&it);
                as_pair* apr = as_pair_fromval(val);
                erl_list.push_back(aspike_get_binary_asval(env, as_pair_1(apr)));

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
                        vnt = aspike_get_binaryb_asval(env, as_pair_2(aprsm));
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
                        vnt = aspike_get_binary_asval(env, as_pair_2(aprsm));
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

ERL_NIF_TERM aspike_dump_cdt_records(ErlNifEnv* env, const as_record* p_rec) {
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

        ERL_NIF_TERM typeTerm = aspike_format_value_out(env, type, as_bin_get_value(p_bin));
        ERL_NIF_TERM cell = enif_make_tuple2(env, name_term, typeTerm);
        res = enif_make_list_cell(env, cell, res);
    }

    as_record_iterator_destroy(&it);
    return res;
}

ERL_NIF_TERM aspike_get_binary_asval(ErlNifEnv* env, const as_val * val) {
    ERL_NIF_TERM fcap_key;
    as_string *keystr = as_string_fromval(val);
    auto len = as_string_len(keystr);
    unsigned char * val_data;
    val_data = enif_make_new_binary(env, len, &fcap_key);
    memcpy(val_data, as_string_get(keystr), len);
    return fcap_key;
}

ERL_NIF_TERM aspike_get_binaryb_asval(ErlNifEnv* env, const as_val * val) {
    ERL_NIF_TERM fcap_key;
    as_bytes *keystr = as_bytes_fromval(val);
    auto len = as_bytes_size(keystr);
    unsigned char * val_data;
    val_data = enif_make_new_binary(env, len, &fcap_key);
    memcpy(val_data, as_bytes_get(keystr), len);
    return fcap_key;
}
