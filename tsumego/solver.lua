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
local board = require("board.board")

local image = require 'image'

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "tsumego/solver.h"))
local symbols, s = utils.ffi_include(paths.concat(common.script_path(), "solver.h"))
local C = ffi.load("libexperimental_deeplearning_yuandong_go_tsumego_solver_c.so")

-- print(s)
local tg = {}

local black_lives = tonumber(symbols.BLACK_LIVES)
local black_dies = tonumber(symbols.BLACK_DIES)
local white_lives = tonumber(symbols.WHITE_LIVES)
local white_dies = tonumber(symbols.WHITE_DIES)
local die_at_loc = tonumber(symbols.DIE_AT_LOC)

local all_moves = ffi.new("AllMoves")

--[[
function tg.set_die_at_crit(x, y)
    -- Find a way so that stone die at x, y.
    local crit = ffi.new("TGCriterion")
    crit.goal = die_at_loc
    crit.critical_loc = common.xy2coord(x, y) 
    crit.max_depth = 10
    return crit
end
]]

function tg.set_target(aspect)
    -- Find a way so that stone die at x, y.
    local crit = ffi.new("TGCriterion")
    crit.max_count = -1 -- 1000000
    crit.dead_thres = 3
    crit.target_player = aspect == 'b' and common.black or common.white 
    return crit
end

function tg.solve(b, crit)
    crit.region = board.get_stones_bbox(b)

    local res = C.TsumegoSearch(b, crit, all_moves)
    -- Convert all moves to (x, y, player)
    local player = b._next_player
    local moves = { }
    for i = 0, all_moves.num_moves - 1 do
        local x, y = common.coord2xy(all_moves.moves[i])
        table.insert(moves, {x, y, player})
        player = common.opponent(player)
    end
    return moves
end

return tg
