#!/bin/bash -e
 th train.lua --nGPU 1 --datasource gogod --num_forward_models 2048 --alpha 0.05 --epoch_size 128000 --data_augmentation
