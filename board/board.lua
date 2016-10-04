--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local ffi = require 'ffi'
local pl = require 'pl.import_into'()
local utils = require 'utils.utils'
local common = require 'common.common'

-- local image = require 'image'

-- local symbols, s = utils.ffi_include(paths.concat(common.lib_path, "board/board.h"))
local script_path = common.script_path()
local symbols, s = utils.ffi_include(paths.concat(script_path, "board.h"))
-- print(s)
local board = {}

board.pass = tonumber(symbols.M_PASS)
board.resign = tonumber(symbols.M_RESIGN)
board.chinese_rule = tonumber(symbols.RULE_CHINESE)
board.japanese_rule = tonumber(symbols.RULE_JAPANESE)

local C = ffi.load(paths.concat(script_path, "../libs/libboard.so"))
board.C = C
board.s = s
board.symbols = symbols
board.script_path = script_path

local ids = ffi.new("GroupId4")

local debugging = false

local function dprintf(...)
    if debugging then
        print(string.format(...))
    end
end

function board.print_info()
    -- Print relevant information
    print("----Go board for LUA version 1.0----")
    local b = board.new()
    print(string.format("sizeof(Board) = %d", ffi.sizeof(b)))
    print(string.format("sizeof(Group) = %d", ffi.sizeof("Group")))
    print(string.format("sizeof(Info) = %d", ffi.sizeof("Info")))
    print(string.format("sizeof(GroupId4) = %d", ffi.sizeof("GroupId4")))
    print(string.format("sizeof(Board._infos) = %d", ffi.sizeof(b._infos)))
    print(string.format("sizeof(Board._groups) = %d", ffi.sizeof(b._groups)))

    print("-----------------------------------")
end

function board.get_board_size()
    return common.board_size
end

function board.new(n)
    dprintf("Start board.new()")
    if n == nil then
        return ffi.new("Board")
    elseif type(n) == 'number' then
        return ffi.new("Board[?]", n)
    end
end

function board.copyfrom(b)
    local bb = ffi.new("Board")
    ffi.copy(bb, b, ffi.sizeof("Board"))
    return bb
end

function board.copyfrom2(bb, b)
    ffi.copy(bb, b, ffi.sizeof("Board"))
end

function board.clear(b)
    dprintf("Start board.clear()")
    C.ClearBoard(b)
end

function board.show(b, choice)
    dprintf("Start board.show()")
    if choice == "last_move" then
        C.ShowBoard(b, tonumber(symbols.SHOW_LAST_MOVE))
    elseif choice == "all" then
        C.ShowBoard(b, tonumber(symbols.SHOW_ALL))
    else
        C.ShowBoard(b, tonumber(symbols.SHOW_NONE))
    end
    print " "
end

function board.show_fancy(b, choice)
    dprintf("Start board.show()")
    if choice == "last_move" then
        C.ShowBoardFancy(b, tonumber(symbols.SHOW_LAST_MOVE))
    elseif choice == "all" then
        C.ShowBoardFancy(b, tonumber(symbols.SHOW_ALL))
    elseif choice == 'all_rows_cols' then
        C.ShowBoardFancy(b, tonumber(symbols.SHOW_ALL_ROWS_COLS))
    else
        C.ShowBoardFancy(b, tonumber(symbols.SHOW_NONE))
    end
    print " "
end
 

function board.dump(b)
    dprintf("Start board.dump()")
    C.DumpBoard(b)
    C.VerifyBoard(b)
end

-- This one returns Coords.
function board.get_all_stones(b) 
    local black_moves = ffi.new("AllMoves")
    local white_moves = ffi.new("AllMoves")
    C.GetAllStones(b, black_moves, white_moves)
    local res = { }
    for i = 0, black_moves.num_moves - 1 do
        table.insert(res, { black_moves.moves[i], common.black })
    end
    for i = 0, white_moves.num_moves - 1 do
        table.insert(res, { white_moves.moves[i], common.white })
    end
    return res
end

function board.get_black_white_stones(b) 
    local black_moves = ffi.new("AllMoves")
    local white_moves = ffi.new("AllMoves")
    C.GetAllStones(b, black_moves, white_moves)
    local blacks = { }
    for i = 0, black_moves.num_moves - 1 do
        table.insert(blacks, black_moves.moves[i])
    end
    local whites = { }
    for i = 0, white_moves.num_moves - 1 do
        table.insert(whites, white_moves.moves[i])
    end
    return blacks, whites
end

function board.is_true_eye(b, x, y, player)
    return C.IsTrueEyeXY(b, x - 1, y - 1, player) == common.TRUE
end

function board.is_self_atari(b, x, y, player)
    local num_stone = ffi.new("int[1]")
    if C.TryPlay(b, x - 1, y - 1, player, ids) == common.TRUE then
        local is_self_atari = C.IsSelfAtariXY(b, ids, x - 1, y - 1, player, num_stone) == common.TRUE
        return is_self_atari, tonumber(num_stone[0])
    end
end

function board.is_move_giving_simple_ko(b, x, y, player)
    if C.TryPlay(b, x - 1, y - 1, player, ids) == common.TRUE then
        return C.IsMoveGivingSimpleKo(b, ids, player) == common.TRUE
    end
end 

function board.tryplay(b, x, y, player)
    dprintf("Start board.tryplay()")
    -- print(x, y)
    return C.TryPlay(b, x - 1, y - 1, player, ids) == common.TRUE
end

function board.play(b, x, y, player)
    dprintf("Start board.play()")
    -- print(x, y)
    if C.TryPlay(b, x - 1, y - 1, player, ids) == common.TRUE then
        C.Play(b, ids)
        return true
    else
        return false
    end
end

function board.place_handicap(b, x, y, player)
    return C.PlaceHandicap(b, x - 1, y - 1, player) == common.TRUE
end

function board.play2(b, m)
    dprintf("Start board.play()")
    -- print(x, y)
    if C.TryPlay2(b, m, ids) == common.TRUE then
        C.Play(b, ids)
        return true
    else
        return false
    end
end

function board.opponent(player)
    return 3 - player
end

function board.get_ply(b)
    return b._ply
end

function board.is_game_end(b)
    return C.IsGameEnd(b) == common.TRUE
end

-- Feature extractors ---
function board.get_liberties_map(b, player)
    dprintf("Start board.get_liberties_map()")
    local liberties = torch.FloatTensor(19, 19)
    C.GetLibertyMap(b, player, liberties:data())
    return liberties
end

function board.get_stones(b, player)
    local stones = torch.FloatTensor(19, 19)
    C.GetStones(b, player, stones:data())
    return stones
end

function board.get_simple_ko(b, player)
    local simple_ko = torch.FloatTensor(19, 19)
    C.GetSimpleKo(b, player, simple_ko:data())
    return simple_ko
end

function board.get_history(b, player)
    local history = torch.FloatTensor(19, 19)
    C.GetHistory(b, player, history:data())
    return history
end

function board.get_distance_map(b, player)
    local distance_map = torch.FloatTensor(19, 19)
    C.GetDistanceMap(b, player, distance_map:data())
    return distance_map
end

function board.get_stones_bbox(b)
    local r = ffi.new("Region")
    C.GetBoardBBox(b, r)
    return r
end

function board.expand_region(r, margin)
    r.left = math.max(r.left - margin, 0)
    r.top = math.max(r.top - margin, 0)
    r.right = math.min(r.right + margin, common.board_size)
    r.bottom = math.min(r.bottom + margin, common.board_size)
end

function board.get_attacker(b, r) 
    -- From the bounding box, we could get the likely attacker.
    return C.GuessLDAttacker(b, r)
end

-- Check whether the play at (x, y) by player is the victim of ladder.
function board.check_ladder(b, x, y, player)
    dprintf("Start board.is_ladder()")
    -- print(x, y)
    if C.TryPlay(b, x - 1, y - 1, player, ids) == common.TRUE then
        return C.CheckLadder(b, ids, player)
    end
end

-- Scoring
function board.get_fast_score(b, rule)
    rule = rule or board.chinese_rule
    return C.GetFastScore(b, rule)
end

--[[
function board.show_map(m)
    local name = m:type()
    return image.hflip(m:float()):transpose(1, 2):type(name)
end
]]

function board.show_compact_map(m)
    assert(m:size(1) == 19)
    assert(m:size(2) == 19)
    local s = ""
    for i = 1, 19 do
        s = s .. "   "
        for j = 1, 19 do
            local v = m[j][20 - i] 
            if v == common.black then 
                s = s .. 'x '
            else
                s = s .. 'o '
            end
        end
        s = s .. "\n"
    end
    print(s)
end

return board
