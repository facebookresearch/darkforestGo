--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local nnutils = require 'fbcode.experimental.deeplearning.yuandong.utils.nnutils'
local goutils = require 'utils.goutils'
local common = require("common.common")
local dcnn_utils = require 'board.dcnn_utils'
local board = require("board.board")
local sgf = require("utils.sgf")

local om = require 'board.ownermap'
-- local dp = require('board.default_policy')
local dp = require('pachi_tactics.moggy')

local pl = require 'pl.import_into'()

-- Initialize the def policy and so on.
local def_policy = dp.new()
local ownermap = om.new()

function check_resign(b, opt)
    -- check every 10 rounds.
    local resign_thres = 10
    local player = b._next_player

    local score, livedead, territory, scores = om.util_compute_final_score(
        ownermap, b, opt.komi + opt.handi, nil, 
        function (b, max_depth) return dp.run(def_policy, b, max_depth, false) end
    )
    local min_score = scores:min() 
    local max_score = scores:max()

    local resigned_aspect 

    if min_score > resign_thres then resigned_aspect = common.white end
    if max_score < -resign_thres then resigned_aspect = common.black end

    if min_score == max_score and max_score == score then
        -- the estimation is believed to be absolutely correct.
        if score > 0.5 then resigned_aspect = common.white end
        if score < -0.5 then resigned_aspect = common.black end
    end

    return resigned_aspect, score, min_score, max_score
end

function play_one_game(model, b, dcnn_opt, opt)
    -- Start the self play.
    local moves = {}
    while true do 
        if opt.debug then
            board.show(b, 'last_move')
        end
        local m = {}
        -- Resign if one side loose too much.
        if b._ply >= 140 and b._ply % 20 == 1 then
            local resigned_aspect, score, min_score, max_score = check_resign(b, opt) 
            if opt.debug then
                print(string.format("score = %.1f, min_score = %.1f, max_score = %.1f", score, min_score, max_score))
            end
            if resigned_aspect then
                return {
                    moves = moves, 
                    resigned = resigned_aspect,
                    score = score,
                    max_score = max_score,
                    min_score = min_score
                }
            else
                m["C"] = string.format("score: %.1f, min_score: %.1f, max_score: %.1f", score, min_score, max_score)
            end
        end

        -- Generate the move.
        local x, y = dcnn_utils.sample(dcnn_opt, b, b._next_player)

        if x == nil then
            local player_str = b._next_player == common.white and 'W' or 'B'
            m[player_str] = ''
            x, y = 0, 0
        else
            -- Write the location in sgf format.
            local player_str, coord_str = sgf.compose_move(x, y, b._next_player)
            m[player_str] = coord_str
        end

        table.insert(moves, m)
        
        board.play(b, x, y, b._next_player)

        if board.is_game_end(b) then
            local resigned_aspect, score, min_score, max_score = check_resign(b, opt) 
            return {
                moves = moves, 
                resigned = 0,
                score = score,
                max_score = max_score,
                min_score = min_score
            }
        end
    end

    local resigned_aspect, score, min_score, max_score = check_resign(b, opt) 
    return {
        moves = moves, 
        resigned = 0,
        score = score,
        max_score = max_score,
        min_score = min_score
    }
end

local opt = pl.lapp[[
    --codename             (default "df2_cpu")   Code name for models. If this is not empty then --input will be omitted.
    -f,--feature_type      (default "old")       By default we only test old features. If codename is specified, this is omitted.
    -r,--rank              (default "9d")        We play in the level of rank.
    --use_local_model                            Whether we just load local model from the current path
    --komi                 (default 7.5)         The komi we used
    --handi                (default 0)           The handicap stones we placed.
    -c,--usecpu            (default 1)           Whether we use cpu to run the program.
    --shuffle_top_n        (default 300)         We random choose one of the first n move and play it.
    --debug                                      Wehther we use debug mode
    --num_games            (default 10)          The number of games to be played.
    --sample_step          (default -1)          Sample at a particular step.
    --presample_codename   (default "df_cpu")
    --presample_ft         (default "old")
    --copy_path            (default "")
]]

if opt.debug then 
    nnutils.dbg_set()
end

-- opt.feature_type and opt.userank are necessary for the game to be played.
local dcnn_opt = dcnn_utils.init(opt)

local b = board.new()

for i = 1, opt.num_games do
    print(string.format("Play game: %d/%d", i, opt.num_games))
    board.clear(b)
    local sample_step = math.random(390)
    local res = play_one_game(model, b, dcnn_opt, opt)
    if #res.moves <= sample_step then
        print(string.format("bad sample - moves: %d , sample_step: %d", #res.moves, sample_step))
    else
        -- write the SGF file
        local footprint = string.format("%s-%s__%d", utils.get_signature(), utils.get_randString(6), sample_step)
        local srcSGF = string.format("%s.sgf", footprint)
        local f = assert(io.open(srcSGF, "w"))
        local re
        if res.resigned == common.white then
            re = 'B+Resign'
        elseif res.resigned == common.black then
            re = 'W+Resign'
        else
            re = res.score > 0 and string.format("B+%.1f", res.score) or string.format("W+%.1f", -res.score)
        end
        local date = utils.get_current_date()

        --   komi, player_w, player_b, date, re
        local header = { 
            result = re, 
            player_b = opt.codename,
            player_w = opt.codename,
            date = date,
            komi = opt.komi
        }
        f:write(sgf.sgf_string(header, res.moves))
        f:close()
        if opt.copy_path ~= "" then
            local cpcmd = string.format("cp %s %s", srcSGF, opt.copy_path)
            os.execute(cpcmd)
        end
    end
    collectgarbage()
    collectgarbage()
end


dp.free(def_policy)
om.free(ownermap)
