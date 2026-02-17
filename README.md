## Aerospike setup

Obviously you need a running instance of Aerospike. Currently, in the production we are running version 6, but for
development purposes it's OK to go with version 7. Version 8 was found having many differences from 7 and is
not yet tested. CE (Community Edition) version is fine for development. It's recommended to use docker
image to run Aerospike, but you can choose any. For reference, you can follow guides on this page:
 * https://aerospike.com/download/server/community/
 * https://hub.docker.com/r/aerospike/aerospike-server

Run next command to download and run aerospike docker image:
```bash
docker run -d --rm --name aerospike_test --ulimit nofile=65536:65536 -p 3000:3000 -p 3001:3001 -p 3002:3002 -p 3003:3003 aerospike:ce-7.1.0.0
```
This will download and store image and create a container under 'aerospike_test' name. Later you can reference this container with commands like:
```bash
docker stop aerospike_test
docker start aerospike_test
docker logs aerospike_test # to get logs from Aerospike
docker exec -ti aerospike_test /bin/bash # to get shell inside container
```

If you need to talk to aerospike in AQL you can run the aql utility:
```bash
docker run --rm -ti aerospike/aerospike-tools:latest aql -h 127.0.0.1
```
and there you can run sample queries like
```sql
SELECT * FROM test.rtb_setname
```
to see all records from that namespace/set, or
```sql
SELECT * FROM test.rtb_setname WHERE PK = 'user_defined_PK'
```
to see specific record that namespace/set.

If you need to run AS Admin (ASADM) you can run next command:
```bash
docker run --rm -ti aerospike/aerospike-tools:latest asadm -h 127.0.0.1
```

## Building aspike-port

Just run make:

```bash
$ rebar3 compile
```

Here's the command you can use to speedup compilation and testing the code:
```
rebar3 compile && erl -pa _build/default/lib/aspike_port/ebin -s aspike_nif_test quick_test
```

## Run perf tests

To test Aerospike client, run the erlang shell:
```bash
erl -pa _build/default/lib/aspike_port/ebin
```
and then run next code to initialize NIF connection:
```erlang
application:set_env(aspike_port, host, "127.0.0.1").
application:set_env(aspike_port, port, 3000).
application:set_env(aspike_port, user, "").
application:set_env(aspike_port, psw, "").
aspike_nif:as_init().
aspike_nif:host_add().
aspike_nif:connect().
```
Or you can run next command to open the shell with connection initialized based on hardcoded values in the
function aspike_nif_test:init():
```
erl -pa _build/default/lib/aspike_port/ebin -s aspike_nif_test init
```

Next you can run very basic commands just to make sure the connection working and aerospike is able to
accept and store the data:
```erlang
aspike_nif:cdt_put(<<"test">>, <<"rtb_setname">>, <<"user_defined_PK">>, [{<<"fcap_map">>, [<<"map_key_1">>, <<"map_value_1">>, 123, <<"map_key_2">>, <<"map_value_2">>, 456]}], 300).
aspike_nif:cdt_get(<<"test">>, <<"rtb_setname">>, <<"user_defined_PK">>).
```

### Single process tests

To run a single producer process:
```erlang
aspike_nif_test:sp_insert(<<"test">>, <<"rtb_setname">>, 1_000_000, 3600, 0, 1_000_000_000_000, 0, 0).
```
it will insert 1_000_000 keys to namespace=test, set=rtb_setname. Data TTL=3600 seconds. Delay between operations = 0ms. Initial key value = 1_000_000_000_000. (key will be 1_000_001_000_000 ... 1_000_001_999_999).
Function will return: {Number_of_success_operations, Number_of_errors}.

To run a single consumer process:
```erlang
aspike_nif_test:sp_read(<<"test">>, <<"rtb_setname">>, 1_000_000, 0, 1_000_000_000_000, 0, 0, 0).
```
it will read from namespace=test, set=rtb_setname. Delay between operations = 0ms. Initial key value = 1_000_000_000_000. (key will be 1_000_001_000_000 ... 1_000_001_999_999).

To get the raw stats:
```erlang
aspike_nif_test:dump_stats().
```
it will produce 2 files: /tmp/read_stats.txt and /tmp/insert_stats.txt

### Multi-process tests

```erlang
aspike_nif_test:mp_insert(40, 1_000_000, 0).
aspike_nif_test:mp_reads(40, 1_000_000, 0).
```
It will run 10 concurrent insert processes, each will insert 1000000 keys, with 0ms delay
and 20 concurrent read processes, each will read 1000000 keys with 0ms delay.

To run the stress test with different amount of clients you may use function
```erlang
aspike_nif_test:stress_test().
```
This function runs certain amount of operations with different amount of parallel clients and measures the operation times.
Then it outputs the results to the console and writes the same data to json file in the root folder of this project.
You might want to edit function `stress_test()` to define some parameters. The main are:
- **AmountsOfClients** - number of clients to run in parallel. For instance, [5, 10] means first 5 clients will query database in parallel, and after they fnish the same will be repeated with 10 parallel clients.
- **AmountOfOps** - amount operations each client will do. Not all clients combined, but each client.
- **Command** - the command to run. This variable basically defines the `ActionFunc` variable, a function which does the actual operation.
- **ActionFunc** - the test function which defined what to do on each 'Operation'. Basically you want to keep it as simple as possible, like one operation per time, but you can add combination of command, like cdt_put followed by cdt_get. And if you do this keep in mind that Aerospike may be running in eventual consistency mode (check your mode by reading Aerospike docs how to do that). The function defined in `ActionFunc` accepts `Counter` variable which allows you to build unique primary key or any other data to be distinct from the other operation.
- **TestName** - variable defines so called 'test name', but basically it's going to be the name of the JSON file.

### Memory leak test
You can use next function:
```erlang
aspike_nif_test:memory_leak_test().
```
and you want to define in `ActionFunc` all code you are going to cover. By default all async functions are covered as of now. Usually it's OK to leave test running or couple hours.