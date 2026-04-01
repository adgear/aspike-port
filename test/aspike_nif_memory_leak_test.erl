-module(aspike_nif_memory_leak_test).

-export([
    run/0
]).

-define(ASPIKE_DEFAULT_POLICY, {3, 250, 30000, 1000}).

run() ->
    aspike_nif_test:init(),
    init_tester(),

    AmountOfOps = 100_000_000,
    AmountOfClients = 50,

    Namespace = <<"test">>,
    SetName = <<"test_set">>,
    ActionFunc = fun(Counter) ->
        {RecordKeyName, {BinName1, Key1, Value1}, {BinName2, Key2, Value2}} = aspike_nif_test_utils:get_test_data_from_counter(Counter),
        % No need to keep the record longer than 5 seconds
        Bins = [{BinName1, [Key1, Value1, Counter, Key2, Value2, Counter]}, {BinName2, [Key2, Value2, Counter]}],
        PK1 = <<<<"user1_">>/binary, (integer_to_binary(Counter))/binary>>,
        PK2 = <<<<"user2_">>/binary, (integer_to_binary(Counter))/binary>>,
        BinName3 = <<<<"bn3_">>/binary, (integer_to_binary(Counter))/binary>>,
        Value3 = <<<<"value3_">>/binary, (integer_to_binary(Counter))/binary>>,
        aspike_nif_test:cdt_put(Namespace, SetName, RecordKeyName, Bins, 5),
        aspike_nif_test:cdt_get(Namespace, SetName, RecordKeyName),
        aspike_nif_test:cdt_delete_by_keys(Namespace, SetName, RecordKeyName, BinName1, [Key1]),
        aspike_nif_test:cdt_put(Namespace, SetName, PK1, Bins, 5),
        aspike_nif_test:cdt_put(Namespace, SetName, PK2, Bins, 5),
        aspike_nif_test:cdt_delete_by_keys_batch(Namespace, SetName, BinName1, [{PK1, [Key1, Key2]}, {PK2, [Key1, Key2]}]),
        Map = #{
            key_one => Value1,
            "key_two" => Value2,
            <<"key_three">> => Value3
        },
        aspike_nif:map_put(Namespace, SetName, PK1, BinName3, 5, Map),
        aspike_nif_test:segment_tag_get(Namespace, SetName, PK1, BinName3),
        {ok, ok}
    end,

    aspike_nif_test_utils:memory_leak_test(ActionFunc, AmountOfOps, AmountOfClients),
    ok.

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

