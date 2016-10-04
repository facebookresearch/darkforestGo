--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant
-- of patent rights can be found in the PATENTS file in the same directory.
--

require 'torch'
require 'cutorch'
require 'nn'
require 'cunn'
require 'cudnn'
require 'nngraph'

require 'xlua'

require 'train.rl_framework.infra.forwardmodel'
local common = require 'common.common'
local board = require 'board.board'
local sgfloader = require 'utils.sgf'
local goutils = require 'utils.goutils'

local pl = require 'pl.import_into'()
local argcheck = require 'argcheck'
local tnt = require 'torchnet'

local fm_go = { }

local FMGo, ForwardModel = torch.class('fm_go.FMGo', 'rl.ForwardModel', fm_go)

local function protected_play(b, game)
    local x, y, player = sgfloader.parse_move(game:play_current(), false)
    if player ~= nil and board.play(b, x, y, player) then
        game:play_next()
        return true
    else
        return false
    end
end

function FMGo:get_sa(b, game)
    -- Now we have a valid situation, Extract feature for the current game.
    local x, y, player = sgfloader.parse_move(game:play_current())
    local rank
    if self.opt.userank then
        local br, wr = game:get_ranks(self.opt.datasource)
        rank = (player == common.white) and wr or br
        if rank == nil then rank = '9d' end
    end
    -- require 'fb.debugger'.enter()
    local feature = goutils.extract_feature(self.b, player, self.opt, rank, game.dataset_info)
    local style = 0
    if self.data_augmentation then
        style = torch.random(0, 7)
        feature = goutils.rotateTransform(feature, style)
    end

    -- Check if we see any NaN.
    if feature:ne(feature):sum() > 0 or move ~= move then
        print(feature)
        print(move)
        require('fb.debugger').enter()
    end

    local sample
    local nstep = self.opt.nstep
    -- DCNN model.
    local moves = torch.LongTensor(nstep)
    local xys = torch.LongTensor(nstep, 2)
    for i = 1, nstep do
        local x, y, player = sgfloader.parse_move(game:play_current(i - 1))
        local x_rot, y_rot = goutils.rotateMove(x, y, style)
        moves[i] = goutils.xy2moveIdx(x_rot, y_rot)
        if moves[i] < 1 or moves[i] > 361 then
            board.show(self.b, 'last_move')
            print("Original loc")
            print(x)
            print(y)

            print("rotated")
            print(x_rot)
            print(y_rot)
            print(player)
            print(moves[i])
            error("Move invalid!")
        end

        xys[i][1] = x_rot
        xys[i][2] = y_rot

        --[[
        print(string.format("game: idx = %d/%d", self.sample_idx, self.dataset:size()))
        board.show(self.b, 'last_move')
        print(x)
        print(y)
        local coord_str, player_str = goutils.compose_move_gtp(x, y, player)
        print(player_str .. " " .. coord_str)
        print(feature[10])
        print(x_rot)
        print(y_rot)
        require 'fb.debugger'.enter()
        ]]
    end

    return {
        s = feature,
        a = moves,
        xy = xys,
        ply = self.game.ply,
        sgf_idx = self.sample_idx
    }
end

FMGo.__init = argcheck{
    {name="self", type="fm_go.FMGo"},
    {name="dataset", type="tnt.IndexedDataset"},
    {name="partition", type="string"},
    {name="opt", type="table"},
    call = function(self, dataset, partition, opt)
        self.dataset = dataset
        self.opt = opt
        self.data_augmentation = (partition == "train" and opt.data_augmentation)

        self.b = board.new()
        self:load_random_game(true)
    end
}

FMGo.load_random_game = argcheck{
    {name="self", type="fm_go.FMGo"},
    {name="apply_random_moves", type="boolean", default=false, opt=true},
    call = function(self, apply_random_moves)
        local game
        local sample_idx
        while true do
            sample_idx = math.random(self.dataset:size())
            local sample = self.dataset:get(sample_idx)
            for k, v in pairs(sample) do
                sample = v
                break
            end
            -- require 'fb.debugger'.enter()
            local content = sample.table.content
            local filename = sample.table.filename
            game = sgfloader.parse(content:storage():string(), filename)
            if game ~= nil and game:has_moves() and game:get_boardsize() == common.board_size and game:play_start() then
                board.clear(self.b)
                goutils.apply_handicaps(self.b, game)

                local game_play_through = true
                if apply_random_moves then
                    local round = math.random(game:num_round()) - 1
                    for j = 1, round do
                        if not protected_play(self.b, game) then
                            game_play_through = false
                            break
                        end
                    end
                end
                if game_play_through then break end
            end
        end

        -- require 'fb.debugger'.enter()

        -- Get the final result of the game.
        game.player_won = game:get_result_enum()
        game.dataset_info = self.opt.datasource

        -- Then we start from the beginning.
        self.game = game
        self.sample_idx = sample_idx

        --[[
        print(string.format("New game: idx = %d/%d", self.sample_idx, self.dataset:size()))
        board.show(self.b, 'last_move')
        ]]

        self.move_counter = 1
        self.max_move_counter = self.opt.max_move_counter and self.opt.max_move_counter or 500
    end
}

FMGo.reset = argcheck{
    {name="self", type="fm_go.FMGo"},
    call = function(self)
        -- Reset don't do anything, game transition handled internally.
        return
    end
}

-- forward() -> s, a
-- Here we only return s and a.
FMGo.forward = argcheck{
    {name="self", type="fm_go.FMGo"},
    {name="action_index", type="number"},
    call = function(self, action_index)
        -- action_index will be omitted.
        local nstep = self.opt.nstep
        -- Next game if reload is needed.
        -- [FIXME] Note that for now we don't count the final move, since after final move, there is nothing to predict next
        -- Well we could always predict resign/pass.
        local game_restarted = false
        repeat
            if game_restarted or self.game:play_get_ply() >= self.game:play_get_maxply() - nstep + 1 then
                self:load_random_game()
                game_restarted = false
            else
                if not protected_play(self.b, self.game) then
                    game_restarted = true
                else
                    self.move_counter = self.move_counter + 1
                    if self.move_counter >= self.max_move_counter then
                        game_restarted = true
                    end
                end
            end
            -- Check whether it is a valid situation.
            if not game_restarted then
                for i = 1, nstep do
                    local x1, y1, player = sgfloader.parse_move(self.game:play_current(i - 1), false)
                    -- player ~= nil: It is a valid move
                    -- y1 > 0: it is not a pass/(0, 0) or resign/(1, 0)
                    -- Sometime pass = (20, 20) so we need to address this as well.
                    if player == nil or y1 == 0 or y1 == common.board_size + 1 then
                        game_restarted = true
                        break
                    end
                end
            end
        until not game_restarted

        -- require 'fb.debugger'.enter()
        -- No nonterminal state.
        return self:get_sa(self.b, self.game)
    end
}

return fm_go
