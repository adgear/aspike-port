#ifndef ASPIKE_NIF_H
#define ASPIKE_NIF_H

#define MAX_HOST_SIZE 1024
#define MAX_KEY_STR_SIZE 1024
#define MAX_NAMESPACE_SIZE 32	// based on current server limit
#define MAX_SET_SIZE 64			// based on current server limit
#define AS_BIN_NAME_MAX_SIZE 16
#define MAX_BINS_NUMBER 1024

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

#define RETURN_ERROR_WITH_MSG_IF(t_var, p_rec, err_code, err_msg) \
    if(t_var) { \
        rc = erl_error; \
        code = enif_make_int(env, int(err_code)); \
        msg = enif_make_string(env, err_msg, ERL_NIF_UTF8); \
        if(!p_rec) { \
            as_record_destroy(p_rec); \
        } \
        return enif_make_tuple3(env, rc, code, msg); \
    }

aerospike* get_aerospike ();
bool get_is_aerospike_initialised ();
bool get_is_connected ();
ERL_NIF_TERM get_erl_error ();
ERL_NIF_TERM get_erl_ok ();

#endif // ASPIKE_NIF_H
