% -------------------------------------------------------------------------------

-module(aspike_nif).

-include("../include/defines.hrl").

-export([
    as_init/0,
    host_add/0,
    host_add/1,
    host_add/2,
    host_clear/0,
    host_info/1,
    host_info/3,
    host_list/0,
    connect/0,
    connect/2,
    key_exists/0,
    key_exists/1,
    key_exists/3,
    key_inc/0,
    key_inc/1,
    key_inc/2,
    key_inc/4,
    key_get/0,
    key_get/1,
    key_get/3,
    key_generation/0,
    key_generation/1,
    key_generation/3,
    key_put/0,
    key_put/1,
    key_put/2,
    key_put/4,
    key_select/0,
    key_select/1,
    key_select/2,
    key_select/4,
    key_remove/0,
    key_remove/1,
    key_remove/3,
    mk_args/2,
    node_random/0,
    node_names/0,
    node_get/1,
    node_info/2,
    help/0,
    help/1,
    nif_help/1,
    b/0,
    % --------------------------------------
    a_key_put/0,
    a_key_put/1,
    a_key_put/6,
    foo/1,
    bar/1,
    binary_put/5,
    binary_remove/5,
    binary_get/3,
    cdt_get/4,
    cdt_get/3,
    cdt_expire/4,
    cdt_delete_by_keys/5,
    cdt_delete_by_keys_batch/4,
    cdt_put/5,
    cdt_put/6
]).

-nifs([
    as_init/0,
    nif_host_add/2,
    host_clear/0,
    nif_host_list/0,
    connect/2,
    key_exists/3,
    key_inc/4,
    key_get/3,
    key_generation/3,
    key_put/4,
    key_remove/3,
    key_select/4,
    nif_node_random/0,
    nif_node_names/0,
    nif_node_get/1,
    nif_node_info/2,
    nif_help/1,
    nif_host_info/3,
    % --------------------------------------
    a_key_put/6,
    foo/1,
    bar/1,
    binary_put/5,
    binary_remove/5,
    binary_get/3,
    cdt_get/4,
    cdt_expire/4,
    cdt_delete_by_keys/5,
    cdt_delete_by_keys_batch/4,
    cdt_put/6
]).

% -------------------------------------------------------------------------------

-on_load(init/0).

-define(LIBNAME, ?MODULE).

% -------------------------------------------------------------------------------

init() ->
    erlang:load_nif(utils:find_lib(?LIBNAME), 0).

not_loaded(Line) ->
    erlang:nif_error({not_loaded, [{module, ?MODULE}, {line, Line}]}).

as_init() ->
    not_loaded(?LINE).

-spec host_add() -> {ok, string()} | {error, string()}.
host_add() ->
    host_add(?DEFAULT_HOST, ?DEFAULT_PORT).

% @doc Adds host's address and port; they will be used to establish connection
%
% Note (see https://discuss.aerospike.com/t/multiple-nics-in-the-aerospike-java-client/862):
%
% The Host(s) passed in the AerospikeClient constructor is only used to request the server
% host list and populate the cluster map.
% The multiple Hosts passed here are used only in case of network failure on the first one.
-spec host_add(string() | inet:ip_address(), non_neg_integer()) -> {ok, string()} | {error, string()}.
host_add(Host, Port) when is_list(Host), is_integer(Port) ->
    nif_host_add(Host, Port);
host_add(Host, Port) ->
    case inet:is_ip_address(Host) of 
        true -> host_add(inet:ntoa(Host), Port);
        false -> {error, "wrong address"}
        end.

nif_host_add(_, _) ->
    not_loaded(?LINE).

% @doc Adds list of [{HostName, Port}]
-spec host_add([{string(), non_neg_integer()}]) -> [{ok, string()} | {error, string()}].
host_add(HLst) ->
    [host_add(H, A) || {H, A} <- HLst].

% @doc Clears host list
-spec host_clear() -> {ok, string()}.
host_clear() ->
    not_loaded(?LINE).

% @doc Returns list of [{Hostname, TLSname, Port}]
%
% If there is no TLS then TLSname =[],, i.e. empty string.
-spec host_list() -> {ok, [{inet:ip_address(), string(), non_neg_integer()}]} | {error, term}.
host_list() ->
    as_render:hosts_render(nif_host_list()).

nif_host_list() ->
    not_loaded(?LINE).

connect() ->
    connect(?DEFAULT_USER, ?DEFAULT_PSW).

% @doc Create connection using User and PWd credential
-spec connect(string(), string()) -> {ok, string()} | {error, string()}.
connect(_, _) ->
    not_loaded(?LINE).

key_exists() ->
    key_exists(?DEFAULT_KEY).

key_exists(Key) ->
    key_exists(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% @doc Checks if Key exists in Namesplace Set
-spec key_exists(string(), string(), string()) -> {ok, string()} | {error, string()}.
key_exists(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    not_loaded(?LINE).

key_inc() ->
    key_inc([{"n-bin-111", 1}, {"n-bin-112", 10}, {"n-bin-113", -1}]).

key_inc(Lst) ->
    key_inc(?DEFAULT_KEY, Lst).

key_inc(Key, Lst) ->
    key_inc(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key, Lst).

% Changes value ob Bin by Val for Key in Namespace Set; here Lst is a list of tuples [{Bin, Val}].
-spec key_inc(string(), string(), string(), [{string(), integer()}]) ->
    {ok, string()} | {error, string()}.
key_inc(Namespace, Set, Key, Lst) when
    is_list(Namespace), is_list(Set), is_list(Key), is_list(Lst)
->
    not_loaded(?LINE).

key_put() ->
    key_put([{"n-bin-111", 1111}, {"n-bin-112", 1112}, {"n-bin-113", 1113}]).

key_put(Lst) ->
    key_put(?DEFAULT_KEY, Lst).

key_put(Key, Lst) ->
    key_put(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key, Lst).

% Sets values of Bin to Val for Key in Namespace Set; here Lst is a list of tuples [{Bin, Val}].
-spec key_put(string(), string(), string(), [{string(), integer()}]) ->
    {ok, string()} | {error, string()}.
key_put(Namespace, Set, Key, Lst) when
    is_list(Namespace), is_list(Set), is_list(Key), is_list(Lst)
->
    not_loaded(?LINE).

-spec binary_put(binary(), binary(), binary(), [{binary(), binary()|integer()|[integer()]}], integer()) -> 
    {ok, string()} | {error, string()}.
binary_put(_Namespace, _Set, _Key, _BinList, _TTL) ->
    not_loaded(?LINE).

% {MaxRetries, SleepBetweenRetries, SocketTimeout, TotalTimeout}  timeouts in milliseconds
cdt_put(Namespace, Set, Key, BinList, TTL) ->
    cdt_put(Namespace, Set, Key, BinList, TTL, {0, 0, 30000, 1000}).
-spec cdt_put(binary(), binary(), binary(), 
        [{binary(), binary()|integer()|[integer()]}], integer(), 
        {integer(), integer(), integer(), integer()}) -> 
            {ok, string()} | {error, string()}.
cdt_put(_Namespace, _Set, _Key, _BinList, _TTL, _Policy) ->
    not_loaded(?LINE).

-spec binary_remove(binary(), binary(), binary(), [binary()], integer()) -> 
    {ok, string()} | {error, string()}.
binary_remove(_Namespace, _Set, _Key, _BinNameList, _TTL) ->
    not_loaded(?LINE).

key_remove() ->
    key_remove(?DEFAULT_KEY).

key_remove(Key) ->
    key_remove(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% @doc Removes Key from  Namesplace Set
-spec key_remove(string(), string(), string()) -> {ok, string()} | {error, string()}.
key_remove(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    not_loaded(?LINE).

key_select() ->
    key_select(["n-bin-111", "n-bin-112", "n-bin-113"]).

key_select(Lst) ->
    key_select(?DEFAULT_KEY, Lst).

key_select(Key, Lst) ->
    key_select(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key, Lst).

% Gets value of Bin for Key in Namespace Set; here Lst is a list of [Bin].
-spec key_select(string(), string(), string(), [string()]) ->
    {ok, [{string(), term()}]} | {error, string()}.
key_select(Namespace, Set, Key, Lst) when
    is_list(Namespace), is_list(Set), is_list(Key), is_list(Lst)
->
    not_loaded(?LINE).

key_get() ->
    key_get(?DEFAULT_KEY).

key_get(Key) ->
    key_get(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% Gets values of all Bin for Key in Namespace Set.
-spec key_get(string(), string(), string()) -> {ok, [{string(), term()}]} | {error, string()}.
key_get(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    not_loaded(?LINE).

% Gets values of all Bin for Key in Namespace Set.
-spec binary_get(binary(), binary(), binary()) -> {ok, [{binary(), term()}]} | {error, string()}.
binary_get(Namespace, Set, Key) when is_binary(Namespace), is_binary(Set), is_binary(Key) ->
    not_loaded(?LINE).

cdt_get(Namespace, Set, Key) ->
    cdt_get(Namespace, Set, Key, {0, 0, 30000, 1000}).
% {MaxRetries, SleepBetweenRetries, SocketTimeout, TotalTimeout}  timeouts in milliseconds
-spec cdt_get(binary(), binary(), binary(), {integer(), integer(), integer(), integer()}) -> {ok, [{binary(), term()}]} | {error, string()}.
cdt_get(Namespace, Set, Key, _Policy) when is_binary(Namespace), is_binary(Set), is_binary(Key) ->
    not_loaded(?LINE).

-spec cdt_expire(binary(), binary(), binary(), integer()) -> {ok, [{binary(), term()}]} | {error, string()}.
cdt_expire(Namespace, Set, Key, TTL) when is_binary(Namespace), is_binary(Set), is_binary(Key), is_integer(TTL) ->
    not_loaded(?LINE).

-spec cdt_delete_by_keys(binary(), binary(), binary(), binary(), [binary()]) -> {ok, string()} | {error, string()}.
cdt_delete_by_keys(Namespace, Set, Key, BinName, SubkeysList) when is_binary(Namespace), is_binary(Set), is_binary(Key), is_binary(BinName), is_list(SubkeysList) ->
    not_loaded(?LINE).

-spec cdt_delete_by_keys_batch(binary(), binary(), binary(), [{binary(), [binary()]}]) -> {ok, [integer()]} | {error, string()}.
cdt_delete_by_keys_batch(Namespace, Set, BinName, KeysSubkeysList) when is_binary(Namespace), is_binary(Set), is_binary(BinName), is_list(KeysSubkeysList) ->
    not_loaded(?LINE).

key_generation() ->
    key_generation(?DEFAULT_KEY).

key_generation(Key) ->
    key_generation(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% Gets Generation number and TTL for Key in Namespace Set.
-spec key_generation(string(), string(), string()) -> {ok, map()} | {error, string()}.
key_generation(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    not_loaded(?LINE).

% @doc Returns random node in form {Address:Port}, for example: {{127,0,0,1},3010}
-spec node_random() -> {ok, {inet:ip_address(), non_neg_integer()}} | {error, term()}.
node_random() ->
    as_render:node_render(nif_node_random()).

nif_node_random() ->
    not_loaded(?LINE).

% @doc Returns list of node names and addresses.
-spec node_names() -> {ok, [{string(), {inet:ip_address(), non_neg_integer()}}]} | {error, term()}.
node_names() ->
    case nif_node_names() of
        {ok, L} ->
            T = [{N, as_render:node_render({ok, A})} || {N, A} <- L],
            {ok, [{N, Y} || {N, {X, Y}} <- T, X == ok]};
        Any ->
            Any
    end.

nif_node_names() ->
    not_loaded(?LINE).

% @doc Returns node in form {Address:Port}, for example: {{127,0,0,1},3010}
-spec node_get(string()) -> {ok, {inet:ip_address(), non_neg_integer()}} | {error, term()}.
node_get(NodeName) when is_list(NodeName) ->
    as_render:node_render(nif_node_get(NodeName)).

nif_node_get(NodeName) when is_list(NodeName) ->
    not_loaded(?LINE).

% @doc Returns information about Item for NodeName
% Useful Items:
% "bins", "sets", "node", "namespaces", "udf-list", "sindex-list:", "edition", "get-config"
-spec node_info(string(), string()) -> {ok, {string(), map()}} | {error, string()}.
node_info(NodeName, Item) when is_list(NodeName), is_list(Item) ->
    as_render:info_render(nif_node_info(NodeName, Item), Item).

nif_node_info(_, _) ->
    not_loaded(?LINE).

help() ->
    help("namespaces").

% @doc Returns information about Item.
% Useful Items:
% "bins", "sets", "node", "namespaces", "udf-list", "sindex-list:", "edition", "get-config"
-spec help(string()) -> {ok, {string(), map()}} | {error, string()}.
help(Item) when is_list(Item) ->
    as_render:info_render(nif_help(Item), Item).

nif_help(_) ->
    not_loaded(?LINE).

-spec host_info(string()) -> {ok, [string()]} | {error, string()}.
host_info(Item) ->
    host_info(?DEFAULT_HOST, ?DEFAULT_PORT, Item).

% @doc @doc Returns information about Item for HostName, Port
% Useful Items:
% "bins", "sets", "node", "namespaces", "udf-list", "sindex-list:", "edition", "get-config"
-spec host_info(string(), non_neg_integer(), string()) ->
    {ok, {string(), map()}} | {error, string()}.
host_info(HostName, Port, Item) when is_list(HostName), is_integer(Port), is_list(Item) ->
    as_render:info_render(nif_host_info(HostName, Port, Item), Item).

nif_host_info(_, _, _) ->
    not_loaded(?LINE).

% @doc Shortcut for testing
-spec b() -> ok.
b() ->
    as_init(),
    host_add(),
    connect(),
    ok.

% @doc Used in ${tsl.erl} to create argument list for testin function
-spec mk_args(atom(), non_neg_integer()) -> [term()].
mk_args(_, _) -> [].

% -----------------------------------------------------------------
a_key_put() ->
    a_key_put(10000).

a_key_put(Rep) ->
    a_key_put("erl-bin-nif", 999, ?DEFAULT_NAMESPACE, ?DEFAULT_SET, ?DEFAULT_KEY, Rep).

a_key_put(_, _, _, _, _, _) ->
    not_loaded(?LINE).

foo(_X) ->
    exit(nif_library_not_loaded).

bar(_Y) ->
    exit(nif_library_not_loaded).

% -----------------------------------------------------------------
% 2> tsl:tst(aspike_nif, key_put, 0, 10000).
% aspike_nif:key_put, N=0, R=10000, Time=580.5629
% 3> tsl:tst(aspike_nif, key_put, 0, 100000).
% aspike_nif:key_put, N=0, R=100000, Time=588.68821
% -----------------------------------------------------------------


