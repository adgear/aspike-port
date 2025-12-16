#ifndef ASYNC_METHODS_H
#define ASYNC_METHODS_H

#include <erl_nif.h>

// Async method declarations
ERL_NIF_TERM aspike_nif_cdt_put_async (ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[]);

#endif // ASYNC_METHODS_H
