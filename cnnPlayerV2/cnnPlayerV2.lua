--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local utils = require 'utils.utils'

utils.require_torch()
utils.require_cutorch()

local goutils = require 'utils.goutils'
local common = require("common.common")
local sgfloader = require 'utils.sgf'
local pl = require 'pl.import_into'()
local board = require 'board.board'

-- Let's follow the gtp protocol.
-- Load a model and wait for the input. 

local opt = pl.lapp[[
    -i,--input             (default "./models/df2.bin")          Input CNN models.
    -f,--feature_type      (default "old")                       By default we only test old features:
    -r,--rank              (default "9d")                        We play in the level of rank.
    -c,--usecpu                                                  Whether we use cpu to run the program.
]]

-- opt.feature_type and opt.userank are necessary for the game to be played.
opt.userank = true

local b = board.new()
local board_initialized = false
-- Load the trained CNN models.
-- Send the signature
-- print("Loading model = " .. opt.input)
local model = torch.load(opt.input)

io.stderr:write("CNNPlayerV2")

-- Adhoc strategy, if they pass, we pass.
local enemy_pass = false 
--
-- not supporting final_score in this version
-- Return format: 
--     command correct: true/false
--     output string: 
--     whether we need to quit the program.
local commands = {
    boardsize = function (board_size) 
        local s = tonumber(board_size)
        if s ~= board.get_board_size(b) then 
            error(string.format("Board size %d is not supported!", s))
        end
        return true
    end,
    clear_board = function () 
        board.clear(b) 
        enemy_pass = false
        board_initialized = true
        return true
    end,
    komi = function(komi) 
        io.stderr:write("The current algorithm has no awareness of komi. Nevertheless, it can still play the game.")
        -- return board.set_komi(b) 
        return true
    end,
    play = function(p, coord)
        -- Receive what the opponent plays and update the board.
        -- Alpha + number
        if not board_initialized then error("Board should be initialized!!") end
        local x, y, player = goutils.parse_move_gtp(coord, p)
        if not board.play(b, x, y, player) then
            error("Illegal move from the opponent!")
        end
        board.show(b)
        print(" ")

        if goutils.is_pass(x, y) then enemy_pass = true end 
        return true
    end,
    genmove = function(player)
        if not board_initialized then error("Board should be initialized!!") end
        -- If enemy pass then we pass.
        if enemy_pass then
            return true, "pass"
        end

        -- Call CNN to get the move.
        -- First extract features
        player = (player:lower() == 'w') and common.white or common.black
        local sortProb, sortInd = goutils.play_with_cnn(b, player, opt, opt.rank, model)
 
        -- Apply the moves until we have seen a valid one.
        local xf, yf, idx = goutils.tryplay_candidates(b, player, sortProb, sortInd)

        local move
        if xf == nil then
            io.stderr:write("Error! No move is valid!")
            -- We just pass here.
            move = "pass"
            -- Play pass here.
            board.play(b, 1, 1, player)
        else
            move = goutils.compose_move_gtp(xf, yf)   
            -- Don't use any = signs.
            io.stderr:write(string.format("idx: %d, x: %d, y: %d, movestr: %s", idx, xf, yf, move))
            -- Actual play this move
            if not board.play(b, xf, yf, player) then
                io.stderr:write("Illegal move from move_predictor! move = " .. move)
            end
        end

        -- Show the current board 
        board.show(b)
        print(" ")

        -- Tell the GTP server we have chosen this move
        return true, move
    end,
    name = function () return true, "go_player_v2" end,
    version = function () return true, "version 1.0" end,
    tsdebug = function () return true, "not supported yet" end,
    protocol_version = function () return true, "0.1" end,
    quit = function () return true, "Byebye!", true end,
    -- final_score = function () return 0 end,
}

-- Add list_commands and known_command
local all_commands = {}
for k, _ in pairs(commands) do table.insert(all_commands, k) end
local all_commands_str = table.concat(all_commands, "\n")

commands.list_commands = function () 
    return true, all_commands_str    
end

commands.known_command = function (c) return true, type(commands[c]) == 'function' and "true" or "false" end

-- Begin the main loop
while true do
    local line = io.read()
    local content = pl.utils.split(line)

    local cmdid = ''
    if string.match(content[1], "%d+") then
        cmdid = table.remove(content, 1)
    end
       
    local command = table.remove(content, 1)
    local successful, outputstr, quit

    if commands[command] == nil then
        print("Warning: Ignoring unknown command - " .. line)
    else
        successful, outputstr, quit = commands[command](unpack(content))
    end

    if successful then 
        if outputstr == nil then outputstr = '' end
        print(string.format("=%s %s\n\n\n", cmdid, outputstr))
    else
        print(string.format("?%s ???\n\n\n", cmdid))
    end
    io.flush()
    if quit then break end
end

-- Remove models and perform a few garbage collections
model = nil
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()

