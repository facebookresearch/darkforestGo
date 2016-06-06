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

local lib_path = '/mnt/vol/gfsai-cached-oregon/ai-group/users/yuandong/go/libSep6'
local symbols, s = utils.ffi_include(paths.concat(lib_path, "common/comm_pipe.h"))
local pipe = {}

local C = ffi.load(paths.concat(lib_path, "libcomm_pipe.so"))

function pipe.init(filename, id, is_create_new)
    is_create_new = is_create_new == true and 1 or 0
    local p = ffi.new("Pipe")
    -- C start from 0
    if is_create_new then print("Create pipe " .. filename .. " " .. tostring(id)) end
    C.PipeInit(filename, id - 1, is_create_new, p)
    return p
end

function pipe.send(p, m)
    return C.PipeWrite(p, m, ffi.sizeof(m)) == 0
end

function pipe.receive(p, m)
    return C.PipeRead(p, m, ffi.sizeof(m)) == 0
end

function pipe.close(p)
    C.PipeClose(p)
end

return pipe
