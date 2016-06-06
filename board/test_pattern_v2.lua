--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local pat = require 'board.pattern_v2'
local board = require 'board.board'
local common = require 'common.common'
local goutils = require 'utils.goutils'
local utils = require 'utils.utils'
local sgf =  require 'utils.sgf'
local pl = require 'pl.import_into'()

Index = require 'fbcode.blobs.lua.index'
IndexLoader = require 'fbcode.blobs.lua.index-loader'
PathResolver = require 'fbcode.blobs.lua.path-resolver'
StreamMultiReader = require 'fbcode.blobs.lua.stream-multi-reader'

-- Load a sgf file and try havest its pattern.
local opt = pl.lapp[[
    -s,--sgf       (default "test.sgf")   The sgf file loaded for test 5-6.
    --sgf_list     (default "")           A list of sgf files.
    --debug                               Use debug
    -v,--verbose   (default 1)            Verbose level
    --cnt_threshold (default 1)           Pattern cnt threshold.
    --learning_rate (default 0.001)       The learning rate.
    --save_pattern_prefix (default "")      The pattern file to be saved.
    --load_pattern_file (default "")      The pattern file to be loaded.
    --num_games_train   (default -1)      The number of games we used for training. -1 = all
    --num_games_harvest (default 1000)    The number of games we used for harvesting. -1 = all
    --num_games_test    (default -1)      The number of games we used for testing. -1 = all
    --num_iters    (default 100)          The number of iterations we go through the dataset.
]]

function dataLoader(index_path)
    return StreamMultiReader:newFromIndex{
        index = Index:new(
        IndexLoader:newInMemoryUnordered():addSubloader(
        IndexLoader:newFile(index_path)
        )
        ),
        num_threads = 10,
        path_resolver = PathResolver:new():addSearchPath(paths.dirname(index_path)),
        convert_raw_entry_fn = StreamMultiReader.rawEntryToTensorTable,
    }
end

local root = '/mnt/vol/gfsai-oregon/ai-group/datasets/go_gogod'
local mtbl_file = 'GoGoD2015.mtbl'
print("Initialize data loader...")
local dataLoader = dataLoader(paths.concat(root, mtbl_file))

print("Load training list ...")
local sgf_name_train = utils.readlines(paths.concat(root, "train.lst"))

print("Load test list ...")
local sgf_name_test = utils.readlines(paths.concat(root, "test.lst"))

if opt.num_games_train < 0 then opt.num_games_train = #sgf_name_train end
opt.num_games_train = math.min(opt.num_games_train, #sgf_name_train)
if opt.num_games_test < 0 then opt.num_games_test = #sgf_name_test end
opt.num_games_test = math.min(opt.num_games_test, #sgf_name_test)

function retrieve_sgf(filename)
    local game
    for k, v in pairs(dataLoader:findKeysAndRead{ keys = { filename } }) do
        local sgf_file = sgf.parse(v.value, k)
        if sgf_file ~= nil and sgf_file:has_moves() and sgf_file:get_boardsize() == common.board_size 
            -- and sgf[1].WR and sgf[1].BR and sgf[1].WR:sub(2, 2) == 'd' and sgf[1].BR:sub(2, 2) == 'd' 
            then
                game = sgf_file
                break
        end
    end
    return game
end

function get_moves(game)
    local moves = { } 
    game:play(function (move, counter) 
        local x, y, player = sgf.parse_move(move, false, true)
        local move_str, player_str = goutils.compose_move_gtp(x, y, player) 
        local m = common.xy2coord(x, y)
        table.insert(moves, {m, player})
        return true
    end)
    return moves
end

function run_many(game, func)
    local b = board.new()
    board.clear(b)

    local moves = get_moves(game)
    goutils.apply_handicaps(b, game, true)

    -- Play it
    local be = pat.new(b)

    local start = common.wallclock()
    local results = { func(be, moves) }
    local duration = common.wallclock() - start

    pat.free(be)

    return duration, #moves, results
end

function run_harvest(num_games_harvest)
    local total_duration = 0
    local total_counter = 0

    for i = 1, num_games_harvest do
        local filename = sgf_name_train[i]
        local game = retrieve_sgf(filename)
        -- require 'fb.debugger'.enter()
        if game ~= nil then
            --[[
            print(game.sgf[1].PW)
            print(game.sgf[1].PB)
            print(game.sgf[1].RE)
            print(string.format("Harvest %d/%d: %s. #Moves: %d", i, opt.num_games_train, filename, game:get_total_moves()))
            ]]

            local duration, move_counter = run_many(game, pat.harvest_many)
            total_duration = total_duration + duration
            total_counter = total_counter + move_counter
        end
        --[[
        if i % 100 == 0 then 
        pat.print()
        end
        ]]
    end

    pat.print()
    print(string.format("Harvest: #game: %d. Time per move = %f usec [%f sec/%d]", num_games_harvest, total_duration / total_counter * 1e6, total_duration, total_counter))
end

function run_one_epoch(num_games, sgf_game, eval_only, shuffle)
    local total_duration = 0
    local total_counter = 0

    local total_likelihood = 0
    local total_top1 = 0
    local total_selected_positions = 0
    local total_positions = 0

    local perm
    if shuffle then
        perm = torch.randperm(num_games)
    end

    for i = 1, num_games do
        local filename = perm and sgf_game[perm[i]] or sgf_game[i]
        local game = retrieve_sgf(filename)

        if game ~= nil then
            -- print(string.format("Train %d/%d: %s. #Moves: %d", i, opt.num_games, filename, game:get_total_moves()))
            local duration, move_counter, results = run_many(game, function (be, moves) return pat.train_many(be, moves, eval_only) end)

            total_duration = total_duration + duration
            total_counter = total_counter + move_counter

            total_positions = total_positions + move_counter
            total_selected_positions = total_selected_positions + results[1].n
            total_likelihood = total_likelihood + results[1].sum_loglikelihood
            total_top1 = total_top1 + results[1].sum_top1
        end
    end
    print(string.format("%s: #game: %d, #positions: %d/%d, aver likelihood: %f, aver top1: %f", 
          eval_only and "Test" or "Train",
          num_games,
          total_selected_positions, total_positions, 
          total_likelihood / total_selected_positions, 
          total_top1 / total_selected_positions))

    return total_duration, total_counter
end

pat.params.verbose = opt.verbose
pat.params.cnt_threshold = opt.cnt_threshold
pat.params.learning_rate = opt.learning_rate
-- pat.params.prior_neighbor = common.FALSE
-- pat.params.prior_nakade = common.FALSE
pat.params.prior_neighbor = common.TRUE
pat.params.prior_nakade = common.TRUE

pat.params.prior_save_atari = common.TRUE
pat.params.prior_kill_other = common.TRUE
pat.params.prior_resp = common.TRUE

if opt.load_pattern_file ~= "" then
    pat.init(opt.load_pattern_file)
    pat.params.verbose = opt.verbose
    pat.params.cnt_threshold = opt.cnt_threshold
    pat.params.learning_rate = opt.learning_rate
    pat.update_params()
else
    print("Initialize pattern library...")
    pat.init()
    print("Run harvest...")
    run_harvest(opt.num_games_harvest)
    if opt.save_pattern_prefix ~= "" then
        print("Save harvest...")
        pat.save(opt.save_pattern_prefix .. "-harvest.bin")
    end
    pat.train_start()
end

print("Start training..")

total_duration = 0
total_counter = 0

total_test_duration = 0
total_test_counter = 0

-- Run a few iterations.
for iter = 1, opt.num_iters do
    print(string.format("=== Epoch: %d/%d ===", iter, opt.num_iters))
    local duration_training, counter_training = run_one_epoch(opt.num_games_train, sgf_name_train, false, true)
    local duration_test, counter_test = run_one_epoch(opt.num_games_test, sgf_name_test, true)

    total_duration = total_duration + duration_training
    total_counter = total_counter + counter_training

    total_test_duration = total_test_duration + duration_test
    total_test_counter = total_test_counter + counter_test
    print(string.format("Training: Time per move = %f usec [%f sec/%d]", total_duration / total_counter * 1e6, total_duration, total_counter))
    print(string.format("Test: Time per move = %f usec [%f sec/%d]", total_test_duration / total_test_counter * 1e6, total_test_duration, total_test_counter))
    -- save the pattern.
    if opt.save_pattern_prefix ~= "" then
        local filename = string.format("%s-%d-%d.bin", opt.save_pattern_prefix, iter, opt.num_iters)
        print("Save to " .. filename)
        pat.save(filename)
    end
end

pat.print()
print(string.format("Training: Time per move = %f usec [%f sec/%d]", total_duration / total_counter * 1e6, total_duration, total_counter))
print(string.format("Test: Time per move = %f usec [%f sec/%d]", total_test_duration / total_test_counter * 1e6, total_test_duration, total_test_counter))

--[[
-- Finally lets sample the move.
local num_moves = 200
local duration = 0
local total_num_moves = 0
for i = 1, 100 do
    local be = pat.new()
    local start = common.wallclock()
    local moves = pat.sample_many(be, num_moves)
    duration = duration + common.wallclock() - start
    total_num_moves = total_num_moves + num_moves
    board.show(pat.get_board(be), 'last_move')
    pat.free(be)
end
print(string.format("Time per sample = %f usec [%f sec/%d]", duration / total_num_moves * 1e6, duration, total_num_moves))
]]

pat.destroy()

--[[
pat.harvest_play(be, m)

if opt.debug then
print(string.format("%d: %s %s", counter, player_str, move_str))
pat.print(be)
if not pat.check(be) then
return false
end
end
board.play(b, x, y, player)
total_counter = total_counter + 1
]]
