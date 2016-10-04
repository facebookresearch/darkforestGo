--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local tnt = require 'torchnet.env'
local argcheck = require 'argcheck'
local doc = require 'argcheck.doc'

doc[[

### rl.Engine

The Engine module implements the training procedure for reinforcement learning.

procedure in `train`, including data sampling, forward prop, back prop, and
parameter updates. It also operates as a coroutine allowing a user control
 (i.e. increment some sort of `tnt.Meter`) at events such as 'start',
'start-epoch', 'forward', 'forward-criterion', 'backward', etc.

Accordingly, `train` requires a network (nn.Module), a criterion expressing the
loss function (nn.Criterion), a dataset iterator (tnt.DatasetIterator), and a
learning rate, at the minimum. The `test` function allows for simple evaluation
of a model on a dataset.

A `state` is maintained for external access to outputs and parameters of modules
as well as sampled data.
]]

require 'nn'

local rl = require 'train.rl_framework.infra.env'
local RLEngine, Engine = torch.class('rl.Engine', 'tnt.Engine', rl)

RLEngine.__init = argcheck{
   {name="self", type="rl.Engine"},
   call =
      function(self)
         Engine.__init(self, {
            "onStart", "onStartEpoch", "onSample", "onForward",
            "onEndEpoch", "onUpdate", "onEnd"
         })
      end
}

local function clear_errs(state)
    state.errs = nil
    state.errs_count = nil
end

local function accumulate_errs(state, errs)
    if not state.errs then
        state.errs = { }
        state.errs_count = 0
    end
    -- require 'fb.debugger'.enter()
    for k, e in pairs(errs) do
        if type(e) == 'number' then
            e = torch.FloatTensor({e})
        end

        if state.errs[k] == nil then
            state.errs[k] = e
        else
            state.errs[k]:add(e)
        end
    end
    state.errs_count = state.errs_count + 1
end

RLEngine.train = argcheck{
   {name="self", type="rl.Engine"},
   {name="agent", type="rl.Agent"},
   {name="iterator", type="tnt.DatasetIterator"},
   {name="opt", type="table"},
   call =
      function(self, agent, iterator, opt)
         -- assert(opt.lr, "Learning rate has to be set")
         local state = {
            agent = agent,
            iterator = iterator,
            maxepoch = opt.maxepoch or 1000,
            sample = {},
            epoch = 0, -- epoch done so far
            lr = opt.lr,
            training = true,
         }

         local function update_sampling_before()
             -- If the sampler is multiple threads, we need to call synchronize() to make sure
             -- when the sampling model is being updated, all the samplers are not using it.
             if iterator.__threads then
                 iterator.__threads:synchronize()
             end
         end

         self.hooks("onStart", state)
         while state.epoch < state.maxepoch do
            state.agent:training()
            clear_errs(state)
            state.t = 0
            self.hooks("onStartEpoch", state)

            for sample in state.iterator() do
               state.sample = sample
               self.hooks("onSample", state)

               -- This includes forward/backward and parameter update.
               -- Different RL will use different approaches.
               local errs = state.agent:optimize(sample)
               accumulate_errs(state, errs)

               state.t = state.t + 1
               self.hooks("onUpdate", state)
            end

            -- Update the sampling model.
            -- state.agent:update_sampling_model(update_sampling_before)
            state.agent:update_sampling_model()

            state.epoch = state.epoch + 1
            self.hooks("onEndEpoch", state)
         end
         self.hooks("onEnd", state)
         return state
      end
}

RLEngine.test = argcheck{
   {name="self", type="rl.Engine"},
   {name="agent", type="rl.Agent"},
   {name="iterator", type="tnt.DatasetIterator"},
   call = function(self, agent, iterator)
      local state = {
          agent = agent,
          iterator = iterator,
          sample = {},
          t = 0, -- samples seen so far
          training = false
      }

      self.hooks("onStart", state)
      state.agent:evaluate()
      for sample in state.iterator() do
         state.sample = sample
         self.hooks("onSample", state)
         local errs = state.agent:optimize(sample)
         accumulate_errs(state, errs)

         state.t = state.t + 1
         self.hooks("onForward", state)
      end
      self.hooks("onEnd", state)
      return state
   end
}
