all: deps compile

deps:
	git submodule update --init
	git -C aerospike-client-c submodule update --init

compile: compile_aerospike_client compile_gateway_client

compile_aerospike_client:
	make -C aerospike-client-c EVENT_LIB=libev

compile_gateway_client:
	make -C c_src
