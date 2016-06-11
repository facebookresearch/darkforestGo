--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local common = {}
local ffi = require 'ffi'
local utils = require 'utils.utils'
require 'paths'

common.res_unknown = 0
common.empty = 0
common.black = 1
common.white = 2
common.board_size = 19

common.TRUE = 1
common.FALSE = 0

common.player_name = { [0] = 'U', [1] = 'B', [2] = 'W' }

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "common/common.h"))
function common.script_path()
   local str = debug.getinfo(2, "S").source:sub(2)
   return str:match("(.*/)") or "./"
end

local script_path = common.script_path()

-- Codename for models and their path
common.codenames = {
    darkforest = {
        model_name = paths.concat(script_path, "../models/df.bin"),
        feature_type = 'old'
    },
    darkfores1 = {
        model_name = paths.concat(script_path, "../models/df1.bin"),
        feature_type = 'extended'
    },
    darkfores2 = {
        model_name = paths.concat(script_path, "../models/df2.bin"), 
        feature_type = 'extended'
    },
    df2_cpu = {
        model_name = paths.concat(script_path, "../models/df2_cpu.bin"), 
        feature_type = 'extended'
    },
}

--
local symbols, s = utils.ffi_include(paths.concat(common.script_path(), "common.h"))
local C = ffi.load(paths.concat(script_path, "../libs/libcommon.so"))

function common.opponent(p) return 3 - p end
function common.wallclock() return C.wallclock() end

-- From move x, y (starting from 1) to Coord
-- #define OFFSETXY(x, y)  ( ((y) + BOARD_MARGIN) * MACRO_BOARD_EXPAND_SIZE + (x) + BOARD_MARGIN )
function common.xy2coord(x, y) 
    -- BOARD_MARGIN = 1
    -- BOARD_EXPAND_SIZE = 21
    return x + y * 21
end

-- From Coord to x, y (starting from 1)
function common.coord2xy(m) 
    -- BOARD_MARGIN = 1
    -- BOARD_EXPAND_SIZE = 21
    return m % 21, math.floor(m / 21)
end

return common
