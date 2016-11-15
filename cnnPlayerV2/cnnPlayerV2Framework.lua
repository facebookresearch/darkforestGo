--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local goutils = require 'utils.goutils'
local utils = require('utils.utils')
local common = require("common.common")
local sgfloader = require 'utils.sgf'
local board = require 'board.board'
local om = require 'board.ownermap'
local dp_simple = require('board.default_policy')
local dp_pachi = require('pachi_tactics.moggy')
local dp_v2 = require('board.pattern_v2')

local pl = require 'pl.import_into'()

local class = require 'class'

-- Let's follow the gtp protocol.
local cnnplayer = class('CNNPlayerV2')

-- Handicap according to the number of stones.
local handicaps = {
    [2] = "D4 Q16",
    [3] = "D4 Q16 Q4",
    [4] = "D4 Q16 D16 Q4",
    [5] = "*4 K10",
    [6] = "*4 D10 Q10",
    [7] = "*4 D10 Q10 K10",
    [8] = "*4 D10 Q10 K16 K4",
    [9] = "*8 K10",
    [13] = { "*9 G13 O13 G7 O7", "*9 C3 R3 C17 R17" },
}

local disable_time_left = false

local function parse_handicap(num_stone, stone_list)
    local hlist = handicaps[num_stone]
    if type(hlist) == 'table' then
        -- Randomly pick one.
        hlist = hlist[math.random(#hlist)]
    end
    local tokens = pl.stringx.split(hlist)
    for _, token in ipairs(tokens) do
        if token:sub(1, 1) == '*' then
            parse_handicap(tonumber(token:sub(2, -1)), stone_list)
        else
            table.insert(stone_list, token)
        end
    end
end

local function verify_player(b, player)
    if player ~= b._next_player then
        local supposed_player = (b._next_player == common.white and 'W' or 'B')
        local curr_player = (player == common.white and 'W' or 'B')
        print(string.format("Wrong player! The player is supposed to be %s but actually is %s...", supposed_player, curr_player))
        return false
    else
        return true
    end
end

-- shuffle for array-like table
local function shuffle_list(list)
    for i=1,#list do
        local pos = math.random(#list)
        list[i], list[pos] = list[pos], list[i]
    end
    return true
end

function cnnplayer:time_left(color, num_seconds, num_moves)
    local thiscolor = (color:lower() == 'w' or color:lower() == 'white') and common.white or common.black
    if self.mycolor and thiscolor == self.mycolor and num_seconds and num_moves then
        io.stderr:write(string.format("timeleft -- color: %s, num_seconds: %s, num_moves: %s", color, num_seconds, num_moves))
        if self.cbs.on_time_left then
            if not disable_time_left then
                self.cbs.on_time_left(tonumber(num_seconds), tonumber(num_moves))
            else
                print("Time left was disabled")
            end
        end
    else
        io.stderr:write(string.format("enemy timeleft -- color: %s, num_seconds: %s, num_moves: %s", color, num_seconds, num_moves))
    end

    return true
end

function cnnplayer:add_to_sgf_history(x, y, player)
    table.insert(self.sgf_history, { x, y, player })
    if self.opt.save_sgf_per_move then
        self:save_sgf(string.format("game-%d.sgf", self.b._ply - 1))
    end
end

function cnnplayer:undo_sgf_history()
    local x, y, player = unpack(table.remove(self.sgf_history))
    if self.opt.save_sgf_per_move then
        self:save_sgf(string.format("game-%d.sgf", self.b._ply - 1))
    end
    return x, y, player
end

function cnnplayer:save_sgf(filename)
    -- Save the current history to sgf.
    local f = io.open(filename, "w")
    if not f then
        return false, "file " .. filename .. " cannot be opened"
    end
    local header = {
        komi = self.val_komi,
        handi = self.val_handi,
        rule = self.rule
    }
    f:write(sgfloader.sgf_string(header, self.sgf_history))
    f:close()
    io.stderr:write("Sgf " .. filename .. " saved.\n")
    return true
end

function cnnplayer:boardsize(board_size)
    if board_size == nil then return false end
    local s = tonumber(board_size)
    if s ~= board.get_board_size(b) then
        error(string.format("Board size %d is not supported!", s))
    end
    return true
end

function cnnplayer:show_board()
    board.show_fancy(self.b, "all_rows_cols")
    return true
end

function cnnplayer:game_info()
    io.stderr:write(string.format("Komi: %.1f, Handi: %.1f", self.val_komi, self.val_handi))
    io.stderr:write(string.format("Move ply: %d", self.b._ply))
    board.show_fancy(self.b, 'show_all')
    self:score(true)
    return true
end

function cnnplayer:verbose(verbose_level)
    -- Change verbose level.
    if self.cbs.set_verbose_level then
        self.cbs.set_verbose_level(tonumber(verbose_level))
        return true
    else
        return false, "verbose level cannot be set!"
    end
end

function cnnplayer:dump_board()
    board.dump(self.b)
    return true
end

function cnnplayer:default_policy(max_depth, verbose)
    if max_depth == nil then return false, "Max depth must be specified!" end
    local b2 = board.copyfrom(self.b)
    max_depth = tonumber(max_depth)
    local fast_score = self.dp.run(self.def_policy, b2, max_depth, verbose == "true")
    io.stderr:write(string.format("Fast_score: %f", fast_score))
    -- Incomplete default policy
    if max_depth > 0 then
        io.stderr:write(string.format("Warning: since max_depth = %d, the score might not be accurate", max_depth))
    end
    board.show_fancy(b2, 'last_move')
    return true
end

function cnnplayer:clear_board()
    -- To prevent additional overhead for clear_board twice.
    if not self.board_history or #self.board_history > 0 or self.b._ply > 1 then
        board.clear(self.b)
        self.board_initialized = true
        self.board_history = { }
        self.sgf_history = { }

        -- Default value.
        self.val_komi = 6.5
        self.val_handi = 0
        self.mycolor = nil
        -- Call the new game callback when the board is cleaned.
        if self.cbs.new_game then
            self.cbs.new_game()
        end
    end
    return true
end

function cnnplayer:show_board_history()
    if not self.board_history then
        return true, "no board history"
    end
    for i = 1, #self.board_history do
        print("History " .. i)
        board.show_fancy(self.board_history[i], "all_rows_cols")
    end
    print("Current board: ")
    board.show_fancy(self.b, "all_rows_cols")
    return true, "Total board history: " .. #self.board_history
end

function cnnplayer:setup_board(filename, till_move, donnot_flip_vertical)
    donnot_flip_vertical = donnot_flip_vertical or false
    self:clear_board()
    -- Load the sgf file and play until till_move
    io.stderr:write("Loading " .. filename .. " board flip: " .. tostring(donnot_flip_vertical))
    local content = io.open(filename)
    if content == nil then
        return false, "File " .. filename .. " cannot be loaded"
    end
    local game = assert(sgfloader.parse(content:read("*a")))
    goutils.apply_handicaps(self.b, game, true)
    -- If HA != 0, then we need to apply more handicap
    self.val_handi = game:get_handi_count()
    -- Setup komi count.
    self.val_komi = game:get_komi() or 6.5
    if self.cbs.set_komi then
        self.cbs.set_komi(self.val_komi + self.val_handi)
    end

    till_move = till_move ~= nil and tonumber(till_move)
    local moves = { }
    game:play(function (move, counter)
        -- Vertically flip it so that sgf in KGS looks the same as the one show in terminal.
        local x, y, player = sgfloader.parse_move(move, true, donnot_flip_vertical)

        table.insert(moves, {x, y, player})
        local c, player_str = goutils.compose_move_gtp(x, y, player)

        local orig_move_str = move.B or move.W

        local move_str = player_str .. ' ' .. c .. '[' .. orig_move_str .. ']'

        -- require 'fb.debugger'.enter()
        if counter > self.val_handi then
            io.stderr:write("Move: " .. move_str .. "\n")
            board.play(self.b, x, y, player)
            -- self:play(player_str, c, false)
        else
            io.stderr:write("Handicap: " .. move_str)
            board.place_handicap(self.b, x, y, player)
        end
        if move.C then
            io.stderr:write("\n---------------------\n")
            io.stderr:write(move.C)
            io.stderr:write("\n---------------------\n")
        end
        if self.cbs.get_value then
            self.cbs.get_value(self.b, player)
        end
        return true
    end, till_move)
    io.stderr:write("All move imported\n")
    -- Finally we set the board.
    if self.cbs.set_board then
        self.cbs.set_board(self.b)
    end
    -- Set move history, it has to be after set_board
    if self.cbs.set_move_history then
        self.cbs.set_move_history(moves)
    end
    if self.cbs.adjust_params_in_game then
        self.cbs.adjust_params_in_game(self.b)
    end

    -- Save the current game.
    self.game = game
    self.sgf_history = moves
    board.show_fancy(self.b, "all_rows_cols")
    return true
end

function cnnplayer:extract_win_rate(filename, run_from, run_to, save_to)
    -- Extract the win rate of a game and save it to a text file.
    save_to = save_to or 'win_rate.txt'
    filename = filename or "*"

    local f = io.open(save_to, "w")
    if f == nil then
        print("open file " .. save_to .. " error!")
        return false
    end
    run_from = run_from or 1
    run_to = run_to or -1

    -- require 'fb.debugger'.enter()

    -- Setup the board.
    if filename ~= '*' then
        self:setup_board(filename, tonumber(run_from), false)
    elseif not self.game then
        return false, "Game is not loaded!"
    end

    -- Actually run the game
    self.game:play(function (move, counter)
        local x, y, player = sgfloader.parse_move(move, false, true)
        local c, player_str = goutils.compose_move_gtp(x, y, player)

        if x and y and player then
            local res, suggest_move, win_rate = self:genmove(player_str)
            if suggest_move ~= c then
                self:undo()
                self:play(player_str, c, false)
            end
            local print_str = string.format("[%d] Suggest: %s %s, Winrate: %f\nActual move: %s %s",
                                            self.b._ply - 1, player_str, suggest_move, win_rate, player_str, c)
            io.stderr:write(print_str)
            f:write(print_str .. "\n")
            return true
        end
    end, tonumber(run_to), true)

    f:close()
    io.stderr:write("All win rates computed and saved.")
    return true
end

function cnnplayer:run_cmds(filename, run_to)
    -- Run the command sequence.
    local lines = pl.utils.readlines(filename)
    for i, line in ipairs(lines) do
        if not line then break end
        if run_to and i > tonumber(run_to) then break end
        io.stderr:write(line)
        local ret, quit = self:getCommands(line, "io")
        io.stderr:write(ret)
        io.flush()
        if quit then
            return true, "", true
        end
    end
    return true
end

function cnnplayer:komi(komi_val)
    self.val_komi = tonumber(komi_val)
    if self.cbs.set_komi then
        self.cbs.set_komi(komi_val + self.val_handi)
    end
    return true
end

function cnnplayer:set_direct_handicap(handi_moves, isGive)
    self.val_handi = #handi_moves
    if self.cbs.set_board then
        self.cbs.set_board(self.b)
    end
    if self.cbs.set_komi then
        if isGive then
            self.cbs.set_komi(self.val_komi + self.val_handi, -1 * self.val_handi)
        else
            self.cbs.set_komi(self.val_komi + self.val_handi, self.val_handi)
        end
    end
    -- Finally set the handicap stones in the history
    --if self.cbs.set_move_history then
    --    self.cbs.set_move_history(handi_moves)
    --end
    --
    for _, m in ipairs(handi_moves) do
        table.insert(self.sgf_history, m)
    end
end

function cnnplayer:peek_simulation(num_simulations)
    -- Set the number of simulations for peek command.
    if num_simulations == nil or tonumber(num_simulations) == nil then
        return false, "Invalid number of simulations " .. num_simulations
    end
    if self.cbs.peek_simulation then
        self.cbs.peek_simulation(tonumber(num_simulations))
        return true, num_simulations
    else
        return false, "The function cbs.peek_simulation not set"
    end
end

function cnnplayer:place_free_handicap(num_stones)
    num_stones = tonumber(num_stones)
    if num_stones == nil then
        return false, "invalid argument"
    end
    if handicaps[num_stones] == nil then
        return false, "invalid handicap, #stone = " .. num_stones
    end
    if #self.board_history > 0 then
        return false, "board is not empty!"
    end
    -- Parse handicap and place all stones.
    local hlist = { }
    parse_handicap(num_stones, hlist)
    -- Set handicap to the board. It is always black stone.
    local moves = { }
    local handi_cnt = 0
    for _, l in ipairs(hlist) do
        handi_cnt = handi_cnt + 1
        local x, y, player = goutils.parse_move_gtp(l, 'B')
        table.insert(moves, {x, y, player})
        board.play(self.b, x, y, common.black)
        if handi_cnt < num_stones then
            board.play(self.b, 0, 0, common.white) -- play pass
        end
    end
    self:set_direct_handicap(moves, false)
    return true, table.concat(hlist, " ")
end

function cnnplayer:set_free_handicap(...)
    local hlist = { ... }
    if #self.board_history > 0 then
        return false, "board is not empty!"
    end

    --shuffle_list(hlist)
    local handi_count = 0
    local moves = { }
    for _, l in ipairs(hlist) do
        handi_count = handi_count + 1
        local x, y, player = goutils.parse_move_gtp(l, 'B')
        table.insert(moves, {x, y, player})
        board.play(self.b, x, y, common.black)
        if handi_count < #hlist then
            board.play(self.b, 0, 0, common.white) -- play pass
        end
    end
    self:set_direct_handicap(moves, true)
    return true
end

function cnnplayer:attention(left_top, right_bottom)
    -- Set attention for the player.
    if self.cbs.set_attention then
        local x_left, x_right, y_top, y_bottom
        local p = 'B'
        if left_top ~= nil then
            x_left, y_top, player = goutils.parse_move_gtp(left_top, p)
        else
            x_left, y_top = 1, 1
        end
        if right_bottom ~= nil then
            x_right, y_bottom, player = goutils.parse_move_gtp(right_bottom, p)
        else
            x_right, y_bottom = common.board_size, common.board_size
        end
        if x_left > x_right then x_left, x_right = x_right, x_left end
        if y_top > y_bottom then y_top, y_bottom = y_bottom, y_top end

        -- io.stderr:write(string.format("Attention = [%d, %d, %d, %d]", x_left, y_top, x_right, y_bottom))
        self.cbs.set_attention(x_left, y_top, x_right, y_bottom)
        return true
    end
end

function cnnplayer:undo()
    -- Simple undo command.
    if not self.board_history or #self.board_history == 0 then
        return true, "Nothing to undo"
    end
    -- Set the board.
    local undone_move = self.b._last_move
    self.b = table.remove(self.board_history)
    -- Notify the callback if there is any
    if self.cbs.undo_func then
        self.cbs.undo_func(self.b, undone_move)
    end
    local x, y, player = self:undo_sgf_history()
    local move_str, player_str = goutils.compose_move_gtp(x, y, player)
    io.stderr:write(string.format("undo move: %s %s, now ply: %d", player_str, move_str, self.b._ply))
    board.show_fancy(self.b, 'all_rows_cols')
    return true
end

function cnnplayer:play(p, coord, show_board)
    -- Receive what the opponent plays and update the board.
    -- Alpha + number
    local t_start = common.wallclock()

    if not self.board_initialized then error("Board should be initialized!!") end
    local x, y, player = goutils.parse_move_gtp(coord, p)

    if not verify_player(self.b, player) then
        return false, "Invalid move!"
    end

    -- Save the history.
    table.insert(self.board_history, board.copyfrom(self.b))
    if not board.play(self.b, x, y, player) then
        io.stderr:write(string.format("Illegal move from the opponent! x = %d, y = %d, player = %d", x, y, player))
        return false, "Invalid move"
    end

    if show_board then
        board.show_fancy(self.b, "all_rows_cols")
    end

    if self.cbs.move_receiver then
        self.cbs.move_receiver(x, y, player)
    end

    -- Check if we need to adjust parameters in the engine.
    if self.cbs.adjust_params_in_game then
        self.cbs.adjust_params_in_game(self.b)
    end
    io.stderr:write("Time spent in play " .. self.b._ply .. " : " ..  common.wallclock() - t_start)
    self:add_to_sgf_history(x, y, player)

    return true
end

function cnnplayer:score(show_more)
    if self.val_komi == nil or self.val_handi == nil then
        return false, "komi or handi is not set!"
    end
    -- Computing final score could be cpu hungry, so we need to stop computation if possible
    if self.cbs.thread_switch then
        self.cbs.thread_switch("off")
    end

    local score, livedead, territory, scores = om.util_compute_final_score(
           self.ownermap, self.b, self.val_komi + self.val_handi, nil,
           function (b, max_depth) return self.dp.run(self.def_policy, b, max_depth, false) end
    )

    local min_score = scores:min()
    local max_score = scores:max()
    local stones = om.get_territorylist(territory)

    io.stderr:write(string.format("Score (%s): %f, Playout min: %f, Playout max: %f, #dame: %d", self.opt.default_policy, score, min_score, max_score, #stones.dames));
    if show_more then
        -- Show the deadstone.
        local dead_stones = om.get_deadlist(livedead)
        local dead_stones_info = table.concat(dead_stones.b_str, " ") .. " " .. table.concat(dead_stones.w_str, " ")
        io.stderr:write("Deadstones info:")
        io.stderr:write(dead_stones_info)
        om.show_deadstones(self.b, livedead)

        io.stderr:write("Black prob:")
        om.show_stones_prob(self.ownermap, common.black)
        io.stderr:write("White prob:")
        om.show_stones_prob(self.ownermap, common.white)
    end

    if self.cbs.thread_switch then
        self.cbs.thread_switch("on")
    end
    return true, tostring(score), false, { score = score, min_score = min_score, max_score = max_score, num_dame = #stones.dames, livedead = livedead }
end

-- Check candidate pattern move.
function cnnplayer:pattern_next(max_heap_size)
    max_heap_size = max_heap_size or 10
    -- Call
    board.show_fancy(self.b, 'last_move')
    local be = self.dp.new(self.def_policy, self.b)
    print(self.dp.dump_status(be, tonumber(max_heap_size)))
    self.dp.free(be)
    return true
end

function cnnplayer:g()
    local player = self.b._next_player == common.black and 'b' or 'w'
    return self:genmove(player)
end

function cnnplayer:p(coord)
    local player = self.b._next_player == common.black and 'b' or 'w'
    return self:play(player, coord, true)
end

function cnnplayer:genmove(player)
    local t_start = common.wallclock()
    if not self.board_initialized then
        return false, "Board should be initialized!!"
    end
    if player == nil then
        return false, "Player should not be null"
    end
    player = (player:lower() == 'w' or player:lower() == 'white') and common.white or common.black
    if not self.mycolor then
        self.mycolor = player
    end
    if not verify_player(self.b, player) then
        return false, "Invalid move!"
    end
    -- Save the history.
    table.insert(self.board_history, board.copyfrom(self.b))

    -- Do not pass until after 140 ply.
    -- After that, if enemy pass then we pass.
    if self.b._ply >= 140 and goutils.coord_is_pass(self.b._last_move) then
        -- If the situation has too many dames, we don't pass.
        local _, _, _, stats = self:score()
        if stats.num_dame < 5 then
            -- Play pass here.
            board.play(self.b, 0, 0, player)
            if self.cbs.move_receiver then
                self.cbs.move_receiver(0, 0, player)
            end
            return true, "pass"
        end
    end

    -- If we are pretty sure we screwed up, resign.
    local full_ply = math.floor((self.b._ply + 1) / 2)
    if full_ply > 70 and full_ply % 5 == 1 then
        -- Check every 5 rounds.
        io.stderr:write("Check whether we have screwed up...")
        local resign_thres = 10
        local _, _, _, scores = self:score()
        if (player == common.white and scores.min_score > resign_thres) or (player == common.black and scores.max_score < -resign_thres) then
            return true, "resign"
        end
        if scores.min_score == scores.max_score and scores.max_score == scores.score then
            -- The estimation is believed to be absolutely correct.
            if (player == common.white and scores.score > 0.5) or (player == common.black and scores.score < -0.5) then
                return true, "resign"
            end
        end
    end

    -- Call move predictor to get the move.
    io.stderr:write("Start genmove. signature: " .. utils.get_signature())
    local xf, yf, win_rate = self.cbs.move_predictor(self.b, player)
    if win_rate and win_rate < self.opt.win_rate_thres then
        io.stderr:write(string.format("No hope, win_rate %f", win_rate))
        return true, "resign"
    end

    if xf == nil then
        io.stderr:write("Warning! No move is valid!")
        -- Play pass here.
        xf, yf = 0, 0
    end

    local move = goutils.compose_move_gtp(xf, yf)
    -- Don't use any = signs.
    local win_rate_str = win_rate and string.format("%f", win_rate) or "unknown"
    io.stderr:write(string.format("x: %d, y: %d, movestr: %s, win_rate: %s", xf, yf, move, win_rate_str))
    -- Actual play this move
    if not board.play(self.b, xf, yf, player) then
        error("Illegal move from move_predictor! move: " .. move)
    end

    -- Show the current board
    board.show_fancy(self.b, 'all_rows_cols')
    io.stderr:write("Time spent in genmove " .. self.b._ply .. " : " ..  common.wallclock() - t_start)
    self:add_to_sgf_history(xf, yf, player)

    -- Keep this win rate.
    self.win_rate = win_rate

    -- Tell the GTP server we have chosen this move
    return true, move, win_rate
end

function cnnplayer:peek(topk)
    topk = not topk and 3 or tonumber(topk)
    if not self.cbs.move_peeker then
        return false, "Unsupported command: peek " .. topk
    end
    local moves = self.cbs.move_peeker(self.b, self.b._next_player, topk)
    local s = ""
    for i = 1, topk do
        local m = moves[i]
        local move = goutils.compose_move_gtp(m.x, m.y)
        s = s .. string.format("%s %f %f; ", move, m.n, m.win_rate)
    end
    return true, s
end

function cnnplayer:winrate()
    return true, self.win_rate and string.format("%.4f", self.win_rate) or "unknown"
end

function cnnplayer:name()
    return true, self.name
end

function cnnplayer:version()
    return true, self.version
end

function cnnplayer:final_score()
    local res, _, _, stats = self:score()

    if not res then
        return false, "error in computing score"
    end
    local score = stats.score

    -- Compute the final score with single threaded default policy..
    --[[
    print("Compute final score")
    print(self.val_komi)
    board.show(self.b, "all")
    ]]
    local s =  score > 0 and string.format("B+%.1f", score) or string.format("W+%.1f", -score)
    return true, s
end

function cnnplayer:final_status_list(subcommand)
    if subcommand == 'dead' then
        -- Report dead stones.
        -- require 'fb.debugger'.enter()
        -- io.stderr:write("compute final score!")
        local res, _, _, stats = self:score()
        -- io.stderr:write("get deadlist")
        local stones = om.get_deadlist(stats.livedead)

        -- io.stderr:write("Return the string")
        -- Return the string for the deadstones.
        local s = table.concat(stones.b_str, " ") .. " " .. table.concat(stones.w_str, " ")
        return true, s
    else
        return false
    end
end

function cnnplayer:tsdebug()
    return true, "not supported yet"
end

function cnnplayer:protocol_version()
    return true, "0.1"
end

function cnnplayer:quit()
    if self.cbs.quit_func then
        self.cbs.quit_func()
    end
    return true, "Byebye!", true
end

function cnnplayer:list_commands()
    return true, self.all_commands_str
end

function cnnplayer:known_command(c)
    return true, type(cnnplayer[c]) == 'function' and "true" or "false"
end

function cnnplayer:run(command, ...)
    return cnnplayer[command](self, ...)
end

function cnnplayer:__init(splash, name, version, callbacks, opt)
    self.b, self.board_initialized = board.new(), false
    -- not supporting final_score in this version
    -- Return format:
    --     command correct: true/false
    --     output string:
    --     whether we need to quit the program.
    -- Add list_commands and known_command by simple reflection.
    local exclusion_list = { new = true, mainloop = true, getCommands = true, run = true, set_direct_handicap = true, score = true }
    local all_commands = {}
    for k, v in pairs(getmetatable(cnnplayer)) do
        if not exclusion_list[k] and k:sub(1, 1) ~= '_' and type(v) == 'function' then
            table.insert(all_commands, k)
        end
    end
    table.insert(all_commands, "kgs-game_over") -- dummy function, will be used to log off from kgs
    self.all_commands_str = table.concat(all_commands, "\n")

    -- set callbacks
    -- possible callback:
    -- 1. move_predictor(board, player)
    --    call move_predictor when the bot is asked to generate a move. This is mandatory.
    -- 2. move_receiver(x, y, player)
    --    call move_receiver when the bot receives opponent moves.
    -- 3. new_game()
    --    When the client receives the comamnd of clear_board
    -- 4. undo_func(prev_board, undone_move)
    --    When the client click undo
    -- 5. set_board(new_board)
    --    Called when setup_board/clear_board is invoked
    -- 6. set_komi(komi)
    --    When komi's set.
    -- 7. quit_func()
    --    When qutting.
    -- 8. thread_switch("on" or "off")
    --    Switch on/off the computation process.
    -- 9. set_move_history(moves) moves = { {x1, y1, player1}, {x2, y2, player2} }..
    --    Set the history of the game. Called when setup_board is invoked.
    --10. set_attention(x_left, y_top, x_right, y_bottom)
    --    Set the attention of the engine (So that the AI will focus on the region more).
    --11. adjust_params_in_game(board_situation)
    --    Depending on the board situation, change the parameters.
    --12. set_verbose_level(level)
    --    Set the verbose level
    --13. on_time_left(sec, num_move)
    --    On time left.

    local valid_callbacks = {
        move_predictor = true,
        move_receiver = true,
        move_peeker = true,
        new_game = true,
        undo_func = true,
        set_board = true,
        set_komi = true,
        quit_func = true,
        thread_switch = true,
        set_move_history = true,
        set_attention = true,
        adjust_params_in_game = true,
        set_verbose_level = true,
        get_value = true,
        on_time_left = true,
        peek_simulation = true
    }

    assert(callbacks)
    assert(callbacks.move_predictor)

    --[[
    if not callbacks.move_peeker then
        print("Warning! peeker is absent!")
    end
    ]]

    -- Check if there is any misnaming.
    for k, f in pairs(callbacks) do
        if not valid_callbacks[k] then error("The callback function " .. k .. " is not valid") end
        if type(f) ~= 'function' then error("Callback " .. k .. " is not a function!") end
    end

    self.cbs = callbacks
    self.name = name
    self.version = version
    self.ownermap = om.new()
    -- Opt
    local default_opt = {
        win_rate_thres = 0.0,
        default_policy = 'v2',
        default_policy_pattern_file = '../models/playout-model.bin',
        default_policy_temperature = 0.125,
        default_policy_sample_topn = -1,
        save_sgf_per_move = false,
    }
    if opt then
        self.opt = utils.add_if_nonexist(pl.tablex.deepcopy(opt), default_opt)
    else
        self.opt = default_opt
    end

    -- default to chinese rule
    local rule = (opt and opt.rule == "jp") and board.japanese_rule or board.chinese_rule
    self.rule = opt.rule

    if self.opt.default_policy == 'v2' then
        self.dp = dp_v2
        self.def_policy = self.dp.init(self.opt.default_policy_pattern_file, rule)
        self.dp.set_sample_params(self.def_policy, self.opt.default_policy_sample_topn, self.opt.default_policy_temperature)
    elseif self.opt.default_policy == 'pachi' then
        self.dp = dp_pachi
        self.def_policy = self.dp.new(rule)
    elseif self.opt.default_policy == 'simple' then
        self.dp = dp_simple
        -- self.def_policy = self.dp.new_with_params( { opponent_in_danger = false, our_atari = false, nakade = false, pattern = false })
        self.def_policy = self.dp.new(rule)
    end

    io.stderr:write(splash)

    if self.opt.setup_board and self.opt.setup_board ~= "" then
        local items = pl.stringx.split(self.opt.setup_board)
        self:setup_board(items[1], items[2], items[3])
    end
    if self.opt.exec and self.opt.exec ~= "" then
        local valid, _, quit = self:run_cmds(self.opt.exec)
        if quit then
            os.exit()
        end
    end
end

-- Begin the main loop
function cnnplayer:mainloop()
    local mode = "io"
    while true do
        local line = io.read()
        if line == nil then break end
        local ret, quit = self:getCommands(line, mode)
        print(ret)
        io.flush()
        if quit == true then break end
    end
end

function cnnplayer:getCommands(line, mode)
    if line == nil then
        return false
    end
    local content = pl.utils.split(line)
    if #content == 0 then
        return false
    end
    local cmdid = ''
    if string.match(content[1], "%d+") then
        cmdid = table.remove(content, 1)
    end

    local command = table.remove(content, 1)
    local successful, outputstr, quit
    if type(cnnplayer[command]) ~= 'function' then
        print("Warning: Ignoring unknown command - " .. line)
    else
        successful, outputstr, quit = self:run(command, unpack(content))
    end
    local ret
    if successful then
        if outputstr == nil then outputstr = '' end
        ret = string.format("=%s %s\n", cmdid, outputstr)
    else
        ret = string.format("?%s ??? %s\n", cmdid, outputstr)
    end
    if mode == "io" then ret = ret.."\n\n" end
    return ret, quit
end
return cnnplayer
