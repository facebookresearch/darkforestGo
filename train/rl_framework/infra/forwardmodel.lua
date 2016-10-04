--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local argcheck = require 'argcheck'
local doc = require 'argcheck.doc'

local pl = require 'pl.import_into'()

doc[[
### rl.ForwardModel
The ForwardModel module implements the forward model. The application should just override this class to define behavior.
The model could be:
* A simulated environment (any action is possible, real experience)
* A fitted forward model (any action possible)
* Previous experience (arbitrary action is not possible)

User could inherent from this class.
]]

require 'nn'

local rl = require 'train.rl_framework.infra.env'
local ForwardModel = torch.class('rl.ForwardModel', rl)

ForwardModel.__init = argcheck{
   {name="self", type="rl.ForwardModel"},
   call = function(self)
   end
}

-- Reset to a (new, maybe random) state.
ForwardModel.reset = argcheck{
    {name="self", type="rl.ForwardModel"},
    call = function(self)
        error("ForwardModel.reset is not implemented")
    end
}

-- forward(a) -> s', r
ForwardModel.forward = argcheck{
    {name="self", type="rl.ForwardModel"},
    {name="action", type="number"},
    call = function(self, action)
        error("ForwardModel.forward is not implemented")
    end
}

ForwardModel.add_sample = argcheck{
    {name="self", type="rl.ForwardModel"},
    {name="entry", type="table"},
    call = function(self, entry)
        error("ForwardModel.add_sample is not implemented")
    end
}

-- get_actions() -> get available actions for current state (by returning a list)
ForwardModel.get_actions = argcheck{
    {name="self", type="rl.ForwardModel"},
    call = function(self)
        error("ForwardModel.get_actions is not implemented")
    end
}

-- Get the representation of the current state.
ForwardModel.get_curr_state_rep = argcheck{
    {name="self", type="rl.ForwardModel"},
    call = function(self)
        error("ForwardModel.get_curr_state_rep is not implemented")
    end
}

--[[
ForwardModel.clone = argcheck{
    {name="self", type="rl.ForwardModel"},
    call = function(self)
        error("ForwardModel.clone is not implemented")
    end
}
]]
