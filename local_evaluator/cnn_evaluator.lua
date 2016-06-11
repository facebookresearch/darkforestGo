--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

package.path = package.path .. ';../?.lua'

local pl = require 'pl.import_into'()
local utils = require('utils.utils')

utils.require_torch()
utils.require_cutorch()

local ffi = require 'ffi'
local utils = require('utils.utils')
local common = require("common.common")
local threads = require 'threads'
threads.serialization('threads.sharedserialize')

--local ctrl_restart = tonumber(symbols.CTRL_RESTART)
--local ctrl_remove = tonumber(symbols.CTRL_REMOVE)
--local ex = ffi.new("Exchanger")
--C.Init(ex, true, true) 
local opt = pl.lapp[[
  -g,--gpu  (default 1)                      GPU id to use.
  --async                                    Make it asynchronized.
  --pipe_path (default "./")                 Path for pipe file. Default is in the current directory, i.e., go/mcts
  --codename  (default "darkfores2")         Code name for the model to load.
  --use_local_model                          If true, load the local model. 
]]

print("GPU used: " .. opt.gpu)
opt_internal = opt

-- Start 4 GPUs.
-- pool = threads.Threads(#gpus, function () end, function (idx) gpu = tonumber(gpus[idx]) end)
-- for i = 1, #gpus do
--    pool:addjob(function () paths.dofile("cnn_evaluator_run1.lua") end)
-- end
-- pool:synchronize()
paths.dofile("cnn_evaluator_run1.lua")


