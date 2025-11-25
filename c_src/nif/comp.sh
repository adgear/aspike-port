# ERTS_INCLUDE_DIR=$(erl -noshell -eval "io:format(\"~ts/erts-~ts/include/\", [code:root_dir(), erlang:system_info(version)])." -s init stop)
# echo $ERTS_INCLUDE_DIR
ERTS_INCLUDE_DIR=/usr/local/Cellar/erlang/26.0.2/lib/erlang/erts-14.0.2/include/
# ERL_INTERFACE_INCLUDE_DIR=$(erl -noshell -eval "io:format(\"~ts\", [code:lib_dir(erl_interface, include)])." -s init stop)
# echo $ERL_INTERFACE_INCLUDE_DIR
ERL_INTERFACE_INCLUDE_DIR=/usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/include
# ERL_INTERFACE_LIB_DIR=$(erl -noshell -eval "io:format(\"~ts\", [code:lib_dir(erl_interface, lib)])." -s init stop)
# echo $ERL_INTERFACE_LIB_DIR
ERL_INTERFACE_LIB_DIR=/usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/lib

CFILES="aspike_nif.c"

OUTPUT="-o ../priv/aspike_nif.so"

INCLUDES="-I $ERTS_INCLUDE_DIR -I $ERL_INTERFACE_INCLUDE_DIR $CFILES"

# CC_FLAGS="-std=gnu99 -g -Wall -fPIC -O3 -fno-common -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_REENTRANT -D_GNU_SOURCE  -march=nocona -D_DARWIN_UNLIMITED_SELECT"

# CC_FLAGS="-DNIF -O0 -g -std=gnu11 -bundle -flat_namespace -undefined suppress" 
CC_FLAGS="-DNIF -O0 -g  -bundle  -undefined suppress" 

cmd="gcc  $CC_FLAGS  $INCLUDES  $OUTPUT -lm -fPIC -L $ERL_INTERFACE_LIB_DIR -lei"
echo $cmd
$cmd

# S="gcc  -DNIF -O0 -g -std=gnu11   -bundle -flat_namespace -undefined suppress  $INCLUDES  $OUTPUT -lm -fPIC -L $ERL_INTERFACE_LIB_DIR -lei"

# cc -O3 -std=gnu11 -finline-functions -Wall -Wmissing-prototypes -fPIC -I /usr/local/Cellar/erlang/26.0.2/lib/erlang/erts-14.0.2/include/ -I /usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/include -I "../be-tree/src" -c -o /Users/a.rodionov/adgear/erl-be-tree/c_src/betree.o /Users/a.rodionov/adgear/erl-be-tree/c_src/betree.c

# cc -DNIF -O3 -g -std=gnu11 -Wall -Wextra -Wshadow -Wfloat-equal -Wundef -Wcast-align -Wwrite-strings -Wunreachable-code -Wformat=2 -Wswitch-enum -Wswitch-default -Winit-self -Wno-strict-aliasing -I /usr/local/Cellar/erlang/26.0.2/lib/erlang/erts-14.0.2/include/ -I /usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/include -c -o src/value.o src/value.c -lm -fPIC -L /usr/local/Cellar/erlang/26.0.2/lib/erlang/lib/erl_interface-5.4/lib -lei


