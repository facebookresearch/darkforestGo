#
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant 
# of patent rights can be found in the PATENTS file in the same directory.
# 

#!/bin/bash

CPP_FLAGS="-O4 -fPIC -std=c++11 -I/usr/include/malloc/ -Wl,-export-dynamic"
CXX=g++

echo Compiling
$CXX $CPP_FLAGS -I./common -c common/common.c common/comm.c common/comm_pipe.c 
$CXX $CPP_FLAGS -I./common -I./board -c board/board.c board/default_policy.c board/default_policy_common.c board/pattern.c board/pattern_v2.c board/ownermap.c board/sample_pattern_v2.c 
$CXX $CPP_FLAGS -I./common -I./board -c tsumego/rank_move.c 

$CXX $CPP_FLAGS -fpermissive -I./common -I./board -c pachi_tactics/moggy.c pachi_tactics/board_interface.c
$CXX $CPP_FLAGS -fpermissive -I./common -I./board -c pachi_tactics/tactics/1lib.c pachi_tactics/tactics/2lib.c pachi_tactics/tactics/ladder.c pachi_tactics/tactics/nakade.c pachi_tactics/tactics/nlib.c pachi_tactics/tactics/selfatari.c 

echo Create moggy
$CXX -shared -o libmoggy.so moggy.o board.o board_interface.o 1lib.o 2lib.o ladder.o nakade.o nlib.o selfatari.o pattern.o 

$CXX $CPP_FLAGS -I./common -I./board -I./mctsv2 -c mctsv2/tree.c mctsv2/playout_multithread.c mctsv2/playout_callbacks.c mctsv2/event_count.cpp mctsv2/tree_search.c
$CXX $CPP_FLAGS -I./common -c ./local_evaluator/cnn_local_exchanger.c

echo Create libboard and libcomm
$CXX -shared -Wl,-export-dynamic -o libcommon.so common.o
$CXX -shared -Wl,-export-dynamic -o libboard.so board.o common.o
$CXX -shared -Wl,-export-dynamic -o libdefault_policy.so default_policy_common.o default_policy.o board.o common.o pattern.o pattern_v2.o
$CXX -shared -Wl,-export-dynamic -o libownermap.so board.o common.o ownermap.o
$CXX -shared -Wl,-export-dynamic -o libpattern_v2.so pattern_v2.o board.o common.o ownermap.o
$CXX -shared -Wl,-export-dynamic -o libcomm.so comm.o

echo Create libplayout_multithread.so
$CXX -shared -o libplayout_multithread.so tree.o playout_multithread.o board.o tree_search.o playout_callbacks.o common.o cnn_local_exchanger.o comm_pipe.o default_policy.o pattern.o pattern_v2.o default_policy_common.o rank_move.o event_count.o moggy.o board_interface.o 1lib.o 2lib.o ladder.o nakade.o nlib.o selfatari.o -lm

echo Create liblocalexchanger.so
$CXX -shared -o liblocalexchanger.so comm_pipe.o cnn_local_exchanger.o board.o common.o -lm 

echo Compile all test codes
$CXX $CPP_FLAGS -lm -pthread mctsv2/test_playout_multithread.c tree.o playout_multithread.o board.o common.o playout_callbacks.o comm_pipe.o event_count.o tree_search.o cnn_local_exchanger.o default_policy.o default_policy_common.o pattern.o pattern_v2.o rank_move.o moggy.o board_interface.o 1lib.o 2lib.o ladder.o nakade.o nlib.o selfatari.o -I./common -I./board -o test_playout_multithread

echo Put all .so file into directory so that lua could load
DEST_DIR=./libs

cp libboard.so $DEST_DIR
cp libownermap.so $DEST_DIR
cp libpattern_v2.so $DEST_DIR
cp libdefault_policy.so $DEST_DIR
cp libcomm.so $DEST_DIR
cp libcommon.so $DEST_DIR
cp libplayout_multithread.so $DEST_DIR
cp libmoggy.so $DEST_DIR
cp liblocalexchanger.so $DEST_DIR
