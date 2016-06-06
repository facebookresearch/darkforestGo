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

local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "comm.h"))
local comm = {}

local C = ffi.load(paths.concat(script_path, "../libs/libcomm.so"))

function comm.init(id, is_create_new)
    is_create_new = is_create_new == true and 1 or 0
    return C.CommInit(id, is_create_new)
end

function comm.send(channel_id, m)
    C.CommSend(channel_id, m, ffi.sizeof(m))
end

function comm.send_no_block(channel_id, m)
    return C.CommSendNoBlock(channel_id, m, ffi.sizeof(m)) == 0
end

function comm.receive(channel_id, m)
    C.CommReceive(channel_id, m, ffi.sizeof(m))
end

function comm.receive_no_block(channel_id, m)
    return C.CommReceiveNoBlock(channel_id, m, ffi.sizeof(m)) == 0
end

function comm.destroy(channel_id)
    C.CommDestroy(channel_id)
end

return comm
