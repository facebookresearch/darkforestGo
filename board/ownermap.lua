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
local goutils = require('utils.goutils')
local board = require('board.board')

local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "ownermap.h"))
-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "board/ownermap.h"))
local C = ffi.load(paths.concat(script_path, "../libs/libownermap.so"))
-- local C = ffi.load("libexperimental_deeplearning_yuandong_go_board_default_policy_c.so")
local om = {}

-- S_DEAD = 8
-- om.dead_white = 8 + common.white
-- om.dead_black = 8 + common.black
-- om.dame_empty = 3
om.dead_white = tonumber(symbols.S_DEAD) + common.white
om.dead_black = tonumber(symbols.S_DEAD) + common.black
om.dame_empty = tonumber(symbols.S_DAME)

-- print("Dead white = " .. om.dead_white)
-- print("Dead white = " .. om.dead_black)
-- print("Dame empty = " .. om.dame_empty)

-- Utilities for Ownermap
function om.new()
    return C.InitOwnermap()
end

function om.free(ownermap)
    C.FreeOwnermap(ownermap)
end

function om.clear_ownermap(ownermap)
    C.ClearOwnermap(ownermap)
end

function om.accu_ownermap(ownermap, board_after_dp)
    C.AccuOwnermap(ownermap, board_after_dp);
end

function om.get_ownermap(ownermap, ratio)
    local ownermap = torch.CharTensor(common.board_size, common.board_size)
    C.GetOwnermap(ownermap, ratio, ownermap:data())
    return ownermap
end

function om.get_deadstones(ownermap, b, ratio)
    local livedead = torch.CharTensor(common.board_size, common.board_size)
    C.GetDeadStones(ownermap, b, ratio, livedead:data(), nil)
    return livedead
end

function om.get_deadlist(livedead)
    -- From livedead we can get back the location of deadstones
    local livedead2 = livedead:view(-1)
    local dead_whites = { }
    local dead_whites_str = { }
    local dead_blacks = { }
    local dead_blacks_str = { }
    for i = 1, common.board_size * common.board_size do
        local x, y = goutils.moveIdx2xy(i)
        local s = goutils.compose_move_gtp(x, y)

        if livedead2[i] == om.dead_white then
            table.insert(dead_whites, {x, y})
            table.insert(dead_whites_str, s)
        elseif livedead2[i] == om.dead_black then
            table.insert(dead_blacks, {x, y})
            table.insert(dead_blacks_str, s)
        end
    end
    return {
        b = dead_blacks,
        w = dead_whites,
        b_str = dead_blacks_str,
        w_str = dead_whites_str,
        dames = dames,
        dames_str = dames_str
    }
end

function om.get_territorylist(territory) 
    local territory2 = territory:view(-1)
    local whites = { }
    local whites_str = { } 
    local blacks = { }
    local blacks_str = { } 
    local dames = { }
    local dames_str = { }
    for i = 1, common.board_size * common.board_size do
        local x, y = goutils.moveIdx2xy(i)
        local s = goutils.compose_move_gtp(x, y)

        if territory2[i] == common.white then
            table.insert(whites, {x, y})
            table.insert(whites_str, s)
        elseif territory2[i] == common.black then
            table.insert(blacks, {x, y})
            table.insert(blacks_str, s)
        elseif territory2[i] == om.dame_empty then
            -- Dame
            table.insert(dames, {x, y})
            table.insert(dames_str, s)
        end
    end
    return {
        b = blacks,
        w = whites,
        b_str = blacks_str,
        w_str = whites_str,
        dames = dames,
        dames_str = dames_str
    }
end

function om.get_ownermap_float(ownermap_ptr, player)
    local ownermap = torch.FloatTensor(common.board_size, common.board_size)
    C.GetOwnermapFloat(ownermap_ptr, player, ownermap:data())
    return ownermap
end

function om.show_deadstones(b, stones)
    C.ShowDeadStones(b, stones:data())
end

function om.show_stones_prob(ownermap, player)
    C.ShowStonesProb(ownermap, player)
end

function om.get_ttscore_ownermap(ownermap, b)
    local livedead = torch.CharTensor(common.board_size, common.board_size)
    local territory = torch.CharTensor(common.board_size, common.board_size)
    local score = C.GetTTScoreOwnermap(ownermap, b, livedead:data(), territory:data())
    return score, livedead, territory
end

function om.print_list(t)
    local s = "" 
    for _, c in ipairs(t) do
        s = s .. c .. " "
    end
    print(s)
end

function om.util_compute_final_score(ownermap, b, komi, trial, def_policy_func)
     local new_ownermap
     if not ownermap then
         ownermap = om.new()
         new_ownermap = true
     end
     assert(ownermap)
     assert(def_policy_func)

     trial = trial or 1000
     komi = komi or 6.5

     om.clear_ownermap(ownermap)
     local scores = torch.Tensor(trial)
     for i = 1, trial do
         local b2 = board.copyfrom(b)
         scores[i] = def_policy_func(b2, -1) - komi
         om.accu_ownermap(ownermap, b2)
     end
     local score, livedead, territory = om.get_ttscore_ownermap(ownermap, b)
     
     --[[
     local territorylist =  om.get_territorylist(territory) 
     print(string.format("#W = %d, #B = %d, #dame = %d", 
                #territorylist.w, #territorylist.b, #territorylist.dames))
     
     print("White = ")
     print_list(territorylist.w_str)
     print("Black = ")
     print_list(territorylist.b_str)
     print("Dame = ")
     print_list(territorylist.dames_str)
 
     print("Score before komi = " .. score)
     ]]

     if new_ownermap then
         om.free(ownermap)
     end
     return score - komi, livedead, territory, scores
end

-- Some utilities for dp
function om.compute_stats(scores)
    if scores == nil then
        return { 
            std = 0,
            max = 0,
            min = 0,
            mean = 0,
            advantage = '-'
        }
    else
        if type(scores) == 'table' then
            scores = torch.FloatTensor(scores)
        end
        return {
            mean = scores:mean(),
            std = scores:std(),
            max = scores:max(),
            min = scores:min(),
            advantage = scores:mean() > 0 and 'B' or 'W'
        }
    end
end

return om
