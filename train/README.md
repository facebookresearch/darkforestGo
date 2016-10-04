Training Policy network
=================

In this directory, we implement a simple reinforcement learning framework using Lua/torch, and use
it for training the policy network.

1. `./rl_framework/infra` A simple framework for reinforcement learning.

2. `./rl_framework/examples/go` The training code for policy network.

For simple usage, the main program for training is under the root directory. Simply copy the data
from [here](https://www.dropbox.com/sh/ihzvzajywmfvbhm/AACIgYxew4daP1LXY_HCKwNla?dl=0) to `./dataset` and run `./train.sh`
will start the training procedure, which is an implementation of our [paper](http://arxiv.org/abs/1511.06410) plus a few modifications, including adding batch-normalization layers.

With 4 GPUs, the training procedure gives 56.1% top-1 accuracy in KGS dataset in 3.5 days, and 57.1% top-1 in 6.5 days (see the simple log below). The parameters used are the following: `--epoch_size 256000 --GPU 4 --data_augmentation --alpha 0.1 --nthread 4`

<pre>
| Sun Aug 21 21:54:15 2016 | epoch 0001 | ms/batch 721 | train [1pi@1]: 11.230860 [1pi@5]: 30.617970 [3pi@1]: 3.099219 [3pi@5]: 14.042188 [2pi@5]: 18.935938 [2pi@1]: 4.482813 [policy]: 8.361849
| test [1pi@1]: 27.767189 [1pi@5]: 59.403130 [3pi@1]: 5.380469 [3pi@5]: 24.729689 [2pi@5]: 34.030472 [2pi@1]: 8.382812 [policy]: 6.558414 | saved *

| Thu Aug 25 10:35:11 2016 | epoch 0381 | ms/batch 719 | train [1pi@1]: 56.226566 [1pi@5]: 87.523834 [3pi@1]: 21.542580 [3pi@5]: 51.992970 [2pi@5]: 68.728127 [2pi@1]: 34.199612 [policy]: 3.736506
| test [1pi@1]: <b>56.124222</b> [1pi@5]: 87.432816 [3pi@1]: 21.600000 [3pi@5]: 52.107815 [2pi@5]: 68.922661 [2pi@1]: 34.421875 [policy]: 3.737540  | saved *

| Sun Aug 28 00:49:32 2016 | epoch 0661 | ms/batch 721 | train [1pi@1]: 57.075783 [1pi@5]: 88.215240 [3pi@1]: 22.512892 [3pi@5]: 53.472267 [2pi@5]: 70.093361 [2pi@1]: 35.576565 [policy]: 3.638625
| test [1pi@1]: <b>57.101566</b> [1pi@5]: 88.271095 [3pi@1]: 22.295313 [3pi@5]: 53.226566 [2pi@5]: 70.085938 [2pi@1]: 35.185940 [policy]: 3.646803  | saved
</pre>

Note that this is a general framework and could be used to train other tasks (e.g, value networks) in the future. If you have used our engine or training procedure, please cite the following paper:

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


