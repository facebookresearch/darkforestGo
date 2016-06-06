--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

-- Try compare with three playout tactics.
--
local om = require 'board.ownermap'
local patv2_utils = require 'board.pattern_v2_utils'
--
local pat = require 'board.pattern_v2'
local dp = require 'board.default_policy'
local dp_pachi = require('pachi_tactics.moggy')
local dp_dcnn = require('board.default_policy_dcnn')

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
    --temperature     (default 1.0)       Sample temperature, only used for v2     
    --sgf_file        (default "")        If not empty, then we sample after move_from moves.
    --sgf_list        (default "")        A list of sgf files.
    --move_from       (default 0)         Sample since move_from
    --num_moves       (default 200)       The number of moves to simulate.
    --num_games       (default 10)        The number of games to simulate.
    --num_games_loaded  (default 100)      The number of games to be loaded from the database.
    --stats                               If true, get the score stats.
    --start_n         (default 100)       Run default policy from start_n
    --step_n          (default 20)        Run default policy after step n
]]

local ownermap = om.new()
local pat_utils = { } 
local dp_simple_utils = { }
local dp_pachi_utils = { }
local dp_dcnn_utils = { }

function pat_utils.init()
    pat.params.verbose = opt.verbose
    local pat_h = pat.init(opt.pattern_file)
    pat.set_sample_params(pat_h, opt.sample_topn, opt.temperature)

    pat.print(pat_h)

    pat_utils.h = pat_h
    pat_utils.run = pat.run
end

function dp_simple_utils.init()
    dp_simple_utils.h = dp.new()
    dp_simple_utils.run = dp.run
end

function dp_pachi_utils.init()
    dp_pachi_utils.h = dp_pachi.new()
    dp_pachi_utils.run = dp_pachi.run
end

function dp_dcnn_utils.init()
    local dcnn_opt = {
        shuffle_top_n = opt.sample_topn,
        codename = 'darkfores2',
    }
    dp_dcnn_utils.h = dp_dcnn.init(dcnn_opt)
    dp_dcnn_utils.run = dp_dcnn.run
end

function get_stats(dp_utils, b, komi, handi, trial, black_win) 
    local score, livedead, territory, scores = om.util_compute_final_score(
           ownermap, b, komi + handi, trial, 
           function (b, max_depth) return dp_utils.run(dp_utils.h, b, max_depth, false) end
    )
    local stones = om.get_territorylist(territory)

    local mean_err = 0.0
    for i = 1, scores:size(1) do
        if (scores[i] > 0 and not black_win) or (scores[i] < 0 and black_win) then
            mean_err = mean_err + 1.0
        end
    end
    mean_err = mean_err / scores:size(1)

    if (score > 0 and not black_win) or (score < 0 and black_win) then
        pred_win_err = 1.0
    else
        pred_win_err = 0.0
    end

    return {
        mean_err = mean_err,
        score = score,
        pred_win_err = pred_win_err, 
        mean = scores:mean(),
        min = scores:min(), 
        max = scores:max(),
        std = scores:std(),
        livedead = livedead,
        territory = territory
    }
end

----------------------------------- Main --------------------------------
local dps = { 
    -- pachi = dp_pachi_utils,
    -- simple = dp_simple_utils,
    v2 = pat_utils,
    -- dcnn = dp_dcnn_utils,
}

for k, v in pairs(dps) do
    v.init()
end

print("All default policies are initialized!")

-- Load sgf files.
--
local sgf_files = { }
local root_dir = ""
if opt.sgf_file ~= "" then
    table.insert(sgf_files, opt.sgf_file)
elseif opt.sgf_list ~= "" then
    sgf_files = pl.utils.readlines(opt.sgf_list)
    root_dir = pl.path.dirname(opt.sgf_list)
else
    error("Either opt.sgf_file or opt.sgf_list must be non-empty!")
end

local m_group = 10

local stats = { }
for k, v in pairs(dps) do 
    stats[k] = { } 
    for i = 1, m_group do
        stats[k][i] = { 
            ply = opt.start_n + opt.step_n * (i - 1), 
            mean_err = 0.0, 
            mean = 0.0, 
            mean_err_ld = 0.0,
            std = 0.0, 
            n = 0 
        }
    end
end

local root = '/mnt/vol/gfsai-oregon/ai-group/datasets/go_gogod'
local mtbl_file = 'GoGoD2015.mtbl'
patv2_utils.load(root, mtbl_file, opt)

for i = 1, opt.num_games_loaded do
    local filename = patv2_utils.sgf_name_train[i]

    print(string.format("Deal with game [%d/%d]: %s", i, opt.num_games_loaded, filename))
    local game = patv2_utils.retrieve_sgf(filename)

-- Finally lets sample the move.
--[[
for i = 1, #sgf_files do 
    print("Deal with game = " .. sgf_files[i])
    local game = load_sgf(paths.concat(root_dir, sgf_files[i]))
--]]
--
    local b, game = patv2_utils.setup_board(game, opt.start_n)
    if game then 
        local init_b = board.copyfrom(b)
        -- board.show(game_info.b, 'last_move')
        local komi = game:get_komi() 
        local result = game:get_result()
        local handi = game:get_handi_count()

        if result and komi and handi then
            -- Who win the game
            local black_win = (result:sub(1, 2) == 'B+') 
            local total_moves = game:get_total_moves() 
            print(string.format("#moves: %d, res: %s, black_win: %s\n", game:get_total_moves(), result, black_win and "yes" or "no"))

            local stat = { } 

            for j = 1, m_group do
                if opt.start_n + opt.step_n * (j - 1) > total_moves then break end

                for k, v in pairs(dps) do
                    local scores = get_stats(v, b, komi, handi, opt.num_games, black_win) 
                    -- require 'fb.debugger'.enter()
                    if j == 1 then stat[k] = { } end
                    table.insert(stat[k], { pred_win_err = scores.pred_win_err, mean = scores.mean, std = scores.std, mean_err = scores.mean_err })

                    --[[
                    local territorylist =  om.get_territorylist(scores.territory) 
                    print("==================== " .. k .. " =====================")
                    print(string.format("#W = %d, #B = %d, #dame = %d", 
                    #territorylist.w, #territorylist.b, #territorylist.dames))

                    print("White = ")
                    om.print_list(territorylist.w_str)
                    print("Black = ")
                    om.print_list(territorylist.b_str)
                    print("Dame = ")
                    om.print_list(territorylist.dames_str)

                    om.show_deadstones(b, scores.livedead)
                    print("")
                    print(string.format("%s: score = %f, std = %f, min = %f, max = %f", k, scores.mean, scores.std, scores.min, scores.max))
                    ]]
                end

                game:play(function (move, counter) 
                    local x, y, player = sgf.parse_move(move, false, true)
                    board.play(b, x, y, player)  
                    return true
                end, opt.step_n, true)
            end

            -- Compute the diff. 
            for k, v in pairs(stat) do
                local s = k .. ": "
                -- require 'fb.debugger'.enter()
                for j = 1, #v do
                    v[j].mean = math.abs(v[j].mean - v[#v].mean)
                    stats[k][j].mean = stats[k][j].mean + v[j].mean
                    stats[k][j].std = stats[k][j].std + v[j].std
                    stats[k][j].mean_err = stats[k][j].mean_err + v[j].mean_err
                    stats[k][j].mean_err_ld = stats[k][j].mean_err_ld + v[j].pred_win_err 
                    stats[k][j].n = stats[k][j].n + 1

                    s = s .. string.format("[%d]: %f %f %f %f", stats[k][j].ply, v[j].mean, v[j].std, v[j].mean_err, v[j].pred_win_err)
                end
                print(s)
            end
        end
    else
        print("No result, skipping")
    end
end

-- Finally show the stats
for k, v in pairs(stats) do
    print(k .. ": ")
    for j = 1, m_group do
        local n = v[j].n
        print(string.format("[%d]: pred_err = %f, mean = %f, std = %f, pred_err_ld = %f", v[j].ply, v[j].mean_err / n, v[j].mean / n, v[j].std / n, v[j].mean_err_ld / n))
    end
end

om.free(ownermap)
