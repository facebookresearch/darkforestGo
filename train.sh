#
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.
#

#!/bin/bash -e
th train.lua --nGPU 1 --datasource kgs --num_forward_models 2048 --nthread 4 --alpha 0.05 --epoch_size 128000 --data_augmentation
