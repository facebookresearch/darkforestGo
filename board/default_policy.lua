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

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "board/default_policy.h"))
local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "default_policy.h"))
-- local C = ffi.load("libexperimental_deeplearning_yuandong_go_board_board_c.so")
local C = ffi.load(paths.concat(script_path, "../libs/libdefault_policy.so"))
local dp = {}

local function script_path()
   local str = debug.getinfo(2, "S").source:sub(2)
   return str:match("(.*/)") or "./"
end

dp.default_typename = {
    [0] = "normal",
    [1] = "ko_fight",
    [2] = "opponent_in_danger",
    [3] = "our_atari",
    [4] = "nakade",
    [5] = "pattern",
    [6] = 'no_move'
}

dp.default_typename_hash = { }
for i = 1, #dp.default_typename do
    dp.default_typename_hash[dp.default_typename[i]] = i
end

function dp.new(rule)
    dp.rule = rule or board.chinese_rule
    return C.InitDefPolicy()
end

function dp.free(def_policy)
    C.DestroyDefPolicy(def_policy);
end

function dp.new_with_params(params_table, rule)
    local policy = dp.new(rule)
    local params = ffi.new("DefPolicyParams")
    C.InitDefPolicyParams(params)
 
    for k, v in pairs(params_table) do
        local idx = dp.default_typename_hash[k] 
        if idx then 
            params.switches[idx] = v and common.TRUE or common.FALSE
        end
    end

    if dp.set_params(policy, params) then 
        C.DefPolicyParamsPrint(policy)
        return policy 
    end
end

function dp.new_params(use_for_server)
    local res = ffi.new("DefPolicyParams")
    C.InitDefPolicyParams(res)
    -- A hack in the server side
    if use_for_server then
        res.switches[0] = common.FALSE
        res.switches[1] = common.FALSE
        -- Only enable extending on our atari, kill opponent if they are big and in atari, and nakade points (important). 
        res.switches[2] = common.TRUE
        res.switches[3] = common.TRUE
        res.switches[4] = common.TRUE
        res.switches[5] = common.FALSE
        res.switches[6] = common.FALSE
        -- Save our groups if they are big and in atari.
        res.thres_save_atari = 5; 
        -- Kill opponent groups if they are big and in atari.
        res.thres_opponent_libs = 1;
        res.thres_opponent_stones = 5;
    end
    return res
end

function dp.set_params(def_policy, def_params)
    return C.SetDefPolicyParams(def_policy, def_params) == common.TRUE
end

function dp.typename(move_type)
    return C.GetDefMoveType(move_type)
end

local moves = ffi.new("DefPolicyMoves")

function dp.get_candidate_moves(def_policy, b)
    moves.board = b
    C.ComputeDefPolicy(def_policy, moves, nil)
    -- Then dump all moves from def_policy.
    local moves_table = { }
    if moves.num_moves > 0 then
        for i = 0, moves.num_moves - 1 do
            local x, y = common.coord2xy(moves.moves[i].m)
            table.insert(moves_table, { x, y })
        end
    end
    return moves_table
end

function dp.run(def_policy, b, max_depth, verbose)
    local def_move = C.RunDefPolicy(def_policy, nil, nil, b, nil, max_depth, verbose and common.TRUE or common.FALSE)
    return board.get_fast_score(b, dp["rule"]), def_move
end

function dp.run_old(def_policy, b, max_depth, verbose)
    local def_move = C.RunOldDefPolicy(def_policy, nil, nil, b, nil, max_depth, verbose and common.TRUE or common.FALSE)
    return board.get_fast_score(b, dp["rule"]), def_move
end

return dp
