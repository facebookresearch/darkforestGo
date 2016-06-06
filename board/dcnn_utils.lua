--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local utils
local common = require("common.common")
local board = require("board.board")
local goutils = require 'utils.goutils'
local utils = require 'utils.utils'
local pl = require 'pl.import_into'()

local dcnn_utils = { }

-- Parameters to use
-- medatory:
--     shuffle_top_n           Use topn for sampling, if topn == 1, then we only output the best legal move.
--     codename                codename for the model
--          input, feature_type: for customized model, omitted if codename is specified.
--     presample_codename:     codename for presample model.
--     temperature:            Temperature for presample model (default 1)
--     sample_step:            Since which ply we start to use normal model, default is -1
-- optional:
--     usecpu            whether to use cpu for evaluation.
--     use_local_model   whether we load a local .bin file.
function dcnn_utils.init(options)
    -- opt.feature_type and opt.userank are necessary for the game to be played.
    local opt = pl.tablex.deepcopy(options)
    opt.sample_step = opt.sample_step or -1
    opt.temperature = opt.temperature or 1
    opt.shuffle_top_n = opt.shuffle_top_n or 1
    opt.rank = opt.rank or '9d'

    if opt.usecpu == nil or opt.usecpu == false then
        utils = require 'utils.utils'
        utils.require_cutorch()
    else
        g_nnutils_only_cpu = true
        utils = require 'utils.utils'
    end

    opt.userank = true
    assert(opt.shuffle_top_n >= 1)

    -- print("Loading model = " .. opt.input)
    opt.input = (opt.codename == "" and opt.input or common.codenames[opt.codename].model_name)
    opt.feature_type = (opt.codename == "" and opt.feature_type or common.codenames[opt.codename].feature_type)
    opt.attention = { 1, 1, common.board_size, common.board_size }

    local model_name = opt.use_local_model and pl.path.basename(opt.input) or opt.input
    if opt.verbose then print("Load model " .. model_name) end
    local model = torch.load(model_name)
    if opt.verbose then print("Load model complete") end

    local preSampleModel
    local preSampleOpt = pl.tablex.deepcopy(opt)

    if opt.temperature > 1 then
        if opt.verbose then print("temperature: " , opt.temperature) end
        preSampleModel = goutils.getDistillModel(model, opt.temperature)
    elseif opt.presample_codename ~= nil and opt.presample_codename ~= false then
        local code = common.codenames[opt.presample_codename]
        if opt.verbose then print("Load preSampleModel " .. code.model_name) end
        preSampleModel = torch.load(code.model_name)
        preSampleOpt.feature_type = code.feature_type
    else
        preSampleModel = model
    end

    opt.preSampleModel = preSampleModel
    opt.preSampleOpt = preSampleOpt
    opt.model = model
    if opt.valueModel and opt.valueModel ~= "" then opt.valueModel = torch.load(opt.valueModel) end 
    if opt.verbose then print("dcnn ready!") end

    return opt
end

function dcnn_utils.dbg_set()
    utils.dbg_set()
end

function dcnn_utils.play(opt, b, player)
    -- It will return sortProb, sortInd, value, output
    return goutils.play_with_cnn(b, player, opt, opt.rank, opt.model)
end

function dcnn_utils.batch_play(opt, bs)
    return goutils.batch_play_with_cnn(bs, opt, opt.rank, opt.model)
end

function dcnn_utils.sample(opt, b, player)
    local sortProb, sortInd, value
    if b._ply > opt.sample_step then -- after sample, sample from normal model
        if opt.debug then print("normal model") end
        sortProb, sortInd = goutils.play_with_cnn(b, player, opt, opt.rank, opt.model)
    elseif b._ply < opt.sample_step then  -- before sample the move, encouraging more diverse moves
        if opt.debug then print("presample model") end
        sortProb, sortInd = goutils.play_with_cnn(b, player, opt.preSampleOpt, opt.preSampleOpt.rank, opt.preSampleModel)
    else
        if opt.debug then print("uniform sample") end
        sortProb, sortInd = goutils.randomPlay(b, player, opt, opt.rank, opt.model)
    end
    if opt.debug then 
        print("ply: ", b._ply)
        local j = 1
        for k = 1, 20 do
            local x, y = goutils.moveIdx2xy(sortInd[k][j])
            local check_res, comments = goutils.check_move(b, x, y, player) 
            if check_res then
                -- The move is all right.
                utils.dprint("   Move (%d, %d), ind = %d, move = %s, conf = (%f)", 
                   x, y, sortInd[k][j], goutils.compose_move_gtp(x, y, tonumber(player)), sortProb[k][j])
            else
                utils.dprint("   Skipped Move (%d, %d), ind = %d, move = %s, conf = (%f), Reason = %s", 
                   x, y, sortInd[k][j], goutils.compose_move_gtp(x, y, tonumber(player)), sortProb[k][j], comments)
            end
        end
    end
    
    if opt.valueModel and value then
        print("current value: " .. string.format("%.3f", value))
    end

    -- Apply the moves until we have seen a valid one.
    if opt.shuffle_top_n == 1 then
        local xf, yf, idx =  goutils.tryplay_candidates(b, player, sortProb, sortInd)
        return xf, yf
    else
        local xf, yf, idx = goutils.tryplay_candidates_sample(b, player, sortProb, sortInd, opt.shuffle_top_n)
        return xf, yf
    end
end

function dcnn_utils.get_value(opt, b, player)
    return goutils.get_value(b, player, opt)
end
return dcnn_utils
