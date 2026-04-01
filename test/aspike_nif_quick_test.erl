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
        InsertRes = aspike_nif_test:cdt_put(Namespace, SetName, PK1, Bins1, 300),
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
        ReadRes = aspike_nif_test:cdt_get(Namespace, SetName, PK1),
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
        DeleteRes = aspike_nif_test:cdt_delete_by_keys(Namespace, SetName, PK1, BinName, KeysToDelete),
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
        ReadAfterDeleteRes = aspike_nif_test:cdt_get(Namespace, SetName, PK1),
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
        AsyncBinReadRes = aspike_nif_test:segment_tag_get(Namespace, SetName, PK1, <<"profile2">>),
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

