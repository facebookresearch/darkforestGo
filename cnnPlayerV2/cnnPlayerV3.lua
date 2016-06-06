--
-- Copyright (c) 2016-present, Facebook, Inc.
-- All rights reserved.
--
-- This source code is licensed under the BSD-style license found in the
-- LICENSE file in the root directory of this source tree. An additional grant 
-- of patent rights can be found in the PATENTS file in the same directory.
-- 

package.path = package.path .. ';../?.lua'

local pl = require 'pl.import_into'()
local dcnn_utils = require 'board.dcnn_utils'
local opt = pl.lapp[[
    --codename             (default "darkfores2") Code name for models.
    -f,--feature_type      (default "old")       By default we only test old features. If codename is specified, this is omitted.
    -r,--rank              (default "9d")        We play in the level of rank.
    --use_local_model                            Whether we just load local model from the current path
    -c,--usecpu                                  Whether we use cpu to run the program.
    --shuffle_top_n        (default 1)           We random choose one of the first n move and play it.  
    --debug                                      Wehther we use debug mode
    --exec                 (default "")         Whether we run an initial script 
    --setup_board          (default "")         Setup board. The argument is "sgfname moveto"
    --win_rate_thres       (default 0.0)        If the win rate is lower than that, resign.
    --sample_step          (default -1)         Sample at a particular step.
    --temperature          (default 1)
    --presample_codename   (default "darkforest")
    --presample_ft         (default "old")
    --valueModel           (default "../models/value_model.bin")
    --verbose                                    Whether we print more information
]]
--for k,v in pairs(opt) do
--    print(k, v)
--end

local common = require("common.common")
local CNNPlayerV2 = require 'cnnPlayerV2.cnnPlayerV2Framework'

if opt.debug then 
    dcnn_utils.dbg_set()
end

local dcnn_opt = dcnn_utils.init(opt)
local callbacks = { }
function callbacks.move_predictor(b, player)
    return dcnn_utils.sample(dcnn_opt, b, player)
end

function callbacks.get_value(b, player)
    local value = -1
    if dcnn_opt.valueModel then
        value= dcnn_utils.get_value(dcnn_opt, b, player)
    end
    print("value: ".. string.format("%.3f", value))
end

function callbacks.new_game()
    collectgarbage()
    collectgarbage()
end

function callbacks.set_attention(x_left, y_top, x_right, y_bottom)
    -- Set the attention region if the feature_type is extended_with_attention
    if opt.feature_type == "extended_with_attention" then
        opt.attention = { x_left, y_top, x_right, y_bottom }
    end
end

local opt2 = {
    rule = opt.rule,
    exec = opt.exec,
    setup_board = opt.setup_board,
    win_rate_thres = opt.win_rate_thres,
}

local cnnplayer = CNNPlayerV2("CNNPlayerV2", "go_player_v2", "1.0", callbacks, opt2)
cnnplayer:mainloop()

model = nil
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
