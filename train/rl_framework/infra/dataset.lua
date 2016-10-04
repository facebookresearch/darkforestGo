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

local pl = require 'pl.import_into'()
local rl = require 'train.rl_framework.infra.env'
local nnutils = require 'utils.nnutils'

doc[[

### rl.Dataset

The Dataset module implements the dataset that could automatically generate contents for training.

For opt we have:
   max_sample_len: When will we get new samples from the model.
      1: get a new sample every time :get is called.

   sampling:
      max: Find the most probable action and return it. If epsilon is > 0, use e-greedy approach.
      random: Sampling according to the distribution.
      uniform: uniform sampling (no deep network forward involved)

   actor:
      q: use q-function as the action
      policy: use policy function as the action
]]

require 'nn'

local Dataset = torch.class('rl.Dataset', 'tnt.Dataset', rl)

-- For all the auxillary function, the first dimension is always the batch size.
local function sampling_max(action_values, valid_actions, T, eps)
    -- require 'fb.debugger'.enter()
    eps = eps or 0

    local action_values2 = action_values:clone()

    -- require 'fb.debugger'.enter()
    action_values2:map(valid_actions, function(av, valid) return valid == 0 and -math.huge or av end)
    local max_values, maxIndices = action_values2:max(2)
    -- require 'fb.debugger'.enter()
    if eps > 0 then
        for i = 1, action_values2:size(1) do
            if math.random() < eps then
                local candidates = { }
                for j = 1, valid_actions:size(2) do
                    if valid_actions[i][j] == 1 then table.insert(candidates, j) end
                end
                maxIndices[i] = candidates[math.random(#candidates)]
                break
            end
        end
    end
    return maxIndices
end

local function sampling_random(action_values, valid_actions, T)
    local action_values2 = action_values:clone()
    action_values2:map(valid_actions, function(av, valid) return valid == 0 and -math.huge or av end)

    action_values2:mul(1 / T):exp()
    local actions = torch.multinomial(action_values2, 1)
    return actions
end

local function sampling_uniform(actions)
    local idx = math.random(#actions)
    return actions[idx]
end

-- Return (pseudo)-loglikelihood.
local function actor_q(bundle, state_rep)
    -- use Q-function.
    -- require 'fb.debugger'.enter()
    return bundle:forward(state_rep, { q=true }).q
end

local function actor_policy(bundle, state_rep)
    return bundle:forward(state_rep, { policy=true }).policy
end

--------------- Make them a table ----------------
Dataset.actors = {
    q = actor_q,
    policy = actor_policy
}

Dataset.samplings = {
    max = sampling_max,
    random = sampling_random,
    uniform = sampling_uniform
}

Dataset.__init = argcheck{
    {name="self", type="rl.Dataset"},
    {name='forward_model_generator', type="function"},
    {name='batchsize', type='number'},
    {name='partition', type='string'},
    {name='epoch_size', type='number'},
    {name='thread_idx', type='number', default=1, opt=true},
    {name='forward_model_init', type="function", default=nil, opt=true},
    {name='forward_model_batch_postprocess', type="function", default=nil, opt=true},
    {name='bundle', type='rl.Bundle', default=nil, opt=true},
    {name="opt", type="table", default=nil, opt=true},
    call = function(self, forward_model_generator, batchsize, partition, epoch_size, thread_idx, forward_model_init, forward_model_batch_postprocess, bundle, opt)
       -- Create a bunch of forward models, each focus on one thread.
       if forward_model_init then
           print("rl.Dataset.__init(): forward_model_init is set, run it")
           self.forward_model_context = forward_model_init(partition)
       end

       local num_fm = opt.num_forward_models
       num_fm = (num_fm and num_fm >= batchsize) and num_fm or batchsize
       print(string.format("rl.Dataset.__init(): #forward model = %d, batchsize = %d", num_fm, batchsize))

       -- print("Build forward models")
       --
       math.randomseed(thread_idx)
       self.forward_models = { }
       for i = 1, num_fm do
           self.forward_models[i] = forward_model_generator(self.forward_model_context, partition)
           if i % 100 == 0 then
               collectgarbage()
               collectgarbage()
           end
           assert(torch.isTypeOf(self.forward_models[i], 'rl.ForwardModel'),
                  'The generated forward_model is not a subtype of rl.ForwardModel')
       end
       -- print("Duplicate bundle")

       if bundle ~= nil then
           self.bundle = bundle:clone('weight', 'bias')
       end
       if opt then
           self.opt = pl.tablex.deepcopy(opt)
       else
           self.opt = { }
       end

       -- print("Initialize actor/sampling...")
       self.actor = rl.func_lookup(self.opt.actor, Dataset.actors)
       self.sampling = rl.func_lookup(self.opt.sampling, Dataset.samplings)
       self.max_sample_len = self.opt.max_sample_len or 1

       if self.opt.sampling ~= "uniform" then
           assert(self.bundle, string.format("When sampling is not uniform, self.bundle cannot be nil. self.opt.actor = %s", self.opt.actor))
           assert(self.actor, string.format("When sampling is not uniform, self.actor cannot be nil! self.opt.actor = %s", self.opt.actor))
        end

       self.batchsize = batchsize
       self.thread_idx = thread_idx
       self.epoch_size = epoch_size
       -- Simple hack
       self.curr_sample_indices = torch.Tensor(#self.forward_models):fill(self.max_sample_len)
       self.forward_model_batch_postprocess = forward_model_batch_postprocess

       self.makebatch = nnutils.torchnet_makebatch()
       self.tocuda = nnutils.torchnet_tocuda()
       self.tofloat = nnutils.torchnet_tofloat()

       -- self.counter = 1
       -- print("Done Dataset initialization ...")
    end
}

Dataset.sample_batch = argcheck{
    {name='self', type='rl.Dataset'},
    call = function(self)
        -- sample a batch from the forward models.
        local indices = torch.randperm(#self.forward_models)
        local selected = { }
        local selected_indices = { }
        for i = 1, self.batchsize do
            local idx = indices[i]
            local curr_model = self.forward_models[idx]

            if self.curr_sample_indices[idx] >= self.max_sample_len then
                self.curr_sample_indices[idx] = 1
                self.forward_models[idx]:reset()
            else
                self.curr_sample_indices[idx] = self.curr_sample_indices[idx] + 1
            end
            table.insert(selected, self.forward_models[idx])
            table.insert(selected_indices, idx)
        end
        return selected, selected_indices
    end
}

Dataset.batch_action = argcheck{
    {name='self', type='rl.Dataset'},
    {name='selected', type='table'},
    call = function(self, selected)
        local action
        if self.opt.sampling == 'uniform' then
            action = torch.FloatTensor(self.batchsize, 1)
            for i = 1, self.batchsize do
                local all_actions = selected[i]:get_actions()
                action[i][1] = sampling_uniform(all_actions)
            end
        elseif self.opt.sampling == 'replay' then
            -- No action is needed.
            action = torch.FloatTensor(self.batchsize, 1):zero()
        else
            -- require 'fb.debugger'.enter()
            -- Prepare for the batch for forwarding.
            local s_reps = { }
            local valid_actions = { }
            for i = 1, self.batchsize do
                table.insert(s_reps, selected[i]:get_curr_state_rep())
                valid_actions[i] = selected[i]:get_actions()
            end
            s_reps = self.makebatch(s_reps)
            s_reps = self.tocuda(s_reps)

            if self.forward_model_batch_postprocess then
                s_reps = self.forward_model_batch_postprocess(s_reps)
            end

            local action_values = self.actor(self.bundle, s_reps):float()
            local valid_action_mask = action_values:clone():zero()

            for i = 1, self.batchsize do
                for j = 1, #valid_actions[i] do
                    local valid_idx = valid_actions[i][j]
                    valid_action_mask[i][valid_idx] = 1
                end
            end
            action = self.sampling(action_values, valid_action_mask, self.opt.T, self.opt.eps)
        end
        return action
    end
}

Dataset.batch_forward = argcheck{
    {name="self", type="rl.Dataset"},
    {name="selected", type="table"},
    {name="action", type="torch.*Tensor"},
    call = function(self, selected, action)
        -- Then we run forward and get the data.
        local res = { }
        for i = 1, self.batchsize do
            local this_res = selected[i]:forward(action[i][1])
            table.insert(res, this_res)
            -- If we arrived the terminal state, reset the forward model.
            if this_res.nonterminal == 0 then
                selected[i]:reset()
            end
        end
        res = self.makebatch(res)
        -- require 'fb.debugger'.enter()
        return res
    end
}

Dataset.get = argcheck{
    {name='self', type='rl.Dataset'},
    {name='idx', type='number'},
    call = function(self, idx)
        -- Simply draw an example from the forward model.
        local selected, selected_indices = self:sample_batch()
        local action = self:batch_action(selected)
        local res = self:batch_forward(selected, action)

        -- require 'fb.debugger'.enter()
        res.fm_indices = torch.FloatTensor(selected_indices)
        res.thread_idx = self.thread_idx

        -- require 'fb.debugger'.enter()
        -- print(string.format("RLDataset.get(): idx = %d, counter = %d", idx, self.counter))
        -- self.counter = self.counter + 1
        return res
    end
}

Dataset.size =
   function(self)
       return self.epoch_size / self.batchsize
   end
