--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local sgf = require("utils.sgf")
local goutils = require 'utils.goutils'
local utils = require('utils.utils')
local board = require("board.board")
local common = require("common.common")
local tg = require 'tsumego.solver'

-- Tsumego example
local example1 = '/home/yuandong/test/tsumego-1.sgf' 
local game = sgf.parse(io.open(example1):read("*a"))

local sgf_play = { } 

local b = board.new()
board.clear(b)

-- Assume the board is cleared. And we will setup everything given this game.
-- First put all the existing stones there. (apply_handicap is not a good name).
goutils.apply_handicaps(b, game)
board.play(b, goutils.parse_move_gtp('T16', 'B'))
board.play(b, goutils.parse_move_gtp('O19', 'W'))
board.play(b, goutils.parse_move_gtp('N19', 'B'))
board.play(b, goutils.parse_move_gtp('M19', 'W'))
board.play(b, goutils.parse_move_gtp('O18', 'B'))
board.play(b, goutils.parse_move_gtp('N18', 'W'))

board.play(b, goutils.parse_move_gtp('P19', 'B'))
board.play(b, goutils.parse_move_gtp('N19', 'W'))


board.show(b, 'last_move')

local crit = tg.set_target('b')
local moves = tg.solve(b, crit)

print("Best sequence:")
for i = 1, #moves do
    local x, y, player = unpack(moves[i])
    local c, player_str = goutils.compose_move_gtp(x, y, player)
    print("Move: " .. c .. " " .. player_str) 
    if not board.play(b, x, y, player) then
        error("The move " .. c .. " " .. player_str .. " cannot be played!")
    end
end

board.show(b, 'last_move')


