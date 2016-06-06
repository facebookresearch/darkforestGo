--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

-- Default policy with pure DCNN
local common = require("common.common")
local board = require("board.board")
local dcnn_utils = require 'board.dcnn_utils'
local utils = require 'utils.utils'

local ffi = require 'ffi'
local symbols, s = utils.ffi_include(paths.concat(common.script_path(), "default_policy_common.h"))

local dp = { } 

-- Initialize with a given codename and rule
function dp.init(options)
    local opt = dcnn_utils.init(options)
    opt.rule = options.rule or board.chinese_rule
    return opt
end

function dp.run(def_policy, b, max_depth, verbose)
    -- keep sampling until we cannot make move anymore.
    local x, y
    local counter = 0
    local def_move = ffi.new("DefPolicyMove")
    while true do
        if max_depth > 0 and counter > max_depth then break end

        x, y = dcnn_utils.sample(def_policy, b, b._next_player)
        if x == nil or y == nil then break end
        def_move.m = common.xy2coord(x, y)

        board.play(b, x, y, b._next_player)
        counter = counter + 1
    end
    local score = board.get_fast_score(b, def_policy.rule)
    -- Also output the last move.
    return board.get_fast_score(b, def_policy.rule), def_move
end

return dp
