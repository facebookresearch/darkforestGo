--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

require 'torch'
require 'cutorch'
require 'nn'
require 'cunn'
require 'cudnn'
require 'nngraph'

require 'xlua'

local framework = require 'train.rl_framework.infra.framework'
local rl = require 'train.rl_framework.infra.env'
local pl = require 'pl.import_into'()

require 'train.rl_framework.infra.bundle'
require 'train.rl_framework.infra.agent'

local tnt = require 'torchnet'

-- cutorch.setDevice(3)

-- Build simple models.
function build_policy_model(opt)
    local network_maker = require('train.rl_framework.examples.go.models.' .. opt.model_name)
    local network, crit, outputdim, monitor_list = network_maker({1, 25, 19, 19}, opt)
    return network:cuda(), crit:cuda()
end

local opt = pl.lapp[[
    --actor          (default "policy")
    --sampling       (default "replay")
    --optim          (default "supervised")
    --loss           (default 'policy')
    --alpha          (default 0.1)
    --nthread        (default 8)
    --batchsize      (default 256)
    --num_forward_models  (default 4096)       Number of forward models.
    --progress                                 Whether to print the progress
    --epoch_size          (default 12800)      Epoch size
    --epoch_size_test     (default 128000)      Epoch size for test.
    --data_augmentation                        Whether to use data_augmentation

    --nGPU                (default 1)          Number of GPUs to use.
    --nstep               (default 3)          Number of steps.
    --model_name          (default 'model-12-parallel-384-n-output-bn')
    --datasource          (default 'kgs')
    --feature_type        (default 'extended')
]]

opt.userank = true
opt.intermediate_step = opt.epoch_size / opt.batchsize / 10
print(pl.pretty.write(opt))

local model, crits = build_policy_model(opt)

local bundle = rl.Bundle{
    models = {
        policy = model,
    },
    crits = crits
}

local agent = rl.Agent{
    bundle = bundle,
    opt = opt
}

local stats = {
    sgf_idx = { },
    board_freq = torch.FloatTensor(19, 19):zero(),
    ply = { },
    count = 0
}

local callbacks = {
    thread_init = function()
       require 'train.rl_framework.examples.go.ParallelCriterion2'
    end,
    forward_model_init = function(partition)
        local tnt = require 'torchnet'
        return tnt.IndexedDataset{
            fields = { opt.datasource .. "_" .. partition },
            path = './dataset'
        }
    end,
    forward_model_generator = function(dataset, partition)
        local fm_go = require 'train.rl_framework.examples.go.fm_go'
        return fm_go.FMGo(dataset, partition, opt)
    end,
    onSample = function(state)
        -- Compute the stats.
        --[[
        if state.signature == 'train' then return end
        for i = 1, state.sample.sgf_idx:size(1) do
            local idx = state.sample.sgf_idx[i]
            if stats.sgf_idx[idx] == nil then stats.sgf_idx[idx] = 0 end
            stats.sgf_idx[idx] = stats.sgf_idx[idx] + 1

            local xy = state.sample.xy[i]
            local x = xy[1]
            local y = xy[2]

            stats.board_freq[x][y] = stats.board_freq[x][y] + 1
            stats.count = stats.count + 1

            local ply = state.sample.ply[i]
            if stats.ply[ply] == nil then stats.ply[ply] = 0 end
            stats.ply[ply] = stats.ply[ply] + 1
        end

        if stats.count % (2000 * opt.batchsize) == 0 then
            print(stats.board_freq:clone():mul(1.0 / stats.count))
            require 'fb.debugger'.enter()
        end
        ]]
    end,
    --[[
    onStartEpoch = function()
        print("In onStartEpoch")
    end,
    onStart = function()
        print("In onStart")
    end,
    onSample = function()
        print("In onSample")
    end,
    onUpdate = function()
        print("In onUpdate")
    end,
    onEndEpoch = function()
        print("In onEndEpoch")
    end
    ]]
}

-- callbacks:
--    forward_model_generator
--    checkpoint_filename(state, err): Get checkpoint filename
--    tune_lr(state): tune the learning rate
--    print(log, state): print the current state
--      (All the remaining functions take state as input)
--    onStartEpoch
--    onStart
--    onSample
--    onUpdate
-- For now just shortcut the trainloss/testloss.
framework.run_rl(agent, callbacks, opt)
