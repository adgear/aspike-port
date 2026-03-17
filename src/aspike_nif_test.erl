-module(aspike_nif_test).

-export([
    init/0,
    quick_test/0,
    stress_test/0,
    memory_leak_test/0,
    get_api_mode/1,

    sp_insert/9,
    sp_insert/8,
    pool_insert/8,
    sp_insert/5,
    sp_insert/2,
    sp_insert/1,
    mp_insert/3,
    sp_read/2,
    sp_read/8,
    mp_reads/3,
    mp_port_insert/3,

    rand_read/2,
    rand_read/8,
    mp_rand_reads/3,
    infinite_test/4,

    test_insertion/5,
    test_reading/3,
    dump_stats/0,

    cdt_insert_test/5,
    cdt_insert_test_mk/5,
    cdt_del_test/5,
    cdt_del_batch_test/5,
    cdt_get_test/4,
    pool_cdt_insert/1,
    pool_cdt_read/1
]).

-define(INIT_FLAG, aspike_nif_test_init_done).
-define(FCAP_BIN, <<"fcap_map">>).
-define(ASPIKE_DEFAULT_POLICY, {3, 250, 30000, 1000}).

-define(SYNC_MIN_CONNECTION, 50).
-define(SYNC_MAX_CONNECTION, 100).
-define(ASYNC_MIN_CONNECTION, 50).
-define(ASYNC_MAX_CONNECTION, 300).
-define(EVENT_LOOPS_AMOUNT, 1).

%get_api_mode(_Operation) -> sync.
get_api_mode(_Operation) -> async.

init() ->
    case persistent_term:get(?INIT_FLAG, false) of
        false ->
            application:set_env(aspike_port, host, "127.0.0.1"),
            application:set_env(aspike_port, port, 3000),
            application:set_env(aspike_port, user, ""),
            application:set_env(aspike_port, psw, ""),
            aspike_nif:set_connections_per_node(
                ?SYNC_MIN_CONNECTION,
                ?SYNC_MAX_CONNECTION,
                ?ASYNC_MIN_CONNECTION,
                ?ASYNC_MAX_CONNECTION
            ),
            aspike_nif:set_event_loops_amount(?EVENT_LOOPS_AMOUNT),
            aspike_nif:host_add(),
            aspike_nif:as_init(),
            io:format("aspike_nif:connect: ~p~n", [aspike_nif:connect()]),
            persistent_term:put(?INIT_FLAG, true),
            ok;
        _ -> ok
    end.

quick_test() ->
    init(),
    Mode = atom_to_list(get_api_mode(default)),
    io:format("Working in ~s mode with Aerospike server.~n", [Mode]),
    Namespace = <<"test">>,
    SetName = <<"rtb_setname">>,
    PrimaryKey = <<"user_defined_key_3">>,
    DataToInsert = [
        {<<"fcap_map_1">>, [<<"map_key_1_1">>, <<0, 1, 0, 2, 1>>, 123, <<"map_key_1_2">>, <<0, 1, 0, 2, 2>>, 456]},
        {<<"fcap_map_2">>, [<<"map_key_2_1">>, <<0, 1, 0, 2, 3>>, 345, <<"map_key_2_2">>, <<0, 1, 0, 2, 4>>, 678]}
    ],

    InsertRes = cdt_put(Namespace, SetName, PrimaryKey, DataToInsert, 300),
    io:format("cdt_put result:~n~p~n~n", [InsertRes]),

    ReadRes = cdt_get(Namespace, SetName, PrimaryKey),
    io:format("cdt_get result:~n~p~n~n", [ReadRes]),
    case ReadRes of
        {ok, ReadData} ->
            case aspike_nif_test_utils:compare_cdt_data(DataToInsert, ReadData) of
                true -> io:format("Read data match the inserted one. All good.~n", []);
                false -> io:format("Read data doesn't match the inserted one. Investigate why.~n", [])
            end;
        {error, Error} ->
            io:format("Read error happened: ~p.~n", [Error])
    end,

    % Test cdt_delete_by_keys async functionality
    io:format("Testing cdt_delete_by_keys async...~n", []),
    BinName = <<"fcap_map_1">>,
    KeysToDelete = [<<"map_key_1_1">>, <<"map_key_1_2">>],
    DeleteRes = cdt_delete_by_keys(Namespace, SetName, PrimaryKey, BinName, KeysToDelete),
    io:format("cdt_delete_by_keys result:~n~p~n~n", [DeleteRes]),

    % Read again to verify deletion
    ReadAfterDeleteRes = cdt_get(Namespace, SetName, PrimaryKey),
    io:format("cdt_get result after delete:~n~p~n~n", [ReadAfterDeleteRes]),
    case ReadAfterDeleteRes of
        {ok, ReadAfterDeleteData} ->
            ExpectedData = [
                {<<"fcap_map_1">>, []},
                {<<"fcap_map_2">>, [<<"map_key_2_1">>, <<0, 1, 0, 2, 3>>, 345, <<"map_key_2_2">>, <<0, 1, 0, 2, 4>>, 678]}
            ],
            case aspike_nif_test_utils:compare_cdt_data(ExpectedData, ReadAfterDeleteData) of
                true -> io:format("Read after delete successful, deleted keys are absent.~n", []);
                false -> io:format("Read data doesn't match the expected one. Investigate why.~n", [])
            end;
        {error, DeleteError} ->
            io:format("Read after delete error: ~p.~n", [DeleteError])
    end,

    io:format("get_connections_stats:~n~p~n~n", [aspike_nif:get_connections_stats()]).

init_tester() ->
    case whereis(tester) of
        undefined -> ok;
        _ -> unregister(tester)
    end,
    register(tester, self()),

    case whereis(collector) of
        undefined ->
            register(collector, spawn_link(fun() -> aspike_nif_test_utils:collector_start() end));
        _ -> ok
    end.

stress_test() ->
    init(),
    init_tester(),

    TestNamePrefix = "local",
    AmountOfOps = 10_000,
    %Command = cdt_put,
    %Command = cdt_get,
    Command = cdt_delete_by_keys,
    AmountsOfClients = [1, 2, 4, 8, 10, 12, 14, 20, 50, 100, 200, 250, 300],
    %AmountsOfClients = [1],

    Namespace = <<"test">>,
    SetName = <<"test_set">>,
    ActionFunc = case Command of
        cdt_put ->
            fun(Counter) ->
                {RecordKeyName, Bins, _, _} = aspike_nif_test_utils:get_test_date_from_counter(Counter),
                % in seconds, 10 * 60 means 10 minutes
                TTL = 10 * 60,
                cdt_put(Namespace, SetName, RecordKeyName, Bins, TTL)
            end;
        cdt_get ->
            fun(Counter) ->
                % NOTE: this test will be fail in sync mode because of the bug with insertion
                % of two keys into a map - only 1 key will be added to a map
                {RecordKeyName, Bins, {MapKey1, Value1, Value2}, {MapKey2, Value3, Value4}} = aspike_nif_test_utils:get_test_date_from_counter(Counter),

                % first, insert data
                % in seconds
                TTL = 10,
                cdt_put(Namespace, SetName, RecordKeyName, Bins, TTL),

                % now read data back and validate
                Result = cdt_get(Namespace, SetName, RecordKeyName),
                case Result of
                    {error, _} -> Result;
                    {ok, Data} ->
                        case Data of
                            [{MapKey2, [Value3, {Value4,_,_}]}, {MapKey1, [Value1, {Value2,_,_}]}] -> Result;
                            _ -> {error, <<"cdt_get() doesn't match data put by cdt_put()">>}
                        end
                end
            end;
        cdt_delete_by_keys ->
            fun(Counter) ->
                % NOTE: this test will be fail in sync mode because of the bug with insertion
                % of two keys into a map - only 1 key will be added to a map
                {RecordKeyName, _, {MapKey1, Value1, Value2}, {MapKey2, Value3, Value4}} = aspike_nif_test_utils:get_test_date_from_counter(Counter),

                % first, insert data
                Bins = [{MapKey1, [Value1, Value2, Counter, Value3, Value4, Counter]}, {MapKey2, [Value3, Value4, Counter]}],
                % in seconds
                TTL = 10,
                cdt_put(Namespace, SetName, RecordKeyName, Bins, TTL),

                % then, delete some keys from the map
                cdt_delete_by_keys(Namespace, SetName, RecordKeyName, MapKey1, [Value1]),

                % now read data back and validate
                Result = cdt_get(Namespace, SetName, RecordKeyName),
                case Result of
                    {error, _} -> Result;
                    {ok, Data} ->
                        case Data of
                            [{MapKey2, [Value3, {Value4,_,_}]}, {MapKey1, [Value3, {Value4,_,_}]}] -> Result;
                            _ -> {error, <<"cdt_get() doesn't match data after cdt_delete_by_keys()">>}
                        end
                end
            end
        end,

    ModeAtom = get_api_mode(default),
    TestName = TestNamePrefix ++ " " ++ atom_to_list(ModeAtom) ++ " " ++ atom_to_list(Command) ++ " " ++ integer_to_list(AmountOfOps),

    aspike_nif_test_utils:stress_test_loop(TestName, ActionFunc, AmountOfOps, AmountsOfClients),
    ok.

memory_leak_test() ->
    init(),
    init_tester(),

    AmountOfOps = 100_000_000,
    AmountOfClients = 50,

    Namespace = <<"test">>,
    SetName = <<"test_set">>,
    ActionFunc = fun(Counter) ->
        {RecordKeyName, Bins, {MapKey1, Value1, _}, _} = aspike_nif_test_utils:get_test_date_from_counter(Counter),
        % No need to keep the record longger than 5 seconds
        cdt_put(Namespace, SetName, RecordKeyName, Bins, 5),
        cdt_get(Namespace, SetName, RecordKeyName),
        cdt_delete_by_keys(Namespace, SetName, RecordKeyName, MapKey1, [Value1])
     end,

    aspike_nif_test_utils:memory_leak_test(ActionFunc, AmountOfOps, AmountOfClients),
    ok.

cdt_put(Namespace, Set, RecordKeyName, BinList, TTL) ->
    cdt_put(Namespace, Set, RecordKeyName, BinList, TTL, ?ASPIKE_DEFAULT_POLICY).

cdt_put(Namespace, Set, RecordKeyName, BinList, TTL, Policy) ->
    case get_api_mode(cdt_put) of
        sync ->
            aspike_nif:cdt_put_sync(Namespace, Set, RecordKeyName, BinList, TTL, Policy);
        async ->
            AsyncCmd = fun() ->
                aspike_nif:cdt_put_async(Namespace, Set, RecordKeyName, BinList, TTL, Policy)
                       end,
            call_aerospike_async_nif(AsyncCmd)
    end.

cdt_get(Namespace, Set, RecordKeyName) ->
    cdt_get(Namespace, Set, RecordKeyName, ?ASPIKE_DEFAULT_POLICY).

cdt_get(Namespace, Set, RecordKeyName, Policy) ->
    case get_api_mode(cdt_get) of
        sync ->
            aspike_nif:cdt_get_sync(Namespace, Set, RecordKeyName, Policy);
        async ->
            AsyncCmd = fun() -> aspike_nif:cdt_get_async(Namespace, Set, RecordKeyName, Policy) end,
            call_aerospike_async_nif(AsyncCmd)
    end.

cdt_delete_by_keys(Namespace, Set, RecordKeyName, BinName, SubkeysList) ->
    case get_api_mode(cdt_delete_by_keys) of
        sync ->
            aspike_nif:cdt_delete_by_keys_sync(Namespace, Set, RecordKeyName, BinName, SubkeysList);
        async ->
            AsyncCmd = fun() ->
                aspike_nif:cdt_delete_by_keys_async(Namespace, Set, RecordKeyName, BinName, SubkeysList)
                       end,
            call_aerospike_async_nif(AsyncCmd)
    end.

cdt_delete_by_keys_batch(Namespace, Set, BinName, KeysSubkeysList) ->
    % there is only sync version of this operation right now
    aspike_nif:cdt_delete_by_keys_batch(Namespace, Set, BinName, KeysSubkeysList).

call_aerospike_async_nif(AsyncCmd) ->

    % we define time to wait (TTW) much higher compare to prod values
    % because dev machines are not so powerful. Value in milliseconds.
    TTW = 1000,

    case AsyncCmd() of
        {ok, in_progress} ->
            receive
                {ok, Response} ->
                    {ok, Response};
                {error, {_NifErrorCode, _AspikeErrorCode, ErrorMessage}} ->
                    {error, ErrorMessage}
            after TTW ->
                {error, <<"timeout waiting for the response from aerospike">>}
            end;
        {ok, Response} ->
            {ok, Response};
        {error, {_NifErrorCode, _AspikeErrorCode, ErrorMessage}} ->
            {error, ErrorMessage}
    end.

cdt_del_test(0, _, _, _, _) -> ok;
cdt_del_test(N, NKeys, NSKeys, TTL, Timeout) ->
    Key = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    Subkey1 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey2 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    cdt_delete_by_keys(
        <<"test">>, <<"gateway_fcap_test1">>, Key, ?FCAP_BIN, [Subkey1, Subkey2]
    ),
    case Timeout of
        0 -> ok;
        _ -> timer:sleep(Timeout)
    end,
    cdt_del_test(N-1, NKeys, NSKeys, TTL, Timeout).

cdt_del_batch_test(0, _, _, _, _) -> ok;
cdt_del_batch_test(N, NKeys, NSKeys, TTL, Timeout) ->
    Key1 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    Key2 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    Subkey1 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey2 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey3 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey4 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    cdt_delete_by_keys_batch(
        <<"test">>, <<"gateway_fcap_test1">>, ?FCAP_BIN, [{Key1, [Subkey1, Subkey2]}, {Key2, [Subkey3, Subkey4]}]
    ),
    case Timeout of
        0 -> ok;
        _ -> timer:sleep(Timeout)
    end,
    cdt_del_batch_test(N-1, NKeys, NSKeys, TTL, Timeout).

cdt_get_test(0, _, _, _) -> ok;
cdt_get_test(N, NKeys, TTL, Timeout) ->
    Key = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    cdt_get(
        <<"test">>, <<"gateway_fcap_test1">>, Key, {2, 1000, 30000, 1000}
    ),
    case Timeout of
        0 -> ok;
        _ -> timer:sleep(Timeout)
    end,
    cdt_get_test(N-1, NKeys, TTL, Timeout).

cdt_insert_test(0, _, _, _, _) -> ok;
cdt_insert_test(N, NKeys, NSKeys, TTL, Timeout) ->
    Key = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    Subkey = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    Value = base64:encode(crypto:strong_rand_bytes(40)),
    Bins = [
        {?FCAP_BIN, [Subkey, Value, erlang:system_time(seconds) + TTL]}
    ],
    cdt_put(
        <<"test">>,
        <<"gateway_fcap_test1">>,
        Key,
        Bins,
        TTL,
        {3, 1000, 30000, 1000}
    ),
    case Timeout of
        0 -> ok;
        _ -> timer:sleep(Timeout)
    end,

    cdt_insert_test(N-1, NKeys, NSKeys, TTL, Timeout).

cdt_insert_test_mk(0, _, _, _, _) -> ok;
cdt_insert_test_mk(N, NKeys, NSKeys, TTL, Timeout) ->
    Key = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    Subkey1 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey1">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey2 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey2">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey3 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey3">>, integer_to_binary(rand:uniform(NSKeys))]),
    Value1 = base64:encode(crypto:strong_rand_bytes(40)),
    Value2 = base64:encode(crypto:strong_rand_bytes(40)),
    Value3 = base64:encode(crypto:strong_rand_bytes(40)),
    Bins = [
        {?FCAP_BIN, [
            Subkey1, Value1, erlang:system_time(seconds) + TTL,
            Subkey2, Value2, erlang:system_time(seconds) + TTL,
            Subkey3, Value3, erlang:system_time(seconds) + TTL
        ]}
    ],
    cdt_put(
        <<"test">>,
        <<"gateway_fcap_test1">>,
        Key,
        Bins,
        TTL,
        {3, 1000, 30000, 1000}
    ),
    case Timeout of
        0 -> ok;
        _ -> timer:sleep(Timeout)
    end,

    cdt_insert_test_mk(N-1, NKeys, NSKeys, TTL, Timeout).

% single process insert
sp_insert(N) ->
   sp_insert(<<"test">>, <<"rtb-gateway-fcap-users">>, N, 3600, 10).

sp_insert(N, Sleep) ->
   T1 = erlang:system_time(microsecond),
   sp_insert(<<"test">>, <<"rtb-gateway-fcap-users">>, N, 3600, Sleep),
   T = erlang:system_time(microsecond) - T1,
   Avg = (T - (Sleep * 1000)*N)/N,
   {T, Avg}.

sp_insert(Namespace, Set, N, TTL, Sleep) ->
	sp_insert(Namespace, Set, N, TTL, Sleep, 1_000_000_000_000, 0, 0).

sp_insert(_, _, 0, _, _, _, Oks, Errs) -> {Oks, Errs};

sp_insert(Namespace, SetName, TimesToInsert, TTL, Sleep, AddP, Oks, Errs) ->
    sp_insert(<<"sp_insert">>, Namespace, SetName, TimesToInsert, TTL, Sleep, AddP, Oks, Errs).

sp_insert(_, _, _, 0, _, _, _, Oks, Errs) -> {Oks, Errs};

sp_insert(ProcName, Namespace, SetName, TimesToInsert, TTL, Sleep, AddP, Oks, Errs) ->
   case TimesToInsert rem 10000 of
     0 -> io:format("~s: writes left to do: ~p ~n", [ProcName, TimesToInsert]);
     _ -> ok
   end,
   Key = integer_to_binary(TimesToInsert + AddP),
   Bins = [
      {<<"column1">>, <<"fcap">>},
      {<<"column2">>, <<"campaign.164206.3684975">>},
      {<<"timestamps">>, <<0,0,0,0,0,0,0,2,0,0,0,0,101,231,111,33,0,0,0,0,101,231,64,10>>}
   ],
   T1 = erlang:system_time(microsecond),
   {O1, E1} = case aspike_nif:binary_put(Namespace, SetName, Key, Bins, TTL) of
     {ok, _} -> {Oks+1, Errs};
     EE ->
	io:format("~s: Error ~p ~n", [ProcName, EE]),
	{Oks, Errs+1}
   end, 
   T = erlang:system_time(microsecond) - T1,
   case erlang:get(insert_stats) of
      undefined -> erlang:put(insert_stats, [T]);
      ISList -> erlang:put(insert_stats, [T | ISList])
   end,

   case Sleep of
     0 -> ok;
     _ -> timer:sleep(rand:uniform(Sleep))
   end,
   sp_insert(ProcName, Namespace, SetName, TimesToInsert - 1, TTL, Sleep, AddP, O1, E1).


pool_insert(_, _, 0, _, _, _, Oks, Errs) -> {Oks, Errs};
pool_insert(Namespace, Set, N, TTL, Sleep, AddP, Oks, Errs) ->
   case N rem 10000 of
     0 -> io:format("Pool write N: ~p ~n", [N]);
     _ -> ok
   end,
   Key = integer_to_binary(N + AddP),
   Bins = [
      {<<"column1">>, <<"fcap">>},
      {<<"column2">>, <<"campaign.164206.3684975">>},
      {<<"timestamps">>, <<0,0,0,0,0,0,0,2,0,0,0,0,101,231,111,33,0,0,0,0,101,231,64,10>>}
   ],
   {O1, E1} = case aspike_srv_worker:binary_key_put(Namespace, Set, Key, Bins, TTL) of
     {ok, _} -> {Oks+1, Errs};
     _ -> {Oks, Errs+1}
   end, 

   case Sleep of
     0 -> ok;
     _ -> timer:sleep(rand:uniform(Sleep))
   end,
   pool_insert(Namespace, Set, N-1, TTL, Sleep, AddP, O1, E1).

mp_insert(NProc, AmountOfInserts, Sleep) ->
    lists:map(fun(ProcNumber) ->
        spawn(fun() ->
            T1 = erlang:system_time(microsecond),
            StartValue = 1_000_000_000_000 * ProcNumber,
            Ret = sp_insert(integer_to_list(ProcNumber), <<"test">>, <<"rtb-gateway-fcap-users">>, AmountOfInserts, 3600, Sleep, StartValue, 0, 0),
            InsertTime = (erlang:system_time(microsecond) - T1) div AmountOfInserts,
            io:format("Result from process ~p: return value: ~p, avg time for one insert : ~p µs ~n", [ProcNumber, Ret, InsertTime])
        end)
    end, lists:seq(1, NProc)).

mp_port_insert(NProc, N, Sleep) ->
   pooler:start(),
   pooler:new_pool(
        #{
          name => aspike,
          init_count => NProc,
          max_count => NProc,
          start_mfa => {aspike_srv_worker, start_link, []}
        }
   ),
   lists:map(fun(E) -> 
     spawn(fun() ->
   	T1 = erlang:system_time(microsecond),
	Ret = pool_insert(<<"test">>, <<"rtb-gateway-fcap-users">>, N, 3600, Sleep, 1_000_000_000_000 * E, 0, 0),
        RR = (erlang:system_time(microsecond) - T1) div N,
	io:format("Insert Process ~p ret: ~p rate: ~p ~n", [E, Ret, RR])
     end)
   end, lists:seq(1, NProc)).

mp_reads(NProc, N, Sleep) ->
   lists:map(fun(E) -> 
     spawn(fun() ->
	Ret = sp_read(<<"test">>, <<"rtb-gateway-fcap-users">>, N, Sleep, 1_000_000_000_000 * E, 0, 0, 0),
	io:format("Read Process ~p ret: ~p ~n", [E, Ret])
     end)
   end, lists:seq(1, NProc)).

sp_read(N, Sleep) ->
   T1 = erlang:system_time(microsecond),
   Ret = sp_read(<<"test">>, <<"rtb-gateway-fcap-users">>, N, Sleep, 1_000_000_000_000, 0, 0, 0),
   T = erlang:system_time(microsecond) - T1,
   Avg = (T - (Sleep * 1000)*N)/N,
   {Ret, {T, Avg}}.
sp_read(_, _, 0, _, _, Oks, Nfs, Errs) -> {Oks, Nfs, Errs};
sp_read(Namespace, Set, N, Sleep, AddP, Oks, Nfs, Errs) ->
   case N rem 10000 of
     0 -> io:format("read N: ~p ~n", [N]);
     _ -> ok
   end,
   Key = integer_to_binary(N + AddP),
   T1 = erlang:system_time(microsecond),
   {Oks1, Nfs1, Errs1} = case aspike_nif:binary_get(Namespace, Set, Key) of
      {ok, Ret} ->
	  case check_ret(Ret) of
	     true -> {Oks+1, Nfs, Errs};
	     _ -> {Oks, Nfs, Errs+1}
          end; 
      {error, ERet} ->
	  case string:str(ERet, "AEROSPIKE_ERR_RECORD_NOT_FOUND") of
	    0 -> {Oks, Nfs, Errs+1};
            _ -> {Oks, Nfs+1, Errs}
          end
   end,
   T = erlang:system_time(microsecond) - T1,
   case erlang:get(read_stats) of
      undefined -> erlang:put(read_stats, [T]);
      ISList -> erlang:put(read_stats, [T | ISList])
   end,
   case Sleep of
     0 -> ok;
     _ -> timer:sleep(rand:uniform(Sleep))
   end,
   sp_read(Namespace, Set, N-1, Sleep, AddP, Oks1, Nfs1, Errs1).


mp_rand_reads(NProc, N, Sleep) ->
   lists:map(fun(E) -> 
     spawn(fun() ->
	Ret = rand_read(<<"test">>, <<"rtb-gateway-fcap-users">>, N, Sleep, 1_000_000_000_000, 0, 0, 0),
	io:format("Process ~p ret: ~p ~n", [E, Ret])
     end)
   end, lists:seq(1, NProc)).

rand_read(N, Sleep) ->
   T1 = erlang:system_time(microsecond),
   Ret = sp_read(<<"test">>, <<"rtb-gateway-fcap-users">>, N, Sleep, 1_000_000_000_000, 0, 0, 0),
   T = erlang:system_time(microsecond) - T1,
   Avg = (T - (Sleep * 1000)*N)/N,
   {Ret, {T, Avg}}.
rand_read(_, _, 0, _, _, Oks, Nfs, Errs) -> {Oks, Nfs, Errs};
rand_read(Namespace, Set, N, Sleep, AddP, Oks, Nfs, Errs) ->
   case N rem 10000 of
     0 -> io:format("read N: ~p ~n", [N]);
     _ -> ok
   end,
   Rand = rand:uniform(1_000_000),
   Key = integer_to_binary(Rand + AddP),
   {Oks1, Nfs1, Errs1} = case aspike_nif:binary_get(Namespace, Set, Key) of
      {ok, Ret} ->
	  case check_ret(Ret) of
	     true -> {Oks+1, Nfs, Errs};
	     _ -> {Oks, Nfs, Errs+1}
          end; 
      {error, ERet} ->
	  case string:str(ERet, "AEROSPIKE_ERR_RECORD_NOT_FOUND") of
	    0 -> {Oks, Nfs, Errs+1};
            _ -> {Oks, Nfs+1, Errs}
          end
   end,
   case Sleep of
      SI when is_integer(SI), SI > 1 -> timer:sleep(rand:uniform(SI));
      _ -> ok
   end,
   rand_read(Namespace, Set, N-1, Sleep, AddP, Oks1, Nfs1, Errs1).

check_ret(Ret) ->
   Bins = [
      {<<"column1">>, <<"fcap">>},
      {<<"column2">>, <<"campaign.164206.3684975">>},
      {<<"timestamps">>, <<0,0,0,0,0,0,0,2,0,0,0,0,101,231,111,33,0,0,0,0,101,231,64,10>>}
   ],
   lists:all(fun(E) -> E =:= true end,  [proplists:get_value(K, Ret, undefined) == V || {K,V} <- Bins]).

infinite_test(NProc, Ttl, WSleep, _RSleep) ->
   lists:map(fun(E) -> 
     spawn(fun() ->
	Ret = sp_insert(<<"test">>, <<"rtb-gateway-fcap-users">>, 999_999_999_999, Ttl, WSleep, 1_000_000_000_000 * E, 0, 0),
	io:format("Insert Process ~p ret: ~p ~n", [E, Ret])
     end)
   end, lists:seq(1, NProc)).

test_insertion(0, _, _, Oks, Errs) -> {Oks, Errs};
test_insertion(N, Sleep, PrevLatency, Oks, Errs) ->
   Key = integer_to_binary(N),
   T1 = erlang:system_time(microsecond),
   Bins = [
      {<<"column1">>, <<"fcap">>},
      {<<"column2">>, integer_to_binary(T1)},
      {<<"column3">>, integer_to_binary(PrevLatency)},
      {<<"timestamps">>, <<0,0,0,0,0,0,0,2,0,0,0,0,101,231,111,33,0,0,0,0,101,231,64,10>>}
   ],
   {O1, E1} = case aspike_nif:binary_put(<<"test">>, <<"rtb-gateway-fcap-users">>, Key, Bins, 600) of
     {ok, _} -> {Oks+1, Errs};
     EE ->
	io:format("Error ~p ~n", [EE]), 
	{Oks, Errs+1}
   end, 
   Latency = erlang:system_time(microsecond) - T1,
   case Sleep of
     0 -> ok;
     _ -> timer:sleep(Sleep)
   end,
   test_insertion(N - 1, Sleep, Latency, O1, E1).

test_reading(0, _, XDRLat) -> 
    io:format("collecting is done, save results", []),
    XDRList = erlang:get(xdr_stats),

    {ok, IFile} = file:open("/tmp/xdr_stats.txt", [write]),
    lists:foreach(fun(E) ->
        file:write(IFile, io_lib:fwrite("~p~n", [E]))
    end, XDRList),
    file:close(IFile),

    erlang:put(xdr_stats, []),
    XDRLat;
test_reading(N, Sleep, XDRLat) ->
   Key = integer_to_binary(N),
   T1 = erlang:system_time(microsecond),
   {Status, XDRLatRet} = case aspike_nif:binary_get(<<"test">>, <<"rtb-gateway-fcap-users">>, Key) of
      {ok, Ret} ->
          TW1 = binary_to_integer(proplists:get_value(<<"column2">>, Ret, <<"0">>)),
          L1  = binary_to_integer(proplists:get_value(<<"column3">>, Ret, <<"0">>)),
          XDRLat1 = T1 - TW1 - L1,
          io:format("Key: ~p XDRLat: ~p ~n", [Key, XDRLat1]),
          {ok, XDRLat1};
      {error, ERet} ->
	  case string:str(ERet, "AEROSPIKE_ERR_RECORD_NOT_FOUND") of
	    0 -> {err, XDRLat};
            _ -> {nf, XDRLat}
          end
   end,
   case erlang:get(xdr_stats) of
      undefined -> erlang:put(xdr_stats, [XDRLatRet]);
      XDRList -> erlang:put(xdr_stats, [XDRLatRet | XDRList])
   end,
   case Sleep of
     0 -> ok;
     _ -> timer:sleep(rand:uniform(Sleep))
   end,
   case Status of
	nf -> test_reading(N, Sleep, XDRLat);
        _  -> test_reading(N -1 , Sleep, (XDRLatRet + XDRLat) div 2)
   end.
  
dump_stats() -> 
   Istat = case erlang:get(insert_stats) of
	IsL when is_list(IsL) -> IsL;
	_ -> []
   end,
   Rstat = case erlang:get(read_stats) of
	RsL when is_list(RsL) -> RsL;
	_ -> []
   end,
   {ok, IFile} = file:open("/tmp/insert_stats.txt", [write]),
   lists:foreach(fun(E) ->
	%file:write_file("/tmp/insert_stats.txt", io_lib:fwrite("~p~n", [E]), [append])
        file:write(IFile, io_lib:fwrite("~p~n", [E]))
   end, Istat),
   file:close(IFile),
   {ok, RFile} = file:open("/tmp/read_stats.txt", [write]),
   lists:foreach(fun(E) ->
	%file:write_file("/tmp/read_stats.txt", io_lib:fwrite("~p~n", [E]), [append]) 
        file:write(RFile, io_lib:fwrite("~p~n", [E]))
   end, Rstat),
   file:close(RFile),

   erlang:put(read_stats, []),
   erlang:put(insert_stats, []),
   ok.

pool_cdt_insert(0) -> ok;
pool_cdt_insert(N) when is_integer(N), N > 0 ->
    Key = integer_to_binary(N),
    Value = base64:encode(crypto:strong_rand_bytes(40)),
    aspike_srv_worker:cdt_put(<<"test">>, <<"rtb-gateway-fcap-users2">>, 
        Key, [{<<"fcap_map">>, [<<"campaign.111">>, Value, 777]}], 6000),
    pool_cdt_insert(N-1).

pool_cdt_read(0) -> ok;
pool_cdt_read(N) ->
    Key = integer_to_binary(N),
    aspike_srv_worker:cdt_get(<<"test">>, <<"rtb-gateway-fcap-users2">>, Key),
    pool_cdt_read(N - 1).
