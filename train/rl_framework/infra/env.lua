local rl = { }

require 'torch'

function rl.func_lookup(v, func_table)
    return type(v) == 'function' and v or func_table[v]
end

return rl
