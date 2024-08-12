
-module(aspike_srv).

-behaviour(gen_server).

-include("../include/defines.hrl").

% -------------------------------------------------------------------------------

-define(LIBNAME, aspike_port).

-ifndef(TEST).
-define(TEST, true).
-endif.

% -------------------------------------------------------------------------------

% API
-export([
    start_link/0
]).

-ifdef(TEST).
-export([
    start/0
]).
-endif.

-export([
    mk_args/2,
    b/0
]).

% gen_server callbacks
-export([
    init/1,
    handle_call/3,
    handle_cast/2,
    handle_info/2,
    terminate/2
]).

-export([
    aerospike_init/0,
    command/1,
    connect/0,
    connect/2,
    config_info/0,
    cluster_info/0,
    host_add/0,
    host_add/1,
    host_add/2,
    host_info/1,
    host_clear/0,
    host_info/3,
    host_list/0,
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
    binary_key_get/3,
    key_generation/0,
    key_generation/1,
    key_generation/3,
    key_put/0,
    key_put/1,
    key_put/2,
    key_put/4,
    binary_key_put/5,
    key_select/0,
    key_select/1,
    key_select/2,
    key_select/4,
    key_remove/0,
    key_remove/1,
    key_remove/3,
    node_random/0,
    node_names/0,
    node_get/1,
    node_info/2,
    port_status/0,
    port_info/0,
    help/0,
    help/1,
    % ------
    bar/1,
    foo/1
]).

-record(state, {
    ext_prg :: string(),
    port :: port()
}).

-type state() :: #state{}.

% -------------------------------------------------------------------------------
%  API
% -------------------------------------------------------------------------------

% @doc Shortcut for testing
-spec b() -> ok.
b() ->
    host_add(),
    connect(),
    ok.

% @doc Used in ${tsl.erl} to create argument list for testin function
-spec mk_args(atom(), non_neg_integer()) -> [term()].
mk_args(_, _) -> [].

-ifdef(TEST).
start() ->
    gen_server:start({local, ?MODULE}, ?MODULE, utils:find_lib(?LIBNAME), []).
-endif.

start_link() ->
    gen_server:start_link({local, ?MODULE}, ?MODULE, utils:find_lib(?LIBNAME), []).

% @doc This function is used to execute all other call.
-spec command(term()) -> term().
command(Cmd) ->
    gen_server:call(?MODULE, {command, Cmd}, ?DEFAULT_TIMEOUT + 10).

% @doc Initialises port (c level) global variables.
% Is called automatically during init
-spec aerospike_init() -> {ok, string()} | {error, string()}.
aerospike_init() ->
    command({aerospike_init}).

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
    command({host_add, Host, Port});
host_add(Host, Port) ->
    case inet:is_ip_address(Host) of 
        true -> host_add(inet:ntoa(Host), Port);
        false -> {error, "wrong address"}
        end.

% @doc Adds list of [{HostName, Port}]
-spec host_add([{string(), non_neg_integer()}]) -> [{ok, string()} | {error, string()}].
host_add(HLst) ->
    [host_add(H, A) || {H, A} <- HLst].

% @doc Clears host list
-spec host_clear() -> {ok, string()}.
host_clear() ->
    command({host_clear}).

% @doc Returns list of [{Hostname, TLSname, Port}]
%
%there is no TLS then TLSname =[]
-spec host_list() -> {ok, [{inet:ip_address(), string(), non_neg_integer()}]} | {error, term}.
host_list() ->
    as_render:hosts_render(command({host_list})).

-spec connect() -> {ok, string()} | {error, string()}.
connect() ->
    connect(?DEFAULT_USER, ?DEFAULT_PSW).

% @doc Create connection using User and PWd credential
-spec connect(string(), string()) -> {ok, string()} | {error, string()}.
connect(User, Pwd) when is_list(User), is_list(Pwd) ->
    command({connect, User, Pwd}).

key_exists() ->
    key_exists(?DEFAULT_KEY).

key_exists(Key) ->
    key_exists(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% @doc Checks if Key exists in Namesplace Set
-spec key_exists(string(), string(), string()) -> {ok, string()} | {error, string()}.
key_exists(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    command({key_exists, Namespace, Set, Key}).

key_inc() ->
    key_inc([{"p-bin-111", 1}, {"p-bin-112", 10}, {"p-bin-113", -1}]).

key_inc(Lst) ->
    key_inc(?DEFAULT_KEY, Lst).

key_inc(Key, Lst) ->
    key_inc(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key, Lst).

% Changes values of Bin by Val for Key in Namespace Set; here Lst is a list of tuples [{Bin, Val}].
-spec key_inc(string(), string(), string(), [{string(), integer()}]) ->
    {ok, string()} | {error, string()}.
key_inc(Namespace, Set, Key, Lst) when
    is_list(Namespace), is_list(Set), is_list(Key), is_list(Lst)
->
    command({key_inc, Namespace, Set, Key, Lst}).

key_put() ->
    key_put([{"p-bin-111", 1111}, {"p-bin-112", 1112}, {"p-bin-113", 1113}]).

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
    command({key_put, Namespace, Set, Key, Lst}).

-spec binary_key_put(binary(), binary(), binary(), [{binary(), binary()|integer()}], integer()) ->
    {ok, string()} | {error, string()}.
binary_key_put(Namespace, Set, Key, Lst, TTL) when
    is_binary(Namespace), is_binary(Set), is_binary(Key), is_integer(TTL) ->
    command({binary_key_put, Namespace, Set, Key, Lst, TTL}).

key_remove() ->
    key_remove(?DEFAULT_KEY).

key_remove(Key) ->
    key_remove(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% @doc Removes Key from  Namesplace Set
-spec key_remove(string(), string(), string()) -> {ok, string()} | {error, string()}.
key_remove(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    command({key_remove, Namespace, Set, Key}).

key_select() ->
    key_select(["p-bin-111", "p-bin-112", "p-bin-113"]).

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
    command({key_select, Namespace, Set, Key, Lst}).

key_get() ->
    key_get(?DEFAULT_KEY).

key_get(Key) ->
    key_get(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% Gets values of all Bin for Key in Namespace Set.
-spec key_get(string(), string(), string()) -> {ok, [{string(), term()}]} | {error, string()}.
key_get(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    command({key_get, Namespace, Set, Key}).

-spec binary_key_get(binary(), binary(), binary()) -> {ok, [{string(), term()}]} | {error, string()}.
binary_key_get(Namespace, Set, Key) when is_binary(Namespace), is_binary(Set), is_binary(Key) ->
    command({binary_key_get, Namespace, Set, Key}).

key_generation() ->
    key_generation(?DEFAULT_KEY).

key_generation(Key) ->
    key_generation(?DEFAULT_NAMESPACE, ?DEFAULT_SET, Key).

% Gets Generation number and TTL for Key in Namespace Set.
-spec key_generation(string(), string(), string()) -> {ok, map()} | {error, string()}.
key_generation(Namespace, Set, Key) when is_list(Namespace), is_list(Set), is_list(Key) ->
    command({key_generation, Namespace, Set, Key}).

% @doc Returns aerospike configuration
-spec config_info() -> {ok, map()} | {error, term()}.
config_info() ->
    command({config_info}).

% @doc Returns aerospike cluster configuration
-spec cluster_info() -> {ok, map()} | {error, term()}.
cluster_info() ->
    command({cluster_info}).

% @doc Returns random node in form {Address:Port}, for example: {{127,0,0,1},3010}
-spec node_random() -> {ok, {inet:ip_address(), non_neg_integer()}} | {error, term()}.
node_random() ->
    as_render:node_render(command({node_random})).

% @doc Returns list of node names and addresses.
-spec node_names() -> {ok, [{string(), {inet:ip_address(), non_neg_integer()}}]} | {error, term()}.
node_names() ->
    case command({node_names}) of
        {ok, L} ->
            T = [{N, as_render:node_render({ok, A})} || {N, A} <- L],
            {ok, [{N, Y} || {N, {X, Y}} <- T, X == ok]};
        Any ->
            Any
    end.

% @doc Returns node in form {Address:Port}, for example: {{127,0,0,1},3010}
-spec node_get(string()) -> {ok, {inet:ip_address(), non_neg_integer()}} | {error, term()}.
node_get(NodeName) when is_list(NodeName) ->
    as_render:node_render(command({node_get, NodeName})).

% @doc Returns information about Item for NodeName
% Useful Items:
% "bins", "sets", "node", "namespaces", "udf-list", "sindex-list:", "edition", "get-config"
-spec node_info(string(), string()) -> {ok, {string(), map()}} | {error, string()}.
node_info(NodeName, Item) when is_list(NodeName), is_list(Item) ->
    as_render:info_render(command({node_info, NodeName, Item}), Item).

-spec help() -> {ok, string()} | {error, term()}.
help() -> help("namespaces").

% @doc Returns information about Item.
% Useful Items:
% "bins", "sets", "node", "namespaces", "udf-list", "sindex-list:", "edition", "get-config"
-spec help(string()) -> {ok, {string(), map()}} | {error, string()}.
help(Item) when is_list(Item) ->
    as_render:info_render(command({help, Item}), Item).

-spec host_info(string()) -> {ok, {string(), map()}} | {error, string()}.
host_info(Item) ->
    host_info(?DEFAULT_HOST, ?DEFAULT_PORT, Item).

% @doc @doc Returns information about Item for HostName, Port
% Useful Items:
% "bins", "sets", "node", "namespaces", "udf-list", "sindex-list:", "edition", "get-config"
-spec host_info(string(), non_neg_integer(), string()) ->
    {ok, {string(), map()}} | {error, string()}.
host_info(HostName, Port, Item) when is_list(HostName), is_integer(Port), is_list(Item) ->
    as_render:info_render(command({host_info, HostName, Port, Item}), Item).

% @doc Returns information about Erlang PORT
-spec port_info() -> [tuple()] | undefined.
port_info() ->
    gen_server:call(?MODULE, port_info).

% @doc Returns status of aspike_port
-spec port_status() -> {ok, map()} | {error, term()}.
port_status() ->
    command({port_status}).

-spec foo(integer()) -> {ok, integer()} | {error, term()}.
foo(X) when is_integer(X) ->
    command({foo, X}).

-spec bar(integer()) -> {ok, integer()} | {error, term()}.
% bar(X) when is_integer(X) ->
bar(X) ->
    command({bar, X}).

% -------------------------------------------------------------------------------
% Callbacks
% -------------------------------------------------------------------------------

-spec init(string()) -> {ok, state()}.
init(ExtPrg) ->
    process_flag(trap_exit, true),
    Port = open_port({spawn_executable, ExtPrg}, [{packet, 2}, binary, nouse_stdio]),
    spawn(fun aerospike_init/0),
    {ok, #state{ext_prg = ExtPrg, port = Port}}.

handle_call({command, Msg}, {Caller, _}, State = #state{port = Port}) ->
    Res = call_port(Caller, Port, Msg),
    {reply, Res, State};
handle_call(port_info, _, State = #state{port = Port}) ->
    Res = erlang:port_info(Port),
    {reply, Res, State};
handle_call(Msg, _From, State) ->
    io:format("~p:~p Msg = ~p~n", [?MODULE, ?FUNCTION_NAME, Msg]),
    {noreply, State}.

handle_cast({command, Msg}, State = #state{port = Port}) ->
    Port ! {self(), {command, term_to_binary(Msg)}},
    {noreply, State};
handle_cast(Msg, State) ->
    io:format("~p:~p Msg = ~p~n", [?MODULE, ?FUNCTION_NAME, Msg]),
    {noreply, State}.

handle_info(Msg, State) ->
    io:format("~p:~p Msg = ~p~n", [?MODULE, ?FUNCTION_NAME, Msg]),
    {noreply, State}.

terminate(_Reason, _State) ->
    ok.

% -------------------------------------------------------------------------------
% helpers
% -------------------------------------------------------------------------------
-spec call_port(pid(), pid(), term()) -> {ok, term()} | {error, timeout}.
call_port(Caller, Port, Msg) ->
    TbMsg = term_to_binary(Msg),
    Port ! {self(), {command, TbMsg}},
    receive
        {_Port, {data, Data}} -> Caller ! binary_to_term(Data)
    after ?DEFAULT_TIMEOUT -> {error, timeout_is_out}
    end.

% -------------------------------------------------------------------------------
    % aql -h 127.0.0.1:3010
    % asadm -e info
    % 
    % docker run -d --name aerospike -p 3010-3012:3000-3002 aerospike/aerospike-server-enterprise
    % 
    % make EVENT_LIB=libev
% -------------------------------------------------------------------------------
