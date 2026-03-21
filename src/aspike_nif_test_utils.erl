-module(aspike_nif_test_utils).

%% API
-export([
    get_test_data_from_counter/1,
    stress_test_loop/4,
    memory_leak_test/3,
    collector_start/0,
    compare_cdt_data/2
]).

get_test_data_from_counter(Counter) ->
    RecordKeyName = <<<<"user_">>/binary, (integer_to_binary(Counter))/binary>>,
    BinName1 = <<<<"bn1_">>/binary, (integer_to_binary(Counter))/binary>>,
    Key1 = <<<<"key_1_">>/binary, (integer_to_binary(Counter))/binary>>,
    Value1 = <<<<"value_1_">>/binary, <<0, 1, 0>>/binary, (integer_to_binary(Counter))/binary>>,
    BinName2 = <<<<"bn2_">>/binary, (integer_to_binary(Counter))/binary>>,
    Key2 = <<<<"Key_2_">>/binary, (integer_to_binary(Counter))/binary>>,
    Value2 = <<<<"Value_2_">>/binary, <<0, 1, 0>>/binary, (integer_to_binary(Counter))/binary>>,
    {RecordKeyName, {BinName1, Key1, Value1}, {BinName2, Key2, Value2}}.

stress_test_loop(_TestName, _, _, []) ->
    collector ! send_stats,
    receive
        {ok, _Stats} ->
            %% I commented out saving data to JSON fle because we don't need it anymore

            %io:format("~nStats: ~p~n~n", [Stats]),
            %%StatByMode = maps:get(by_mode, Stats),
            %%ModeAtom = aspike_nif_test:get_api_mode(default),
            %%DataOfThisMode = maps:get(ModeAtom, StatByMode),
            %%JSON = lists:join("", [
            %%    "{\"title\": \"" ++ TestName ++ "\", \"data\": [",
            %%    lists:join(", ", lists:map(fun(Data) ->
            %%        {AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet} = Data,
            %%        io_lib:format("{\"clients\": ~p, \"amountOfOps\": ~p, \"min\": ~p, \"max\": ~p, \"avg\": ~p, \"done\": ~p, \"failed\": ~p}",
            %%            [AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet]
            %%        )
            %%    end, DataOfThisMode)),
            %%    "]}"
            %%]),
            %%io:format("JSON: ~s~n", [JSON]),
            %%FileName = string:replace(TestName, " ", "_", all) ++ ".json",
            %%{ok, FileDesc} = file:open(FileName, [write]),
            %%file:write(FileDesc, JSON),
            %%file:close(FileDesc),
            %%io:format("Saved results to file: ~s~n", [FileName]),
            collector ! clear_stats
    end;

stress_test_loop(TestName, ActionFunc, AmountOfOps, [AmountOfClients | Tail]) ->
    collector ! {test_starts, AmountOfClients},
    receive
        collector_ack -> ok
    end,
    stress_test_runner(AmountOfClients, ActionFunc, AmountOfOps),
    receive
        collection_done -> ok
    end,
    stress_test_loop(TestName, ActionFunc, AmountOfOps, Tail).

stress_test_runner(AmountOfClients, ActionFunc, AmountOfOps) ->
    ModeAtom = aspike_nif_test:get_api_mode(default),
    io:format("Starting ~p clients each with ~p operations in ~p mode ...~n", [AmountOfClients, AmountOfOps, ModeAtom]),
    lists:map(fun(ProcNumber) ->
        spawn(fun() ->
            Counter = AmountOfOps * ProcNumber,
            ProcName = integer_to_list(ProcNumber),
            StartTime = erlang:system_time(microsecond),
            Results = stress_test_runner_loop(ProcName, ActionFunc, AmountOfOps, Counter, 0, 0),
            EndTime = erlang:system_time(microsecond),
            TimePerAction = (EndTime - StartTime) div AmountOfOps,
            {OpsDone, ErrorsMet} = Results,
            collector ! {load_finished, {1, {ProcName, AmountOfOps, OpsDone, ErrorsMet, TimePerAction}}}
        end)
    end, lists:seq(1, AmountOfClients)).

stress_test_runner_loop(_, _, 0, _, Oks, Errs) -> {Oks, Errs};

stress_test_runner_loop(ProcName, ActionFunc, AmountOfOps, Counter, Oks, Errs) ->
    Result = try ActionFunc(Counter) of
        Res -> Res
    catch
        throw:Error -> Error
    end,

    {OpsDone, ErrorsMet} = case Result of
        {ok, _} -> {Oks + 1, Errs};
        {error, _ErrorMessage} ->
            %io:format("~s: got error: ~s~n", [ProcName, ErrorMessage]),
            {Oks, Errs + 1}
    end,

    stress_test_runner_loop(ProcName, ActionFunc, AmountOfOps - 1, Counter + 1, OpsDone, ErrorsMet).

memory_leak_test(ActionFunc, AmountOfOps, AmountOfClients) ->
    collector ! {test_starts, AmountOfClients},
    receive
        collector_ack -> ok
    end,
    memory_leak_test_runner(AmountOfClients, ActionFunc, AmountOfOps),
    receive
        collection_done -> ok
    end,
    collector ! send_stats,
    receive
        {ok, Stats} ->
            StatByMode = maps:get(by_mode, Stats),
            ModeAtom = aspike_nif_test:get_api_mode(default),
            DataOfThisMode = maps:get(ModeAtom, StatByMode),
            [{_, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet}] = DataOfThisMode,
            io:format("Clients: ~p, amountOfOps: ~p, min: ~p, max: ~p, avg: ~p, done: ~p, failed: ~p}",
                [AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet]
            ),
            collector ! clear_stats
    end.

memory_leak_test_runner(AmountOfClients, ActionFunc, AmountOfOps) ->
    ModeAtom = aspike_nif_test:get_api_mode(default),
    io:format("Starting ~p clients each with ~p operations in ~p mode ...~n", [AmountOfClients, AmountOfOps, ModeAtom]),
    lists:map(fun(ProcNumber) ->
        spawn(fun() ->
            Counter = AmountOfOps * ProcNumber,
            ProcName = integer_to_list(ProcNumber),
            StartTime = erlang:system_time(microsecond),
            Results = memory_leak_test_runner_loop(ProcName, ActionFunc, AmountOfOps, Counter, 0, 0),
            EndTime = erlang:system_time(microsecond),
            TimePerAction = (EndTime - StartTime) div AmountOfOps,
            {OpsDone, ErrorsMet} = Results,
            collector ! {load_finished, {1, {ProcName, AmountOfOps, OpsDone, ErrorsMet, TimePerAction}}}
        end)
    end, lists:seq(1, AmountOfClients)).

memory_leak_test_runner_loop(_, _, 0, _, Oks, Errs) -> {Oks, Errs};

memory_leak_test_runner_loop(ProcName, ActionFunc, AmountOfOps, Counter, Oks, Errs) ->
    Result = ActionFunc(Counter),

    {OpsDone, ErrorsMet} = case Result of
        {ok, _} -> {Oks + 1, Errs};
        {error, _ErrorMessage} ->
            {Oks, Errs + 1}
    end,

    memory_leak_test_runner_loop(ProcName, ActionFunc, AmountOfOps - 1, Counter + 1, OpsDone, ErrorsMet).

collector_start() ->
    collector_reset_stats(),
    collector_loop().

collector_reset_stats() ->
    erlang:put(collector_stats, #{
        status => done,
        results_received => 0,
        results_expected => 0,
        by_clients => #{},
        by_mode => #{
            async => [],
            sync => []
        }
    }).

collector_loop() ->
    receive
        {test_starts, ResultsExpected} ->
            Stats = erlang:get(collector_stats),
            erlang:put(collector_stats, maps:merge(Stats, #{
                results_expected => ResultsExpected,
                results_received => 0,
                status => collecting
            })),
            tester ! collector_ack;
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
                    erlang:put(collector_stats, UpdatedStats);
                    %io:format("Result from child ~p: ops done: ~p, errors met: ~p, avg time per operation : ~p µs ~n", [ClientId, OpsDone, ErrorsMet, TimePerOp]);
                _ ->
                    io:format("Collector: Received unknown version from a message: ~p~n", [Version])
            end;
        send_stats ->
            tester ! {ok, erlang:get(collector_stats)};
        clear_stats ->
            collector_reset_stats()
    end,

    CurrentStats = erlang:get(collector_stats),
    Status = maps:get(status, CurrentStats),
    Received = maps:get(results_received, CurrentStats),
    Expected = maps:get(results_expected, CurrentStats),
    Remaining = Expected - Received,
    case {Status, Remaining} of
        {collecting, 0} ->
            io:format("All clients have finished~n", []),
            collector_process_results(CurrentStats);
        _ ->
            ok
    end,
    collector_loop().

collector_process_results(Stats) ->
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
    PercentOfFailed = round((ErrorsMet / (OpsPerClient * AmountOfClients)) * 100),
    io:format("Total successful ops: ~p, Total failed ops: ~p (~p % from total)~n", [OpsDone, ErrorsMet, PercentOfFailed]),

    Stats = erlang:get(collector_stats),
    StatByMode = maps:get(by_mode, Stats),
    ModeAtom = aspike_nif_test:get_api_mode(default),
    DataOfThisMode = maps:get(ModeAtom, StatByMode),
    erlang:put(collector_stats, maps:merge(Stats, #{
        status => done,
        %by_clients => #{},
        by_mode => maps:merge(StatByMode, #{
            ModeAtom => lists:append(DataOfThisMode, [{AmountOfClients, OpsPerClient, Min, Max, Avg, OpsDone, ErrorsMet}])
        })
    })),
    tester ! collection_done.

%% @doc Compare insert data with read data from CDT operations
%% Insert data contains tuples like {<<0,1,0,2,1>>,123,1769657419} with timestamps
%% Read data has these flattened to <<0,1,0,2,1>>,123 (timestamp removed)
%% Returns true if data matches (ignoring timestamps), false otherwise
compare_cdt_data(InsertData, ReadData) ->
    try
        % Sort both lists by map keys for consistent comparison
        SortedInsertData = lists:sort(InsertData),
        SortedReadData = lists:sort(ReadData),

        % Compare each map
        compare_maps(SortedInsertData, SortedReadData)
    catch
        _:_ -> false
    end.

%% Internal function to compare individual maps
compare_maps([], []) ->
    true;
compare_maps([{MapKey, InsertValues} | InsertRest], [{MapKey, ReadValues} | ReadRest]) ->
    case compare_map_values(InsertValues, ReadValues) of
        true -> compare_maps(InsertRest, ReadRest);
        false -> false
    end;
compare_maps(_, _) ->
    false.

%% Internal function to compare map values
%% Insert values (flattened): [<<"map_key_1_1">>, <<0,1,0,2,1>>, 123, <<"map_key_1_2">>, <<0,1,0,2,2>>, 456]
%% Read values (with timestamps): [<<"map_key_1_1">>, {<<0,1,0,2,1>>,123,Timestamp}, <<"map_key_1_2">>, {<<0,1,0,2,2>>,456,Timestamp}]
%% We need to compare flattened insert data with tuple read data, ignoring timestamps and order
compare_map_values(InsertValues, ReadValues) ->
    try
        % Convert insert values to key-value pairs
        InsertPairs = extract_insert_pairs(InsertValues, []),
        % Convert read values to key-value pairs (ignoring timestamps)
        ReadPairs = extract_read_pairs(ReadValues, []),
        % Sort both lists and compare
        lists:sort(InsertPairs) =:= lists:sort(ReadPairs)
    catch
        _:_ -> false
    end.

%% Helper function to extract key-value pairs from flattened insert data
extract_insert_pairs([], Acc) ->
    lists:reverse(Acc);
extract_insert_pairs([Key, Bin, Num | Rest], Acc) ->
    extract_insert_pairs(Rest, [{Key, {Bin, Num}} | Acc]);
extract_insert_pairs(_, _) ->
    throw(invalid_format).

%% Helper function to extract key-value pairs from read data (with timestamps)
extract_read_pairs([], Acc) ->
    lists:reverse(Acc);
extract_read_pairs([Key, {Bin, Num, _Timestamp} | Rest], Acc) ->
    extract_read_pairs(Rest, [{Key, {Bin, Num}} | Acc]);
extract_read_pairs(_, _) ->
    throw(invalid_format).
