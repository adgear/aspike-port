-module(aspike_nif_quick_test).

-export([
    run/0
]).

-define(ASPIKE_DEFAULT_POLICY, {3, 250, 30000, 1000}).

run() ->
    aspike_nif_test:init(),
    Namespace = <<"test">>,
    SetName = <<"rtb_setname">>,
    PK1 = <<"user_1">>,
    Bins1 = [
        {<<"profile">>, [<<"first_name">>, <<0, 1, 0, 2, 1>>, 123, <<"last_name">>, <<0, 1, 0, 2, 2>>, 456]},
        {<<"settings">>, [<<"theme">>, <<0, 1, 0, 2, 3>>, 345, <<"reload">>, <<0, 1, 0, 2, 4>>, 678]}
    ],

    try
        % Test CDT put operation
        InsertRes = cdt_put(Namespace, SetName, PK1, Bins1, 300),
        case InsertRes of
            {ok, "put"} -> ok;
            {ok, <<"put">>} -> ok;
            {error, InsertError} ->
                io:format("ERROR: cdt_put failed with: ~p~n", [InsertError]),
                halt(1);
            InsertOther ->
                io:format("ERROR: cdt_put returned unexpected result: ~p~n", [InsertOther]),
                halt(1)
        end,

        % Test CDT get operation and data validation
        ReadRes = cdt_get(Namespace, SetName, PK1),
        case ReadRes of
            {ok, ReadData} ->
                case aspike_nif_test_utils:compare_cdt_data(Bins1, ReadData) of
                    true -> ok;
                    false ->
                        io:format("ERROR: Read data doesn't match inserted data~n"),
                        io:format("Expected: ~p~n", [Bins1]),
                        io:format("Got: ~p~n", [ReadData]),
                        halt(1)
                end;
            {error, ReadError} ->
                io:format("ERROR: cdt_get failed with: ~p~n", [ReadError]),
                halt(1)
        end,

        % Test cdt_delete_by_keys functionality
        BinName = <<"profile">>,
        KeysToDelete = [<<"first_name">>, <<"last_name">>],
        DeleteRes = cdt_delete_by_keys(Namespace, SetName, PK1, BinName, KeysToDelete),
        case DeleteRes of
            {ok, "keys_deleted"} -> ok;
            {error, DeleteError} ->
                io:format("ERROR: cdt_delete_by_keys failed with: ~p~n", [DeleteError]),
                halt(1);
            DeleteOther ->
                io:format("ERROR: cdt_delete_by_keys returned unexpected result: ~p~n", [DeleteOther]),
                halt(1)
        end,

        % Verify deletion by reading again
        ReadAfterDeleteRes = cdt_get(Namespace, SetName, PK1),
        case ReadAfterDeleteRes of
            {ok, ReadAfterDeleteData} ->
                ExpectedData = [
                    {<<"profile">>, []},
                    {<<"settings">>, [<<"theme">>, <<0, 1, 0, 2, 3>>, 345, <<"reload">>, <<0, 1, 0, 2, 4>>, 678]}
                ],
                case aspike_nif_test_utils:compare_cdt_data(ExpectedData, ReadAfterDeleteData) of
                    true -> ok;
                    false ->
                        io:format("ERROR: Data after delete doesn't match expected result~n"),
                        io:format("Expected: ~p~n", [ExpectedData]),
                        io:format("Got: ~p~n", [ReadAfterDeleteData]),
                        halt(1)
                end;
            {error, ReadAfterDeleteError} ->
                io:format("ERROR: cdt_get after delete failed with: ~p~n", [ReadAfterDeleteError]),
                halt(1)
        end,

        % Test map operations
        Map = #{
            key_one => <<"value_1">>,
            "key_two" => <<"value_2">>,
            <<"key_three">> => <<"value_3">>
        },
        MapPutResult = aspike_nif:map_put(Namespace, SetName, PK1, <<"profile2">>, 5, Map),
        case MapPutResult of
            {ok, done} -> ok;
            {error, MapError} ->
                io:format("ERROR: map_put failed with: ~p~n", [MapError]),
                halt(1);
            MapOther ->
                io:format("ERROR: map_put returned unexpected result: ~p~n", [MapOther]),
                halt(1)
        end,

        % Test segment_tag_get operation
        AsyncBinReadRes = segment_tag_get(Namespace, SetName, PK1, <<"profile2">>),
        case AsyncBinReadRes of
            {ok, ReadMap} when is_map(ReadMap) ->
                % Verify we can read the expected values
                try
                    <<"value_1">> = maps:get(<<"key_one">>, ReadMap),
                    <<"value_2">> = maps:get(<<"key_two">>, ReadMap),
                    <<"value_3">> = maps:get(<<"key_three">>, ReadMap),
                    ok
                catch
                    error:{badkey, Key} ->
                        io:format("ERROR: segment_tag_get result missing expected key: ~p~n", [Key]),
                        io:format("Got map: ~p~n", [ReadMap]),
                        halt(1);
                    error:{badmatch, _} ->
                        io:format("ERROR: segment_tag_get result has wrong values~n"),
                        io:format("Got map: ~p~n", [ReadMap]),
                        halt(1)
                end;
            {error, SegmentError} ->
                io:format("ERROR: segment_tag_get failed with: ~p~n", [SegmentError]),
                halt(1);
            SegmentOther ->
                io:format("ERROR: segment_tag_get returned unexpected result: ~p~n", [SegmentOther]),
                halt(1)
        end,

        io:format("All done, all works fine~n", []),
        halt()

    catch
        Class:Reason:Stacktrace ->
            io:format("ERROR: Unexpected exception occurred~n"),
            io:format("Class: ~p~n", [Class]),
            io:format("Reason: ~p~n", [Reason]),
            io:format("Stacktrace: ~p~n", [Stacktrace]),
            halt(1)
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