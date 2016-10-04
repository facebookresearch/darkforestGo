--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

local goutils = {}
local pl = require 'pl.import_into'()
local common = require("common.common")
local sgfloader = require('utils.sgf')
local board = require('board.board')
require 'image'
-- Feature extractor, usage:
-- goutils.get_features(b, t)
--    wher t is the table that contains features.
--

function goutils.get_old_features(b, player)
    -- we should flip the board and color so we are always black (THIS IS NOT TRUE for player code)
    -- we should calculate liberties at each position
    -- we should get ko
    -- planes we should write:
    -- 0: our stones with 1 liberty
    -- 1: our stones with 2 liberty
    -- 2: our stones with 3 or more liberties
    -- 3: their stones with 1 liberty
    -- 4: their stones with 2 liberty
    -- 5: their stones with 3 or more liberty
    -- 6: simple ko
    -- 7: my stone
    -- 8: enemy stone
    -- 9: empty place
    -- 10: my history (-1 means black komi; the number in a position is its moveIdx; the removed stone will still save its last time position, unless this position is occupied again).
    -- 11: enemy history
    local history_decay = 0.1

    local our_liberties = board.get_liberties_map(b, player)
    local our_stones = board.get_stones(b, player)
    local our_history = board.get_history(b, player)

    local opponent = board.opponent(player)
    local enemy_liberties = board.get_liberties_map(b, opponent)
    local enemy_stones = board.get_stones(b, opponent)
    local enemy_history = board.get_history(b, opponent)

    local empty_place = board.get_stones(b, 0)
    local simple_ko = board.get_simple_ko(b, player)

    -- Make some transform.
    local features = torch.FloatTensor(12, 19, 19)
    features[1]:copy(our_liberties:eq(1))
    features[2]:copy(our_liberties:eq(2))
    features[3]:copy(our_liberties:ge(3))

    features[4]:copy(enemy_liberties:eq(1))
    features[5]:copy(enemy_liberties:eq(2))
    features[6]:copy(enemy_liberties:ge(3))

    features[7]:copy(simple_ko)
    features[8]:copy(our_stones)
    features[9]:copy(enemy_stones)

    local curr_ply = board.get_ply(b)
    -- Note that for entries with no stones, its ply would be zero.
    our_history:add(-curr_ply):mul(history_decay):exp()
    our_history:cmul(our_stones)

    enemy_history:add(-curr_ply):mul(history_decay):exp()
    enemy_history:cmul(enemy_stones)

    features[10]:copy(empty_place)
    features[11]:copy(our_history)
    features[12]:copy(enemy_history)
    return features
end

local function feature_liberties(b, player)
    local liberties = board.get_liberties_map(b, player)
    local features = torch.FloatTensor(3, 19, 19)
    features[1]:copy(liberties:eq(1))
    features[2]:copy(liberties:eq(2))
    features[3]:copy(liberties:ge(3))
    return features
end

local function feature_decay_history(b, player)
    local history = board.get_history(b, player)
    local curr_ply = board.get_ply(b)
    local history_decay = 0.1
    -- Note that for entries with no stones, its ply would be zero.
    history:add(-curr_ply):mul(history_decay):exp()
    return history
end

local function distance_transform(m)
    assert(m, "Distance transform: Input matrix is empty!!")
    -- Compute a distance transform for a binary matrix m
    local dt = m:clone()
    dt:apply(function (x) if x == 1 then return 0 else return math.huge end end)
    -- Two pass algorithm
    -- First dimension.
    for i = 2, dt:size(1) do
        for j = 1, dt:size(2) do
            dt[i][j] = math.min(dt[i - 1][j] + 1, dt[i][j])
        end
    end
    for i = 1, dt:size(1) - 1 do
        for j = 1, dt:size(2) do
            dt[i][j] = math.min(dt[i + 1][j] + 1, dt[i][j])
        end
    end
    -- Second dimension
    for i = 1, dt:size(1) do
        for j = 2, dt:size(2) do
            dt[i][j] = math.min(dt[i][j - 1] + 1, dt[i][j])
        end
    end
    for i = 1, dt:size(1) do
        for j = 1, dt:size(2) - 1 do
            dt[i][j] = math.min(dt[i][j + 1] + 1, dt[i][j])
        end
    end
    return dt
end

-- One-time static features
local static_position_mask
local static_border_mask

local feature_mapping = {
    liberties = {
        function (b, player) return feature_liberties(b, player) end, 3
    },
    history = function (b, player) return feature_decay_history(b, player) end,
    stones = function (b, player) return board.get_stones(b, player) end,
    simpleko = function (b, player) return board.get_simple_ko(b, player) end,
    -- Position mask: the closer a position is to the corner, the higher the value.
    position_mask = function (b, player)
        if not static_position_mask then
            local s = common.board_size
            static_position_mask = torch.FloatTensor(s, s)
            local c = (s + 1) / 2
            for i = 1, s do
                for j = 1, s do
                    static_position_mask[i][j] = (i - c) ^2 + (j - c) ^2
                end
            end
            static_position_mask:mul(-0.5):exp()
        end
        return static_position_mask
    end,
    -- Return whether a location is on the border of the board.
    border = function (b, player)
        if not static_border_mask then
            local s = common.board_size
            static_border_mask = torch.FloatTensor(s, s)
            static_border_mask:zero()
            static_border_mask[1]:fill(1.0)
            static_border_mask[s]:fill(1.0)
            static_border_mask[{{}, {s}}]:fill(1.0)
            static_border_mask[{{}, {1}}]:fill(1.0)
        end
        return static_border_mask
    end,
    -- Return the color each empty site becomes to
    closest_color = {
        function (b, player, named_features)
            -- print(named_features)
            -- local our_dist = distance_transform(named_features['our stones']):view(1, common.board_size, common.board_size)
            -- local opponent_dist = distance_transform(named_features['opponent stones']):view(1, common.board_size, common.board_size)
            local our_dist = board.get_distance_map(b, player)
            local opponent_dist = board.get_distance_map(b, board.opponent(player))
            -- Check which distance is smaller for the border.
            return torch.cat(our_dist:lt(opponent_dist), opponent_dist:lt(our_dist), 1):float()
        end, 2
    },
    attention = {
        function (b, player, named_features, dataset_info)
            local s = common.board_size
            if type(dataset_info) == 'string' then
                if dataset_info == 'tsumego' then
                    -- In this case, we only focus on the rectangle with stones, with some margins.
                    local our_dist = board.get_distance_map(b, player)
                    local opponent_dist = board.get_distance_map(b, board.opponent(player))
                    local dist_to_all_stones = torch.cmin(our_dist, opponent_dist)
                    -- Threshold the distance to get the attention.
                    return dist_to_all_stones:lt(5):float()
                else
                    return torch.FloatTensor(s, s):fill(1.0)
                end
            elseif type(dataset_info) == 'table' then
                -- Attention region.
                local a = dataset_info
                local attention = torch.FloatTensor(s, s):fill(0.0)
                -- print(string.format("Attention in goutils = sub(%d, %d), sub(%d, %d)", a[1], a[3], a[2], a[4]))
                attention[{{a[1], a[3]}, {a[2], a[4]}}]:fill(1.0)
                -- require 'fb.debugger'.enter()
                return attention
            else
                error("dataset_info is invalid!")
            end
        end, 1
    }
}

local player_mapping = {
    our = function (player) return player end,
    opponent = function (player) return board.opponent(player) end,
    empty = function (player) return 0 end,
}

local function split_cmds(f, player)
    local ss = pl.utils.split(f)
    local player_cmd, feature_cmd
    local actor
    if #ss == 2 then
        player_cmd, feature_cmd = unpack(ss)
        actor = player_mapping[player_cmd](player)
    else
        feature_cmd = ss[1]
        actor = player
    end
    local func = feature_mapping[feature_cmd]
    local dim = 1
    -- print(func)
    if type(func) == 'table' then
        dim = func[2]
        func = func[1]
    end
    return actor, func, dim
end

function goutils.get_feature_dim(fnames)
    local d = 0
    for _, f in ipairs(fnames) do
        local actor, func, n = split_cmds(f, common.black)
        d = d + n
    end
    return d
end

-- Example input:
--   fnames = { "our_stones", "opponent_stones" }
function goutils.get_features(b, player, fnames, dataset_info)
    local named_features = { }
    local dim = goutils.get_feature_dim(fnames)
    local features = torch.FloatTensor(dim, common.board_size, common.board_size)
    local idx = 0
    for _, f in ipairs(fnames) do
        -- print(f)
        local actor, func, n = split_cmds(f, player)
        features[{{idx + 1, idx + n}, {}, {}}]:copy(func(b, actor, named_features, dataset_info))

        -- Add named feature index.
        if n == 1 then
            named_features[f] = features[idx + 1]
        else
            for k = 1, n do
                named_features[f .. ' ' .. tostring(k)] = features[idx + k]
            end
        end
        idx = idx + n
    end
    return features, named_features
end

local features_list = {
    complete = {
        "our liberties", "opponent liberties", "our simpleko", "our stones", "opponent stones", "empty stones", "our history", "opponent history"
    },
    extended = {
        "our liberties", "opponent liberties", "our simpleko", "our stones", "opponent stones", "empty stones", "our history", "opponent history",
        "border", 'position_mask', 'closest_color'
    },
    extended_with_attention = {
        "our liberties", "opponent liberties", "our simpleko", "our stones", "opponent stones", "empty stones", "our history", "opponent history",
        "border", 'position_mask', 'closest_color', 'attention'
    },
}

function goutils.addGrade(feature, grade)
    local channel
    if grade == nil or grade=='None' or grade=='none' or string.sub(grade,-1,-1)=='k' then
        channel = 1
    elseif string.sub(grade,-1,-1)=='p' then
        channel = 9
    else
        --assert(string.sub(grade,-1,-1)=='d', string.format('get rank wrong, %s \n', grade))
        --channel = tonumber(string.sub(grade,1,-2))
        if string.sub(grade, -1, -1) == 'd' then
            channel = tonumber(string.sub(grade,1,-2))
            if channel then
                assert(channel>0 and channel<10, string.format('get channel wrong, %d \n', channel))
            else
                channel = 1
            end
        else
            channel = 1
        end
    end
    local goban_size = feature:size(2)
    local rank = torch.FloatTensor(9, goban_size, goban_size):zero()
    rank[channel]:fill(1)
    return torch.cat(feature, rank, 1)
end

-- Opt:
--    userank = true/false
--    type = [old, complete, extended, extended_with_attention]
-- If opt.attention exists and opt.feture_type == 'extended_with_attention", then get_feature will use the attention region
function goutils.extract_feature(b, player, opt, rank, dataset_info)
    local features, named_features
    if opt.feature_type == 'old' then
        -- Legancy features..
        features = goutils.get_old_features(b, player)
        named_features = {
            ["our stones"] = features[8],
            ["opponent stones"] = features[9],
            ["empty stones"] = features[10],
        }
    else
        if opt.feature_type == 'extended_with_attention' and opt.attention then
            dataset_info = opt.attention
        end
        features, named_features = goutils.get_features(b, player, features_list[opt.feature_type], dataset_info)
    end
    if opt.userank then
        return goutils.addGrade(features, rank), named_features
    else
        return features, named_features
    end
end

function goutils.extract_feature_dim(opt)
    local feature_dim
    if opt.feature_type == "old" then
        -- Legancy code, old features are 12 dimensions.
        feature_dim = 12
    else
        feature_dim = goutils.get_feature_dim(features_list[opt.feature_type])
    end
    if opt.userank then feature_dim = feature_dim + 9 end
    return feature_dim
end

-- Transformations.
function goutils.rotateTransform(img, style)
    -- 8 possible transformations. style = 0 .. 7
    -- extract bits
    local h = math.floor(style / 4)
    local v = math.floor( (style - h * 4) / 2 )
    local t = style % 2

    if h == 1 then
        img = image.hflip(img)
    end

    if v == 1 then
        img = image.vflip(img)
    end

    if t == 1 then
        img = img:transpose(2, 3)
    end
    return img
end

function goutils.rotateMove(x, y, style)
    local h = math.floor(style / 4)
    local v = math.floor( (style - h * 4) / 2 )
    local t = style % 2

    if h == 1 then
        y = common.board_size + 1 - y
    end

    if v == 1 then
        x = common.board_size + 1 - x
    end

    if t == 1 then
        x, y = y, x
    end
    return x, y
end

local function rotate_table(game, style)
    if type(game) == 'table' or torch.typename(game) == 'tds.Hash' then
        for k, v in pairs(game) do
            game[k] = rotate_table(v, style)
        end
        return game
    elseif type(game) == 'string' and #game == 2 then
        -- Rotate the coordinate.
        local x, y = sgfloader.parse_move(game)
        x, y = goutils.rotateMove(x, y, style)
        local player_str, coord_str = sgfloader.compose_move(x, y)
        return coord_str
    else
        return game
    end
end

function goutils.rotateSgf(game, style)
    -- Rotate an SGF file.
    -- Recursively loop through to find any coordinates, and rotate them.
    return rotate_table(game, style)
end

function goutils.is_pass(x, y)
    return x == 0 and y == 0
end

function goutils.coord_is_pass(m)
    -- [TODO]: Ideally we need to load from C's symbol.
    return m == 0
end

function goutils.is_resign(x, y)
    return x == 1 and y == 0
end

function goutils.parse_move_gtp(coord, player)
    if not coord then return end
    coord = coord:lower()
    local x, y
    if coord == 'pass' then
        x, y = 0, 0
    elseif coord == 'resign' then
        x, y = 1, 0
    else
        x = coord:byte(1) - 97 + 1
        -- Note that gtp movement skip letter 'I', so we should adjust it accordingly.
        if x > 9 then x = x - 1 end
        y = tonumber(coord:sub(2, -1))
    end
    if player ~= nil then
        player = (player:lower() == 'w' or player:lower() == 'white') and common.white or common.black
    end
    return x, y, player
end

function goutils.compose_move_gtp(x, y, player)
    local player_str
    if player ~= nil then
        player_str = common.player_name[player]
    end
    local coord_str
    if y == 0 then
        if x == 0 or x == common.board_size + 1 then coord_str = "pass"
        elseif x == 1 then coord_str = "resign"
        else error(string.format("goutils.compose_move_gtp: Parsing error on (%d, %d, %d)!", x, y, player)); end
    else
        -- Note that gtp movement skip letter 'I', so we should adjust it accordingly.
        if x >= 9 then x = x + 1 end
        coord_str = string.char(x + 65 - 1) .. tostring(y)
    end

    return coord_str, player_str
end

function goutils.moveIdx2xy(idx)
    local x = math.floor((idx - 1) / common.board_size) + 1
    local y = (idx - 1) % common.board_size + 1
    return x, y
end

function goutils.xy2moveIdx(x, y)
    return (x - 1) * common.board_size + y
end

function goutils.apply_handicaps(b, sgf, hflip)
    local black_stones, white_stones = sgf:get_handicaps()
    if black_stones then
        for i = 1, #black_stones do
            if black_stones[i] ~= nil then
                board.place_handicap(b, sgfloader.parse_move('B' .. black_stones[i], true, hflip))
            end
        end
    end
    if white_stones then
         for i = 1, #white_stones do
             if white_stones[i] ~= nil then
                 board.place_handicap(b, sgfloader.parse_move('W' .. white_stones[i], true, hflip))
             end
        end
    end
end

function goutils.getDistillModel(model, T)
    assert(T>1)
    local distillModel = model:clone()
    distillModel:get(1):get(distillModel:get(1):size()).bias:div(T)
    distillModel:get(1):get(distillModel:get(1):size()).weight:div(T)

    return distillModel
end

function goutils.randomPlay(b, player, opt, rank, model)
    local probs = torch.rand(19*19, 1)
    local sortProb, sortInd = torch.sort(probs, 1, true)
    return sortProb, sortInd
end

function goutils.get_value(b, player, opt)
    local feature, named_features = goutils.extract_feature(b, player, opt, opt.rank)
    local nplane, h, w = unpack(feature:size():totable())
    if not opt.usecpu then
       feature = feature:cuda()
    end
    local value = opt.valueModel:forward(feature:view(1, nplane, h, w))
    value = player == common.black and value[1] or -value[1]
    return value
end

function goutils.batch_play_with_cnn(bs, opt, rank, model)
    local fempty, features
    local nplane, h, w
    local nbatch = #bs
    for i = 1, nbatch do
        local feature, named_features = goutils.extract_feature(bs[i], bs[i]._next_player, opt, rank)
        if not fempty then
            nplane, h, w = unpack(feature:size():totable())
            fempty = feature.new():resize(nbatch, h*w)
            features = feature.new():resize(nbatch, nplane, h, w)
        end
        fempty[i]:copy(named_features["empty stones"]:view(h*w))
        features[i]:copy(feature)
    end
    if not opt.usecpu then
       features = features:cuda()
    end

    -- Apply conv model.
    local output = model:forward(features)
    -- If the output is multitask, only take the first one.
    local output_move_pred = type(output) == 'table' and output[1] or output
    local probs = output_move_pred:exp():float():view(nbatch, h*w)

    -- We constrain that the next move is in an empty place.
    -- local filterProbs = probs
    local filterProbs = torch.min(torch.cat(fempty, probs, 3), 3)
    local sortProb, sortInd = torch.sort(filterProbs, 2, true)
    return sortProb, sortInd
end

function goutils.play_with_cnn(b, player, opt, rank, model)
    assert(b)
    assert(player)
    assert(opt)
    assert(rank)
    assert(model)
    local feature, named_features = goutils.extract_feature(b, player, opt, rank)
    local nplane, h, w = unpack(feature:size():totable())
    local fempty = named_features["empty stones"]:view(h*w)
    if not opt.usecpu then
       feature = feature:cuda()
    end

    -- Apply conv model.
    local output = model:forward(feature:view(1, nplane, h, w))
    -- If the output is multitask, only take the first one.
    local output_move_pred = type(output) == 'table' and output[1] or output
    local probs = output_move_pred:exp():float():view(h*w)

    -- We constrain that the next move is in an empty place.
    -- local filterProbs = probs
    local filterProbs = torch.min(torch.cat(fempty, probs, 2), 2)
    local sortProb, sortInd = torch.sort(filterProbs, 1, true)
    local value
    if opt.valueModel then
        -- the value is always on current player's view.
        -- Different from alpha go, which is always on a fix player's view, say black.
        value = opt.valueModel:forward(feature:view(1, nplane, h, w))
        value = player == common.black and value[1] or -value[1]
    else
        value = -2
    end
    -- We also give the original output, in case the caller needs anything more than the next move.
    return sortProb, sortInd, value, output
end

-- Sample from RNN model.
function goutils.sample_from_rnn(b, player, opt, rank, model)
    -- Extract the feature first.
    local feature, named_features = goutils.extract_feature(b, player, opt, rank)
    local nplane, h, w = unpack(feature:size():totable())
    if not opt.usecpu then
       feature = feature:cuda()
    end

    -- Apply the first model
    local cnn_model = model:get(1):get(1)
    local rnn_model = model:get(2)

    local seq_len = rnn_model.params.seq_length

    -- Apply the CNN model
    local representation = cnn_model:forward(feature:view(1, nplane, h, w))
    -- Then sample from RNN
    local moves = feature.new():resize(1, seq_len + 1):fill(common.board_size * common.board_size + 1)
    local second_rep = rnn_model:sample({representation, moves})

    -- Now we have the sampled move.
    return moves[1][{{2, seq_len + 1}}]
end

function goutils.check_move(b, x, y, player)
    -- Never play illegal moves.
    if not board.tryplay(b, x, y, player) then
        return false, "Not a valid move"
    end
    -- Never play self-atari if the loss is huge.
    local is_self_atari, num_stones = board.is_self_atari(b, x, y, player)
    if is_self_atari and num_stones >= 10 then
        return false, string.format("Self-atari with %d stone loss", num_stones)
    end
    -- Never play in a true eye. Maybe sometime we need to do that?
    -- if board.is_true_eye(b, x, y, player) then return false end
    -- Check whether it is a ladder move.
    local ladder_depth = board.check_ladder(b, x, y, player)
    -- If we don't have ladder (ladder_depth == 0) or the ladder is too short (maybe good tactics suggested by DCNN), we use the move.
    if ladder_depth >= 6 then
        return false, string.format("Ladder move with depth = %d", ladder_depth)
    end
    return true
end

function goutils.tryplay_candidates(b, player, sortProb, sortInd)
    local xf, yf, idx
    -- For debug reason, plot the first 20 candidates.
    --[[
    print('First 20 candidate moves...')
    for i = 1, 20 do
        local x, y = goutils.moveIdx2xy(sortInd[i][1])
        print(string.format("[%d]: (%d, %d) idx = %d, conf = %f", i, x, y, sortInd[i][1], sortProb[i][1]))
    end
    ]]
    for i = 1, sortInd:size(1) do
        local x, y = goutils.moveIdx2xy(sortInd[i][1])
        local check_res, comments = goutils.check_move(b, x, y, player)
        if check_res then
            -- print(string.format("Take x = %d, y = %d [conf = %f]", x, y, sortProb[i][1]));
            xf, yf, idx = x, y, sortInd[i][1]
            break
        else
            --print(string.format("Move x = %d, y = %d [conf = %f] is not valid, Reason = %s", x, y, sortProb[i][1], comments));
        end
    end
    return xf, yf, idx
end

function goutils.weightedSample(sortProb, sortInd, topk)
    --assert(sortProb:dim() == 2, string.format("sortProb:dim(): %d", sortProb:dim()))
    --assert(sortProb:size(1) > topk, string.format("sortProb:size(1): %d --  topk: %d", sortProb:size(1), topk))
    local cumProb = torch.cumsum(sortProb:sub(1,topk))
    local needle = torch.rand(1)[1]*cumProb[topk][1]

    local sampleInd = -1
    for i=1,cumProb:size(1) do
        if needle<cumProb[i][1] then
            sampleInd = i
            break
        end
    end
    -- sampleInd == -1 means no move to choose.
    return sampleInd
end

function goutils.tryplay_candidates_sample(b, player, sortProb, sortInd, topk)
    local xf, yf, idx
    local max_iter = 200
    local iter = 1
    while iter < max_iter do
        if topk >= 360 then
            return xf, yf, idx
        end
        local i = goutils.weightedSample(sortProb,sortInd, topk)
        if i == -1 then break end
        -- print("sample find: ", i)
        local x, y = goutils.moveIdx2xy(sortInd[i][1])
        local check_res, comments = goutils.check_move(b, x, y, player)
        if check_res then
            -- print(string.format("Take x = %d, y = %d [conf = %f]", x, y, sortProb[i][1]));
            xf, yf, idx = x, y, sortInd[i][1]
            break
        else
            --exclude this high prob ladder move from sample pool and bring in next candidate.
            -- print("ignoring: ", i)
            sortProb[i][1] = 0
            topk = topk + 1
            --print(string.format("Move x = %d, y = %d [conf = %f] is not valid, Reason = %s", x, y, sortProb[i][1], comments));
        end
        iter = iter +1
    end
    if iter >= max_iter then
        print("not find a valid move in ", max_iter, " iterations ..")
    end
    return xf, yf, idx
end

function goutils.checkSampleCondition(sortProb, topk, delta)
    if sortProb[1][1] - sortProb[topk][1] > delta then
        return false
    else
        return true
    end
end

function goutils.tryplay_candidates_sample_cond(b, player, sortProb, sortInd, topk, delta)
    if goutils.checkSampleCondition(sortProb, topk, delta) then
        --print("sampling..")
        --print("top3: ", sortProb[1][1]," | ", sortProb[2][1], " | ", sortProb[3][1])
        return goutils.tryplay_candidates_sample(b, player, sortProb, sortInd, topk)
    else
        return goutils.tryplay_candidates(b, player, sortProb, sortInd)
    end
end

return goutils
