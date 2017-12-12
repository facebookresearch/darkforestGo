DarkForest, the Facebook Go engine
========

**Update[12/11/2017]** DarkForestGo has been incorporated into ELF [platform](https://github.com/facebookresearch/ELF/tree/master/go).

**Update**: The training code is open source now. See below for detailed instructions.

DarkForest is a Go game engine powered by Deep Learning and developed at Facebook AI Research.
* [It has a stable rank of 5d on the KGS servers](http://www.gokgs.com/graphPage.jsp?user=darkfmcts3)
* [Pure Policy Network achieves a stable rank of 3d on KGS](http://www.gokgs.com/graphPage.jsp?user=darkfores2)
* [It received the 3rd place in the KGS Go Tournament](http://www.weddslist.com/kgs/past/119/index.html)
* [It received the 2nd place in UEC Computer Go Cup](http://jsb.cs.uec.ac.jp/~igo/eng/result2.html)

We hope that releasing the source code and pre-trained [models](https://www.dropbox.com/sh/6nm8g8z163omb9f/AABQxJyV7EIdbHKd9rnPQGnha?dl=0) are beneficial to the community.

Details of the engine are given in our [paper](http://arxiv.org/abs/1511.06410) and [poster](http://yuandong-tian.com/ICLR2016-poster.pdf), and if you use our engine in future research, cite our paper:

```
Better Computer Go Player with Neural Network and Long-term Prediction, ICLR 2016
Yuandong Tian, Yan Zhu

@article{tian2015better,
  title={Better Computer Go Player with Neural Network and Long-term Prediction},
  author={Tian, Yuandong and Zhu, Yan},
  journal={arXiv preprint arXiv:1511.06410},
  year={2015}
}
```

![Architecture](../master/figure.png?raw=true)

Although DarkForest is standalone and does not depend on external libraries, some portions of the tactics and pattern code were inspired by the Pachi [engine](https://github.com/pasky/pachi).

Build
------------
Dependencies:

1. Install [torch7](http://torch.ch/docs/getting-started.html).
2. [Install CUDA / CuDNN](https://github.com/facebook/fb.resnet.torch/blob/master/INSTALL.md)
2. Install a few packages
```bash
luarocks install class
luarocks install image
luarocks install tds
luarocks install cudnn
```
 This program supports 1 to 4 GPUs.

Then just compile with the following command:

```bash
sh ./compile.sh
```

GCC 4.8+ is required. Depending on the location of your C++ compiler, please change the script accordingly.
Tested in CentOS 6.5 and Ubuntu 14.04, 15.04.


Install gcc-4.9 as a second compiler and create symlink as:

    sudo ln -s /usr/bin/gcc-4.9 /usr/local/cuda/bin/gcc
    sudo ln -s /usr/bin/g++-4.9 /usr/local/cuda/bin/g++
During the installation of torch and cudnn, either change the build script or replace symlink at /usr/bin/cc with:

    sudo ln -s /usr/bin/gcc-4.9 /usr/bin/cc

More info at (http://stackoverflow.com/questions/6622454/cuda-incompatible-with-my-gcc-version)


After the compilation `cc` symlink can be reverted back to latest version.

If you get errors like:

    These bindings are for version 5005 or above ...

Download latest cuDNN from nvidia at (https://developer.nvidia.com/rdp/cudnn-download), registration required.



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
th cnnPlayerMCTSV2.lua --num_gpu [num_gpu] --time_limit 10
```
or (if you want to use a set of plausibly good parameters):

```bash
th cnnPlayerMCTSV2.lua --use_formal_params --num_gpu [num_gpu] --time_limit 10
```

To load an existing game up to move 23:
```bash
th cnnPlayerMCTSV2.lua [other_options] --setup_board "/path/to/sgf 23"
```

When you are in the interactive environment, type

* `clear_board` to clear the board
* `genmove b`    to genmove the black move.
* `play w Q4`    to play a move at Q4 for specific color.
* `quit`         to quit.

A complete game may look like:

```bash
clear_board
[MCTS initialization ...]
place_free_handicap 3
genmove b
[MCTS generates moves..e.g., it returns Q16]
play w D4
genmove b
[MCTS generates moves...]
quit
```

For more commands, please use command `list_commands`, check the details of [GTP protocol](http://senseis.xmp.net/?GTP) or take a look at the source code.

Training
--------------
To train the policy network from scratch, please run `./train.sh`. 1 GPU is needed. Please install
`torchnet` first (e.g., `luarocks install torchnet`).

Differences with the award-winning versions
--------------
The difference between this open source version (A) and that in KGS/competitions (B) is the following:
* (A) runs on a single machine and uses pipe as client/server communications. (B) uses thrift RPC services as a way to communicate.
* (B) uses more computational resources.
* We might have tuned parameters for (B) extensively, but not for (A). We will give the tip of parameter tuning soon.

Troubleshooting
----------------
**Q**: My program hanged on genmove/quit, what happened?
**A**: Make sure you run the GPU server under ./local\_evaluator, the server remains active and the pipe file path matches between the server and the client.

If you have any questions or find any bugs, please **open a Github issue** by clicking *"Issues"* tab and then click *"New Issue"*.

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
Training code

* `./dataset`
Dataset used for training. Please download them [here](https://www.dropbox.com/sh/ihzvzajywmfvbhm/AACIgYxew4daP1LXY_HCKwNla?dl=0) and save to the `./dataset` directory.

* `./models`
All pre-trained models. Please download them [here](https://www.dropbox.com/sh/6nm8g8z163omb9f/AABQxJyV7EIdbHKd9rnPQGnha?dl=0) and save to the `./models` directory.

* `./sgfs`
Some exemplar sgf files.

License
----------
Please check the LICENSE file for the license of Facebook DarkForest Go engine.

