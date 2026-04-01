-module(aspike_nif_stress_test).

-export([
    run/0
]).

-define(ASPIKE_DEFAULT_POLICY, {3, 250, 30000, 1000}).

run() ->
    aspike_nif_test:init(),
    init_tester(),

    TestNamePrefix = "local",
    AmountOfOps = 10_000,
    %Command = cdt_put,
    %Command = cdt_get,
    %Command = cdt_delete_by_keys,
    %Command = cdt_delete_by_keys_batch,
    Command = segment_tag_get,
    AmountsOfClients = [1, 2, 4, 8, 10, 12, 14, 20, 50, 100, 200, 250, 300],
    %AmountsOfClients = [1],

    Namespace = <<"test">>,
    SetName = <<"test_set">>,
    ActionFunc = get_action_func(Command, Namespace, SetName),

    ModeAtom = aspike_nif_test:get_api_mode(default),
    TestName = TestNamePrefix ++ " " ++ atom_to_list(ModeAtom) ++ " " ++ atom_to_list(Command) ++ " " ++ integer_to_list(AmountOfOps),

    aspike_nif_test_utils:stress_test_loop(TestName, ActionFunc, AmountOfOps, AmountsOfClients),
    ok.

%% Get action function for the specified command
get_action_func(cdt_put, Namespace, SetName) ->
    cdt_put_action_func(Namespace, SetName);
get_action_func(cdt_get, Namespace, SetName) ->
    cdt_get_action_func(Namespace, SetName);
get_action_func(cdt_delete_by_keys, Namespace, SetName) ->
    cdt_delete_by_keys_action_func(Namespace, SetName);
get_action_func(cdt_delete_by_keys_batch, Namespace, SetName) ->
    cdt_delete_by_keys_batch_action_func(Namespace, SetName);
get_action_func(segment_tag_get, Namespace, SetName) ->
    segment_tag_get_action_func(Namespace, SetName).

%% CDT Put stress test action function
cdt_put_action_func(Namespace, SetName) ->
    fun(Counter) ->
        {RecordKeyName, {BinName1, Key1, Value1}, {BinName2, Key2, Value2}} = aspike_nif_test_utils:get_test_data_from_counter(Counter),
        Bins = [{BinName1, [Key1, Value1, Counter]}, {BinName2, [Key2, Value2, Counter]}],
        % in seconds, 10 * 60 means 10 minutes
        TTL = 10 * 60,
        PutRes = aspike_nif_test:cdt_put(Namespace, SetName, RecordKeyName, Bins, TTL),
        case PutRes of
            {error, PutError} ->
                io:format("aspike_nif_test:cdt_put() got error while writing data:~n~p~n", [PutError]),
                false;
            {ok, _} -> ok
        end,
        PutRes
    end.

%% CDT Get stress test action function
cdt_get_action_func(Namespace, SetName) ->
    fun(Counter) ->
        % NOTE: this test will be fail in sync mode because of the bug with insertion
        % of two keys into a map - only 1 key will be added to a map
        {RecordKeyName, {BinName1, Key1, Value1}, {BinName2, Key2, Value2}} = aspike_nif_test_utils:get_test_data_from_counter(Counter),
        Bins = [{BinName1, [Key1, Value1, Counter]}, {BinName2, [Key2, Value2, Counter]}],

        % first, insert data
        % in seconds
        TTL = 10,
        aspike_nif_test:cdt_put(Namespace, SetName, RecordKeyName, Bins, TTL),

        % now read data back and validate
        Result = aspike_nif_test:cdt_get(Namespace, SetName, RecordKeyName),
        case Result of
            {error, _} -> Result;
            {ok, Data} ->
                case Data of
                    [{BinName2, [Key2, {Value2,_,_}]}, {BinName1, [Key1, {Value1,_,_}]}] -> Result;
                    _ -> {error, <<"aspike_nif_test:cdt_get() doesn't match data put by aspike_nif_test:cdt_put()">>}
                end
        end
    end.

%% CDT Delete by Keys stress test action function
cdt_delete_by_keys_action_func(Namespace, SetName) ->
    fun(Counter) ->
        {RecordKeyName, {BinName1, Key1, Value1}, {BinName2, Key2, Value2}} = aspike_nif_test_utils:get_test_data_from_counter(Counter),

        % first, insert data
        % NOTE: this test will be fail in sync mode because of the bug with insertion
        % of two keys into a map - only 1 key will be added to a map
        Bins = [{BinName1, [Key1, Value1, Counter, Key2, Value2, Counter]}, {BinName2, [Key2, Value2, Counter]}],
        % in seconds
        TTL = 10,
        aspike_nif_test:cdt_put(Namespace, SetName, RecordKeyName, Bins, TTL),

        % then, delete some keys from the map in BinName1
        aspike_nif_test:cdt_delete_by_keys(Namespace, SetName, RecordKeyName, BinName1, [Key1, Key2]),

        % now read data back and validate
        Result = aspike_nif_test:cdt_get(Namespace, SetName, RecordKeyName),
        case Result of
            {error, _} -> Result;
            {ok, Data} ->
                case Data of
                    [{BinName2, [Key2, {Value2,_,_}]}, {BinName1, []}] -> Result;
                    _ ->
                        io:format("aspike_nif_test:cdt_get() doesn't match data after aspike_nif_test:cdt_delete_by_keys(). Data is:~n~p~n", [Data]),
                        throw({error, wrong_data_match})
                end
        end,
        {ok, ok}
    end.

%% CDT Delete by Keys Batch stress test action function
cdt_delete_by_keys_batch_action_func(Namespace, SetName) ->
    fun(Counter) ->
        {_, {BinName1, Key1, Value1}, {BinName2, Key2, Value2}} = aspike_nif_test_utils:get_test_data_from_counter(Counter),
        PK1 = <<<<"user1_">>/binary, (integer_to_binary(Counter))/binary>>,
        PK2 = <<<<"user2_">>/binary, (integer_to_binary(Counter))/binary>>,

        % NOTE: this test will be fail in sync mode because of the bug with insertion
        % of two keys into a map - only 1 key will be added to a map

        % first, insert data
        Bins = [{BinName1, [Key1, Value1, Counter, Key2, Value2, Counter]}, {BinName2, [Key2, Value2, Counter]}],
        % in seconds
        TTL = 10,
        Put1Res = aspike_nif_test:cdt_put(Namespace, SetName, PK1, Bins, TTL),
        case Put1Res of
            {error, PutError1} ->
                io:format("aspike_nif_test:cdt_put() got error while writing PK1 (~p):~n~p~n", [PK1, PutError1]),
                throw(Put1Res);
            _ -> ok
        end,
        Put2Res = aspike_nif_test:cdt_put(Namespace, SetName, PK2, Bins, TTL),
        case Put2Res of
            {error, PutError2} ->
                io:format("aspike_nif_test:cdt_put() got error while writing PK2 (~p):~n~p~n", [PK2, PutError2]),
                throw(Put2Res);
            _ -> ok
        end,

        % then, delete all keys from the map in Bin1, and leave Bin2 intact, and we do this for both PKs
        aspike_nif_test:cdt_delete_by_keys_batch(Namespace, SetName, BinName1, [{PK1, [Key1, Key2]}, {PK2, [Key1, Key2]}]),

        % now read data back and validate PK1
        Read1 = aspike_nif_test:cdt_get(Namespace, SetName, PK1),
        case Read1 of
            {error, Error1} ->
                io:format("aspike_nif_test:cdt_get() got error while reading PK1 (~p):~n~p~n", [PK1, Error1]),
                throw(Read1);
            {ok, Data1} ->
                case Data1 of
                    [{BinName2, [Key2, {Value2,_,_}]}, {BinName1, []}] -> true;
                    _ ->
                        io:format("aspike_nif_test:cdt_get() doesn't match data from PK1 after aspike_nif_test:cdt_delete_by_keys_batch(). Data1 is:~n~p~n", [Data1]),
                        throw({error, wrong_data_match})
                end
        end,

        % read data back and validate PK2
        Read2 = aspike_nif_test:cdt_get(Namespace, SetName, PK2),
        case Read2 of
            {error, Error2} ->
                io:format("aspike_nif_test:cdt_get() got error while reading PK2 (~p):~n~p~n", [PK2, Error2]),
                throw(Read2);
            {ok, Data2} ->
                case Data2 of
                    [{BinName2, [Key2, {Value2,_,_}]}, {BinName1, []}] -> true;
                    _ ->
                        io:format("aspike_nif_test:cdt_get() doesn't match data from PK2 after aspike_nif_test:cdt_delete_by_keys_batch(). Data2 is:~n~p~n", [Data2]),
                        throw({error, wrong_data_match})
                end
        end,

        {ok, ok}
    end.

%% Segment Tag Get stress test action function
segment_tag_get_action_func(Namespace, SetName) ->
    fun(Counter) ->
        PK1 = <<<<"user1_">>/binary, (integer_to_binary(Counter))/binary>>,
        BinName3 = <<<<"bn3_">>/binary, (integer_to_binary(Counter))/binary>>,
        Value1 = <<<<"value1_">>/binary, (integer_to_binary(Counter))/binary>>,
        Value2 = <<<<"value2_">>/binary, (integer_to_binary(Counter))/binary>>,
        Value3 = <<<<"value3_">>/binary, (integer_to_binary(Counter))/binary>>,

        Map = #{
            key_one => Value1,
            "key_two" => Value2,
            <<"key_three">> => Value3
        },
        MapPutResult = aspike_nif:map_put(Namespace, SetName, PK1, BinName3, 5, Map),
        {ok, done} = MapPutResult,

        AsyncBinReadRes = aspike_nif_test:segment_tag_get(Namespace, SetName, PK1, BinName3),
        {ok, ReadMap} = AsyncBinReadRes,
        Value1 = maps:get(<<"key_one">>, ReadMap),
        Value2 = maps:get(<<"key_two">>, ReadMap),
        Value3 = maps:get(<<"key_three">>, ReadMap),

        {ok, ok}
    end.

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

