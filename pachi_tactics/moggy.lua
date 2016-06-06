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

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "pachi_tactics/moggy.h"))
local script_path = common.script_path() 
local symbols, s = utils.ffi_include(paths.concat(script_path, "moggy.h"))
local C = ffi.load(paths.concat(script_path, "../libs/libmoggy.so"))
local dp = {}

-- Lua interface for moggy playout.
function dp.new(rule) 
    dp.rule = rule or board.chinese_rule
    return C.playout_moggy_init(nil)
end

function dp.free(def_policy)
    C.playout_moggy_destroy(def_policy)
end

function dp.run(def_policy, b, max_depth, verbose)
    -- Current
    local m = C.play_random_game(def_policy, nil, nil, b, nil, max_depth, verbose and common.TRUE or common.FALSE);
    return board.get_fast_score(b, dp.rule), m
end

return dp
