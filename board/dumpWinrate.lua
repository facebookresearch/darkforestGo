--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local pl = require 'pl.import_into'()
local goutils = require 'utils.goutils'
local sgf =  require 'utils.sgf'
local playoutv2 = require('mctsv2.playout_multithread')

local opt = pl.lapp[[
   -s,--sgf               (default "")         Sgf file to load
   -s,--start_n           (default -1)         Start from
   -e,--end_n             (default -1)         End to
]]

assert(opt.sgf)
local content = io.open(opt.sgf):read("*a")
local game = assert(sgf.parse(content))
print(game.sgf[1].PW)
print(game.sgf[1].PB)
print(game.sgf[1].RE)

local b = board.new()
board.clear(b)

goutils.apply_handicaps(b, game, true)

local n = game:get_total_moves()
game:play(function (move, counter) 
    local x, y, player = sgf.parse_move(move, false, true)
    if x and y and player then
        board.play(b, x, y, player)
        -- board.show(b, 'last_move')
        return true
    end
end, opt.start_n)

local tr = playoutv2.new(opt.rollout) 
 

-- Then we start the dumping.
game:play(function (move, counter) 
    local x, y, player = sgf.parse_move(move, false, true)
    if x and y and player then
        board.play(b, x, y, player)
        -- board.show(b, 'last_move')
        return true
    end
end, opt.end_n, true)


