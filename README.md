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

GCC 4.8+ is required. Depending on the location of your C++ compiler, please change the script accordingly. Tested in CentOS 6.5.

Usage
------------
Step 1: Download the models.  

Create `./models` directory and download trained [models](https://www.dropbox.com/sh/6nm8g8z163omb9f/AABQxJyV7EIdbHKd9rnPQGnha?dl=0).

Step 2: First run the GPU server   

```bash
cd ./local_evaluator     
sh cnn_evaluator.sh [num_gpu] [pipe file path]
```

* `num_gpu`         the number of GPUs (1-8) you have for the current machine. 
* `pipe file path`  The path that the pipe file is settled. Default is `/data/local/go`. If you have specific other path, then you need to specify the same when running `cnnPlayerMCTSV2.lua`

Example: `sh cnn_evaluator.sh 4 /data/local/go`

Step 3: Run the main program

```bash
cd ./cnnPlayerV2     
th cnnPlayerMCTSV2.lua [options]
```

See `cnnPlayerV2/cnnPlayerMCTSV2.lua` for a lot of options. For a simple first run (assuming you have 4 GPUs), you could use:

```bash
th cnnPlayerMCTSV2.lua --num_gpu [num_gpu] --time_limits 10
```   
or

```bash
th cnnPlayerMCTSV2.lua --use_formal_params --time_limits 10
```   

When you are in the interactive environment, type 

* `clear_board` to clear the board
* `genmove b`    to genmove the black move.
* `play w Q4`    to play a move at Q4 for specific color.
* `quit`         to quit.

For more commands, please use command `list_commands`, check the details of [GTP protocol](http://senseis.xmp.net/?GTP) or take a look at the source code.

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
Training code (will be released soon).

* `./models`  
All pre-trained models. Please download them [here](https://www.dropbox.com/sh/6nm8g8z163omb9f/AABQxJyV7EIdbHKd9rnPQGnha?dl=0) and save to the `./models` directory.

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


