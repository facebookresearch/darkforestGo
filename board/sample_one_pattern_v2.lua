--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

-- Try compare with three playout tactics.
local pat = require 'board.pattern_v2'
local board = require 'board.board'
local common = require 'common.common'
local goutils = require 'utils.goutils'
local utils = require 'utils.utils'
local sgf =  require 'utils.sgf'
local pl = require 'pl.import_into'()

-- Load a sgf file and try havest its pattern.
local opt = pl.lapp[[
    -p,--pattern_file (default "")        The pattern file to load.
    -v,--verbose      (default 1)         Verbose level
    --save_prefix     (default "game")    The prefix of the game.
    --sample_topn     (default -1)        Sample from topn move.
    --temperature     (default 1.0)       Temperature
    --sgf_file        (default "")        If not empty, then we sample after move_from moves.
    --move_from       (default 0)         Sample since move_from
    --num_moves       (default 200)       The number of moves to simulate.
    --num_games       (default 10)        The number of games to simulate.
    --stats                               Whether we compute the statistics.
]]

-- pat.params.verbose = opt.verbose
local pat_h = pat.init(opt.pattern_file)
pat.set_verbose(pat_h, opt.verbose)
-- pat.update_params(pat_h)
 
pat.set_sample_params(pat_h, opt.sample_topn, opt.temperature)
pat.print(pat_h)

local b = board.new()
board.clear(b)

print("Load sgf: " .. opt.sgf_file)
local f = assert(io.open(opt.sgf_file, "r"))
local game = sgf.parse(f:read("*a"))
if game == nil then
    error("Game " .. opt.sgf_file .. " cannot be loaded")
end

goutils.apply_handicaps(b, game, true)

if opt.move_from > 0 then
    game:play(function (move, counter) 
        local x, y, player = sgf.parse_move(move, false, true)
        if x and y and player then
            board.play(b, x, y, player)
            -- board.show(b, 'last_move')
            return true
        end
    end, opt.move_from)
end

--[[
print("Starting board situation")
board.show(b, 'last_move')
]]

local blacks, whites = board.get_black_white_stones(b) 
local header = {
    blacks = blacks,
    whites = whites
}

local duration = 0
local total_num_moves = 0
local stats = { }

print("Current situation!")
board.show_fancy(b, 'all')

local summary = pat.init_sample_summary() 

for i = 1, opt.num_games do
    local be = pat.new(pat_h, b)
    local moves
    if opt.num_moves > 0 then
        moves, this_summary = pat.sample_many(be, opt.num_moves, nil, true)
    else
        this_summary = pat.sample_until(be)
    end
    local score = board.get_fast_score(pat.get_board(be))

    if opt.stats then 
        -- Compute the final score.
        table.insert(stats, score)
    else
        print(string.format("[%d/%d] Score = %f", i, opt.num_games, score))
        pat.print_sample_summary(this_summary)
        board.show_fancy(pat.get_board(be))
        
        -- Save to file
        if opt.save_prefix ~= "" and moves ~= nil then 
            local filename = opt.save_prefix .. '-' .. i .. ".sgf" 
            local f = assert(io.open(filename, "w"))
            f:write(sgf.sgf_string(header, moves))
            f:close()
        end
    end

    pat.free(be)
    pat.combine_sample_summary(summary, this_summary)
end

pat.print_sample_summary(summary)

if opt.stats then
    local mean = 0
    local max = -1000
    local min = 1000
    for i = 1, #stats do
        mean = mean + stats[i]
        max = math.max(max, stats[i])
        min = math.min(min, stats[i])
    end
    mean = mean / #stats
    print(string.format("#sample = %d, mean = %f, min = %f, max = %f", #stats, mean, min, max))
end

pat.destroy(pat_h)
