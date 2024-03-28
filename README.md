### buld

you are need aerospike client installed on your machine, something like this (for Ubuntu/Debian):
```bash
wget https://download.aerospike.com/artifacts/aerospike-client-c/6.5.1/aerospike-client-c-libev_6.5.1_debian11_x86_64.tgz
tar xvf aerospike-client-c-libev_6.5.1_debian11_x86_64.tgz
cd aerospike-client-c-libev_6.5.1_debian11_x86_64
dpkg -i aerospike-client-c-libev-devel_6.5.1-debian11_amd64.deb aerospike-client-c-libev_6.5.1-debian11_amd64.deb
```
after just run rebar3:

```bash
./rebar3 compile
```

### run perf tests
to start aspike client run these commands in erlang shell

```erlang
application:set_env(aspike_port, host, "Aerospike-discovery-node-ip").
application:set_env(aspike_port, psw, "Aerospike-password").
application:set_env(aspike_port, port, 3000).
application:set_env(aspike_port, user, "Aerospike-username").
aspike_nif:as_init().
aspike_nif:host_add().
aspike_nif:connect().
```

#### single process tests
to run single writing process: 
```erlang
aspike_nif_perf:sp_insert(<<"global-store">>, <<"rtb-gateway-fcap-users">>, 1_000_000, 3600, 0, 1_000_000_000_000, 0, 0).
```
it will insert 1_000_000 keys to namespace=global-store, set=rtb-gateway-fcap-users. Data TTL=3600 seconds. Delay between operations = 0ms. Initial key value = 1_000_000_000_000. (key will be 1_000_001_000_000 ... 1_000_001_999_999).
Function will return: {Number_of_success_operations, Number_of_errors}.

to run single read process:
```erlang
aspike_nif_perf:sp_read(<<"global-store">>, <<"rtb-gateway-fcap-users">>, 1_000_000, 0, 1_000_000_000_000, 0, 0, 0).
```
it will read from namespace=global-store, set=rtb-gateway-fcap-users. Delay between operations = 0ms. Initial key value = 1_000_000_000_000. (key will be 1_000_001_000_000 ... 1_000_001_999_999).

To get raw stats:
```erlang
aspike_nif_perf:dump_stats().
```
it will produce 2 files: /tmp/read_stats.txt and /tmp/insert_stats.txt

#### multi-process tests

```erlang
aspike_nif_perf:mp_insert(10, 1_000_000, 0).
aspike_nif_perf:mp_reads(20, 1_000_000, 0).
```

it will run 10 concurrent insert processes, each will insert 1000000 keys, with 0ms delay
and 20 concurrent read processes, each will read 1000000 keys with 0ms delay
