#!/bin/sh

# ERL_INTERFACE_INCLUDE_DIR=$(erl -noshell -eval "io:format(\"~ts\", [code:lib_dir(erl_interface, include)])." -s init stop)
# echo $ERL_INTERFACE_INCLUDE_DIR
ERL_INTERFACE_INCLUDE_DIR="/usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/include"

# ERL_INTERFACE_LIB_DIR=$(erl -noshell -eval "io:format(\"~ts\", [code:lib_dir(erl_interface, lib)])." -s init stop)
# echo $ERL_INTERFACE_LIB_DIR
ERL_INTERFACE_LIB_DIR="/usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/lib"

AERO_BASE="/Users/a.rodionov/prj/aerospiking/aerospike-client-c"

CFILES="complex.c rw_command.c ei.c aerocalls.c foobar.c"
INCLUDES="-I$ERL_INTERFACE_INCLUDE_DIR -I/usr/local/opt/libevent/include -I/usr/local/opt/openssl/include -I$AERO_BASE/target/Darwin-x86_64/include"

# CC_FLAGS="-O0 -g -std=gnu11 -lm -fPIC"
CC_FLAGS="-std=gnu99 -g -Wall -fPIC -O3 -fno-common -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE  -march=nocona -D_DARWIN_UNLIMITED_SELECT"

LD_FLAGS="-L$ERL_INTERFACE_LIB_DIR -lei -L/usr/local/lib -L/usr/local/opt/openssl/lib -lev -lssl -lcrypto  -lpthread -lm -lz  -lm -lz $AERO_BASE/target/Darwin-x86_64/lib/libaerospike.a"

S="gcc $CC_FLAGS -o ../../priv/aspike_port $CFILES $INCLUDES  $LD_FLAGS"
echo $S
eval $S  

# cc -std=gnu99 -g -Wall -fPIC -O3 -fno-common -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE  -march=nocona -D_DARWIN_UNLIMITED_SELECT -I/usr/local/opt/libevent/include -I/usr/local/opt/openssl/include -I../../../target/Darwin-x86_64/include -I../../utils/src/include -DAS_USE_LIBEV -o target/obj/example.o -c src/main/example.c
# cc -o target/example target/obj/example.o target/obj/example_utils.o ../../../target/Darwin-x86_64/lib/libaerospike.a  -L/usr/local/lib -L/usr/local/opt/openssl/lib -lev -lssl -lcrypto  -lpthread -lm -lz  -lm -lz