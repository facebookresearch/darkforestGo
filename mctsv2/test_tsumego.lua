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
local playout = require 'mctsv2.playout_multithread' 

local pl = require 'pl.import_into'()

-- Tsumego example
-- local example1 = '/home/yuandong/test/tsumego-1.sgf' 
local solver = { } 

-- opt: verbose, json_filename
function solver.solve(tr, game, opt)
    local b = board.new()
    board.clear(b)

    -- Assume the board is cleared. And we will setup everything given this game.
    -- First put all the existing stones there. (apply_handicap is not a good name).
    goutils.apply_handicaps(b, game, true)

    if opt.verbose then board.show(b, 'last_move') end

    playout.set_tsumego_mode(tr, b, 1)
    if opt.verbose then 
        playout.print_params(tr) 
        print("Start playing! Defender: " .. (playout.tree_params.defender == common.white and 'W' or 'B'))
    end

    -- local filename = "mcts_tsumego.json"
    local m, move_seq = playout.play_rollout(tr, opt.json_filename)

    local s = ""
    local res = { } 
    for i = 1, #move_seq do 
        local x, y = common.coord2xy(move_seq[i]) 
        table.insert(res, {x, y, b._next_player})

        local coord, player = goutils.compose_move_gtp(x, y, b._next_player)
        s = s .. player .. " " .. coord .. " "

        board.play2(b, move_seq[i])
    end

    return {
        solved = (m.win_rate == 1.0), 
        move_seq = res, 
        final_board = b,
        move_seq_str = s
    }
end

function solver.print_gt(game, opt)
    local board_stack = { }
    local first_variation = { }
    local first_variation_collected = false

    local b = board.new()
    board.clear(b)

    -- Assume the board is cleared. And we will setup everything given this game.
    -- First put all the existing stones there. (apply_handicap is not a good name).
    goutils.apply_handicaps(b, game, true)

    game:play_start(
       function ()
           -- table.insert(board_stack, board.copyfrom(b))
       end,
       function ()
           -- b = table.remove(board_stack)
           if #first_variation > 0 then
               first_variation_collected = true
           end
       end)

    if opt.verbose then
        print("Initial board position = ")
        board.show(b, "last_move")
    end

    local s = ""
    while not first_variation_collected do
        local move = game:play_current()
        -- require 'fb.debugger'.enter()
        -- print("Move:")
        -- print(move)
        local x, y, player = sgf.parse_move(move, false, true)
        if x ~= nil then 
            table.insert(first_variation, {x, y, player})
             
            local c, player_str = goutils.compose_move_gtp(x, y, player)
            s = s .. player_str .. " " .. c .. " "
            if not board.play(b, x, y, player) then
                error("The move " .. player_str .. " " .. c .. " cannot be played!")
            end
        end

        -- Move to next.
        if not game:play_next() then break end
    end

    return {
        solved = true,
        move_seq = first_variation, 
        final_board = b,
        move_seq_str = s
    }
end

local num_rollout = 300000
-- local num_rollout = 100000
-- playout.params.print_search_tree = common.TRUE
-- playout.params.decision_mixture_ratio = 1.0
playout.tree_params.sigma = 0.05
playout.tree_params.use_online_model = common.TRUE
playout.tree_params.online_model_alpha = 0.0001
-- playout.params.verbose = 3
-- playout.tree_params.verbose = 3
playout.tree_params.num_tree_thread = 64 

local tr = playout.new(num_rollout)
-- local sgf_list = '/home/yuandong/test/tsumego/tsumego.lst' 
local sgf_list = '/home/yuandong/test/tsumego/tsumego_sample.lst' 
local dirname = pl.path.dirname(sgf_list)
local lines = pl.utils.readlines(sgf_list)

local num_solved = 0
local num_total = 0
local topn = { }

local print_gt = true
local opt = { verbose = true, save_feature = true, output_json = true }

for idx, f in ipairs(lines) do
    print(f)
    local game = sgf.parse(io.open(paths.concat(dirname, f)):read("*a"))

    if opt.output_json then
        opt.json_filename = string.format("mcts-%d.json", idx)
    end

    local sol = solver.solve(tr, game, opt)
    local sol_gt = solver.print_gt(game, opt)

    if opt.save_feature then
        local filename = string.format("feature-%d.feature", idx)
        playout.save_tree_feature(tr, filename)
    end

    if opt.verbose then
        print(string.format("%s, Move: %s", (sol.solved and "Solved" or "Unsolved"), sol.move_seq_str))
        print("GT Move: " .. sol_gt.move_seq_str)
        -- board.show(sol.final_board, 'last_move')   
        -- board.show(sol_gt.final_board, 'last_move')   
    end

    -- Compare with gt solved, usually sol.move_seq is longer than sol_gt.move_seq
    for i = 1, math.min(#sol.move_seq, #sol_gt.move_seq) do
        local xmatch = (sol.move_seq[i][1] == sol_gt.move_seq[i][1]) 
        local ymatch = (sol.move_seq[i][2] == sol_gt.move_seq[i][2])
        if not topn[i] then
            topn[i] = { 0, 0 }
        end
        if xmatch and ymatch then
            topn[i][1] = topn[i][1] + 1
        end
        topn[i][2] = topn[i][2] + 1
    end

    if sol.solved then num_solved = num_solved + 1 end
    num_total = num_total + 1
end

print(string.format("Summary: solved/total: %d/%d", num_solved, num_total))
for i, c in ipairs(topn) do
    print(string.format("Top %d: %.2f (%d/%d)", i, 100 * c[1] / c[2], c[1], c[2]))
end

playout.free(tr)
