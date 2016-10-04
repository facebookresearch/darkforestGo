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
require 'train.rl_framework.infra.bundle'

doc[[

### rl.Agent

The Agent module implements the agent behavior.

For opt we have:
   optim:
      supervised:
      q_learning:
      double_q_learning:
      policy_gradient:
      actor_critic:
]]

require 'nn'

local Agent = torch.class('rl.Agent', rl)

-- Methods
-- Pure supervised approach.
-- input.s is the input state and input.a is the next action the agent is supposed to take.
Agent.optim_supervised = function(self, input, test_mode)
    -- minimize ||Q(s, a) - (r + \gamma max_a' Q_fix(s', a'))||^2
    local bundle = self.bundle
    local alpha = self.alpha

    bundle:forward(input.s, { policy=true } )
    local gradOutput, errs = bundle:backward_prepare(input.s, { policy=input.a })

    if not test_mode then
        -- require 'fb.debugger'.enter()
        bundle:backward(input.s, gradOutput)
        -- require 'fb.debugger'.enter()
        bundle:update(alpha)
        -- require 'fb.debugger'.enter()
    end
    return errs
end

-- input should have the following elements:
--   s, a, s2, r (all in tensor format)
Agent.optim_q_learning = function(self, input, test_mode)
    -- minimize ||Q(s, a) - (r + \gamma max_a' Q_fix(s', a'))||^2
    local bundle = self.bundle
    local gamma = self.opt.gamma
    local alpha = self.alpha

    local fix_q = bundle:forward(input.s2, { fix_q=true }).fix_q
    local target = fix_q:max(2):mul(gamma):cmul(input.nonterminal):add(input.r)

    bundle:forward(input.s, { q=true } )
    local gradOutput, errs = bundle:backward_prepare(input.s, { q={input.a, target} })

    if not test_mode then
        bundle:backward(input.s, gradOutput)
        bundle:update(alpha)
    end
    return errs
end

Agent.optim_double_q_learning = function(self, input, test_mode)
    -- minimize ||Q(s, a) - (r + \gamma max_a' Q_fix(s', argmax_a Q(s', a)))||^2
    local bundle = self.bundle
    local gamma = self.opt.gamma
    local alpha = self.alpha

    local s2_res = bundle:forward(input.s2, { q=true, fix_q=true })
    local _, maxActions = s2_res.q:max(2)

    local fix_q = s2_res.fix_q

    local target = fix_q.new():resize(fix_q:size())
    for i = 1, fix_q:size(1) do
        target[i] = fix_q[i][maxActions[i]]
    end
    target:mul(gamma):add(input.r)

    bundle:forward(input.s, { q=true } )
    local gradOutput, errs = bundle:backward_prepare(input.s, { q={input.a, target} })
    if not test_mode then
        bundle:backward(input.s, gradOutput)
        bundle:update(alpha)
    end
    return errs
end

-- input should be:
--  s = {s1, s2, s3, ..., s_n },
--  a = {a1, a2, a3, ..., a_n }
--  r = {r1, r2, r3, ..., r_n }
--  where si + a_i = s_{i+1}, r_i
Agent.optim_policy_gradient = function(self, input)
    local bundle = self.bundle
    local gamma = self.opt.gamma
    local alpha = self.alpha

    local n = #input.s
    local r = input.r[n]:clone()

    for i = n, 1, -1 do
        local policy_logprob = bundle:forward(input.s[i], {policy=true}).policy
        local gradOutput, errs = bundle:backward_prepare(input.s[i], {policy=input.a[i], grad_weight=r})
        bundle:backward(input.s[i], gradOutput)
        bundle:update(alpha)
        if i > 1 then
            r:mul(gamma):add(input.r[i-1])
        end
    end
end

-- input should be:
--  s = {s1, s2, s3, ..., s_n },
--  a = {a1, a2, a3, ..., a_n }
--  r = {r1, r2, r3, ..., r_n }
--  where si + a_i = s_{i+1}, r_i
Agent.optim_actor_critic = function(self, input)
    local bundle = self.bundle
    local gamma = self.opt.gamma
    local alpha = self.alpha

    local n = #input.s
    local r = input.r[n]:clone()

    for i = n, 1, -1 do
        local res = bundle:forward(input.s[i], {policy=true, value=true})
        local logprob = res.policy
        local value = res.value
        local diff = r - value
        -- Actor model
        local gradOutput, errs = bundle:backward_prepare(input.s[i], {policy=input.a[i], alpha = diff, value=r})
        bundle:backward(input.s[i], gradOutput)
        bundle:update(alpha)
        if i > 1 then
            r:mul(gamma):add(input.r[i-1])
        end
    end
end

--------------- Make them a table ----------------
Agent.optims = {
    supervised = Agent.optim_supervised,
    q_learning = Agent.optim_q_learning,
    double_q_learning = Agent.optim_double_q_learning,
    policy_gradient = Agent.optim_policy_gradient,
    actor_critic = Agent.optim_actor_critic
}

Agent.__init = argcheck{
    noordered = true,
   {name="self", type="rl.Agent"},
   {name="bundle", type="rl.Bundle"},
   {name="opt", type='table'},
   call =
      function(self, bundle, opt)
          self.bundle = bundle
          self.opt = pl.tablex.deepcopy(opt)
          -- Learning rate.
          self.alpha = self.opt.alpha
          -- From opt, we could specify the behavior of agent.
          self.optim = rl.func_lookup(self.opt.optim, Agent.optims)
      end
}

Agent.training = argcheck{
   {name="self", type="rl.Agent"},
   call = function(self)
       self.bundle:training()
       self.test_mode = false
   end
}

Agent.evaluate = argcheck{
   {name="self", type="rl.Agent"},
   call = function(self)
       self.bundle:evaluate()
       self.test_mode = true
   end
}

Agent.reduce_lr = argcheck{
   {name="self", type="rl.Agent"},
   {name="ratio", type="number"},
   call = function(self, ratio)
       self.alpha = self.alpha / ratio
   end
}

Agent.get_lr = argcheck{
   {name="self", type="rl.Agent"},
   call = function(self)
       return self.alpha
   end
}

-- Agent behavior.
Agent.optimize = argcheck{
   {name="self", type="rl.Agent"},
   {name="input", type="table"},
   call =
      function(self, input)
          -- we run the optimization method.
          self.bundle:clear_forwarded()
          return self:optim(input, self.test_mode)
      end
}

local function copy_model(dst, dst_str, src, src_str)
    -- Update the parameters.
    -- require 'fb.debugger'.enter()
    local params_dst = dst:parameters()
    local params_src = src:parameters()
    assert(#params_dst == #params_src,
           string.format("#%s [%d] is not equal to #%s [%d]!", dst_str, #params_dst, src_str, #params_src))
    for i = 1, #params_dst do
        local dst_sizes = params_dst[i]:size()
        local src_sizes = params_src[i]:size()
        assert(#dst_sizes == #src_sizes,
               string.format("Parameter tensor order at layer %d: #%s [%d] is not equal to #%s [%d]!", i, dst_str, #dst_sizes, src_str, #src_sizes))
        for j = 1, #dst_sizes do
            assert(dst_sizes[j] == src_sizes[j],
                   string.format("Parameter tensor size at layer %d: %s [%d] is not equal to %s [%d]!", i, dst_str, dst_sizes[j], src_str, src_sizes[j]))
        end

        params_dst[i]:copy(params_src[i])
    end
end

-- Update the model bundle used for dataset sampling.
Agent.update_sampling_model = argcheck{
    {name="self", type="rl.Agent"},
    {name="cb_before", type="function", opt=true, default=nil},
    {name="cb_after", type="function", opt=true, default=nil},
    call = function(self, cb_before, cb_after)
        if cb_before then cb_before() end

        -- Update the model
        if self.opt.optim == 'q_learning' or self.opt.optim == 'double_q_learning' then
            print("Updating agent fix_q model")
            local fix_q = self.bundle:get_model("fix_q")
            assert(fix_q, "Agent.update_sampling_model: the model of fix_q has to be available.")
            local q = self.bundle:get_model("q")
            copy_model(fix_q, "fix_q", q, "q")
        elseif self.opt.optim == 'supervised' then
            print("Supervised approach, no need to update agent's model")
        else
            error(string.format("Agent.update_sampling_model: optim: %s, Not implemented yet!", self.opt.optim))
        end

        if cb_after then cb_after() end
    end
}

Agent.get_bundle = argcheck{
   {name="self", type="rl.Agent"},
   call =
      function(self)
          return self.bundle
      end
}

