Facebook DarkForest Go Project
========

Build
------------
Dependency: 

1. Install [torch7](http://torch.ch/docs/getting-started.html).
2. Install luarocks: class, image, tds, cudnn

For Cudnn, please install the driver from NVidia. This program supports 1-4 GPUs.

Then just compile with the following command:

```bash
sh ./compile.sh
```

GCC 4.8+ is required. Tested in CentOS 6.5. 

Usage
------------
Step 1: First run the GPU server 

```bash
cd ./local_evaluator     
sh cnn_evaluator.sh [num_gpu] [pipe file path]
```

* `num_gpu`         the number of GPUs you have for the current machine. 
* `pipe file path`  The location that the pipe file is settled. Default is /data/local/go/

Step 2: Run the main program

```bash
cd ./CNNPlayerV2     
th cnnPlayerMCTSV2.lua --num_gpu [num_gpu]
```

When you are in the interactive environment, type 

* `clear_board` to clear the board
* `genmove b`    to genmove the black move.
* `play w Q4`    to play a move at Q4 for specific color.
* `quit`         to quit.

For more commands, please use command `list_commands`, check the details of [GTP protocol](http://senseis.xmp.net/?GTP) or take a look at the source code (in ./CNNPlayerV2/cnnPlayerV2Framework.lua). 

Award
--------------
* Stable **KGS 5d**. [link](http://www.gokgs.com/graphPage.jsp?user=darkfmcts3)
* 3rd place in KGS Go Tournament. [link](http://www.weddslist.com/kgs/past/119/index.html)
* 2nd place in UEC Computer Go Cup. [link](http://jsb.cs.uec.ac.jp/~igo/eng/result2.html)

Trouble Shooting 
----------------
**Q**: My program hanged on genmove/quit, what happened?  
**A**: Make sure you run the GPU server under ./local\_evaluator, the server remains active and the pipe file path matches between the server and the client.

If you have any questions, please open a github issue.

Code Overview
-------------

The system consists of the following parts. 

* `./CNNPlayerV2`  
Lua (terminal) interface for Go.   

1. `CNNPlayerV3.lua`              Run Pure-DCNN player
2. `CNNPlayerMCTSV2.lua`          Run player with DCNN + MCTS

* `./board`   
Things about board and its evaluations. Board data structure and different playout policy.

* `./mctsv2`  
Implementation of Monte Carlo Tree Search

* `./local_evaluator`  
Simple GPU-based server. Communication with search threads via pipe. 

* `./utils`  
Simple utilities, e.g., read/write sgf files.

* `./test`  
Test utilities.

* `./train`  
Training code (to be released soon).

* `./models`  
All pre-trained models. Please download them elsewhere.

* `./sgfs`
Some exemplar sgf files.

License
----------
Please check LICENSE file for the license of Facebook DarkForest Go engine. 

Reference
----------
If you use the pre-trained models or any engine, please reference the following paper:

```
Better Computer Go Player with Neural Network and Long-term Prediction, ICLR 2016  
Yuandong Tian, Yan Zhu
```

Here is the arxiv [link](http://arxiv.org/abs/1511.06410)


