local pl = require 'pl.import_into'()

require 'torch'
require 'nn'

-- Some global variables.

local conv_layer, relu_layer, tanh_layer, sigmoid_layer, maxpool_layer

if cudnn then
    conv_layer = cudnn.SpatialConvolution
    relu_layer = cudnn.ReLU
    tanh_layer = cudnn.Tanh
    sigmoid_layer = cudnn.Sigmoid
    maxpool_layer = cudnn.SpatialMaxPooling
else
    conv_layer = nn.SpatialConvolutionMM
    relu_layer = nn.ReLU
    tanh_layer = nn.Tanh
    sigmoid_layer = nn.Sigmoid
    maxpool_layer = nn.SpatialMaxPooling
end

-- Some local utility that make networks.
-- make_network, it take layer specification + inputdim as input, return the actual layer and output dim.
local function conv_layer_size(inputsize, kw, dw, pw)
    dw = dw or 1
    pw = pw or 0
    return math.floor((inputsize + pw * 2 - kw) / dw) + 1
end

local function spatial_layer_size(layer, inputdim)
    layer.nip = layer.nip or inputdim[2]
    layer.nop = layer.nop or layer.nip

    -- print(pl.pretty.write(inputdim))
    assert(#inputdim == 4, 'Spatial_layer_size: Input dim must be 4 dimensions')
    assert(layer.nip == inputdim[2], string.format('Spatial_layer_size: the number of input channel [%d] is not the same as specified [%d]!', inputdim[2], layer.nip))
    assert(layer.nop, 'Spatial_layer_size: layer.nop is null!')

    layer.pw = layer.pw or 0
    layer.dw = layer.dw or 1

    layer.kh = layer.kh or layer.kw
    layer.ph = layer.ph or layer.pw
    layer.dh = layer.dh or layer.dw

    local outputw = conv_layer_size(inputdim[4], layer.kw, layer.dw, layer.pw)
    local outputh = conv_layer_size(inputdim[3], layer.kh, layer.dh, layer.ph)

    return {inputdim[1], layer.nop, outputh, outputw}
end

local function spec_expand(dim, spec, inputdim)
    local res = {}
    local total = 0
    for i = 1, #spec do
        if type(spec[i]) == 'table' then
            this_res, this_total = spec_expand(dim, spec[i], inputdim)
        else
            this_res = pl.tablex.deepcopy(inputdim)
            this_res[dim] = spec[i]
            this_total = spec[i]
        end
        table.insert(res, this_res)
        total = total + this_total
    end
    return res, total
end

local function make_network(layer, inputdim)
    if layer.showinputdim then
        print("++++++++++++++++++++++++++++")
        print("Layer spec = " .. pl.pretty.write(layer, '', false))
        print("Input dim = " .. pl.pretty.write(inputdim, '', false))
        print("----------------------------")
    end

    if not layer.type or layer.type == 'seq' then
        local seq = nn.Sequential()

        -- Maka a sequential network
        local curr_inputdim = inputdim
        local ll
        for _, l in ipairs(layer) do
            ll, curr_inputdim = make_network(l, curr_inputdim)
            seq:add(ll)
        end
        return seq, curr_inputdim

    elseif layer.type == 'parallel' then
        local para = nn.ParallelTable()

        local ll
        local outputdim = {}
        for idx, l in ipairs(layer) do
            ll, outputdim[idx] = make_network(l, inputdim[idx])
            para:add(ll)
        end
        return para, outputdim
    elseif layer.type == 'join' then
        assert(type(inputdim) == 'table', 'MakeNetwork::Join: inputdim must be a table!')

        local outputdim
        for idx, dim in ipairs(inputdim) do
            if not outputdim then
                outputdim = dim
            else
                for dim_idx, d_size in ipairs(dim) do
                    if dim_idx == layer.dim then
                        outputdim[dim_idx] = outputdim[dim_idx] + d_size
                    else
                        assert(outputdim[dim_idx] == d_size, 'Join table, dimension ' .. dim_idx .. ' disagree!')
                    end
                end
            end
        end
        return nn.JoinTable(layer.dim), outputdim

    elseif layer.type == 'conv' then
        layer.nip = layer.nip or inputdim[2]
        layer.dw = layer.dw or 1

        layer.kh = layer.kh or layer.kw
        layer.dh = layer.dh or layer.dw

        layer.pw = layer.pw or math.floor(layer.kw / 2)
        layer.ph = layer.ph or math.floor(layer.kh / 2)

        assert(layer.nip == inputdim[2], string.format('MakeNetwork::Conv: #input channel [%d] must match with specification [%d]!', inputdim[2], layer.nip));
        assert(layer.kw)
        assert(layer.kh)
        assert(layer.pw)
        assert(layer.ph)
        assert(layer.dw)
        assert(layer.dh)
        assert(layer.nip)
        assert(layer.nop)
        local conv_layer = conv_layer(layer.nip, layer.nop, layer.kw, layer.kh, layer.dw, layer.dh, layer.pw, layer.ph)
        return conv_layer, spatial_layer_size(layer, inputdim)

    elseif layer.type == 'relu' then
        return relu_layer(), inputdim
    elseif layer.type == 'tanh' then
        return tanh_layer(), inputdim
    elseif layer.type == 'sigmoid' then
        return sigmoid_layer(), inputdim
    elseif layer.type == 'bn' then
        assert(#inputdim == 2, 'Error! Input to BatchNormalization must be 2-dimensional.')
        return nn.BatchNormalization(inputdim[2]), inputdim

    elseif layer.type == 'spatialbn' then
        assert(#inputdim == 4, 'Error! Input to SpatialBatchNormalization must be 4-dimensional.')
        return nn.SpatialBatchNormalization(inputdim[2]), inputdim

    elseif layer.type == 'thres' then
        return nn.Threshold(0, 1e-6), inputdim

    elseif layer.type == 'maxp' then
        assert(layer.kw and layer.dw, "MakeNetwork:MaxP: kw and dw should not be nil")

        layer.kh = layer.kh or layer.kw
        layer.dh = layer.dh or layer.dw

        return maxpool_layer(layer.kw, layer.kh, layer.dw, layer.dh), spatial_layer_size(layer, inputdim)

    elseif layer.type == 'maxp1' then
        assert(layer.kw and layer.dw, "MakeNetwork:MaxP1: kw and dw should not be nil")
        local outputdim = { inputdim[1], conv_layer_size(inputdim[2], layer.kw, layer.dw), inputdim[3] }
        return nn.TemporalMaxPooling(layer.kw, layer.dw), outputdim

    elseif layer.type == 'reshape' then
        if layer.dir == '4-2' then
            layer.wi = layer.wi or inputdim[4]
            layer.nip = layer.nip or inputdim[2]
            layer.nop = layer.nop or inputdim[2]*inputdim[3]*inputdim[4]
        elseif layer.dir == '3-2' then
            -- For temporal 1D network, inputdim[2] is the length and inputdim[3] is the number of channels.
            layer.wi = layer.wi or inputdim[2]
            layer.nip = layer.nip or inputdim[3]
            layer.nop = layer.nop or inputdim[2]*inputdim[3]
        end

        if layer.wi then
            -- Reshape from image to vector.
            if layer.dir == '4-2' then
               layer.hi = layer.hi or inputdim[3]

               assert(#inputdim == 4, 'MakeNetwork::Reshape4-2: Input dim must be 4 dimensions')
               local outputsize = { inputdim[1], layer.nip*layer.wi*layer.hi }
               assert(outputsize[2] == inputdim[2]*inputdim[3]*inputdim[4], 'MakeNetwork::Reshape4-2: Input dim must match with specified dimensions')
               assert(outputsize[2] > 0,
                 string.format("MakeNetwork::Reshape4-2: outputsize[2] = %d, (nip, wi, hi) = (%d, %d, %d)",
                      outputsize[2], layer.nip, layer.wi, layer.hi))
               return nn.View(outputsize[2]), outputsize
            elseif layer.dir == '3-2' then
               assert(#inputdim == 3, 'MakeNetwork::Reshape3-2: Input dim must be 3 dimensions')
               local outputsize = { inputdim[1], layer.nip*layer.wi }
               assert(outputsize[2] == inputdim[2]*inputdim[3], 'MakeNetwork::Reshape3-2: Input dim must match with specified dimensions')
               assert(outputsize[2] > 0,
                 string.format("MakeNetwork::Reshape3-2: outputsize[2] = %d, (nip, wi) = (%d, %d)",
                      outputsize[2], layer.nip, layer.wi))
               return nn.View(outputsize[2]), outputsize
            end
        elseif layer.wo then
            -- Reshape from vector to image.
            assert(#inputdim == 2, 'MakeNetwork::Reshape2-4: Input dim must be 2 dimensions')
            layer.nip = layer.nip or inputdim[2]
            layer.ho = layer.ho or layer.wo
            layer.nop = layer.nop or inputdim[2] / (layer.wo * layer.ho)

            local outputsize = { inputdim[1], layer.nop, layer.ho, layer.wo }
            assert(outputsize[2]*outputsize[3]*outputsize[4] == inputdim[2], 'MakeNetwork::Reshape2-4: Input dim must match with specified dimensions')
            return nn.View(layer.nop, layer.ho, layer.wo), outputsize
        end
    elseif layer.type == 'fc' then
        assert(#inputdim == 2, 'MakeNetwork::FC: Input dim must be 2 dimensions')
        layer.nip = layer.nip or inputdim[2]
        assert(layer.nip == inputdim[2], string.format('MakeNetwork::FC: the number of input channel [%d] is not the same as specified [%d]!', inputdim[2], layer.nip))
        return nn.Linear(layer.nip, layer.nop), { inputdim[1], layer.nop }

    elseif layer.type == 'conv1' then
        -- inputdim[1] : batchsize
        -- inputdim[2] : input length
        -- inputdim[3] : nip
        assert(layer.kw, 'MakeNetwork:Conv1: kw must be specified')
        assert(#inputdim == 3, 'MakeNetwork:Conv1: Input dim must be 3 dimensions')
        assert(layer.nop, 'MakeNetwork:Conv1: nop must be specified')
        -- Note that for temporal convolutional,
        layer.nip = layer.nip or inputdim[3]
        layer.dw = layer.dw or 1

        assert(layer.nip == inputdim[3], string.format('MakeNetwork::Conv1: the number of input channels [%d] is not the same as specified [%d]!', inputdim[3], layer.nip))
        local outputdim = {inputdim[1], conv_layer_size(inputdim[2], layer.kw, layer.dw), layer.nop}
        return nn.TemporalConvolution(layer.nip, layer.nop, layer.kw, layer.dw), outputdim

    elseif layer.type == 'usample' then
        assert(#inputdim == 4, 'MakeNetwork::USample: Input dim must be 4 dimensions')
        layer.wi = layer.wi or inputdim[3]
        assert(layer.wi == inputdim[3], string.format('MakeNetwork::USample: Input height [%d] much match with specification [%d].', inputdim[3], layer.wi))
        assert(layer.wi == inputdim[4], string.format('MakeNetwork::USample: Input width [%d] much match with specification [%d].', inputdim[4], layer.wi))
        return nn.SpatialUpSamplingNearest(layer.wo / layer.wi), { inputdim[1], inputdim[2], layer.wo, layer.wo }
    elseif layer.type == 'recursive-split' then
        -- Check if the size are the same.
        -- print("InputDim:")
        -- print(pl.pretty.write(inputdim))

        outputdim, total_use = spec_expand(layer.dim, layer.spec, inputdim)
        assert(total_use == inputdim[layer.dim], string.format("MakeNetwork::RecursiveSplitTable: Total usage specified by layer.spec [%d] is not the same as the inputdim[%d] (which is %d)", total_use, layer.dim, inputdim[layer.dim]))

        return nn.RecursiveSplitTable(layer.dim - 1, #inputdim - 1, layer.spec), outputdim
    elseif layer.type == 'addtable' then
        assert(type(inputdim) == 'table', "MakeNetwork::addtable: Inputdim must be a table.")
        assert(inputdim[1], "MakeNetwork::addtable: Inputdim must not be empty.")

        for i = 2, #inputdim do
            assert(#inputdim[i] == #inputdim[1], string.format("MakeNetwork::addtable: Each entry of input dim must be of the same length, yet #input[%d] = %d while #inputdim[1] = %d", i, #inputdim[i], #inputdim[1]))
            for j = 1, #inputdim[i] do
                assert(inputdim[i][j] == inputdim[1][j], string.format("MakeNetwork::addtable: Each entry of inputdim must be of same size. Now inputdim[%d][%d] = %d while inputdim[1][%d] = %d", i, j, inputdim[i][j], j, inputdim[1][j]))
            end
        end

        return nn.CAddTable(), inputdim[1]
    elseif layer.type == 'dropout' then
        return nn.Dropout(layer.ratio), inputdim
    elseif layer.type == 'logsoftmax' then
        return nn.LogSoftMax(), inputdim
    else
        error("Unknown layer type " .. layer.type);
    end
end

local function merge_tables(tbls)
    if #tbls == 0 then return {} end
    local res = tbls[1]
    for i = 2,#tbls do
        for j = 1, #tbls[i] do
            table.insert(res, tbls[i][j])
        end
    end

    return res
end

local nnutils = {
    make_network = make_network,
    spatial_layer_size = spatial_layer_size,
    merge_tables = merge_tables
}


function nnutils.torchnet_custom_merge()
  local transform = require 'torchnet.transform'
  local utils = require 'torchnet.utils'
  return transform.tableapply(
    function (field)
        if type(field) == 'table' and field[1] then
            if type(field[1]) == 'number' then
                -- Make table of numbers a tensor. e.g., {1, 2, 3, 4, 2, 1} -> tensor
                -- This is particular useful for making target a tensor.
                return torch.Tensor(field)
            elseif torch.typename(field[1]) and torch.typename(field[1]):match('Tensor') then
                -- { TensorSample1, TensorSample2, TensorSample3 } -> make it a tensor.
                return utils.table.mergetensor(field)
            elseif type(field[1]) == 'table' and torch.typename(field[1][1]) and torch.typename(field[1][1]):match('Tensor') then
                -- { { t1, s1 }, { t2, s2}, {t3, s3} } -> { Tensor{ t1, t2, t3}, Tensor{s1, s2, s3} }
                -- All t1, s1, t2, s2 are tensors.
                local n = #field[1]
                local final_res = {}
                for i = 1, n do
                    local res = {}
                    for j = 1, #field do
                        table.insert(res, field[j][i])
                    end
                    table.insert(final_res, utils.table.mergetensor(res))
                end
                return final_res
            end
        end
        return field
    end)
end

function nnutils.torchnet_makebatch()
    local transform = require 'torchnet.transform'
    return transform.makebatch{
        merge = nnutils.torchnet_custom_merge()
    }
end

function nnutils.torchnet_tocuda()
    local function tocuda(x)
        if torch.typename(x) and torch.typename(x):match("Tensor") then
            return x:cuda()
        elseif type(x) == 'table' then
            local res = { }
            for k, v in pairs(x) do
                res[k] = tocuda(v)
            end
            return res
        else
            error("nnutils.torchnet_tocuda(): wrong type")
        end
    end
    return tocuda
end

function nnutils.torchnet_tofloat()
    local function tofloat(x)
        if torch.typename(x) and torch.typename(x):match("Tensor") then
            return x:float()
        elseif type(x) == 'table' then
            local res = { }
            for k, v in pairs(x) do
                res[k] = tofloat(v)
            end
            return res
        else
            error("nnutils.torchnet_tofloat(): wrong type")
        end
    end
    return tofloat
end

-- buf as the table that holds all newly allocated memories.
function nnutils.send2gpu(sample, buf)
    for k, v in pairs(sample) do
        if torch.typename(v) and torch.typename(v):match("torch%..*Tensor") then
            if not buf[k] then
                buf[k] = torch.CudaTensor()
            end
            v = v:squeeze()
            buf[k]:resize(v:size()):copy(v)
            sample[k] = buf[k]
        elseif type(v) == 'table' then
            buf[k] = { }
            nnutils.send2gpu(v, buf[k])
        end
    end
end

return nnutils
