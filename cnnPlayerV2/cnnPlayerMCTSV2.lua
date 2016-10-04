--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

package.path = package.path .. ';../?.lua'

local utils = require 'utils.utils'

utils.require_torch()
utils.require_cutorch()

local playoutv2 = require('mctsv2.playout_multithread')
local common = require("common.common")
local goutils = require 'utils.goutils'
local CNNPlayerV2 = require 'cnnPlayerV2.cnnPlayerV2Framework'
local board = require 'board.board'
local pl = require 'pl.import_into'()

local opt = pl.lapp[[
    --rollout         (default 1000)         The number of rollout we use.
    --dcnn_rollout    (default -1)           The number of dcnn rollout we use (If we set to -1, then it is the same as rollout), if cpu_only is set, then dcnn_rollout is not used.
    --dp_max_depth    (default 10000)        The max_depth of default policy.
    -v,--verbose      (default 1)            The verbose level (1 = critical, 2 = info, 3 = debug)
    --print_tree                             Whether print the search tree.
    --max_send_attempts (default 3)          #attempts to send to the server.
    --pipe_path         (default "/data/local/go/") Pipe path
    --tier_name         (default "ai.go-evaluator") Tier name
    --server_type       (default "local")    We can choose "local" or "cluster". For open source version, for now "cluster" is not usable.
    --tree_to_json                           Whether we save the tree to json file for visualization. Note that pipe_path will be used.
    --num_tree_thread   (default 16)         The number of threads used to expand MCTS tree.
    --num_gpu           (default 1)          The number of gpus to use for local play.
    --sigma             (default 0.05)       Sigma used to perturb the win rate in MCTS search.
    --use_sigma_over_n                       use sigma / n (or sqrt(nparent/n)). This makes sigma small for nodes with confident win rate estimation.
    --num_virtual_games (default 0)          Number of virtual games we use.
    --acc_prob_thres    (default 0.8)        Accumulated probability threshold. We remove the remove if by the time we see it, the accumulated prob is greater than this thres.
    --max_num_move      (default 20)          Maximum number of moves to consider in each tree node.
    --min_num_move      (default 1)          Minimum number of moves to consider in each tree node.
    --decision_mixture_ratio (default 5.0)   Mixture MCTS count ratio with cnn_confidence.
    --time_limit        (default 0)          Limit time for each move in second. If set to 0, then there is no time limit.
    --win_rate_thres    (default 0.0)        If the win rate is lower than that, resign.
    --use_pondering                          Whether we use pondering
    --exec              (default "")         Whether we run an initial script
    --setup_board       (default "")         Setup board. The argument is "sgfname moveto"
    --dynkomi_factor    (default 0.0)        Use dynkomi_factor
    --num_playout_per_rollout (default 1)    Number of playouts per rollouts.
    --single_move_return                     Use single move return (When we only have one choice, return the move immediately)
    --expand_search_endgame                  Whether we expand the search in end game.
    --default_policy    (default "v2")       The default policy used. Could be "simple", "v2".
    --default_policy_pattern_file (default "../models/playout-model.bin") The patter file
    --default_policy_temperature  (default 0.125)   The temperature we use for sampling.
    --online_model_alpha         (default 0.0)      Whether we use online model and its alpha
    --online_prior_mixture_ratio (default 0.0)      Online prior mixture ratio.
    --use_rave                               Whether we use RAVE.
    --use_cnn_final_score                    Whether we use CNN final score.
    --min_ply_to_use_cnn_final_score (default 100)     When to use cnn final score.
    --final_mixture_ratio            (default 0.5)    The mixture ratio we used.
    --percent_playout_in_expansion   (default 0)      The percent of threads that will run playout when we expand the node. Other threads will block wait.
    --use_old_uct                                     Use old uct
    --use_async                                       Open async model.
    --cpu_only                                        Whether we only use fast rollout.
    --expand_n_thres                 (default 0)      Statistics collected before expand.
    --sample_topn                    (default -1)     If use v2, topn we should sample..
    --rule                           (default cn)     Use JP rule : jp, use CN rule: cn
    --heuristic_tm_total_time        (default 0)      Time for heuristic tm (0 mean you don't use it).
    --min_rollout_peekable           (default 20000)  The command peek will return if the minimal number of rollouts exceed this threshold
    --save_sgf_per_move                               If so, then we save sgf file for each move
    --use_formal_params                               If so, then use formal parameters
]]

local function load_params_for_formal_game()
    opt.rollout = 1000000  --       (default 1000)         The number of rollout we use.
    opt.dcnn_rollout = -1          -- The number of dcnn rollout we use (If we set to -1, then it is the same as rollout), if cpu_only is set, then dcnn_rollout is not used.
    opt.dp_max_depth = 10000 --    (default 10000)        The max_depth of default policy.
    opt.verbose = 1 --      (default 1)            The verbose level (1 = critical, 2 = info, 3 = debug)
    opt.print_tree = true --                             Whether print the search tree.
    opt.max_send_attempts = 3 -- (default 3)          #attempts to send to the server.
    opt.pipe_path = "/data/local/go/" --         (default "/data/local/go/") Pipe path
    opt.tier_name = "ai.go-evaluator" --         (default "ai.go-evaluator") Tier name
    opt.server_type = "local" --       (default "local")                 We can choose "local" or "cluster"
    opt.tree_to_json = false --                           Whether we save the tree to json file for visualization. Note that pipe_path will be used.
    opt.num_tree_thread = 16 --   (default 16)         The number of threads used to expand MCTS tree.
    opt.num_virtual_games = 5
    opt.acc_prob_thres = 1 --    (default 0.8)        Accumulated probability threshold. We remove the remove if by the time we see it, the accumulated prob is greater than this thres.
    opt.max_num_move = 7 --      (default 20)          Maximum number of moves to consider in each tree node.
    opt.min_num_move = 1    --  (default 1)          Minimum number of moves to consider in each tree node.
    opt.decision_mixture_ratio = 5.0 -- (default 5.0)   Mixture MCTS count ratio with cnn_confidence.
    opt.win_rate_thres = 0.1  --  (default 0.0)        If the win rate is lower than that, resign.
    opt.use_pondering = true --                        Whether we use pondering
    opt.dynkomi_factor = 0.0 --   (default 0.0)        Use dynkomi_factor
    opt.single_move_return = false --                     Use single move return (When we only have one choice, return the move immediately)
    opt.expand_search_endgame = false --                  Whether we expand the search in end game.
    opt.default_policy= "v2" --    (default "v2")       The default policy used. Could be "simple", "pachi", "v2".
    opt.default_policy_pattern_file = "../models/playout-model.bin" -- The default policy pattern file
    opt.default_policy_temperature = 0.5
    opt.online_model_alpha = 0.0 --         (default 0.0)      Whether we use online model and its alpha
    opt.online_prior_mixture_ratio = 0.0 -- (default 0.0)      Online prior mixture ratio.
    opt.use_rave = false --                               Whether we use RAVE.
    opt.use_cnn_final_score = false --                    Whether we use CNN final score.
    opt.min_ply_to_use_cnn_final_score = 100 -- (default 100)     When to use cnn final score.
    opt.final_mixture_ratio = 0.5 -- (default 0.5)    The mixture ratio we used.
    opt.use_old_uct = false
    opt.percent_playout_in_expansion = 5
    opt.use_async = false      --                                 Open async model.
    opt.cpu_only = false     --                                   Whether we only use fast rollout.
    opt.expand_n_thres = 0 --              (default 0)      Statistics collected before expand.
    opt.sample_topn = -1 --                   (default -1)     If use v2, topn we should sample..
    opt.rule = "jp"  --             (default cn)         Use JP rule : jp, use CN rule: cn
    opt.num_playout_per_rollout = 1
    opt.save_sgf_per_move = true
end

if opt.use_formal_params then
    load_params_for_formal_game()
end

-- io.stderr:write("Loading model = " .. opt.input)
local function set_playout_params_from_opt()
    playoutv2.params.print_search_tree = opt.print_tree and common.TRUE or common.FALSE
    playoutv2.params.pipe_path = opt.pipe_path
    playoutv2.params.tier_name = opt.tier_name
    playoutv2.params.server_type = opt.server_type == "local" and playoutv2.server_local or playoutv2.server_cluster
    playoutv2.params.verbose = opt.verbose
    playoutv2.params.num_gpu = opt.num_gpu
    playoutv2.params.dynkomi_factor = opt.dynkomi_factor
    playoutv2.params.cpu_only = opt.cpu_only and common.TRUE or common.FALSE
    playoutv2.params.rule = opt.rule == "jp" and board.japanese_rule or board.chinese_rule

    -- Whether to use heuristic time manager. If so, then (total time is info->common_params->heuristic_tm_total_time)
    if opt.heuristic_tm_total_time > 0 then
        -- 1. Gradually add more time per move for the first 30 moves (ply < 60). (1 sec -> 15 sec, 15s * 30 / 2 = 225 s)
        -- 2. Keep 15 sec move, 31-100 move ( 70 * 15sec = 1050 s)
        -- 3. Decrease the time linearly, 101-130 (30 * 15/2 = 225s)
        -- 4. Once we enter the region that time_left < 120s, try 1sec per move.
        -- If the total time is x, then the max_time_spent y could be computed as:
        --     y * ply1 / 4 + (ply2 - ply1) / 2 * y + y * (ply3 - ply2) / 4  < alpha * x
        --     y * [ -ply1/4 + ply2/4 + ply3 / 4] < alpha * x
        --     y < 4 * alpha * x / (ply2 + ply3 - ply1)
        -- For x = 1800sec, we have y = 4 * alpha * 1800 / (200 + 260 - 60) = 12.5 sec.
        local alpha = 0.75;
        playoutv2.params.heuristic_tm_total_time = opt.heuristic_tm_total_time
        playoutv2.params.max_time_spent = 4 * alpha * opt.heuristic_tm_total_time / (playoutv2.thres_ply2 + playoutv2.thres_ply3 - playoutv2.thres_ply1);
        playoutv2.params.min_time_spent = playoutv2.min_time_spent;
    end

    playoutv2.tree_params.max_depth_default_policy = opt.dp_max_depth
    playoutv2.tree_params.max_send_attempts = opt.max_send_attempts
    playoutv2.tree_params.verbose = opt.verbose
    playoutv2.tree_params.time_limit = opt.time_limit
    playoutv2.tree_params.num_receiver = opt.num_gpu
    playoutv2.tree_params.sigma = opt.sigma
    playoutv2.tree_params.use_pondering = opt.use_pondering
    playoutv2.tree_params.use_cnn_final_score = opt.use_cnn_final_score and common.TRUE or common.FALSE
    playoutv2.tree_params.final_mixture_ratio = opt.final_mixture_ratio
    playoutv2.tree_params.min_ply_to_use_cnn_final_score = opt.min_ply_to_use_cnn_final_score

    playoutv2.tree_params.num_tree_thread = opt.num_tree_thread
    playoutv2.tree_params.rcv_acc_percent_thres = opt.acc_prob_thres * 100.0
    playoutv2.tree_params.rcv_max_num_move = opt.max_num_move
    playoutv2.tree_params.rcv_min_num_move = opt.min_num_move
    playoutv2.tree_params.decision_mixture_ratio = opt.decision_mixture_ratio
    playoutv2.tree_params.single_move_return = opt.single_move_return and common.TRUE or common.FALSE

    playoutv2.tree_params.default_policy_choice = playoutv2.dp_table[opt.default_policy]
    playoutv2.tree_params.pattern_filename = opt.default_policy_pattern_file

    playoutv2.tree_params.use_online_model = math.abs(opt.online_model_alpha) < 1e-6 and common.FALSE or common.TRUE
    playoutv2.tree_params.online_model_alpha = opt.online_model_alpha
    playoutv2.tree_params.online_prior_mixture_ratio = opt.online_prior_mixture_ratio
    playoutv2.tree_params.use_rave = opt.use_rave and common.TRUE or common.FALSE
    playoutv2.tree_params.use_async = opt.use_async and common.TRUE or common.FALSE
    playoutv2.tree_params.expand_n_thres = opt.expand_n_thres
    playoutv2.tree_params.num_virtual_games = opt.num_virtual_games
    playoutv2.tree_params.percent_playout_in_expansion = opt.percent_playout_in_expansion

    playoutv2.tree_params.default_policy_sample_topn = opt.sample_topn
    playoutv2.tree_params.default_policy_temperature = opt.default_policy_temperature
    playoutv2.tree_params.use_old_uct = opt.use_old_uct and common.TRUE or common.FALSE
    playoutv2.tree_params.use_sigma_over_n = opt.use_sigma_over_n and common.TRUE or common.FALSE
    playoutv2.tree_params.num_playout_per_rollout = opt.num_playout_per_rollout
end

local tr
local count = 0
local signature

local function prepare_prefix()
    if opt.tree_to_json then
        local prefix = paths.concat(opt.pipe_path, signature, string.format("mcts_%04d", count))
        count = count + 1
        return prefix
    end
end

local callbacks = { }

function callbacks.set_komi(komi, handi)
    if komi ~= nil then
        playoutv2.set_params(tr, { komi = komi })
    end
    -- for large handi game, cnn need to give more candidate, so mcts could possibly be more aggressive
    local changed_params
    if  handi and handi <= -5 then
        changed_params = { dynkomi_factor=1.0 }
    end
    if changed_params then
        if playoutv2.set_params(tr, changed_params) then
            playoutv2.print_params(tr)
        end
    end
end

function callbacks.adjust_params_in_game(b, isCanada)
    -- When we are at the end of game, pay attention to local tactics.
    if not opt.expand_search_endgame then return end
    local changed_params
    if isCanada then -- enter canada time setting, so lower the rollout number
        local min_rollout = 7500
        changed_params = {num_rollout = min_rollout, num_rollout_per_move = min_rollout}
    else
        if b._ply >= 230 then
            -- Try avoid blunder if there is any.
            -- changed_params = { rcv_max_num_move = 7, rcv_min_num_move = 3 }
        elseif b._ply >= 150 then
            changed_params = { rcv_max_num_move = 5 }
        end
    end
    if changed_params then
        if playoutv2.set_params(tr, changed_params) then
            playoutv2.print_params(tr)
        end
    end
end

function callbacks.on_time_left(sec_left, num_moves)
    playoutv2.set_time_left(tr, sec_left, num_moves)
end

function callbacks.new_game()
    set_playout_params_from_opt()

    if tr then
        playoutv2.restart(tr)
    else
        local rs = {
            rollout = opt.rollout,
            dcnn_rollout_per_move = (opt.dcnn_rollout == -1 and opt.rollout or opt.dcnn_rollout),
            rollout_per_move = opt.rollout
        }

        tr = playoutv2.new(rs)
    end
    count = 0
    signature = utils.get_signature()
    io.stderr:write("New MCTS game, signature: " .. signature)
    os.execute("mkdir -p " .. paths.concat(opt.pipe_path, signature))
    playoutv2.print_params(tr)
end

function callbacks.quit_func()
    if tr then
        playoutv2.free(tr)
    end
end

function callbacks.move_predictor(b, player)
    local prefix = prepare_prefix()
    local m = playoutv2.play_rollout(tr, prefix, b)
    if prefix then io.stderr:write("Save tree to " .. prefix) end
    return m.x + 1, m.y + 1, m.win_rate
end

function callbacks.move_receiver(x, y, player)
    local prefix = prepare_prefix()
    playoutv2.prune_xy(tr, x, y, player, prefix)
end

function callbacks.peek_simulation(num_simulation)
    return playoutv2.set_params(tr, { min_rollout_peekable = num_simulation })
end

function callbacks.move_peeker(b, player, topk)
    return playoutv2.peek_rollout(tr, topk, b)
end

function callbacks.undo_func(b, undone_move)
    if goutils.coord_is_pass(undone_move) then
        playoutv2.undo_pass(tr, b)
    else
        playoutv2.set_board(tr, b)
    end
end

function callbacks.set_board(b)
    playoutv2.set_board(tr, b)
end

function callbacks.thread_switch(arg)
    if arg == "on" then
        playoutv2.thread_on(tr)
    elseif arg == 'off' then
        playoutv2.thread_off(tr)
    else
        io.stderr:write("Command " .. arg .. " is not recognized!")
    end
end

function callbacks.set_move_history(history)
    for _, h in pairs(history) do
        playoutv2.add_move_history(tr, unpack(h))
    end
end

function callbacks.set_verbose_level(verbose_level)
    if playoutv2.set_params(tr, { verbose = verbose_level }) then
        playoutv2.print_params(tr)
    end
end

local opt2 = {
    rule = opt.rule,
    win_rate_thres = opt.win_rate_thres,
    exec = opt.exec,
    setup_board = opt.setup_board,
    default_policy = opt.default_policy,
    default_policy_pattern_file = opt.default_policy_pattern_file,
    default_policy_temperature = opt.default_policy_temperature,
    default_policy_sample_topn = opt.sample_topn,
    save_sgf_per_move = opt.save_sgf_per_move
}

local cnnplayer = CNNPlayerV2("CNNPlayerV2MCTS", "go_player_v2_mcts", "1.0", callbacks, opt2)
cnnplayer:mainloop()

