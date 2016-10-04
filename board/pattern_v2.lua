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
local sgf = require('utils.sgf')
local goutils = require('utils.goutils')

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "board/board.h"))
local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "pattern_v2.h"))
local C = ffi.load(paths.concat(script_path, "../libs/libpattern_v2.so"))

-- print(s)
local pat = {}

pat.train_positive = tonumber(symbols.TRAINING_POSITIVE)
pat.train_evalonly = tonumber(symbols.TRAINING_EVALONLY)
pat.train_negative = tonumber(symbols.TRAINING_NEGATIVE)

pat.type_strs = {
    [tonumber(symbols.SAMPLE_HEAP)] = "heap",
    [tonumber(symbols.SAMPLE_RANDOM)] = 'random',
    [tonumber(symbols.SAMPLE_MUST_MOVE)] = 'must_move'
}

local ids = ffi.new("GroupId4")
pat.params = ffi.new("PatternV2Params")
C.PatternV2DefaultParams(pat.params)

function pat.init(pattern_file, init_new_if_failed, rule)
    pat.rule = rule or board.chinese_rule
    if not pattern_file then init_new_if_failed = true end
    local pat_h = C.InitPatternV2(pattern_file, pat.params, init_new_if_failed and common.TRUE or common.FALSE)
    if not pat_h then 
        error("Initialize pattern failed!")
    end
    return pat_h
end

function pat.update_params(pat_h)
    C.PatternV2UpdateParams(pat_h, pat.params)
    pat.print(pat_h)
end

function pat.set_sample_params(pat_h, topn, T)
    C.PatternV2SetSampleParams(pat_h, topn, T)
end

function pat.set_verbose(pat_h, verbose)
    C.PatternV2SetVerbose(pat_h, verbose)
end

function pat.new(pat_h, b)
    assert(pat_h)
    return C.PatternV2InitBoardExtra(pat_h, b)
end

function pat.free(be) 
    C.PatternV2DestroyBoardExtra(be)
end

-- Play the next move and update all the internal structure.
function pat.play_move(be, m)
    return C.PatternV2PlayMove(be, m)
end

function pat.sample_move(be)
    local prob = ffi.new("float[1]")
    local topn = ffi.new("int[1]")
    C.PatternV2Sample(be, ids, topn, prob);
    return ids.c, topn[0], prob[0]
end

function pat.harvest_move(pat_h, be, m)
   return C.PatternV2Harvest(pat_h, be, m) == common.TRUE
end

function pat.harvest_play(pat_h, be, m)
    -- Harvest move before this move is taken.
    pat.harvest_move(pat_h, be, m)
    pat.play_move(be, m)
    -- Harvest move after this move is taken.
    pat.harvest_move(pat_h, be, m)
end

local function apply_moves(moves, func)
    local all_moves = C.InitAllMovesExt(#moves)
    for i = 1, #moves do
        all_moves.moves[i - 1].m = moves[i][1]
        all_moves.moves[i - 1].player = moves[i][2]
    end

    func(all_moves)
    C.DestroyAllMovesExt(all_moves)
end

function pat.harvest_many(pat_h, be, moves) 
    apply_moves(moves, 
        function(all_moves) 
            C.PatternV2HarvestMany(pat_h, be, all_moves)
        end)
end

function pat.init_sample_summary(summary)
    summary = summary or ffi.new("SampleSummary")
    C.InitSampleSummary(summary)
    return summary
end

function pat.init_perf_summary(summary)
    summary = summary or ffi.new("PerfSummary")
    C.InitPerfSummary(summary)
    return summary
end

function pat.init_grads()
    return C.PatternV2InitGradients()
end

function pat.destroy_grads(grads)
    C.PatternV2DestroyGradients(grads)
end

function pat.sample_many(be, num_moves, summary, output_comment)
    local all_moves = C.InitAllMovesExt(num_moves)
    local all_comments
    if output_comment then all_comments = C.InitAllMovesComments(num_moves) end

    summary = summary or pat.init_sample_summary() 

    local start = common.wallclock()
    C.PatternV2SampleMany(be, all_moves, all_comments, summary)
    local duration = common.wallclock() - start

    -- print(string.format("Sample done, collect moves: #moves = %d", C.AME_NumMoves(all_moves)))
    -- require 'fb.debugger'.enter()
    local res = { }
    for i = 1, num_moves do
        local move = { }
        local mm = all_moves.moves[i - 1]
        move[1] = mm.m
        move[2] = mm.player

        local x, y = common.coord2xy(mm.m)
        local player_str, coord_str = sgf.compose_move(x, y, mm.player)
        -- move[player_str] = coord_str
        local coord_str2, player_str2 = goutils.compose_move_gtp(x, y, mm.player)

        local comment = string.format("move: %s(%s), topn: %d, prob: %f, counter: %d, type: %s, heap_size: %d, total_prob: %f", 
            coord_str2, player_str2, mm.topn, mm.prob, mm.counter, pat.type_strs[mm.type], mm.heap_size, mm.total_prob)
        if output_comment then
            comment = comment .. "\n" .. ffi.string(all_comments.comments[i - 1])
        end
        move['C'] = comment

        table.insert(res, move)
    end
    C.DestroyAllMovesExt(all_moves)
    if all_comments then C.DestroyAllMovesComments(all_comments) end
    return res, summary, duration
end

function pat.sample_until(be, summary)
    summary = summary or pat.init_sample_summary() 

    local start = common.wallclock()
    C.PatternV2SampleUntilSingleThread(be, nil, summary)
    local duration = common.wallclock() - start

    return summary, duration
end

function pat.train_many(pat_h, be, moves, eval_only, summary)
    summary = summary or pat.init_perf_summary() 
    local black_training_sign = eval_only and pat.train_evalonly or pat.train_positive
    local white_training_sign = eval_only and pat.train_evalonly or pat.train_positive
    apply_moves(moves, 
        function(all_moves) 
            C.PatternV2TrainMany(pat_h, be, all_moves, black_training_sign, white_training_sign, summary)
        end)
    return summary
end

-- This function is used for multi-threading training.
function pat.train_many_add_grads(be, grads, moves, eval_only, summary)
    summary = summary or pat.init_perf_summary() 
    local black_training_sign = eval_only and pat.train_evalonly or pat.train_positive
    local white_training_sign = eval_only and pat.train_evalonly or pat.train_positive
    apply_moves(moves, 
        function(all_moves) 
            C.PatternV2TrainManySaveGradients(be, grads, all_moves, black_training_sign, white_training_sign, ffi.cast("PerfSummary *", summary))
        end)
    return summary
end

function pat.train_update_weights(h, grad)
    C.PatternV2UpdateWeightsAndCleanGradients(h, grad)
end

function pat.train_start(pat_h)
    C.PatternV2StartTraining(pat_h)
end

-- In opt, we need to have the following information
--    komi: komi + handi
--    rule (optional)
--    player_won: which player wins the game.
--    iterations: the number of iterations we will try for a single game.
--    training: whether this is a training procedure or eval_only procedure.
function pat.train_policy_gradient(pat_h, grads, b, opt, sample_summary, perf_summary)
    local game_info = ffi.new("GameScoring")
    -- Komi + Handi
    game_info.komi = opt.komi or 6.5
    game_info.rule = opt.rule or board.chinese_rule
    game_info.player_won = opt.player_won
    game_info.board = b
    game_info.iterations = opt.iterations or 100

    sample_summary = sample_summary or pat.init_sample_summary() 
    perf_summary = perf_summary or pat.init_perf_summary() 
    C.PatternV2TrainPolicyGradient(pat_h, grads, game_info, opt.training and common.TRUE or common.FALSE, sample_summary, perf_summary)
    return sample_summary, perf_summary
end
 
function pat.print(pat_h, be)
    C.PatternV2PrintStats(pat_h)
    if be then
        C.PatternV2BoardExtraPrintStats(be)
    end
end

function pat.check(be)
    return C.PatternV2BoardExtraCheck(be) == common.TRUE
end

function pat.get_board(be)
    return C.PatternV2GetBoard(be)
end

function pat.save(pat_h, filename)
    return C.SavePatternV2(pat_h, filename) == common.TRUE
end

function pat.load(pat_h, filename)
    return C.LoadPatternV2(pat_h, filename) == common.TRUE
end

function pat.run(pat_h, b, max_depth, verbose)
    -- Context and verbse is not used for now. 
    local be = pat.new(pat_h, b)

    -- local moves, summary, this_duration = pat.sample_many(be, opt.num_moves)
    local summary, this_duration = pat.sample_until(be)
    -- duration = duration + this_duration
    -- board.copyfrom2(b, pat.get_board(be))
    local final_board = pat.get_board(be)

    local score = board.get_fast_score(final_board, pat.rule)
    board.copyfrom2(b, final_board)

    pat.free(be)
    return score
end

function pat.dump_status(be, max_heap_size)
    return ffi.string(C.PatternV2BoardExtraDumpInfo(be, max_heap_size))
end

function pat.destroy(pat_h)
    C.DestroyPatternV2(pat_h)
end

function pat.print_sample_summary(sample_summary)
    local p = ffi.cast("SampleSummary *", sample_summary)
    C.PrintSampleSummary(p)
end

function pat.print_perf_summary(perf_summary)
    local p = ffi.cast("PerfSummary *", perf_summary)
    C.PrintPerfSummary(p)
end

function pat.combine_sample_summary(dst, src)
    local p_dst = ffi.cast("SampleSummary *", dst)
    local p_src = ffi.cast("const SampleSummary *", src)
    C.CombineSampleSummary(p_dst, p_src)
end

function pat.combine_perf_summary(dst, src)
    local p_dst = ffi.cast("PerfSummary *", dst)
    local p_src = ffi.cast("const PerfSummary *", src)
    C.CombinePerfSummary(p_dst, p_src)
end

return pat
