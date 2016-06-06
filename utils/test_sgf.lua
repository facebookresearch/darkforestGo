--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local sgfloader = require 'utils.sgf'
local pl = require 'pl.import_into'()
local utils = require 'utils.utils'
local common = require("common.common")

Index = require 'fbcode.blobs.lua.index'
IndexLoader = require 'fbcode.blobs.lua.index-loader'
PathResolver = require 'fbcode.blobs.lua.path-resolver'
StreamMultiReader = require 'fbcode.blobs.lua.stream-multi-reader'

local opt = pl.lapp[[
    -i,--input   (default "")           The sgf file to load and play
    -m,--mtbl                           The mtbl file to load and play
]]

if #opt.input > 0 then 
    local sgf = sgfloader.parse(io.open(opt.input):read("*a"))
    print(pl.pretty.write(sgf))   
    sgf:play(function (move, counter)
        print(string.format("Move %d", counter))
        print(sgfloader.show_move(move))
        local x, y, player = sgfloader.parse_move(move)
        print(string.format("x = %d, y = %d, player = %d", x, y, player))
        return true
    end) 
    print("============Saving sgf===============")
    print(sgf:save())
end

if opt.mtbl then
    local root = '/mnt/vol/gfsai-oregon/ai-group/datasets/go-tsumego'
    local index_path = paths.concat(root, 'tsumego.mtbl')
    local filelist = 'tsumego.lst' 
    filelist = paths.concat(root, filelist)
    -- print("Index path = " .. index_path)
    -- print("Filelist = " .. filelist)

    local dataLoader = StreamMultiReader:newFromIndex{
        index = Index:new(
            IndexLoader:newInMemoryUnordered():addSubloader(
                IndexLoader:newFile(index_path)
            )
        ),
        num_threads = 10,
        path_resolver = PathResolver:new():addSearchPath(paths.dirname(index_path)),
        convert_raw_entry_fn = StreamMultiReader.rawEntryToTensorTable,
    }

    local keys = utils.readlines(filelist)
    -- local keys = { "./tsumego/4I2/01-03AOAA/001AOAA-eu1-8/AOAA-eu/AOAAA1-8/5I36A.sgf" }
    local counter = 1
    for k, v in pairs(dataLoader:findKeysAndRead{ keys = keys }) do
        local sgf = sgfloader.parse(v.value, k)
        if sgf == nil or not sgf:is_valid() then 
            print(string.format("==== Testing [%d/%d] SGF is null %s", counter, #keys, sgf:key_str()))
        elseif sgf:get_boardsize() == common.board_size then
            -- print(string.format("==== Testing [%d/%d] %s", counter, #keys, sgf:key_str()))
            if not sgf:play() then 
                -- print(string.format("==== Testing [%d/%d] %s", counter, #keys, sgf:key_str()))
                print("Could not start the game")
            end
        end
        counter = counter + 1
    end
    print("All test passed")
end
