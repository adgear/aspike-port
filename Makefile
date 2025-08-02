all: compile

compile:
	make -C aerospike-client-c
	@rebar3 $@

clean:
	@rebar3 $@
