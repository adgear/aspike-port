-module(aspike_nif_test_utils).

%% API
-export([
    stress_test_loop/4,
    collector_start/0,
    compare_cdt_data/2
]).

stress_test_loop(TestName, _, _, []) ->
    collector ! send_stats,
    receive
        {ok, Stats} ->
            %io:format("~nStats: ~p~n~n", [Stats]),
            ChartSeries = maps:get(chart_series, Stats),
            ModeAtom = aspike_nif_test:get_api_mode(default),
            SeriesOfThisMode = maps:get(ModeAtom, ChartSeries),
            JSON = lists:join("", [
                "{\"title\": \"" ++ TestName ++ "\", \"data\": [",
                lists:join(", ", lists:map(fun(Data) ->
                    {AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet} = Data,
                    io_lib:format("{\"clients\": ~p, \"amountOfOps\": ~p, \"min\": ~p, \"max\": ~p, \"avg\": ~p, \"done\": ~p, \"failed\": ~p}",
                        [AmountOfClients, AmountOfOps, Min, Max, Avg, OpsDone, ErrorsMet]
                    )
                end, SeriesOfThisMode)),
                "]}"
            ]),
            io:format("JSON: ~s~n", [JSON]),
            FileName = string:replace(TestName, " ", "_", all) ++ ".json",
            {ok, FileDesc} = file:open(FileName, [write]),
            file:write(FileDesc, JSON),
            file:close(FileDesc),
            io:format("Saved results to file: ~s~n", [FileName]),
            collector ! clear_stats
    end;

stress_test_loop(TestName, ActionFunc, AmountOfRequests, [AmountOfClients | Tail]) ->
    collector ! {test_starts, AmountOfClients},
    receive
        collector_ack -> ok
    end,
    stress_test_runner(AmountOfClients, ActionFunc, AmountOfRequests),
    receive
        collection_done -> ok
    end,
    stress_test_loop(TestName, ActionFunc, AmountOfRequests, Tail).

stress_test_runner(AmountOfClients, ActionFunc, AmountOfOpsToDo) ->
    ModeAtom = aspike_nif_test:get_api_mode(default),
    io:format("Starting ~p clients each with ~p operations in ~p mode ...~n", [AmountOfClients, AmountOfOpsToDo, ModeAtom]),
    lists:map(fun(ProcNumber) ->
        spawn(fun() ->
            Counter = AmountOfOpsToDo * ProcNumber,
            ProcName = integer_to_list(ProcNumber),
            StartTime = erlang:system_time(microsecond),
            Results = stress_test_runner_loop(ProcName, ActionFunc, AmountOfOpsToDo, Counter, 0, 0),
            EndTime = erlang:system_time(microsecond),
            TimePerInsert = (EndTime - StartTime) div AmountOfOpsToDo,
            {OpsDone, ErrorsMet} = Results,
            collector ! {load_finished, {1, {ProcName, AmountOfOpsToDo, OpsDone, ErrorsMet, TimePerInsert}}}
        end)
    end, lists:seq(1, AmountOfClients)).

stress_test_runner_loop(_, _, 0, _, Oks, Errs) -> {Oks, Errs};

stress_test_runner_loop(ProcName, ActionFunc, AmountOfOpsToDo, Counter, Oks, Errs) ->
    Result = ActionFunc(Counter),

    {OpsDone, ErrorsMet} = case Result of
        {ok, _} -> {Oks + 1, Errs};
        {error, _ErrorMessage} ->
            %io:format("~s: got error: ~s~n", [ProcName, ErrorMessage]),
            {Oks, Errs + 1}
    end,

    stress_test_runner_loop(ProcName, ActionFunc, AmountOfOpsToDo - 1, Counter + 1, OpsDone, ErrorsMet).

collector_start() ->
    collector_reset_stats(),
    collector_loop().

collector_reset_stats() ->
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

collector_loop() ->
    receive
        {test_starts, ResultsExpected} ->
            Stats = erlang:get(collector_stats),
            erlang:put(collector_stats, maps:merge(Stats, #{
                results_expected => ResultsExpected,
                results_received => 0,
                status => collecting
            })),
            stress_tester ! collector_ack;
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
            stress_tester ! {ok, erlang:get(collector_stats)};
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
    PercentOfFailed = round((ErrorsMet / (OpsDone + OpsPerClient)) * 100),
    io:format("Total successful ops: ~p, Total failed ops: ~p (~p % from total)~n", [OpsDone, ErrorsMet, PercentOfFailed]),

    Stats = erlang:get(collector_stats),
    ChartSeries = maps:get(chart_series, Stats),
    ModeAtom = aspike_nif_test:get_api_mode(default),
    SeriesOfThisMode = maps:get(ModeAtom, ChartSeries),
    erlang:put(collector_stats, maps:merge(Stats, #{
        status => done,
        %by_clients => #{},
        chart_series => maps:merge(ChartSeries, #{
            ModeAtom => lists:append(SeriesOfThisMode, [{AmountOfClients, OpsPerClient, Min, Max, Avg, OpsDone, ErrorsMet}])
        })
    })),
    stress_tester ! collection_done.

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
%% We need to compare flattened insert data with tuple read data, ignoring timestamps
compare_map_values([], []) ->
    true;
compare_map_values([Key, Bin, Num | InsertRest], [Key, {Bin, Num, _ReadTimestamp} | ReadRest]) ->
    % Compare key, binary, and number; ignore timestamp from read data
    compare_map_values(InsertRest, ReadRest);
compare_map_values(_, _) ->
    false.
