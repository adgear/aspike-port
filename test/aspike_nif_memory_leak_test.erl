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
        cdt_put(Namespace, SetName, RecordKeyName, Bins, 5),
        cdt_get(Namespace, SetName, RecordKeyName),
        cdt_delete_by_keys(Namespace, SetName, RecordKeyName, BinName1, [Key1]),
        cdt_put(Namespace, SetName, PK1, Bins, 5),
        cdt_put(Namespace, SetName, PK2, Bins, 5),
        cdt_delete_by_keys_batch(Namespace, SetName, BinName1, [{PK1, [Key1, Key2]}, {PK2, [Key1, Key2]}]),
        Map = #{
            key_one => Value1,
            "key_two" => Value2,
            <<"key_three">> => Value3
        },
        aspike_nif:map_put(Namespace, SetName, PK1, BinName3, 5, Map),
        segment_tag_get(Namespace, SetName, PK1, BinName3),
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

%% Helper functions - delegate to aspike_nif_test for the API functions
cdt_put(Namespace, Set, RecordKeyName, BinList, TTL) ->
    cdt_put(Namespace, Set, RecordKeyName, BinList, TTL, ?ASPIKE_DEFAULT_POLICY).

cdt_put(Namespace, Set, RecordKeyName, BinList, TTL, Policy) ->
    case aspike_nif_test:get_api_mode(cdt_put) of
        sync ->
            aspike_nif:cdt_put_sync(Namespace, Set, RecordKeyName, BinList, TTL, Policy);
        async ->
            AsyncCmd = fun(Ref) ->
                aspike_nif:cdt_put_async(Ref, Namespace, Set, RecordKeyName, BinList, TTL, Policy)
            end,
            call_aerospike_async_nif(AsyncCmd)
    end.

cdt_get(Namespace, Set, RecordKeyName) ->
    cdt_get(Namespace, Set, RecordKeyName, ?ASPIKE_DEFAULT_POLICY).

cdt_get(Namespace, Set, RecordKeyName, Policy) ->
    case aspike_nif_test:get_api_mode(cdt_get) of
        sync ->
            aspike_nif:cdt_get_sync(Namespace, Set, RecordKeyName, Policy);
        async ->
            AsyncCmd = fun(Ref) -> aspike_nif:cdt_get_async(Ref, Namespace, Set, RecordKeyName, Policy) end,
            call_aerospike_async_nif(AsyncCmd)
    end.

segment_tag_get(Namespace, Set, RecordKeyName, BinName) ->
    case aspike_nif_test:get_api_mode(segment_tag_get) of
        sync ->
            aspike_nif:segment_tag_get_sync(Namespace, Set, RecordKeyName, BinName);
        async ->
            AsyncCmd = fun(Ref) -> aspike_nif:segment_tag_get_async(Ref, Namespace, Set, RecordKeyName, BinName) end,
            call_aerospike_async_nif(AsyncCmd)
    end.

cdt_delete_by_keys(Namespace, Set, RecordKeyName, BinName, SubkeysList) ->
    case aspike_nif_test:get_api_mode(cdt_delete_by_keys) of
        sync ->
            aspike_nif:cdt_delete_by_keys_sync(Namespace, Set, RecordKeyName, BinName, SubkeysList);
        async ->
            AsyncCmd = fun(Ref) ->
                aspike_nif:cdt_delete_by_keys_async(Ref, Namespace, Set, RecordKeyName, BinName, SubkeysList)
                       end,
            call_aerospike_async_nif(AsyncCmd)
    end.

cdt_delete_by_keys_batch(Namespace, Set, BinName, KeysToRemove) ->
    case aspike_nif_test:get_api_mode(cdt_delete_by_keys) of
        sync ->
            aspike_nif:cdt_delete_by_keys_batch_sync(Namespace, Set, BinName, KeysToRemove);
        async ->
            AsyncCmd = fun(Ref) ->
                aspike_nif:cdt_delete_by_keys_batch_async(Ref, Namespace, Set, BinName, KeysToRemove)
                       end,
            call_aerospike_async_nif(AsyncCmd)
    end.

call_aerospike_async_nif(AsyncCmd) ->
    % Generate a unique reference for this async operation
    Ref = make_ref(),

    % we define time to wait (TTW) much higher compare to prod values
    % because dev machines are not so powerful. Value in milliseconds.
    TTW = 1000,

    case AsyncCmd(Ref) of
        {ok, in_progress} ->
            receive_with_ref_filter(Ref, TTW);
        {ok, Response} ->
            {ok, Response};
        {error, {_NifErrorCode, _AspikeErrorCode, ErrorMessage}} ->
            {error, ErrorMessage}
    end.

% Helper function to receive messages with reference filtering
receive_with_ref_filter(ExpectedRef, TTW) ->
    receive
        {ok, ReceivedRef, Response} when ReceivedRef =:= ExpectedRef ->
            {ok, Response};
        {error, ReceivedRef, {_NifErrorCode, _AspikeErrorCode, ErrorMessage}} when ReceivedRef =:= ExpectedRef ->
            {error, ErrorMessage};
        % Handle messages with wrong references (stale messages)
        {ok, WrongRef, _} when WrongRef =/= ExpectedRef ->
            % This is a stale message from a previous operation, ignore it
            io:format("Received wrong REF (~p) on OK response. I was expecting ~p~n", [WrongRef, ExpectedRef]),
            receive_with_ref_filter(ExpectedRef, TTW);
        {error, WrongRef, {_, _, _}} when WrongRef =/= ExpectedRef ->
            io:format("Received wrong REF (~p) on Error response. I was expecting ~p~n", [WrongRef, ExpectedRef]),
            % This is a stale error message from a previous operation, ignore it
            receive_with_ref_filter(ExpectedRef, TTW);
        _Other ->
            % Unknown message format, ignore and continue
            receive_with_ref_filter(ExpectedRef, TTW)
    after TTW ->
        {error, <<"timeout waiting for the response from aerospike">>}
    end.