--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local Index = require 'fbcode.blobs.lua.index'
local IndexLoader = require 'fbcode.blobs.lua.index-loader'
local PathResolver = require 'fbcode.blobs.lua.path-resolver'
local StreamMultiReader = require 'fbcode.blobs.lua.stream-multi-reader'
local sgf =  require 'utils.sgf'
local common = require 'common.common'
local board = require 'board.board'
local utils = require 'utils.utils'
local goutils = require 'utils.goutils'

local function dataLoader(index_path)
    return StreamMultiReader:newFromIndex{
        index = Index:new(
        IndexLoader:newInMemoryUnordered():addSubloader(
        IndexLoader:newFile(index_path)
        )
        ),
        num_threads = 10,
        path_resolver = PathResolver:new():addSearchPath(paths.dirname(index_path)),
        convert_raw_entry_fn = StreamMultiReader.rawEntryToTensorTable,
    }
end

local patv2_utils = { }

function patv2_utils.load(root, mtbl_file, opt)
    patv2_utils.data_loader = dataLoader(paths.concat(root, mtbl_file))
    patv2_utils.sgf_name_train = utils.readlines(paths.concat(root, "train.lst"))
    patv2_utils.sgf_name_test = utils.readlines(paths.concat(root, "test.lst"), true)

    --[[
    local sgf_name_all = utils.readlines(paths.concat(root, "GoGoD2015.lst"))
    local sgf_name_train_hash = { }
    for i = 1, #sgf_name_train do
    sgf_name_train_hash[ sgf_name_train[i] ] = true
    end
    local sgf_name_test = { }
    for i = 1, #sgf_name_all do
    local filename = sgf_name_all[i]
    if not sgf_name_train_hash[filename] then
    table.insert(sgf_name_test, filename)
    end
    end
    print(string.format("Total = %d, Train = %d, Test = %d", #sgf_name_all, #sgf_name_train, #sgf_name_test)) 
    ]]

    print(string.format("Train = %d, Test = %d", #patv2_utils.sgf_name_train, patv2_utils.sgf_name_test and #patv2_utils.sgf_name_test or 0)) 

    if opt then
        if not opt.num_games_train or opt.num_games_train < 0 then opt.num_games_train = #patv2_utils.sgf_name_train end
        opt.num_games_train = math.min(opt.num_games_train, #patv2_utils.sgf_name_train)

        if patv2_utils.sgf_name_test then
            if not opt.num_games_test or opt.num_games_test < 0 then opt.num_games_test = #patv2_utils.sgf_name_test end
            opt.num_games_test = math.min(opt.num_games_test, #patv2_utils.sgf_name_test)
        end
        return opt
    end
end

function patv2_utils.retrieve_sgf(filename)
    local game
    for k, v in pairs(patv2_utils.data_loader:findKeysAndRead{ keys = { filename } }) do
        local sgf_file = sgf.parse(v.value, k)
        if sgf_file ~= nil and sgf_file:has_moves() and sgf_file:get_boardsize() == common.board_size 
            -- and sgf[1].WR and sgf[1].BR and sgf[1].WR:sub(2, 2) == 'd' and sgf[1].BR:sub(2, 2) == 'd' 
            then
                game = sgf_file
                break
        end
    end
    return game
end

function patv2_utils.get_moves(game)
    local moves = { } 
    game:play(function (move, counter) 
        local x, y, player = sgf.parse_move(move, false, true)
        if x and y and player then 
            local move_str, player_str = goutils.compose_move_gtp(x, y, player) 
            local m = common.xy2coord(x, y)
            table.insert(moves, {m, player})
            return true
        end
    end)
    return moves
end

function patv2_utils.setup_board(game)
    if not game then return end

    local b = board.new()
    board.clear(b)

    goutils.apply_handicaps(b, game, true)

    local n = game:get_total_moves()
    game:play(function (move, counter) 
        local x, y, player = sgf.parse_move(move, false, true)
        if x and y and player then
            board.play(b, x, y, player)
            -- board.show(b, 'last_move')
            return true
        end
    end, start_n)

    return b, game
end

function patv2_utils.save2sgf(init_b, moves, filename)
    local blacks, whites = board.get_black_white_stones(init_b) 
    local header = {
        blacks = blacks,
        whites = whites
    }

    local f = assert(io.open(filename, "w"))
    f:write(sgf.sgf_string(header, moves))
    f:close()
end

return patv2_utils
