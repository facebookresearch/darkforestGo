--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local ffi = require 'ffi'
local utils = require('utils.utils')
local goutils = require 'utils.goutils'
local board = require 'board.board'
local common = require("common.common")
local dp = require('board.default_policy')

local symbols, s = utils.ffi_include(paths.concat(common.script_path(), "package.h"))

local num_first_move = tonumber(symbols.NUM_FIRST_MOVES)
local sig_restart = tonumber(symbols.SIG_RESTART)
local sig_finishsoon = tonumber(symbols.SIG_FINISHSOON)
local sig_ok = tonumber(symbols.SIG_OK)
local move_normal = tonumber(symbols.MOVE_NORMAL)
local move_simple_ko = tonumber(symbols.MOVE_SIMPLE_KO)
local move_tactical = tonumber(symbols.MOVE_TACTICAL)

local hostname = utils.get_hostname()
local curr_seq = 0

local util_pkg = { }

function util_pkg.init(max_batch, feature_type)
    -- Default policy, used to insert some heuristics.
    util_pkg.def_policy = dp.new()
    -- For server use.
    local params = dp.new_params(true)
    dp.set_params(util_pkg.def_policy, params)

    util_pkg.max_batch = max_batch
    util_pkg.opt = {
        feature_type = feature_type,
        userank = true
    }
    util_pkg.features = { }
    util_pkg.t_received = { }

    local boards = ffi.new("MBoard*[?]", max_batch)
    local moves = ffi.new("MMove*[?]", max_batch)
    local anchor = {}  -- prevent gc
    for i = 1, max_batch do
        local b = ffi.new("MBoard")
        table.insert(anchor, b)
        -- Zero based indexing.
        boards[i-1] = b

        local m = ffi.new("MMove")
        table.insert(anchor, m)
        -- Zero based indexing.
        moves[i-1] = m
    end
    util_pkg.anchor = anchor

    -- These two are open interface so that other could use. 
    util_pkg.boards = boards
    util_pkg.moves = moves
end

function util_pkg.dbg_set()
    utils.dbg_set()
end

function util_pkg.get_boards()
    return util_pkg.boards
end

function util_pkg.get_moves()
    return util_pkg.moves
end

function util_pkg.extract_board_feature(k)
    -- utils.dprint("Start waiting on board")
    local mboard = util_pkg.boards[k - 1]
    local t_received = common.wallclock()
    local player = mboard.board._next_player
    -- print("GetBoard return, result = " .. ret)
    curr_seq = math.max(tonumber(mboard.seq), curr_seq)
    if utils.dbg_get() then
        utils.dprint("Receive board at b = %x, seq = %d, k = %d, player = %d [%s]", 
            tonumber(mboard.b), tonumber(mboard.seq), k, tonumber(player), common.player_name[tonumber(player)]);
        board.show(mboard.board, "last_move")
            -- require 'fb.debugger'.enter()
    end
    local feature, named_features = goutils.extract_feature(mboard.board, player, util_pkg.opt, '9d') 
    -- Save feature for future use.
    util_pkg.features[k] = named_features 
    util_pkg.t_received[k] = t_received
    return feature:cuda()
    -- return tonumber(mboard.seq), tonumber(mboard.b)
end

function util_pkg.prepare_move(k, sortProb, sortInd, score)
    -- If they are invalid situations, do not send.
    utils.dprint("Start sending move")

    local mmove = util_pkg.moves[k - 1]
    -- Note that unlike in receive_move, k is no longer batch_idx, since we skip a batch slot if it does not see a board situation.
    local mboard = util_pkg.boards[k - 1]
    local player = mboard.board._next_player

    mmove.t_sent = mboard.t_sent
    -- mmove.board_hash = mboard.board.hash
    mmove.t_received = util_pkg.t_received[k] 
    mmove.t_replied = common.wallclock()
    mmove.hostname = hostname
    mmove.player = player
    mmove.b = mboard.b
    mmove.seq = mboard.seq
    utils.dprint("Send b = %x, seq = %d, k = %d", tonumber(mmove.b), tonumber(mmove.seq), k)
    -- board.show(mboard.board, "last_move")
    -- require 'fb.debugger'.enter()
    if score then
        -- Compute scoring
        mmove.has_score = common.TRUE
        -- mmove.score = score[1][1] - score[2][1]
        mmove.score = 2 * score[1][1] - 361
        -- Convert to black-side score.
        if player == common.white then mmove.score = -mmove.score end
        print(string.format("Predicted Score = %.1f", mmove.score))
    else 
        mmove.has_score = common.FALSE
    end
    --
    -- Deal with tactical moves if there are any.
    -- Index i keeps the location where the next move is added to the list.
    local i = 0
    utils.dprint("Add tactical moves")
    local tactical_moves = dp.get_candidate_moves(util_pkg.def_policy, mboard.board)
    -- local tactical_moves = { }
    for l = 1, #tactical_moves do
        local x = tactical_moves[l][1]
        local y = tactical_moves[l][2]
        mmove.xs[i] = x
        mmove.ys[i] = y
        -- Make the confidence small but not zero.
        mmove.probs[i] = 0.01
        mmove.types[i] = move_tactical
        utils.dprint("   Move (%d, %d), move = %s, type = Tactical move", x, y, goutils.compose_move_gtp(x, y, tonumber(mmove.player)))
        i = i + 1
    end
    
    -- Check if each move is valid or not, if not, go to the next move.
    -- Index j is the next move to be read from CNN.
    utils.dprint("Add CNN moves")
    local j = 1
    while i < num_first_move and j <= common.board_size * common.board_size do
        local x, y = goutils.moveIdx2xy(sortInd[j])
        local check_res, comments = goutils.check_move(mboard.board, x, y, player)
        if check_res then
            -- The move is all right.
            mmove.xs[i] = x
            mmove.ys[i] = y
            mmove.probs[i] = sortProb[j] 
            if board.is_move_giving_simple_ko(mboard.board, x, y, player) then
               mmove.types[i] = move_simple_ko
            else
               mmove.types[i] = move_normal
            end
            utils.dprint("   Move (%d, %d), ind = %d, move = %s, conf = (%f), type = %s",
                x, y, sortInd[j], goutils.compose_move_gtp(x, y, tonumber(mmove.player)), sortProb[j], mmove.types[i] == move_simple_ko and "Simple KO move" or "Normal Move")

            i = i + 1
        else
            utils.dprint("   Skipped Move (%d, %d), ind = %d, move = %s, conf = (%f), Reason = %s", 
                x, y, sortInd[j], goutils.compose_move_gtp(x, y, tonumber(player)), sortProb[j], comments)
        end
        j = j + 1
    end
    -- Put zeros if there is not enough move.
    utils.dprint("Zero padding moves")
    while i < num_first_move do
        -- PASS with zero confidence.
        mmove.xs[i] = 0
        mmove.ys[i] = 0
        mmove.probs[i] = 0
        i = i + 1
    end
    -- Add extra features, for now just the location of stones.
    utils.dprint("Add extra features")
    local f = util_pkg.features[k] 
    if f ~= nil then
        -- Our stones: +1, opponent stones: -1 
        local sent_feature = f["our stones"] - f["opponent stones"]
        sent_feature = sent_feature:view(-1)
        for i = 1, common.board_size * common.board_size do
            mmove.extra[i - 1] = sent_feature[i]
        end
    end
    return mmove
end

function util_pkg.dprint(s)
    utils.dprint(s)
end

local gc_count = 0
local gc_interval = 50
function util_pkg.sparse_gc()
    gc_count = gc_count + 1
    if gc_count == gc_interval then
        collectgarbage()
        gc_count = 0
    end
end

function util_pkg.free()
    dp.free(util_pkg.def_policy)
end

return util_pkg
