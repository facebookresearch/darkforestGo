--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local pl = require 'pl.import_into'()
local utils = require('utils.utils')

utils.require_torch()
utils.require_cutorch()

local ffi = require 'ffi'
local utils = require('utils.utils')
local board = require('board.board')
local common = require("common.common")
local util_pkg = require 'common.util_package'

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "local_evaluator/cnn_local_exchanger.h"))
local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "cnn_local_exchanger.h"))
local C = ffi.load(paths.concat(script_path, "../libs/liblocalexchanger.so"))

local sig_ok = tonumber(symbols.SIG_OK)
local max_batch = opt_internal.async and 128 or 32 

-- number of attempt before wait_board gave up and return nil.
-- previously this number is indefinite, i.e., wait until there is a package (which might cause deadlock).
local num_attempt = 10

cutorch.setDevice(opt_internal.gpu)
local model_filename = common.codenames[opt_internal.codename].model_name
local feature_type = common.codenames[opt_internal.codename].feature_type
assert(model_filename, "opt.codename [" .. opt_internal.codename .. "] not found!")

if opt_internal.use_local_model then 
    model_filename = pl.path.basename(model_filename)
end
print("Loading model = " .. model_filename)
local model = torch.load(model_filename)
print("Loading complete")

-- Server side. 
local ex = C.ExLocalInit(opt_internal.pipe_path, opt_internal.gpu - 1, common.TRUE) 
print("CNN Exchanger initialized.")
print("Size of MBoard: " .. ffi.sizeof('MBoard'))
print("Size of MMove: " .. ffi.sizeof('MMove'))
board.print_info()

-- [board_idx, received time]
local block_ids = torch.DoubleTensor(max_batch) 
local sortProb = torch.FloatTensor(max_batch, common.board_size * common.board_size)
local sortInd = torch.FloatTensor(max_batch, common.board_size * common.board_size)

util_pkg.init(max_batch, feature_type)
-- util_pkg.dbg_set()

print("ready")
io.flush()

-- Preallocate the cuda tensors.
local probs_cuda, sortProb_cuda, sortInd_cuda

-- Feature for the batch.
local all_features
while true do
    -- Get data
    block_ids:zero() 
    if all_features then
        all_features:zero()
    end

    local num_valid = 0

    -- Start the cycle.
    -- local start = common.wallclock()
    for i = 1, max_batch do
        local mboard = util_pkg.boards[i - 1]
        -- require 'fb.debugger'.enter()
        local ret = C.ExLocalServerGetBoard(ex, mboard, num_attempt)
        -- require 'fb.debugger'.enter()
        if ret == sig_ok and mboard.seq ~= 0 and mboard.b ~= 0 then 
            local feature = util_pkg.extract_board_feature(i)
            if feature ~= nil then
                local nplane, h, w = unpack(feature:size():totable())
                if all_features == nil then
                    all_features = torch.CudaTensor(max_batch, nplane, h, w):zero()
                    probs_cuda = torch.CudaTensor(max_batch, h*w)
                    sortProb_cuda = torch.CudaTensor(max_batch, h*w)
                    sortInd_cuda = torch.CudaLongTensor(max_batch, h*w)
                end
                num_valid = num_valid + 1
                all_features[num_valid]:copy(feature)
                block_ids[num_valid] = i
            end
        end
    end
    -- print(string.format("Collect data = %f", common.wallclock() - start))
    -- Now all data are ready, run the model.
    if C.ExLocalServerIsRestarting(ex) == common.FALSE and all_features ~= nil and num_valid > 0 then 
        print(string.format("Valid sample = %d / %d", num_valid, max_batch)) 
        util_pkg.dprint("Start evaluation...")
        local start = common.wallclock()
        local output = model:forward(all_features:sub(1, num_valid))
        local territory
        util_pkg.dprint("End evaluation...")
        -- If the output is multitask, only take the first one.
        -- require 'fb.debugger'.enter()
        if type(output) == 'table' then 
            -- Territory
            -- require 'fb.debugger'.enter()
            if #output == 4 then
                territory = output[4]
            end
            output = output[1]
        end

        local probs_cuda_sel = probs_cuda:sub(1, num_valid)
        local sortProb_cuda_sel = sortProb_cuda:sub(1, num_valid)
        local sortInd_cuda_sel = sortInd_cuda:sub(1, num_valid)

        torch.exp(probs_cuda_sel, output:view(num_valid, -1))
        torch.sort(sortProb_cuda_sel, sortInd_cuda_sel, probs_cuda_sel, 2, true)
        -- local sortProb_cuda = torch.CudaTensor(num_valid, 19*19):fill(0.5)
        -- local sortInd_cuda = torch.CudaTensor(num_valid, 19*19):fill(23)
 
        sortProb:sub(1, num_valid):copy(sortProb_cuda_sel)
        sortInd:sub(1, num_valid):copy(sortInd_cuda_sel)

        local score
        if territory then
            -- Compute score, only if > 0.6 we regard it as black/white territory.
            -- score = territory:ge(0.6):sum(3):float()
            local diff = territory[{{}, {1}, {}}] - territory[{{}, {2}, {}}]
            score = diff:ge(0):sum(3)
            -- score = territory:sum(3):float()
        end
        -- sortProb:copy(sortProb_cuda[{{}, {1, num_first_move}}])
        -- sortInd:copy(sortInd_cuda[{{}, {1, num_first_move}}])
        print(string.format("Computation = %f", common.wallclock() - start))

        local start = common.wallclock()
        -- Send them back.
        for k = 1, num_valid do
            local mmove = util_pkg.prepare_move(block_ids[k], sortProb[k], sortInd[k], score and score[k]) 
            util_pkg.dprint("Actually send move")
            C.ExLocalServerSendMove(ex, mmove)
            util_pkg.dprint("After send move")
        end
        print(string.format("Send back = %f", common.wallclock() - start))

    end

    util_pkg.sparse_gc()

    -- Send control message if necessary. 
    if C.ExLocalServerSendAckIfNecessary(ex) == common.TRUE then
        print("Ack signal sent!")
    end
end

C.ExLocalDestroy(ex)
