--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local ffi = require 'ffi'
local pl = require 'pl.import_into'()
local utils = require('utils.utils')
local common = require("common.common")
local goutils = require('utils.goutils')
local board = require('board.board')

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "mctsv2/playout_multithread.h"))
local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "playout_multithread.h"))
local C = ffi.load(paths.concat(script_path, "../libs/libplayout_multithread.so"))
local playout = {}

playout.params = ffi.new("SearchParamsV2")
playout.tree_params = ffi.new("TreeParams")
C.ts_v2_init_params(playout.params)
C.tree_search_init_params(playout.tree_params)

playout.default_typename = {
    [0] = "normal",
    [1] = "ko_fight",
    [2] = "opponent_atari",
    [3] = "our_atari",
    [4] = "nakade",
    [5] = "pattern",
    [6] = 'no_move'
}

-- S_DEAD = 8
playout.dead_white = 8 + common.white
playout.dead_black = 8 + common.black

playout.server_local = tonumber(symbols.SERVER_LOCAL)
playout.server_cluster = tonumber(symbols.SERVER_CLUSTER)

playout.dp_simple = tonumber(symbols.DP_SIMPLE)
playout.dp_pachi = tonumber(symbols.DP_PACHI)
playout.dp_v2 = tonumber(symbols.DP_V2)
playout.dp_table = {
    simple = playout.dp_simple,
    pachi = playout.dp_pachi,
    v2 = playout.dp_v2
}

playout.thres_ply1 = tonumber(symbols.THRES_PLY1)
playout.thres_ply2 = tonumber(symbols.THRES_PLY2)
playout.thres_ply3 = tonumber(symbols.THRES_PLY3)
playout.thres_time_close = tonumber(symbols.THRES_TIME_CLOSE)
playout.min_time_spent = tonumber(symbols.MIN_TIME_SPENT)

function playout.getParams(p)
    for k, v in pairs(p) do
        if playout.params[k] then
             playout.params[k] = p[k]
        end
    end
end

function playout.new(rs, b)
    assert(rs.rollout)
    assert(rs.rollout_per_move)
    assert(rs.dcnn_rollout_per_move)

    playout.tree_params.num_rollout = rs.rollout
    playout.tree_params.num_rollout_per_move = rs.rollout_per_move
    playout.tree_params.num_dcnn_per_move = rs.dcnn_rollout_per_move
    local tr = C.ts_v2_init(playout.params, playout.tree_params, b)
    C.ts_v2_search_start(tr)
    return tr
end

function playout.set_board(tr, b)
    C.ts_v2_setboard(tr, b)
end

function playout.set_tsumego_mode(tr, b, margin)
    margin = margin or 1
    playout.tree_params.ld_region = board.get_stones_bbox(b)
    playout.tree_params.life_and_death_mode = common.TRUE
    playout.tree_params.use_tsumego_dcnn = common.FALSE
    playout.tree_params.defender = common.opponent(board.get_attacker(b, playout.tree_params.ld_region))

    board.expand_region(playout.tree_params.ld_region, margin)
    -- Set the board and parameters.
    -- The order matters. First change the parameters and then set the board (otherwise you see ghost moves). Finally set the move history.
    C.ts_v2_set_params(tr, nil, playout.tree_params)
    C.ts_v2_setboard(tr, b)

    local moves = board.get_all_stones(b)
    for i = 1, #moves do
        local m, player = unpack(moves[i])
        C.ts_v2_add_move_history(tr, m, player, common.FALSE)
    end
end

function playout.add_move_history(tr, x, y, player)
    local m = C.compose_move(x - 1, y - 1, player)
    C.ts_v2_add_move_history(tr, m.m, m.player, common.FALSE)
end

function playout.restart(tr, b)
    -- Restart MCTS with a given board.
    -- C.ts_v2_restart(tr, b)
    C.ts_v2_setboard(tr, b)
    C.ts_v2_set_params(tr, playout.params, playout.tree_params)
end

-- Set the parameters on the fly. The following params can be set:
local params_changable_on_the_fly = {
    verbose = true,
    print_search_tree = true,
    komi = true,
    dynkomi_factor = true
}

local tree_params_changable_on_the_fly = {
    num_rollout = true,
    num_rollout_per_move = true,
    verbose = true,
    sigma = true,
    decision_mixture_ratio = true,
    rcv_acc_percent_thres = true,
    rcv_max_num_move = true,
    rcv_min_num_move = true,
    use_pondering = true,
    single_move_return = true,
    min_rollout_peekable = true
}

function playout.set_params(tr, params)
    for k, v in pairs(params) do
        if not params_changable_on_the_fly[k] and not tree_params_changable_on_the_fly[k] then return false end
    end
    -- Then we loop things again and actually change the parameters.
    local any_params_change = false
    local any_tree_params_change = false
    for k, v in pairs(params) do
        if params_changable_on_the_fly[k] and playout.params[k] ~= params[k] then
            playout.params[k] = params[k]
            any_params_change = true
        end
        if tree_params_changable_on_the_fly[k] and playout.tree_params[k] ~= params[k] then
            playout.tree_params[k] = params[k]
            any_tree_params_change = true
        end
    end
    if any_params_change and any_tree_params_change then
        return C.ts_v2_set_params(tr, playout.params, playout.tree_params) == common.TRUE
    elseif any_tree_params_change then
        return C.ts_v2_set_params(tr, nil, playout.tree_params) == common.TRUE
    end
end

function playout.print_params(tr)
    C.ts_v2_print_params(tr)
end

function playout.free(tr)
    C.ts_v2_search_stop(tr)
    C.ts_v2_free(tr)
end

function playout.play_rollout(tr, filename, b)
    -- Play n rollout from the board with the player
    local all_moves = ffi.new("AllMoves")
    local best_m = C.ts_v2_pick_best(tr, all_moves, b)
    local res = { }
    for i = 0, all_moves.num_moves - 1 do
        table.insert(res, all_moves.moves[i])
    end
    -- Save to json
    if filename then
        C.ts_v2_tree_to_json(tr, filename)
    end
    -- Finally pick the best move.
    C.ts_v2_prune_ours(tr, best_m.m)
    return best_m, res
end

function playout.peek_rollout(tr, topk, b)
    local topk_moves = ffi.new("Moves")
    C.ts_v2_peek(tr, topk, topk_moves, b)
    local res = { }
    for i = 0, topk - 1 do
        local m = topk_moves.moves[i]
        table.insert(res, { x = m.x + 1, y = m.y + 1, n = m.total_games, win_rate = m.win_rate })
    end
    return res
end

function playout.save_tree_feature(tr, filename)
    C.ts_v2_tree_to_feature(tr, filename)
end

function playout.thread_off(tr)
    C.ts_v2_thread_off(tr)
end

function playout.thread_on(tr)
    C.ts_v2_thread_on(tr)
end

function playout.dump_tree(filename, tr)
    C.tree_print_out_cnn(filename, C.ts_v2_get_tree_pool(tr))
end

function playout.save_search_tree(tr, filename)
    C.ts_v2_tree_to_json(tr, filename)
end

function playout.set_time_left(tr, time_left, num_moves)
    C.ts_v2_set_time_left(tr, time_left, num_moves)
end

function playout.prune(tr, m, filename)
    if filename then
        C.ts_v2_thread_off(tr)
        C.ts_v2_tree_to_json(tr, filename)
        C.ts_v2_prune_opponent(tr, m.m)
        C.ts_v2_thread_on(tr)
    else
        C.ts_v2_prune_opponent(tr, m.m)
    end
end

function playout.prune_xy(tr, x, y, player, filename)
    playout.prune(tr, C.compose_move(x - 1, y - 1, player), filename)
end

function playout.undo_pass(tr, before_board)
    return C.ts_v2_undo_pass(tr, before_board) == common.TRUE
end

-- From C's coordinate to Move structure
function playout.compose_move(m, player)
    return C.compose_move2(m, player)
end

return playout
