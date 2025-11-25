## Aerospike setup

Obviously you need a running instance of Aerospike. Currently, in the production we are running version 6, but for
development purposes it's OK to go with version 7. Version 8 was found having many differences from 7 and is
not yet tested. CE (Community Edition) version is fine for development. It's recommended to use docker
image to run Aerospike, but you can choose any. For reference, you can follow guides on this page:
https://aerospike.com/download/server/community/

For simplicity, run next command to download and run aerospike docker image:
```bash
docker run -d --name aerospike --ulimit nofile=65536:65536 -p 3000:3000 -p 3001:3001 -p 3002:3002 -p 3003:3003 aerospike:ce-7.1.0.0
```
This will download and store image and create a container under 'aerospike' name. Later you can reference this container
with commands like:
```bash
docker stop aerospike
docker start aerospike
docker logs aerospike # to get logs from Aerospike
docker exec -ti aerospike /bin/bash # to get shell inside container
```

You'll need to know the IP address of the aerospike instance, so run the command
```bash
docker inspect -f '{{.NetworkSettings.Networks.bridge.IPAddress}}' aerospike
```
and you should get something like `172.17.0.2`. If that command fails with `template parsing error` just run
`docker inspect aerospike` and try to find the `IPAddress` record manually.

If you need to talk to aerospike in AQL you can run the aql utility:
```bash
docker run -ti aerospike/aerospike-tools:latest aql -h 172.17.0.2
```

## Building aspike-port

Just run make:

```bash
$ make
```

## Run perf tests

To start the Aerospike client, run the erlang shell:
```bash
erl -pa _build/default/lib/*/ebin
```
and then run next code:
```erlang
application:set_env(aspike_port, host, "172.17.0.2").
application:set_env(aspike_port, psw, "").
application:set_env(aspike_port, port, 3000).
application:set_env(aspike_port, user, "").
aspike_nif:as_init().
aspike_nif:host_add().
aspike_nif:connect().
```

### Single process tests

To run a single producer process:
```erlang
aspike_nif_perf:sp_insert(<<"test">>, <<"rtb-gateway-fcap-users">>, 1_000_000, 3600, 0, 1_000_000_000_000, 0, 0).
```
it will insert 1_000_000 keys to namespace=test, set=rtb-gateway-fcap-users. Data TTL=3600 seconds. Delay between operations = 0ms. Initial key value = 1_000_000_000_000. (key will be 1_000_001_000_000 ... 1_000_001_999_999).
Function will return: {Number_of_success_operations, Number_of_errors}.

To run a single consumer process:
```erlang
aspike_nif_perf:sp_read(<<"test">>, <<"rtb-gateway-fcap-users">>, 1_000_000, 0, 1_000_000_000_000, 0, 0, 0).
```
it will read from namespace=test, set=rtb-gateway-fcap-users. Delay between operations = 0ms. Initial key value = 1_000_000_000_000. (key will be 1_000_001_000_000 ... 1_000_001_999_999).

To get the raw stats:
```erlang
aspike_nif_perf:dump_stats().
```
it will produce 2 files: /tmp/read_stats.txt and /tmp/insert_stats.txt

### Multi-process tests

```erlang
aspike_nif_perf:mp_insert(10, 1_000_000, 0).
aspike_nif_perf:mp_reads(20, 1_000_000, 0).
```

It will run 10 concurrent insert processes, each will insert 1000000 keys, with 0ms delay
and 20 concurrent read processes, each will read 1000000 keys with 0ms delay
