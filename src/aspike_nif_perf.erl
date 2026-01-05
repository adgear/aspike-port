-module(aspike_nif_perf).

-export([
    dima_init/0,
    dima_quick_test/0,
    dima_test/0,
    dima_stress_test/2,
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


-define(FCAP_BIN, <<"fcap_map">>).

dima_init() ->
    io:format("set host: ~p~n", [application:set_env(aspike_port, host, "127.0.0.1")]),
    %io:format("set host: ~p~n", [application:set_env(aspike_port, host, "192.168.88.69")]),
    io:format("set port: ~p~n", [application:set_env(aspike_port, port, 3000)]),
    io:format("set user: ~p~n", [application:set_env(aspike_port, user, "")]),
    io:format("set passwd: ~p~n", [application:set_env(aspike_port, psw, "")]),
    io:format("aspike_nif:as_init: ~p~n", [aspike_nif:as_init()]),
    io:format("aspike_nif:host_add: ~p~n", [aspike_nif:host_add()]),
    io:format("aspike_nif:connect: ~p~n", [aspike_nif:connect()]).

dima_quick_test() ->
    dima_init(),
    Namespace = <<"test">>,
    SetName = <<"rtb_setname">>,
    PrimaryKey = <<"user_defined_key_3">>,
    InsertRes = aspike_nif:cdt_put(Namespace, SetName, PrimaryKey, [
        {<<"fcap_map_1">>, [<<"map_key_1_1">>, <<"map_value_1_1">>, 123, <<"map_key_1_2">>, <<"map_value_1_2">>, 456]},
        {<<"fcap_map_2">>, [<<"map_key_2_1">>, <<"map_value_2_1">>, 345, <<"map_key_2_2">>, <<"map_value_2_2">>, 678]}
    ], 300),
    io:format("cdt_put result: ~p~n", [InsertRes]),
    ReadRes = aspike_nif:cdt_get(Namespace, SetName, PrimaryKey),
    io:format("cdt_get result: ~p~n", [ReadRes]).

dima_test() ->
    dima_init(),
    register(test_runner, self()),
    register(collector, spawn_link(fun() -> dima_collector_start() end)),

    TestName = "local sync cdt_put 10k",
    AmountOfRequests = 10_000,

    %dima_test_loop(TestName, AmountOfRequests, [10]).
    dima_test_loop(TestName, AmountOfRequests, [1, 2, 4, 8, 10, 12, 14, 20, 50, 100, 200, 250, 300]).

dima_test_loop(TestName, _, []) ->
    collector ! send_stats,
    receive
        {ok, Stats} ->
            io:format("~nStats: ~p~n~n", [Stats]),
            ChartSeries = maps:get(chart_series, Stats),
            ModeAtom = aspike_nif:get_api_mode(nothing),
            SeriesOfThisMode = maps:get(ModeAtom, ChartSeries),
            JSON = lists:join("", [
                "{\"title\": \"" ++ TestName ++ "\", \"data\": [",
                lists:join(", ", lists:map(fun(Data) ->
                    {AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet} = Data,
                    io_lib:format("{\"clients\": ~p, \"amountOfOps\": ~p, \"min\": ~p, \"max\": ~p, \"avg\": ~p, \"done\": ~p, \"failed\": ~p}", [AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet])
                   end, SeriesOfThisMode)),
                "]}"
            ]),
            io:format("JSON: ~s~n", [JSON]),
            FileName = string:replace(TestName, " ", "_", all) ++ ".json",
            {ok, FileDesc} = file:open(FileName, [write]),
            file:write(FileDesc, JSON),
            file:close(FileDesc),
            io:format("Saved results to file: ~s~n", [FileName])
    end;

dima_test_loop(TestName, AmountOfRequests, [AmountOfClients | Tail]) ->
    collector ! {test_starts, AmountOfClients},
    receive
        collector_ack -> ok
    end,
    dima_stress_test(AmountOfClients, AmountOfRequests),
    receive
        collection_done -> ok
    end,
    dima_test_loop(TestName, AmountOfRequests, Tail).

dima_collector_start() ->
    dima_collector_reset_stats(),
    dima_collector_loop().

dima_collector_reset_stats() ->
    erlang:put(collector_stats, #{
        status => done,
        results_received => 0,
        results_expected => 0,
        by_clients => #{},
        chart_series => #{
            async => [],
            sync => []
        }
    }).

dima_collector_loop() ->
    receive
        {test_starts, ResultsExpected} ->
            Stats = erlang:get(collector_stats),
            erlang:put(collector_stats, maps:merge(Stats, #{
                results_expected => ResultsExpected,
                results_received => 0,
                status => collecting
            })),
            test_runner ! collector_ack;
        {load_finished, Data} ->
            {Version, Results} = Data,
            case Version of
                1 ->
                    {ClientId, AmountOfOps, OpsDone, ErrorsMet, TimePerOp} = Results,
                    Stats = erlang:get(collector_stats),
                    UpdatedStats = maps:merge(Stats, #{
                        results_received => maps:get(results_received, Stats) + 1,
                        by_clients => maps:merge(maps:get(by_clients, Stats), #{
                            ClientId => #{
                                opsAmount => AmountOfOps,
                                opsDone => OpsDone,
                                errorsMet => ErrorsMet,
                                timePerOp => TimePerOp
                            }
                        })
                    }),
                    erlang:put(collector_stats, UpdatedStats),
                    io:format("Result from child ~p: ops done: ~p, errors met: ~p, avg time per operaion : ~p µs ~n", [ClientId, OpsDone, ErrorsMet, TimePerOp]);
                _ ->
                    io:format("Collector: Received unknown version from a message: ~p~n", [Version])
            end;
        send_stats ->
            test_runner ! {ok, erlang:get(collector_stats)}
    end,

    CurrentStats = erlang:get(collector_stats),
    Status = maps:get(status, CurrentStats),
    Received = maps:get(results_received, CurrentStats),
    Expected = maps:get(results_expected, CurrentStats),
    Remaining = Expected - Received,
    case {Status, Remaining} of
        {collecting, 0} ->
            io:format("All clients have finished~n", []),
            dima_collector_process_results(CurrentStats);
        _ ->
            ok
    end,
    dima_collector_loop().

dima_collector_process_results(Stats) ->
    AmountOfClients = maps:get(results_received, Stats),
    ByClients = maps:get(by_clients, Stats),
    ClientIds = maps:keys(ByClients),
    {Min, Max, Total, OpsPerClient, OpsDone, ErrorsMet} = lists:foldl(fun(ClientId, Acc) ->
        ClientData = maps:get(ClientId, ByClients),
        TimePerOp = maps:get(timePerOp, ClientData),
        OpsPerClient = maps:get(opsAmount, ClientData),
        OpsDone = maps:get(opsDone, ClientData),
        ErrorsMet = maps:get(errorsMet, ClientData),
        {Min, Max, TotalTime, MaxOpsPerClient, TotalOpsDone, TotalErrorsMet} = Acc,
        {
            % minimum time per operation
            min(Min, TimePerOp),
            % maximum time per operation
            max(Max, TimePerOp),
            % average time per operation
            TotalTime + TimePerOp,
            % max amount of operations performed by client, which
            % actually is the same for every client, but still, we use
            % max function just in case of human error
            max(MaxOpsPerClient, OpsPerClient),
            % total amount of successful operations
            TotalOpsDone + OpsDone,
            % total amount of failed operations
            TotalErrorsMet + ErrorsMet
        }
    end, {1_000_000_000, -1, 0, 0, 0, 0}, ClientIds),
    Avg = Total / erlang:length(ClientIds),
    io:format("Amount of parallel clients: ~p~n", [AmountOfClients]),
    io:format("Amount of operations per client: ~p~n", [OpsPerClient]),
    io:format("Min: ~p µs, Max: ~p µs, Avg: ~p µs~n", [Min, Max, Avg]),
    io:format("Total successful ops: ~p, Total failed ops: ~p~n", [OpsDone, ErrorsMet]),

    Stats = erlang:get(collector_stats),
    ChartSeries = maps:get(chart_series, Stats),
    ModeAtom = aspike_nif:get_api_mode(nothing),
    SeriesOfThisMode = maps:get(ModeAtom, ChartSeries),
    erlang:put(collector_stats, maps:merge(Stats, #{
        status => done,
        %by_clients => #{},
        chart_series => maps:merge(ChartSeries, #{
            ModeAtom => lists:append(SeriesOfThisMode, [{AmountOfClients, OpsPerClient, Min, Max, Avg, OpsDone, ErrorsMet}])
        })
    })),
    test_runner ! collection_done.

dima_stress_test (AmountOfClients, AmountOfOpsToDo) ->
    Namespace = <<"test">>,
    SetName = <<"rtb-gateway-fcap-users">>,

    ActionFunc = fun(Counter) ->
        Key = integer_to_binary(Counter),
        Bins = [{<<"key1">>, [<<"value1">>, <<"value2">>, 123]}],
        TTL = 60,
        aspike_nif:cdt_put(Namespace, SetName, Key, Bins, TTL)
    end,

    io:format("Starting ~p clients ...~n", [AmountOfClients]),
    lists:map(fun(ProcNumber) ->
        spawn(fun() ->
            Counter = 1_000_000_000_000 * ProcNumber,
            ProcName = integer_to_list(ProcNumber),
            StartTime = erlang:system_time(microsecond),
            Results = dima_stress_test_loop(ProcName, ActionFunc, AmountOfOpsToDo, Counter, 0, 0),
            EndTime = erlang:system_time(microsecond),
            TimePerInsert = (EndTime - StartTime) div AmountOfOpsToDo,
            { OpsDone, ErrorsMet } = Results,
            collector ! {load_finished, {1, {ProcName, AmountOfOpsToDo, OpsDone, ErrorsMet, TimePerInsert}}}
        end)
    end, lists:seq(1, AmountOfClients)).

dima_stress_test_loop (_, _, 0, _, Oks, Errs) -> {Oks, Errs};

dima_stress_test_loop (ProcName, ActionFunc, AmountOfOpsToDo, Counter, Oks, Errs) ->
%%    case AmountOfOpsToDo rem 10000 of
%%        0 -> io:format("~s: writes left to do: ~p ~n", [ProcName, AmountOfOpsToDo]);
%%        _ -> ok
%%    end,

    Result = ActionFunc(Counter),

    {OpsDone, ErrorsMet} = case Result of
        {ok, _} ->
            {Oks + 1, Errs};
        {error, _ErrorMessage} ->
            %io:format("~s: got error: ~s~n", [ProcName, ErrorMessage]),
            {Oks, Errs + 1}
    end,

    dima_stress_test_loop(ProcName, ActionFunc, AmountOfOpsToDo - 1, Counter + 1, OpsDone, ErrorsMet).

% aspike_nif:cdt_put(<<"test">>, <<"someSet">>, <<"keyName">>, [{<<"key1">>, [<<"value1">>, <<"value2">>, 123]}], 10).
% aspike_nif:cdt_get(<<"test">>, <<"someSet">>, <<"keyName">>).

cdt_del_test(0, _, _, _, _) -> ok;
cdt_del_test(N, NKeys, NSKeys, TTL, Timeout) ->
    Key = erlang:iolist_to_binary([<<"Key_Key_Key_Key.namespace.1111111">>, integer_to_binary(rand:uniform(NKeys))]),
    Subkey1 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    Subkey2 = erlang:iolist_to_binary([<<"Key_Key_Key_Key.subkey">>, integer_to_binary(rand:uniform(NSKeys))]),
    aspike_nif:cdt_delete_by_keys(
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
    aspike_nif:cdt_delete_by_keys_batch(
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
    aspike_nif:cdt_get(
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
    aspike_nif:cdt_put(
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
    aspike_nif:cdt_put(
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

