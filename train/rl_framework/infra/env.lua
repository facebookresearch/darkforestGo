--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local rl = { }

require 'torch'

function rl.func_lookup(v, func_table)
    return type(v) == 'function' and v or func_table[v]
end

return rl
