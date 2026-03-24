#ifndef COMMON_METHODS_H
#define COMMON_METHODS_H

int64_t unix_ts();
ERL_NIF_TERM aspike_dump_records(ErlNifEnv* env, const as_record* p_rec);
ERL_NIF_TERM aspike_dump_binary_records(ErlNifEnv* env, const as_record* p_rec);
ERL_NIF_TERM aspike_format_value_out(ErlNifEnv* env, as_val_t type, as_bin_value* val);
ERL_NIF_TERM aspike_dump_cdt_records(ErlNifEnv* env, const as_record* p_rec);
ERL_NIF_TERM aspike_get_binary_from_asval(ErlNifEnv* env, const as_val * val);

#endif // COMMON_METHODS_H