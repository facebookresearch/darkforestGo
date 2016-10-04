--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

local pl = require 'pl.import_into'()
local class = require 'class'
local common = require("common.common")
local tds = require 'tds'

local sgfloader = class('sgfloader')

local function update_session(session, key, values)
   if session == nil then return end
   if #key > 0 and #values >= 1 then
       if session[key] == nil then 
           session[key] = {} 
       end
       for _, v in ipairs(values) do
           table.insert(session[key], v)
       end
   end
end

local function make_direct_mapping(t)
    if type(t) == 'table' then
        if #t == 1 and type(t[1]) ~= 'table' then return t[1] end
        local res = {}
        for k, v in pairs(t) do
            res[k] = make_direct_mapping(v)
        end
        return res
    else 
        return t
    end
end

local function convert_to_tds(t)
    if type(t) == 'table' then
        local res = tds.hash()
        for k, v in pairs(t) do
            res[k] = convert_to_tds(v)
        end
        -- Count the number of numerical terms.
        res.__count = #t
        return res
    else
        return t
    end
end

function print_tds_hash(t, indent)
    indent = indent or 0
    local spaces = string.rep(' ', indent)
    print(spaces .. "{")
    for k, v in pairs(t) do
        if (type(k) == 'string' and k:sub(1, 2) ~= '__') or type(k) == 'number' then
            if type(v) == 'cdata' then
                print(spaces .. '  ' .. k .. " : ")
                print_tds_hash(v, indent + 4)
            else
                print(spaces .. '  ' .. k .. " : " .. v)
            end
        end
    end
    print(spaces .. "}")
end

local function parse_internal(s, i)
    -- parse starting from i, 
    local res = { }
    local session 
    local key = ""
    local values = {}
    local ss = ""
    local in_bracket = false
    local in_backslash = false

    local is_str = type(s) == 'string'
    local len = is_str and #s or s:size(1)

    while true do
        local c = is_str and s:sub(i, i) or string.char(s[i])
        i = i + 1
        if c == '(' then break end
    end

    while i < len do
        local c = is_str and s:sub(i, i) or string.char(s[i])
        -- print(pl.pretty.write({c = c, in_bracket = in_bracket, in_backslash = in_backslash, session = session, key = key, values = values, ss = ss}))
        
        if in_backslash then
            ss = ss .. c
            in_backslash = false
        elseif c == "\\" then
            in_backslash = true
        elseif in_bracket then
            if c == "]" then
                in_bracket = false
                table.insert(values, ss)
                ss = ""
            else
                ss = ss .. c
            end
        elseif c == "\n" or c == "\r" or c == '\t' then
             -- Skip returns.
        else
            if c == "(" then
                update_session(session, key, values)
                if session and next(session) ~= nil then 
                    table.insert(res, session) 
                end
                session = {}
                key = ""
                values = {}
                -- Start a new parsing.
                subt, i_next = parse_internal(s, i)
                assert(subt, string.format("sgf parsing is wrong at loc = %d", i))
                i = i_next
                if next(subt) ~= nil then
                    subt.__name = 'session'
                    table.insert(res, subt)
                end
            elseif c == ";" or c == ")" then 
               -- Save previous session. 
               update_session(session, key, values)
               if session and next(session) ~= nil then 
                 table.insert(res, session) 
               end
               session = {}
               key = ""
               values = {}
               if c == ')' then 
                   break 
               end
            elseif c == "[" then
                -- There is a key in front of us.
                if #ss > 0 then 
                    -- Save previous keys.
                    update_session(session, key, values)
                    -- Start a new pairs of key and ss. 
                    key = ss
                    values = {}
                    ss = ""
                end
                in_bracket = true
            else
                ss = ss .. c
            end
        end
        i = i + 1
    end
    return res, i
end

function sgfloader.parse(s, key)
    -- Given a string, parse it and return a table.
    assert((type(s) == 'string' or torch.typename(s) == 'torch.ByteTensor'), pl.pretty.write(s))
    local res, counter = parse_internal(s, 1)
    if res == nil then return nil end
    res.__name = 'session'
    -- Then if loop through the tree, if the key has only one value, make it a direct dictionary.  
    local instance = sgfloader()
    instance.sgf = convert_to_tds(make_direct_mapping(res))
    instance.key = key
    instance.counter = counter
    return instance
end

-- simple loop on tds.hash
-- if it has nonzero __count, then treat it as array,
-- otherwise use ipairs
local function hash_iter(t)
    if t.__count and t.__count > 0 then
        local i = 0
        return function ()
            i = i + 1
            if i <= t.__count then return i, t[i] end
        end
    else
        return pairs(t)
    end
end

-- Save to a string that represents the SGF file.
local function save_internal(t, num_indent)
    local indent = ''
    for i = 1, num_indent do indent = indent .. ' ' end

    local s = indent .. "(\n"
    for k1, v1 in hash_iter(t) do
        s = s .. indent .. ";"
        if v1.__name == 'session' then
            s = s .. save_internal(v1, num_indent + 2)
        else
            if k1 ~= '__count' then
                for k2, v2 in hash_iter(v1) do
                    if type(k2) == 'string' and k2:sub(1, 2) ~= "__" then
                        s = s .. k2
                    end
                    if type(k2) ~= 'string' or k2:sub(1, 2) ~= '__' then
                        -- require 'fb.debugger'.enter()
                        if torch.type(v2) == 'tds.Hash' or type(v2) == 'table' then
                            for k3, v3 in hash_iter(v2) do
                                local ss = v3:gsub("%]", "\\]"):gsub("%[", "\\[")
                                s = s .. "[" .. ss .. "]"
                            end
                        else
                            local ss = v2:gsub("%]", "\\]"):gsub("%[", "\\[")
                            s = s .. "[" .. ss .. "]"
                        end
                    end
                end
            end
        end
        s = s .. "\n"
    end
    s = s .. indent .. ")"
    return s
end

function sgfloader:save()
    return save_internal(self.sgf, 0)
end

-- Move starts from 1.
local offset = 96

-- Input m could be a tds.hash or a string.
function sgfloader.parse_move(m, strict, hflip)
    strict = strict == nil and true
    hflip = hflip or false
    local x, y, player
    if type(m) == 'cdata' then
        if m.B ~= nil then
            if type(m.B) == 'string' then
                if #m.B == 2 then
                    x, y, player = m.B:byte(1) - offset, m.B:byte(2) - offset, common.black
                elseif #m.B == 0 then
                    -- Pass
                    -- [FIXME]: Any representation for resign?
                    x, y, player = 0, 0, common.black
                end
            end
        elseif m.W ~= nil then
            if type(m.W) == 'string' then
                if #m.W == 2 then
                    x, y, player = m.W:byte(1) - offset, m.W:byte(2) - offset, common.white
                elseif #m.W == 0 then
                    -- Pass
                    x, y, player = 0, 0, common.white
                end
            end
        end
    elseif type(m) == 'string' then
        -- [FIXME]: Don't know the string pattern for "pass/resign"
        if m:sub(1, 1) == 'W' or m:sub(1, 1) == 'w' then
            x, y, player = m:byte(2) - offset, m:byte(3) - offset, common.white
        elseif m:sub(1, 1) == 'B' or m:sub(1, 1) == 'b' then
            x, y, player = m:byte(2) - offset, m:byte(3) - offset, common.black
        end
    end
    if strict and (x == nil or y == nil or player == nil) then
        print(m)
        print_tds_hash(m)
        error("Cannot parse move!")
    end
    if hflip and y then y = 20 - y end
    return x, y, player
end

-- Return two string with move and player str.
function sgfloader.compose_move(x, y, player)
    local player_str = common.player_name[player]
    local coord_str
    if x == 0 and (y == 0 or y == 20) then
        coord_str = ''
    else
        coord_str = string.char(x + offset, y + offset)
    end
    return player_str, coord_str
end

function sgfloader.show_move(m)
    print_tds_hash(m)
end

function sgfloader.add_annotation(m, k, v)
    if m[k] == nil then
        m[k] = tds.hash()
        m[k].__count = 0
    end
    m[k].__count = m[k].__count + 1
    m[k][m[k].__count] = v
end

function sgfloader:show_info()
    assert(self.sgf and self.sgf[1])
    print_tds_hash(self.sgf[1])
end

function sgfloader:is_valid()
    return self ~= nil and self.sgf ~= nil and self.sgf[1] ~= nil
end

function sgfloader:has_moves()
    return self:is_valid() and self:num_round() >= 1
end

function sgfloader:num_round() 
    return self.sgf.__count - 1
end

function sgfloader:get_result() 
    return self.sgf[1].RE
end

function sgfloader:get_handi_count()
    if self.sgf[1].HA then 
        return tonumber(self.sgf[1].HA)
    else
        return 0
    end
end

function sgfloader:get_result_enum()
    local result = self:get_result()
    -- Three consequences: win/loss/not known
    local res = common.res_unknown 
    if result ~= nil then
        if result:sub(3, 6):lower() ~= 'time' then
            local s = result:sub(1, 1):lower()
            if s == 'b' then 
                res = common.black
            elseif s == 'w' then 
                res = common.white 
            end
        end
    end
    return res
end

local function reasonRank(r, datasource)
    if datasource=='gogod' then
        if r==nil then return "9d" end -- assume ancient players are highest
        local r2 = string.match(r, "%dd")
        if r2~=nil then return r2 end
        r2 = string.match(r, "%d+k")
        if r2 ~=nil then return "1d" end
        if string.match(r, "ama")~=nil or string.match(r, "Ama")~=nil or string.match(r, "%d+a")~=nil then
            return "1d"
        end
        return "9d"
    else
        return r
    end
end

function sgfloader:get_ranks(datasource)
    local br = reasonRank(self.sgf[1].BR, datasource)
    local wr = reasonRank(self.sgf[1].WR, datasource)
    return br, wr
end

function sgfloader:get_handicaps()
    return self.sgf[1].AB, self.sgf[1].AW
end

function sgfloader:get_boardsize() 
    if self.sgf[1].SZ == nil then return 19 end --make default board as 19*19
    return tonumber(self.sgf[1].SZ)
end

function sgfloader:get_komi()
    if self.sgf[1].KM then 
        return tonumber(self.sgf[1].KM)
    end
end

function sgfloader:key_str()
    return "key = " .. tostring(self.key)
end

function sgfloader:play_start(backtrace_push, backtrace_pop)
    assert(self.sgf, "SGF cannot be empty! " .. self:key_str())
    assert(self.sgf[1], "SGF header cannot be empty! " .. self:key_str())
    -- Check whether we have a move
    if self.sgf[2] == nil then 
        return false 
    end
    self.ply = 1
    self.var = self.sgf
    -- The stack is used to backtrace.
    self.stack = { }
    if type(backtrace_push) == 'function' then
        self.backtrace_push = backtrace_push
    end
    if type(backtrace_pop) == 'function' then
        self.backtrace_pop = backtrace_pop
    end
    self:play_next()
    return true
end

function sgfloader:play_current(n)
    -- Return the current play in the game tree
    -- print(self.var[self.ply])
    n = n or 0
    return self.var[self.ply + n]
end

function sgfloader:play_is_last()
    assert(self.var, 'SGF main variation should not be empty! ' .. self:key_str())
    return self.ply == self.var.__count
end

function sgfloader:play_get_ply()
    return self.ply
end

function sgfloader:play_get_maxply()
    return self.var.__count
end

function sgfloader:get_total_moves()
    if not self:play_start() then return end
    local counter = 1
    while true do
        local move = self:play_current()
        if not self:play_next() then break end  
        counter = counter + 1
    end
    return counter 
end

function sgfloader:play_set_last()
    assert(self.var, 'SGF main variation should not be empty! ' .. self:key_str())
    self.ply = self.var.__count
end

function sgfloader:play_next()
    while self:play_is_last() do 
        if self.backtrace_pop == nil or #self.stack == 0 then return false end
        -- Backtrace to a previous game situation.
        local last_status = table.remove(self.stack)
        self.var = last_status[1]
        self.ply = last_status[2]
        self.backtrace_pop()
    end
    -- go to the next move, if it is a session, we move to the next variation. 
    self.ply = self.ply + 1
    if self.var == nil then 
        print(pl.pretty.write(self))
        error('SGF main variation should not be empty! ' .. self:key_str())
    end
    local push_count = 0
    while self.var[self.ply].__name == 'session' do
        -- Save the return address.
        table.insert(self.stack, { self.var, self.ply })
        push_count = push_count + 1

        self.var = self.var[self.ply]

        -- Note that We might pick other ply (choose other variation). For now we will pick the first variation. 
        if self.var == nil then 
            print(pl.pretty.write(self))
            error('SGF main variation should not be empty! ' .. self:key_str())
        end

        self.ply = 1
    end

    if self.backtrace_push ~= nil then
        for i = 1, push_count do
            self.backtrace_push()
        end
    end
    return true
end

-- This works if there is no variations.
function sgfloader:play(func, num_moves, continue_play)
    if not continue_play and not self:play_start() then return false end
    if num_moves == 0 then return true end
    local max_ply = self:play_get_maxply()
    -- require 'fb.debugger'.enter()
    local counter = 1
    while true do
        local move = self:play_current()
        if func and not func(move, counter) then return false end
        if not self:play_next() then break end  
        counter = counter + 1
        if num_moves then 
            if num_moves > 0 and counter > num_moves then break end
            if num_moves < 0 and counter > max_ply + num_moves then break end
        end
    end
    return true
end

local function moves2sgf_str(moves, player)
    local s = ""
    for i = 1, #moves do
        local m = moves[i]
        local x, y = common.coord2xy(m)
        -- Flip the coordinate for saving.
        y = 20 - y
        local player_str, coord_str = sgfloader.compose_move(x, y, player)
        if i == 1 then s = player_str end
        s = s .. "[" .. coord_str .. "]" 
    end
    return s
end

-- Header
--   komi, player_w, player_b, date, result, blacks, whites
function sgfloader.sgf_string(header, moves)
    header = header or { }
    header.komi = header.komi or 6.5
    header.handi = header.handi or 0
    header.player_w = header.player_w or "Unknown"
    header.player_b = header.player_b or "Unknown"
    header.date = header.date or '2016-01-01'
    header.result = header.result or 'Unknown'
    header.rule = header.rule or 'Chinese'

    local s = string.format("(;SZ[19]KM[%.1f]PW[%s]PB[%s]DT[%s]RE[%s]RU[%s]", header.komi, header.player_w, header.player_b, header.date, header.result, header.rule)
    if header.handi > 0 then
        s = s .. 'HA[' .. tostring(header.handi) .. ']'
    end

    if header.blacks then
        s = s .. 'A' .. moves2sgf_str(header.blacks, common.black)
    end
    if header.whites then
        s = s .. 'A' .. moves2sgf_str(header.whites, common.white)
    end

    for i = 1, #moves do
        s = s .. ";"
        if moves[i][1] then
            local m, player
            if #moves[i] == 2 then
                -- {m, player}
                m = moves[i][1]
                player = moves[i][2]
            elseif #moves[i] == 3 then
                -- {x, y, player}
                m = common.xy2coord(moves[i][1], moves[i][2]) 
                player = moves[i][3]
            else
                print("Illegal format for moves[i]")
                require 'fb.debugger'.enter()
            end
            s = s .. moves2sgf_str( { m }, player )
        end
        for k, v in pairs(moves[i]) do
            if type(k) == 'string' then
                s = s .. k .. "[" .. v .. "]"
            end
        end
        if i < #moves then s = s .. "\n" end
    end
    s = s .. ")\n"
    return s
end

-- test.
-- print(sgf.parse(io.open("test.sgf"):read("*a")))
return sgfloader
