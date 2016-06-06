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
local patv2_utils = require 'board.pattern_v2_utils'

-- Load a sgf file and try havest its pattern.
local opt = pl.lapp[[
    -s,--sgf       (default "test.sgf")   The sgf file loaded for test 5-6.
    --sgf_list     (default "")           A list of sgf files.
    --debug                               Use debug
    --eval_only                           Whether we only evaluate the existing model
    -v,--verbose   (default 1)            Verbose level
    --cnt_threshold (default 1)           Pattern cnt threshold.
    --learning_rate (default 0.001)       The learning rate.
    --save_pattern_prefix (default "")    The pattern file to be saved.
    --load_pattern_file (default "")      The pattern file to be loaded.
    --load_harvest                        Whether we load a dataset that is just harvested.
    --num_games_train   (default -1)      The number of games we used for training. -1 = all
    --num_games_harvest (default 1000)    The number of games we used for harvesting. -1 = all
    --num_games_test    (default -1)      The number of games we used for testing. -1 = all
    --num_iters    (default 100)          The number of iterations we go through the dataset.
    --pd_iterations (default 0)           Use policy gradient, for each game we iterate pd_iterations times.
    --pd_start_n    (default 200)         We sample after 200 moves.
]]

-- local root = '/mnt/vol/gfsai-oregon/ai-group/datasets/go_gogod'
-- local mtbl_file = 'GoGoD2015.mtbl'
local root = '/mnt/vol/gfsai-oregon/ai-group/datasets/go-tygem-blob'
local mtbl_file = 'data.mtbl'

opt = patv2_utils.load(root, mtbl_file, opt)
if opt.eval_only then opt.num_iters = 1 end

function run_many(h, game, func)
    local b = board.new()
    board.clear(b)

    local moves = patv2_utils.get_moves(game)
    goutils.apply_handicaps(b, game, true)

    -- Play it
    local be = pat.new(h, b)

    local start = common.wallclock()
    local results = { func(h, be, moves) }
    local duration = common.wallclock() - start

    pat.free(be)

    return duration, #moves, results
end

function run_harvest(h, num_games_harvest)
    local total_duration = 0
    local total_counter = 0

    for i = 1, num_games_harvest do
        local filename = patv2_utils.sgf_name_train[i]
        local game = patv2_utils.retrieve_sgf(filename)
        -- require 'fb.debugger'.enter()
        if game ~= nil then
            --[[
            print(game.sgf[1].PW)
            print(game.sgf[1].PB)
            print(game.sgf[1].RE)
            print(string.format("Harvest %d/%d: %s. #Moves: %d", i, opt.num_games_train, filename, game:get_total_moves()))
            ]]

            local duration, move_counter = run_many(h, game, pat.harvest_many)
            total_duration = total_duration + duration
            total_counter = total_counter + move_counter
        end
        --[[
        if i % 100 == 0 then 
        pat.print()
        end
        ]]
    end

    pat.print(h)
    print(string.format("Harvest: #game: %d. Time per move = %f usec [%f sec/%d]", num_games_harvest, total_duration / total_counter * 1e6, total_duration, total_counter))
end

function train_one_epoch(h, num_games, sgf_name, eval_only, shuffle)
    local perm
    if shuffle then
        perm = torch.randperm(num_games)
    end

    local perf_summary = pat.init_perf_summary() 
    local grads = pat.init_grads() 

    for i = 1, num_games do
        local filename = perm and sgf_name[perm[i]] or sgf_name[i]
        local game = patv2_utils.retrieve_sgf(filename)

        if game ~= nil then
            -- print(string.format("Train %d/%d: %s. #Moves: %d", i, opt.num_games, filename, game:get_total_moves()))
            local duration, move_counter, results = 
               run_many(h, game, 
                   function (hh, be, moves) 
                        return pat.train_many_add_grads(be, grads, moves, eval_only, perf_summary)
                        -- return pat.train_many(hh, be, moves, eval_only, perf_summary) 
                   end)
            if not eval_only then
                pat.train_update_weights(h, grads)
            end
        end
    end

    perf_summary.name = eval_only and "Eval" or "Train"
    pat.print_perf_summary(perf_summary)

    pat.destroy_grads(grads)
end

-- Input a table of ptrs, output is a byte tensor that encode these ptrs.
-- Why LUA has such a weak support on this?
local function ptrs2tensor(ptrs)
    local ffi = require 'ffi'
    ffi.cdef"void *memcpy(void *dest, const void *src, size_t n);"
    local res = torch.ByteTensor(#ptrs, 8)
    local buffer = ffi.new("intptr_t[1]")
    for i = 1, #ptrs do
        buffer[0] = ffi.cast("intptr_t", ffi.cast("void *", ptrs[i]))
        ffi.C.memcpy(res[i]:data(), buffer, 8)
    end
    return res
end

local function tensor2ptrs(t, typename)
    local ffi = require 'ffi'
    ffi.cdef"void *memcpy(void *dest, const void *src, size_t n);"
    local buffer = ffi.new("intptr_t[1]")
    local res = { }
    for i = 1, t:size(1) do
        ffi.C.memcpy(buffer, res[i]:data(), 8)
        res[i] = ffi.cast(typename, ffi.new("void *", buffer[0]))
    end
    return res
end

function train_multithread(h, num_games_train, sgf_name_train, num_games_test, sgf_name_test, nthread, iter, save_pattern_prefix)
    -- Create a few threads and run them.
    local Threads = require 'threads'
    local sdl = require 'sdl2'
    local ffi = require 'ffi'

    -- init SDL (follow your needs)
    sdl.init(0)

    local perf_summaries = { } 
    local perf_summaries_ptr = { }
    local grads = { }
    local grads_ptr = { }
    for i = 1, nthread do
        perf_summaries[i] = pat.init_perf_summary() 
        perf_summaries_ptr[i] = tonumber(ffi.cast("intptr_t", ffi.cast("void *", perf_summaries[i])))
        grads[i] = pat.init_grads() 
        grads_ptr[i] = tonumber(ffi.cast("intptr_t", grads[i]))

        print(string.format("thread %d: perf = %x, grad = %x", i, tonumber(perf_summaries_ptr[i]), tonumber(grads_ptr[i])))
    end

    local h_ptr = tonumber(ffi.cast("intptr_t", h))
    print(string.format("h = %x", h_ptr))

    --[[
    local perf_tensors = ptrs2tensor(perf_summaries)
    local grads_tensors = ptrs2tensor(grads)

    local perf_tensors_shared = utils.get_shared_ptr(perf_tensors)
    local grads_tensors_shared = utils.get_shared_ptr(grads_tensors)
    ]]

    print("Initalize thread")

    local threads = Threads(nthread,
        function()
            gffi = require 'ffi'
            gsdl = require 'sdl2'
            gpat = require 'board.pattern_v2'
            gpatv2_utils = require 'board.pattern_v2_utils'
            gboard = require 'board.board'
            g_goutils = require 'utils.goutils'

            local gthreads = require 'threads'
            gthreads.serialization('threads.sharedserialize')
            -- require 'fb.debugger'.enter()
            return tonumber(gsdl.threadID())
        end,
        function(id)
            --[[
            local gutils = require 'utils.utils'
            local g_perfs_tensors = gutils.create_from_shared_ptr(perf_tensors_shared)
            local g_grads_tensors = gutils.create_from_shared_ptr(grads_tensors_shared)
            print("Convert them to ptr")
            local g_perfs_table = tensors2ptr(g_perfs_tensors, "PerfSummary *")
            local g_grads_table = tensors2ptr(g_grads_tensors, "void *")
            ]]
 
            g_perf_summary = gffi.cast("void *", perf_summaries_ptr[id])
            g_grad = gffi.cast("void *", grads_ptr[id])
            g_h = gffi.cast("void *", h_ptr)
            g_sgf_name_train = sgf_name_train
            g_sgf_name_test = sgf_name_test
            g_eval_only = eval_only

            -- require 'fb.debugger'.enter()
            -- print(string.format("g_perf_summary = %x", tonumber(perf_summaries_ptr[id])))
            -- print(string.format("g_grad = %x", tonumber(grads_ptr[id])))
            -- print(string.format("g _h = %x", tonumber(h_ptr)))

            gpatv2_utils.load(root, mtbl_file)
        end
    )

    print("Start Training...")
    local nbatch = 8 

    -- Approximate going through the entire game set.
    for k = 1, iter do
        print(string.format("=== Epoch: %d/%d ===", k, iter))
        for i = 1, num_games_train, nbatch do
            for j = 1, nbatch do
                threads:addjob(
                function() 
                    local game
                    repeat 
                        local idx = math.random(num_games_train)
                        local filename = g_sgf_name_train[idx]
                        game = gpatv2_utils.retrieve_sgf(filename)
                        -- require 'fb.debugger'.enter()
                    until game ~= nil

                    -- print(string.format("Picked a game %s with idx = %d", g_sgf_name[idx], idx))
                    -- print(string.format("Train %d/%d: %s. #Moves: %d", i, opt.num_games, filename, game:get_total_moves()))
                    local b = gboard.new()
                    gboard.clear(b)

                    local moves = gpatv2_utils.get_moves(game)
                    g_goutils.apply_handicaps(b, game, true)

                    -- Play it
                    local be = gpat.new(g_h, b)
                    gpat.train_many_add_grads(be, g_grad, moves, false, g_perf_summary)
                    gpat.free(be)
                end)
            end
            threads:synchronize()
            if not eval_only then
                for j = 1, nthread do
                    pat.train_update_weights(h, grads[j])
                end
            end
            for j = 2, nthread do
                pat.combine_perf_summary(perf_summaries[1], perf_summaries[j])
                pat.init_perf_summary(perf_summaries[j])
            end
        end

        perf_summaries[1].name = "Train"
        pat.print_perf_summary(perf_summaries[1])
        pat.init_perf_summary(perf_summaries[1])

        -- Test 
        for i = 1, num_games_test, nbatch do
            for j = 1, nbatch do
                threads:addjob(
                function() 
                    local idx = i + j - 1
                    local filename = g_sgf_name_test[idx]
                    local game = gpatv2_utils.retrieve_sgf(filename)

                    if game ~= nil then
                        -- print(string.format("Picked a game %s with idx = %d", g_sgf_name[idx], idx))
                        -- print(string.format("Train %d/%d: %s. #Moves: %d", i, opt.num_games, filename, game:get_total_moves()))
                        local b = gboard.new()
                        gboard.clear(b)

                        local moves = gpatv2_utils.get_moves(game)
                        g_goutils.apply_handicaps(b, game, true)

                        -- Play it
                        local be = gpat.new(g_h, b)
                        gpat.train_many_add_grads(be, nil, moves, true, g_perf_summary)
                        gpat.free(be)
                    end
                end)
            end
            threads:synchronize()
            for j = 2, nthread do
                pat.combine_perf_summary(perf_summaries[1], perf_summaries[j])
                pat.init_perf_summary(perf_summaries[j])
            end
        end
        perf_summaries[1].name = "Eval"
        pat.print_perf_summary(perf_summaries[1])
        pat.init_perf_summary(perf_summaries[1])

        if save_pattern_prefix ~= "" then
            local filename = string.format("%s-%d-%d.bin", save_pattern_prefix, k, iter)
            print("Save to " .. filename)
            pat.save(h, filename)
        end
    end

    -- terminate threads
    threads:terminate()

    for i = 1, nthread do
        pat.destroy_grads(grads[i])
    end
end

function policy_gradient_one_epoch(h, num_games, sgf_name, shuffle, num_iterations, training, start_n)
    local perm
    if shuffle then
        perm = torch.randperm(num_games)
    end

    local perf_summary = pat.init_perf_summary() 
    local sample_summary = pat.init_sample_summary()
    local b = board.new()

    local grads = pat.init_grads() 

    for i = 1, num_games do
        -- require 'fb.debugger'.enter()
        local filename = perm and sgf_name[perm[i]] or sgf_name[i]
        local game = patv2_utils.retrieve_sgf(filename)

        if game ~= nil and game:get_total_moves() >= start_n and game:get_result() then
            -- print(string.format("Game %s. #move = %d\n", filename, game:get_total_moves())) 
            board.clear(b)
            goutils.apply_handicaps(b, game, true)
            -- Get the board and play until we hit some random move...
            
            -- local num_move = math.random() % game:get_total_moves()
            local illegal_move
            game:play(function (move, counter) 
                local x, y, player = sgf.parse_move(move, false, true)
                if x and y and player then
                    board.play(b, x, y, player)
                    return true
                else
                    illegal_move = true
                end
            end, start_n)

            if not illegal_move then
                local opt = {
                    iterations = num_iterations,
                    komi = game:get_komi() + game:get_handi_count(),
                    rule = board.chinese_rule,
                    player_won = game:get_result():sub(1, 2) == 'W+' and common.white or common.black,
                    training = training
                }

                -- print(string.format("Train %d/%d: %s. #Moves: %d", i, opt.num_games, filename, game:get_total_moves()))
                pat.train_policy_gradient(h, grads, b, opt, sample_summary, perf_summary)
                if training then
                    pat.train_update_weights(h, grads)
                end
            end
        end
    end

    perf_summary.name = training and "Train" or "Test"
    sample_summary.name = training and "Train" or "Test"

    pat.print_perf_summary(perf_summary)
    pat.print_sample_summary(sample_summary)

    pat.destroy_grads(grads)
end

pat.params.verbose = opt.verbose
pat.params.cnt_threshold = opt.cnt_threshold
pat.params.learning_rate = opt.learning_rate
pat.params.prior_neighbor = common.TRUE
pat.params.prior_nakade = common.TRUE
pat.params.prior_save_atari = common.TRUE
pat.params.prior_kill_other = common.TRUE
pat.params.prior_resp = common.TRUE
pat.params.prior_global = common.FALSE
-- pat.params.prior_global = common.TRUE
pat.params.batch_size = 8

local pat_h
if opt.load_pattern_file ~= "" then
    pat_h = pat.init(opt.load_pattern_file)
    pat.params.verbose = opt.verbose
    pat.params.cnt_threshold = opt.cnt_threshold
    pat.params.learning_rate = opt.learning_rate
    pat.update_params(pat_h)

    if opt.load_harvest then
        pat.train_start(pat_h)
    end
else
    print("Initialize pattern library...")
    pat_h = pat.init()
    print("")
    pat.print(pat_h)
    print("Run harvest...")
    run_harvest(pat_h, opt.num_games_harvest)
    if opt.save_pattern_prefix ~= "" then
        print("Save harvest...")
        pat.save(pat_h, opt.save_pattern_prefix .. "-harvest.bin")
    end
    pat.train_start(pat_h)
end

print("Start training..")
if opt.pd_iterations > 0 then 
    print("Using Policy Gradient") 
    pat.set_sample_params(pat_h, -1, 0.25)
    pat.print(pat_h)
    for iter = 1, opt.num_iters do
        print(string.format("============ Iter: %d/%d ==============", iter, opt.num_iters))
        if not opt.eval_only then
            policy_gradient_one_epoch(pat_h, opt.num_games_train, patv2_utils.sgf_name_train, true, opt.pd_iterations, true, opt.pd_start_n)
        end
        policy_gradient_one_epoch(pat_h, opt.num_games_test, patv2_utils.sgf_name_test, false, opt.pd_iterations, false, opt.pd_start_n)

        if opt.save_pattern_prefix ~= "" then
            local filename = string.format("%s-%d-%d.bin", opt.save_pattern_prefix, iter, opt.num_iters)
            print("Save to " .. filename)
            pat.save(pat_h, filename)
        end
    end 
else
    local nthread = 8 
    train_multithread(pat_h, opt.num_games_train, patv2_utils.sgf_name_train, opt.num_games_test, patv2_utils.sgf_name_test, nthread, opt.num_iters, opt.save_pattern_prefix)
end

-- Run a few iterations.
--[[
for iter = 1, opt.num_iters do
    if opt.pd_iterations > 0 then
        if not opt.eval_only then
            policy_gradient_one_epoch(pat_h, opt.num_games_train, patv2_utils.sgf_name_train, true, opt.pd_iterations, true, opt.pd_start_n)
        end
        policy_gradient_one_epoch(pat_h, opt.num_games_test, patv2_utils.sgf_name_test, false, opt.pd_iterations, false, opt.pd_start_n)
    else
        local nthread = 16
        -- Training
        if not opt.eval_only then
            -- train_one_epoch(pat_h, opt.num_games_train, sgf_name_train, false, true)
        end
        -- Test.
        train_one_epoch_multithread(pat_h, opt.num_games_test, patv2_utils.sgf_name_test, true, nthread)
        -- train_one_epoch(pat_h, opt.num_games_test, sgf_name_test, true)
    end

    -- save the pattern.
    if opt.save_pattern_prefix ~= "" then
        local filename = string.format("%s-%d-%d.bin", opt.save_pattern_prefix, iter, opt.num_iters)
        print("Save to " .. filename)
        pat.save(pat_h, filename)
    end
end
]]

pat.print(pat_h)
pat.destroy(pat_h)

