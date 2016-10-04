--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local tnt = require 'torchnet'
require 'xlua'

local rl = require 'train.rl_framework.infra.env'
local nnutils = require 'utils.nnutils'
require 'train.rl_framework.infra.engine'
require 'train.rl_framework.infra.bundle'
require 'train.rl_framework.infra.dataset'

local Threads = require 'threads'
Threads.serialization('threads.sharedserialize')

local framework = { }

local load_closure = function(thread_idx, partition, epoch_size, fm_init, fm_generator, fm_postprocess, bundle, opt)
    local tnt = require 'torchnet'
    local rl = require 'train.rl_framework.infra.env'
    -- It is by default a batchdataset.
    return rl.Dataset{
        forward_model_init = fm_init,
        forward_model_generator = fm_generator,
        forward_model_batch_postprocess = fm_postprocess,
        batchsize = opt.batchsize,
        thread_idx = thread_idx,
        partition = partition,
        bundle = bundle,
        epoch_size = epoch_size,
        opt = opt
    }
end

local function build_dataset(thread_init, fm_init, fm_gen, fm_postprocess, bundle, partition, epoch_size, opt)
    local dataset
    if opt.nthread > 0 then
        dataset = tnt.ParallelDatasetIterator{
            nthread = opt.nthread,
            init = function()
                require 'cutorch'
                require 'torchnet'
                require 'cudnn'
                require 'train.rl_framework.infra.env'
                require 'train.rl_framework.infra.dataset'
                require 'train.rl_framework.infra.bundle'
                if opt.gpu and opt.nGPU == 1 then
                    cutorch.setDevice(opt.gpu)
                end
                if thread_init ~= nil then thread_init() end
            end,
            closure = function(thread_idx)
                return load_closure(thread_idx, partition, epoch_size, fm_init, fm_gen, fm_postprocess, bundle, opt)
            end
        }
    else
        dataset = tnt.DatasetIterator{
            dataset = load_closure(1, partition, epoch_size, fm_init, fm_gen, fm_postprocess, bundle, opt)
        }
    end
    return dataset
end

local function compute_aver_loss(state)
    local aver_train_errs = { }
    local err_str = ""
    for k, e in pairs(state.errs) do
        aver_train_errs[k] = e:sum(1) / state.errs_count / e:size(1)
        err_str = err_str .. string.format("[%s]: %5.6f ", k, aver_train_errs[k][1])
    end
    return aver_train_errs, err_str
end

-- New framework for torchnet
-- opt:
--    nthread:   the number of thread used.
--    batchsize: sample size of batch
--    epoch_size: sample size of epoch.
--    debug:     whether we are in debug mode.
--    progress:  whether we show the progress bar.
--    maxepoch:
--    lr:
--    train_name
--    test_name
--
-- callbacks:
--    forward_model_init
--    forward_model_generator
--    forward_model_postprocess
--    checkpoint_filename(state, err): Get checkpoint filename
--    tune_lr(state): tune the learning rate
--    print(log, state): print the current state
--      (All the remaining functions take state as input)
--    onStartEpoch
--    onStart
--    onSample
--    onUpdate
--    onEndEpoch
--
--    onAdditionalTest
-- For now just shortcut the trainloss/testloss.
local trainloss = {
    add = function(self) end,
    value = function(self) return 0.0 end,
    reset = function(self) end
}

local testloss = {
    add = function(self) end,
    value = function(self) return 0.0 end,
    reset = function(self) end
}

function framework.run_rl(agent, callbacks, opt)
    if not callbacks then error("Callbacks cannot be nil") end
    if opt.nthread == nil then error("opt.nthread cannot be nil") end
    if not opt.batchsize then error("opt.batchsize cannot be nil") end

    opt.minlr = opt.minlr or 1e-5
    local thread_init = callbacks.thread_init
    local fm_init = callbacks.forward_model_init
    local fm_gen = callbacks.forward_model_generator
    local fm_postprocess = callbacks.forward_model_batch_postprocess

    -- require 'fb.debugger'.enter()
    print("fm_init: " .. tostring(fm_init))
    print("fm_gen: " .. tostring(fm_gen))
    print("fm_postprocess: " .. tostring(fm_postprocess))

    local bundle = agent:get_bundle()
    local batchsize = opt.batchsize

    -- require 'fb.debugger'.enter()

    -- training/test set
    local train_dataset = build_dataset(thread_init, fm_init, fm_gen, fm_postprocess, bundle, "train", opt.epoch_size, opt)
    local test_dataset = build_dataset(thread_init, fm_init, fm_gen, fm_postprocess, bundle, "test", opt.epoch_size_test, opt)

    if not callbacks.checkpoint_filename then
        callbacks.checkpoint_filename = function(state, err)
            local gpu = cutorch.getDevice()
            return string.format("model-g%d_iter%d_%f.bin", gpu, state.epoch, err)
        end
    end

    if not callbacks.tune_lr then
        local lr_acc = 0
        callbacks.tune_lr = function(state)
            if state.model_saved then
                lr_acc = 0
            elseif state.epoch >= 10 then
                lr_acc = lr_acc + 1
                if lr_acc > 20 then
                    agent:reduce_lr(1.2)
                    print(string.format("Reduce learning rate, now is %f", agent:get_lr()))
                    lr_acc = 0
                end
            end
            -- require 'fb.debugger'.enter()
            -- Early stopping.
            if opt.minlr and agent:get_lr() <= opt.minlr then state.epoch = state.maxepoch end
        end
    end

    if not callbacks.print then
        callbacks.print = function(timer, state, train_err_str, test_err_str)
            local t_str = os.date("%c", os.time())
            print(string.format('| %s | epoch %04d | ms/batch %3d | train %s | test %s | saved %s',
                      t_str, state.epoch, timer:value()*1000, train_err_str, test_err_str, state.model_saved and "*" or ""))
            io.flush()
        end
    end

    if not callbacks.print_intermediate then
        callbacks.print_intermediate = function(state, train_err_str)
            local t_str = os.date("%c", os.time())
            print(string.format('| %s | %d | train %s', t_str, state.t, train_err_str))
            io.flush()
        end
    end

    -- Initialize log
    --[[
    local log = tnt.Logger{
        filename = string.format('%s/log', rundir)
    }
    ]]

    -- time one iteration
    local timer = tnt.TimeMeter{
        unit = true
    }

    local memongpu = { }
    -- trainer
    local engine = rl.Engine()

    -- Start the adagrad initialiazation.
    function engine.hooks.onStart(state)
        state.signature = "train"
        if callbacks.onStart then callbacks.onStart(state) end
    end

    function engine.hooks.onStartEpoch(state)
        trainloss:reset()
        timer:reset()

        timer:resume()
        collectgarbage()

        if callbacks.onStartEpoch then callbacks.onStartEpoch(state) end
    end

    function engine.hooks.onSample(state)
        nnutils.send2gpu(state.sample, memongpu)
        if callbacks.onSample then callbacks.onSample(state) end
        if opt.debug then
            require 'fb.debugger'.enter()
        end
    end

    function engine.hooks.onUpdate(state)
        -- require 'fb.debugger'.enter()
        trainloss:add(state)
        timer:incUnit()

        if callbacks.onUpdate then callbacks.onUpdate(state) end

        if opt.progress then
            xlua.progress(state.t, opt.epoch_size / opt.batchsize)
        end
        if opt.intermediate_step and callbacks.print_intermediate and state.t % opt.intermediate_step == 0 then
            local train_aver_loss, train_err_str = compute_aver_loss(state)
            callbacks.print_intermediate(state, train_err_str)
        end
    end

    -- test loop
    local function testeval(agent, iterator)
        local testengine = rl.Engine()

        function testengine.hooks.onStart(state)
            state.signature = "test"
        end

        testengine.hooks.onSample = engine.hooks.onSample

        function testengine.hooks.onForward(state)
            testloss:add(state)
            if opt.progress then
                xlua.progress(state.t, opt.epoch_size_test / opt.batchsize)
            end
            collectgarbage()
        end

        return testengine:test{
            agent = agent,
            iterator = iterator
        }
    end

    local minerr = math.huge

    function engine.hooks.onEndEpoch(state)
        timer:stop()
        if callbacks.onEndEpoch then callbacks.onEndEpoch(state) end

        -- test
        print("Start testing...")
        local test_state = testeval(agent, test_dataset)
        local test_aver_loss, test_err_str = compute_aver_loss(test_state)
        -- local test_aver_loss = { q = { 0.1 } }
        -- local test_err_str = "0.1"
        local train_aver_loss, train_err_str = compute_aver_loss(state)

        state.saved_filename = nil

        local loss_name = opt.loss

        -- Some hack here.
        if test_aver_loss[loss_name][1] < minerr then
            minerr = test_aver_loss[loss_name][1]
            state.model_saved = true
            state.saved_filename = callbacks.checkpoint_filename(state, minerr)
            assert(state.saved_filename)
        end
        if state.saved_filename then
            state.agent:get_bundle():clear_state()
            torch.save(state.saved_filename, { bundle = state.agent:get_bundle(), opt = opt })
        end

        callbacks.tune_lr(state)
        callbacks.print(timer, state, train_err_str, test_err_str)
        if callbacks.onAdditionalTest then callbacks.onAdditionalTest(state) end
        state.model_saved = false
   end

   engine:train{
       agent = agent,
       iterator = train_dataset,
       opt = opt,
   }
end

return framework
