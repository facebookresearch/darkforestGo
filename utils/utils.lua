--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local utils = {}
local pl = require 'pl.import_into'()

utils.dbg = false

function utils.dbg_set()
    utils.dbg = true
end

function utils.dbg_get()
    return utils.dbg
end

function utils.dprint(s, ...)
    if utils.dbg then
       local p = {...}
       if #p == 0 then print(s)
       else print(string.format(s, unpack(p)))
       end
       io.flush()
    end
end

function utils.merge(t1, t2)
    for k, v in pairs(t2) do
        if not t1[k] then t1[k] = v end
    end
    return t1
end

function utils.get_current_date()
    local time = os.date("*t")
    return string.format("%04d-%02d-%02d", time.year, time.month, time.day)
end

function utils.get_current_time()
    local time = os.date("*t")
    return string.format("%04d/%02d/%02d %02d:%02d:%02d", time.year, time.month, time.day, time.hour, time.min, time.sec)
end

function utils.get_signature()
    local time = os.date("*t")
    return string.format("%04d-%02d-%02d_%02d-%02d-%02d", time.year, time.month, time.day, time.hour, time.min, time.sec)
end

function utils.get_randString(len)
    if len < 1 then return nil end
    local ret = ""
    for i = 1, len do
        ret = ret .. string.char(math.random(65, 90)) -- from A to Z
    end
    return ret
end

function utils.get_hostname()
     local hostname = io.popen('hostname', 'r'):read("*a")
    -- Remove the last newline
    return pl.stringx.strip(hostname)
end

--- all inputs are ByteTensors, 0 is false and 1 is true.
function utils.isinf(value)
  return value == math.huge or value == -math.huge
end

function utils.isnan(value)
  return value ~= value
end

function utils.isfinite(value)
  return not isinf(value) and not isnan(value)
end

function utils.bitand(t1, t2)
    return torch.ge(t1 + t2, 2)
end

function utils.bitor(t1, t2)
    return torch.ge(t1 + t2, 1)
end

function utils.bitnot(t)
    return torch.lt(t, 1)
end

function utils.len(t)
    local n
    if type(t) == 'table' then
        n = #t
    elseif t:nDimension() == 1 then
        n = t:size(1)
    else
        error(string.format("t is a high-order tensor! t:nDimension() = %d", t:nDimension()))
    end
    return n
end

function utils.fromhead(t, k)
    local n = utils.len(t)
    local res = {}
    for i = 1, math.min(k, n) do
        table.insert(res, t[i])
    end
    return res
end

function utils.fromtail(t, k)
    local n = utils.len(t)
    local res = {}
    for i = n, math.max(n - k, 0) + 1, -1 do
        table.insert(res, t[i])
    end
    return res
end

-- Find nonzero and return a table.
function utils.nonzero(t)
    local indices = {}
    local n = utils.len(t)

    for i = 1, n do
        if t[i] == 1 then
            table.insert(indices, i)
        end
    end
    return indices
end

-- Select rows and return
function utils.selectrows(t, tb)
    assert(tb:size(1) == t:size(1), 'The first dimension of t and tb must be the same')
    local dims = t:size():totable()
    table.remove(dims, 1)

    local res = t.new():resize(tb:sum(), unpack(dims))
    local counter = 0
    for i = 1, t:size(1) do
        if tb[i] == 1 then
            counter = counter + 1
            res[counter]:copy(t[i])
        end
    end
    return res
end

function utils.perm_compose(indices1, indices2)
    -- body
    local n1 = utils.len(indices1)
    local n2 = utils.len(indices2)

    local indices = {}
    for i = 1, n2 do
        indices[i] = indices1[indices2[i]]
    end
    return indices
end

function utils.removekeys(t, exclude)
    local res = {}
    for k, v in pairs(t) do
        if not exclude[k] then res[k] = v end
    end
    return res
end

--[[

Find the correlation between two matrices.
t1 : m1 by n
t2 : m2 by n

return matrix of size m1 by m2

--]]
function utils.innerprod_func(t1, t2, func, reduction)
    local n = t1:size(2)
    assert(n == t2:size(2), string.format('The column of t1 [%d] must be the same as the column of t2 [%d]', t1:size(2), t2:size(2)))
    local m1 = t1:size(1)
    local m2 = t2:size(1)

    local res = torch.Tensor(m1, m2)
    for i = 1, m1 do
        for j = 1, m2 do
            res[i][j] = func(t1[i], t2[j])
        end
    end

    -- Find the one with smallest distance
    local best, best_indices
    if reduction then
        best, best_indices = reduction(res, 2)
    end

    return res, best, best_indices
end

-----------------------------------------------
-- Landmarks related.

function utils.rect_isin(rect, p, margin)
    margin = margin or 0;

    return rect[1] + margin <= p[1] and p[1] <= rect[3] - margin and
           rect[2] + margin <= p[2] and p[2] <= rect[4] - margin;
end

function utils.fill_kernel(m, x, y, r, c)
    assert(m, "Input image should not be null")
    assert(x and y, "Input coordinates should not be null")
    assert(r and c, "Input radius and color should not be null")
    assert(#m:size() == 2, 'fill_kernel: input m is not 2D!')

    -- fill a circle (x, y, r) with number c.
    local w = m:size(2)
    local h = m:size(1)

    min_x = math.min(math.max(x - r, 1), w)
    max_x = math.min(math.max(x + r, 1), w)
    -- if min_x > max_x then min_x, max_x = max_x, min_x end

    min_y = math.min(math.max(y - r, 1), h)
    max_y = math.min(math.max(y + r, 1), h)

    -- if min_y > max_y then min_y, max_y = max_y, min_y end
    -- local img_rect = {1, 1, m:size(2), m:size(1)}
    -- assert(util.rect_isin(img_rect, {min_x, min_y}) == true, string.format("out of bound, min_x = %f, min_y = %f", min_x, min_y))
    -- assert(util.rect_isin(img_rect, {max_x, max_y}) == true, string.format("out of bound, max_x = %f, max_y = %f", max_x, max_y))

    m:sub(min_y, max_y, min_x, max_x):fill(c);
end

-- Input a 2D mask, find its minimal value and associated location.
function utils.find_min_spot(m)
   local min_1, min_i1 = torch.min(m, 1)
   local min_val, min_i2 = torch.min(min_1, 2)

   local x = min_i2[1][1]
   local y = min_i1[1][x]
   return x, y, min_val
end

-- Input a 2D mask, find its maximal value and associated location.
function utils.find_max_spot(m)
   local max_1, max_i1 = torch.max(m, 1)
   local max_val, max_i2 = torch.max(max_1, 2)

   local x = max_i2[1][1]
   local y = max_i1[1][x]
   return x, y, max_val
end
-------------------------------------Save to json--------------------
local function set_to_array(t)
    if type(t) ~= 'table' then return end
    t.__array = true
    for i, v in ipairs(t) do
        set_to_array(v)
    end
end

function utils.convert_to_table(t)
    local res = {}
    if type(t) == 'table' then
        if debug then print("parsing table") end
        for k, v in pairs(t) do
            res[k] = convert_to_table(v)
        end
    elseif type(t) == 'number' then
        if debug then print("parsing number") end
        res = t
    elseif torch.typename(t) and torch.typename(t):match('Tensor') then
        -- if t is a tensor
        if debug then print("parsing tensor") end
        res = t:totable()
        set_to_array(t)
        -- Layer
    else
        local typename = type(t)
        typename = typename or torch.typename(t)
        error("Convert_to_table error, unsupported datatype = " .. typename)
    end
    return res
end

function utils.save_to_json(t, f)
    -- save a table to json
    if type(t) == 'number' then
        f:write(tostring(t))
        return
    end
    if t.__array then
        -- array must contain all numbers.
        f:write("[\n")
        for i, v in ipairs(t) do
            save_to_json(v, f)
            if i ~= #t then f:write(",") end
        end
        f:write("]\n")
    else
        local counter = 0
        for k, v in pairs(t) do counter = counter + 1 end
        f:write("{\n")
        for k, v in pairs(t) do
            f:write(tostring(k) .. " : ")
            save_to_json(v, f)
            counter = counter - 1
            if counter >= 1 then f:write(",\n") end
        end
        f:write("}\n")
    end
end

------------------------

function utils.readlines(filename, return_nil_if_failed)
    local f = io.open(filename)
    if not f and return_nil_if_failed then return end
    assert(f)
    local res = {}
    while true do
      local line = f:read("*line")
      if line == nil then break end
      table.insert(res, line)
    end
    f:close()
    return res
end

-------------------------------------Save to numpy-----------------
-- run it using PATH=/usr/bin/python

function utils.save_pickle(f, t)
    local py = require 'fb.python'
    py.exec([=[
import numpy as np
import cPickle
with open(filename, "wb") as outfile:
    cPickle.dump(variable, outfile, protocol=cPickle.HIGHEST_PROTOCOL)
]=], {variable = t, filename = f})
end

function utils.start_with(s, m)
    return string.sub(s, 1, string.len(m)) == m
end

-----------------------------------FFI Related------------------------------
local function ffi_replace_symbol(line, symbol_table)
    local res = line
    for k, v in pairs(symbol_table) do
        res = res:gsub(k, v)
    end
    return res
end

local function ffi_include_impl(filename, res, symbol_table, header_table)
    local f = assert(io.open(filename))
    local path = pl.path.dirname(filename)
    local previous_backslash = false
    local in_cplusplus_region = false
    while true do
        local line = f:read("*line")
        if line == nil then break end
        if line:sub(1, 1) == '#' then
            local subheader = line:match('#include "(.+)"')
            -- print(subheader)
            if subheader then
                subfilename = paths.concat(path, subheader)
                if header_table[subfilename] == nil then
                    header_table[subfilename] = true
                    -- print("Load " .. subfilename .. " in " .. filename)
                    ffi_include_impl(subfilename, res, symbol_table, header_table)
                end
            elseif line:match("#define .+\\%s*$") then
                previous_backslash = true
            elseif line:match("#ifdef __cplusplus") then
                in_cplusplus_region = true
            elseif in_cplusplus_region and line:match("#endif") then
                in_cplusplus_region = false
            else
                local symbol, value = line:match("#define ([%w%d_]+)[ ]+([%w%d_]+)")
                if symbol and value then
                    symbol_table[symbol] = ffi_replace_symbol(value, symbol_table)
                end
            end
        elseif not in_cplusplus_region then 
            if line:match(".+\\%s*$") then
                -- Skip all backslash lines.
                previous_backslash = true
            elseif line:match("^static") then
                -- Skip all static elements.
            else
                if not previous_backslash then
                    -- Replace all macro symbols.
                    table.insert(res, ffi_replace_symbol(line, symbol_table))
                end
                previous_backslash = false
            end
        end
    end
    f:close()
end

-- The header table needs to be global.
utils.header_table = { }
utils.header_symbols = { }

function utils.ffi_include(filename)
    local ffi = require 'ffi'
    local res = {}
    if utils.header_table[filename] then 
        return utils.header_symbols, "" 
    end

    utils.header_table[filename] = true
    ffi_include_impl(filename, res, utils.header_symbols, utils.header_table)
    local s = table.concat(res, "\n")
    -- print("===================" .. filename)
    -- print(pl.pretty.write(utils.header_table))
    -- print("===================")
    -- print(s)
    -- print("===================")
    ffi.cdef(s)
    return utils.header_symbols, s
end

-------------------------------
--
function utils.sample_select(t, index)
    local res
    if t:nDimension() == 1 then
        res = torch.Tensor(#index)
    else
        res = torch.Tensor(#index, t:size(2))
    end
    for i = 1, #index do
        if t:nDimension() == 1 then
            res[i] = t[index[i]]
        else
            res[i]:copy(t[index[i]])
        end
    end
    return res
end

function utils.time_readable()
    return io.popen("date"):read()
end

function utils.timeit(t, f)
    local __start = utils.clock()
    local res = {f()}
    t = t + utils.clock() - __start
    return unpack(res)
end

function utils.increment(v, i)
    i = i or 1
    v = v + i
    return v
end

-------------------------------------
-- returns a pointer to a tensor x
function utils.get_shared_ptr(x)
   local ffi = require 'ffi'
   assert(x:isContiguous(), 'Data must be contiguous')
   assert(x:type() ~= 'torch.CudaTensor', 'Not usable for cuda tensor, since free cannot be applied.')
   return {
      size = x:size(),
      dim = x:nDimension(),
      pointer = tonumber(ffi.cast('intptr_t', x:data())),
      type = x:type(),
      ctype = string.match(tostring(x:data()), "<(.*)>"),
      storagetype = string.match(tostring(x:storage():cdata()), "<(.*)>")
   }
end

-- Returns a tensor using the memory pointed to by x
function utils.create_from_shared_ptr(x)
   local ffi = require 'ffi'
   ffi.cdef[[void free(void *ptr);]]
   local x2 = torch.factory(x.type)():resize(x.size)
   local storage_ptr = ffi.cast(x.storagetype, torch.pointer(x2:storage()))
   ffi.C.free(storage_ptr.data)
   storage_ptr.data = ffi.cast(x.ctype, x.pointer)
   storage_ptr.flag = 0
   storage_ptr.refcount = 0
   return x2
end

function utils.get_upvalue(func, key)
    i = 1
    while true do
        local n, v = debug.getupvalue(func, i)
        if not n then break end
        if n == key then return v end
        i = i + 1
    end
end

function utils.add_if_nonexist(t1, t2)
   for k, v in pairs(t2) do
       if not t1[k] then t1[k] = v end
   end
   return t1
end

function utils.require_torch()
    require 'nn'
end

function utils.require_cutorch()
    if pl.path.exists("/dev/nvidiactl") then
        require 'cunn'
        require 'cudnn'
        return true
    end
end

return utils
