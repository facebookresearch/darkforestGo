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

require 'nn'

doc[[

### rl.Bundle
The Bundle module that gives the required output. Note that all the outputs have batchsize as the first dimension.

All the network inputs are state representations.

Q-network("q"): output a list of Q(s, a) for each action.
Target-Q-network("fix_q"): output a list of Q(s, a) for each action, but its parameters is fixed.
Value network("value"): output a value
Policy network("policy"): output a list of logprobs for each action.

=== Member function ===

0. bundle:__init(models, gradient_modifier)
   Example models:
      models["q"] = network1
      models["value,policy"] = network2 (This network outputs value and policy as 1st and 2nd outputs
   The gradient modifier will change the gradient before they are applied to weights. Useful for implementing adagrads, etc.

1. bundle:forward(input, {q=true, value=true})
   Network forward. It returns a table with q and value as "key", and their outputs as "value".

2. bundle:backward(input, {q=grad_q, value=grad_v})
   Network backward. The table stores the gradient output.

3. bundle:update(learning_rate)
   Update the parameters with computed gradient, and zero out the gradients.
]]

local Bundle = torch.class('rl.Bundle', rl)

Bundle.__init = argcheck{
    noordered = true,
    {name="self", type="rl.Bundle"},
    {name="models", type="table"},
    {name="crits", type="nn.Criterion", opt=true},
    {name="gradient_modifier", type="function", opt=true},
    {name="primary_instance", type="boolean", opt=true, default=true},
    call = function(self, models, crits, gradient_modifier, primary_instance)
        -- Example models:
        --    models["q"] = network1
        --    models["value,policy"] = network2 (This network outputs value and policy as 1st and 2nd outputs
        --
        -- self.key2model = { key1: model1, key2: model2 }
        self.models = models
        -- Whether the current instance is a primary instance. If so, they it can clone.
        -- The cloned instance is no longer primary.
        self.primary_instance = primary_instance
        self.key2model = { }
        self.keys2attr = { }
        self.params = { }
        self.gparams = { }
        for k, v in pairs(models) do
            -- require 'fb.debugger'.enter()
            local params, gparams = v:parameters()
            for _, param in ipairs(params) do
                table.insert(self.params, param)
            end
            for _, gparam in ipairs(gparams) do
                table.insert(self.gparams, gparam)
            end

            local keys = pl.stringx.split(k, ",")
            -- require 'fb.debugger'.enter()
            for i, kk in ipairs(keys) do
                assert(self.key2model[kk] == nil, "The key value " .. kk .. " has appeared before!")
                self.key2model[kk] = { model = v, model_key = k, key_idx = i }
            end
            -- From "value,policy" to number of outputs.
            -- We could store other network-specific attributes.
            self.keys2attr[k] = {
                model = v,
                num_output = #keys
            }
        end

        -- Current status. How many model has been forwarded.
        self.forwarded = { }
        self.crits = crits and crits:cuda() or nn.ClassNLLCriterion():cuda()

        --
        self.gradient_modifier = gradient_modifier
    end
}

Bundle.training = argcheck{
    {name="self", type="rl.Bundle"},
    call = function(self)
        for k, m in ipairs(self.key2model) do
            m:training()
        end
    end
}

Bundle.evaluate = argcheck{
    {name="self", type="rl.Bundle"},
    call = function(self)
        for k, m in ipairs(self.key2model) do
            m:evaluate()
        end
    end
}

function Bundle:load_output(model_key, k_idx)
    local m = self.keys2attr[model_key].model
    local num_output = self.keys2attr[model_key].num_output

    if k_idx == 1 and num_output == 1 then
        return m.output
    else
        return m.output[k_idx]
    end
end

function Bundle:save_output(model_key, t, k_idx, c)
    local m = self.keys2attr[model_key].model
    local num_output = self.keys2attr[model_key].num_output

    if k_idx == 1 and num_output == 1 then
        return c
    else
        t = t or { }
        t[k_idx] = c
        return t
    end
end

function Bundle:forward(input, attrs, refresh)
    refresh = refresh or false
    if refresh then self.forwarded = { } end
    local all_output = { }
    for k, v in pairs(attrs) do
        local e = self.key2model[k]
        assert(e, string.format("Bundle.forward: key %s has no model.", k))
        local this_model = e.model
        local this_model_key = e.model_key
        local k_idx = e.key_idx

        if v and self.forwarded[this_model_key] == nil then
            -- print("Before forward model, k = " .. k)
            -- require 'fb.debugger'.enter()
            this_model:forward(input)
            -- print("After forward model, k = " .. k)
            self.forwarded[this_model_key] = true
        end

        all_output[k] = self:load_output(this_model_key, k_idx)
    end
    return all_output
end

local function check_table_continuous(t)
    local prev_i = 0
    for i, _ in ipairs(t) do
        if i ~= prev_i + 1 then return false end
        prev_i = i
    end
    return true
end

local function get_top5(output, v)
    local topn = 5
    local batchsize = output:size(1)
    local top_accuracy = torch.FloatTensor(topn):zero()
    local _, sorted_indices = output:sort(2, true)
    if torch.typename(sorted_indices) ~= 'torch.CudaTensor' then
       sorted_indices2 = torch.CudaTensor(unpack(sorted_indices:size():totable()))
       sorted_indices2:copy(sorted_indices)
       sorted_indices = sorted_indices2
    end

    for i = 1, topn do
        local accuracy = v:eq(sorted_indices:narrow(2, i, 1)):sum()
        top_accuracy[i] = accuracy
    end
    top_accuracy = top_accuracy:cumsum():mul(1.0 / batchsize)

    return {
        ['pi@1'] = top_accuracy[1] * 100,
        ['pi@5'] = top_accuracy[5] * 100
    }
end

-- Return errors
function Bundle:backward_prepare(input, attrs)
    local all_gradOutput = { }
    local all_errs = { }
    local batchsize = input:size(1)

    for k, v in pairs(attrs) do
        local e = self.key2model[k]
        -- require 'fb.debugger'.enter()
        local this_model = e.model
        local this_model_key = e.model_key
        local k_idx = e.key_idx

        -- If the model has not been forwarded, forward it.
        if v and self.forwarded[this_model_key] == nil then
            this_model:forward(input)
            self.forwarded[this_model_key] = true
        end

        local this_output = self:load_output(this_model_key, k_idx)

        -- Prepare the backpropagation depending on the type.
        -- Also prepare the error.
        local grads, errs
        if k == 'q' then
            -- Q-network, we create gradient.
            grads = this_output:clone():zero()
            errs = this_output.new():resize(batchsize)
            local a = v[1]
            local t = v[2]

            -- Loop over batchsize.
            for i = 1, batchsize do
                local g = this_output[i][a[i]] - t[i]
                errs[i] = g*g
                grads[i][a[i]] = g
            end
        elseif k == 'policy' then
            -- Use CrossEntropy to compute the gradient.
            errs = self.crits:forward(this_model.output, v)

            -- Also compute the top-1 and top-5 error.
            if type(this_model.output) == 'table' then
                for i, output in ipairs(this_model.output) do
                    local topk_acc = get_top5(output, v:select(2, i))
                    for kk, vv in pairs(topk_acc) do all_errs[tostring(i) .. kk] = vv end
                end
            else
                local topk_acc = get_top5(this_model.output, v)
                for kk, vv in pairs(topk_acc) do all_errs[kk] = vv end
            end

            grads = self.crits:backward(this_model.output, v)
        else
            error("Unsupported backpropagation for model key: " .. k)
        end

        -- Collect backward gradOutput.
        all_gradOutput[k] = self:save_output(this_model_key, all_gradOutput[k], k_idx, grads)
        all_errs[k] = errs
    end
    -- require 'fb.debugger'.enter()
    return all_gradOutput, all_errs
end

function Bundle:backward(input, gradOutput)
    -- Then formally we do backprop.
    for k, grads in pairs(gradOutput) do
        -- First check if all gradient inputs are ready.
        local e = self.key2model[k]
        local this_model = e.model
        local this_model_key = e.model_key
        local this_output = this_model.output

        local num_output = self.keys2attr[this_model_key].num_output

        -- require 'fb.debugger'.enter()
        if num_output >= 2 then
            -- The gradient input is a table and we need to check it is continuous.
            assert(check_table_continuous(grads), "The grads should be continuous.")
            assert(#grads == #this_output,
                   string.format("#target [%d] should be the same as model's #output [%d]", #target, #this_output))
        end

        -- Backprop
        -- require 'fb.debugger'.enter()
        -- print("Before backward model, k = " .. k)
        this_model:backward(input, grads)
    end
end

Bundle.update = argcheck{
    {name="self", type="rl.Bundle"},
    {name="learning_rate", type="number"},
    call = function(self, learning_rate)
        -- Update all gradients.
        if self.gradient_modifier then
            self.gradient_modifier(self.gparams, self.params)
        end
        -- require 'fb.debugger'.enter()
        --[[
        for k, m in ipairs(self.key2model) do
            m:updateParameters(learning_rate)
            m:zeroGradParameters()
        end
        ]]
        for i = 1, #self.params do
            cutorch.withDevice(self.params[i]:getDevice(), function()
                self.params[i]:add(-learning_rate, self.gparams[i])
                self.gparams[i]:zero()
            end)
        end
        -- require 'fb.debugger'.enter()
        -- Clear the forwarded labels.
        self.forwarded = { }
    end
}

Bundle.get_model = argcheck{
    {name="self", type="rl.Bundle"},
    {name="key", type="string"},
    call = function(self, key)
        return self.key2model[key].model
    end
}

-- Clear all forwarded layers.
Bundle.clear_forwarded = argcheck{
    {name="self", type="rl.Bundle"},
    call = function(self)
        self.forwarded = { }
    end
}

Bundle.clear_state = argcheck{
    {name="self", type="rl.Bundle"},
    call = function(self)
        for k, v in pairs(self.keys2attr) do
            v.model:clearState()
        end
        self.forwarded = { }
    end
}

function Bundle:clone(...)
    assert(self.primary_instance, "non primary rl.Bundle cannot be cloned!")
    local args = {...}

    local dup_models = { }
    for k, m in pairs(self.models) do
        dup_models[k] = m:clone(unpack(args))
    end

    -- The gradient modifier won't propagate.
    return rl.Bundle{
        models = dup_models,
        primary_instance = false
    }
end

