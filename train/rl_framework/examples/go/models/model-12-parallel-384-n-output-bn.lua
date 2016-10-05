require 'nn'
require 'cudnn'
local pl = require('pl.import_into')()
local nnutils = require 'utils.nnutils'
require 'train.rl_framework.examples.go.ParallelCriterion2'

-- Specification.
local function get_network_spec(n)
    return {
        -- input is 19x19x?
        {type='conv', kw=5, dw=1, pw=2, nop=92},
        {type='relu'},
        {type='spatialbn'},
        -- No max pooling, does not make sense.
        -- {type='maxp', kw=3, dw=2},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        -- {type='maxp', kw=3, dw=2},
        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        -- {type='maxp', kw=3, dw=2},
        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        -- {type='maxp', kw=3, dw=2},
        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=384},
        {type='relu'},
        {type='spatialbn'},

        {type='conv', kw=3, dw=1, pw=1, nop=n},
    }
end

return function(inputdim, config)
    assert(inputdim[3] == 19)
    assert(inputdim[4] == 19)

    local net, outputdim = nnutils.make_network(get_network_spec(config.nstep), inputdim)
    if config.nGPU>1 then
        require 'cutorch'
        assert(config.nGPU <= cutorch.getDeviceCount(), 'number of GPUs less than config.nGPU specified')
        local net_single = net
        net = nn.DataParallel(1)
        for i=1, config.nGPU do
            cutorch.withDevice(i, function()
                net:add(net_single:clone())
            end)
        end
    end

    local model = nn.Sequential()
    model:add(net):add(nn.View(config.nstep, 19*19):setNumInputDims(3)):add(nn.SplitTable(1, 2))
    local softmax = nn.Sequential()
    -- softmax:add(nn.Reshape(19*19, true))
    softmax:add(nn.LogSoftMax())
    -- )View(-1):setNumInputDims(2))

    local softmaxs = nn.ParallelTable()
    -- Use self-defined parallel criterion 2, which can handle targets of the format nbatch * #target
    local criterions = nn.ParallelCriterion2()
    for k = 1, config.nstep do
        softmaxs:add(softmax:clone())
        local w = 1.0 / k
        criterions:add(nn.ClassNLLCriterion(), w)
    end
    model:add(softmaxs)
    return model, criterions, outputdim
end
