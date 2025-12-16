all: deps compile

deps:
	cd aerospike-client-c && git submodule update --init

compile: compile_aerospike_client compile_gateway_client compile_erl

compile_aerospike_client:
	make -C aerospike-client-c EVENT_LIB=libev

compile_gateway_client:
	make -C c_src

compile_erl:
	@rebar3 compile

clean:
	@rebar3 clean
