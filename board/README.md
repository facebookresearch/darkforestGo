Board library
=================

Default policy
--------------

In DarkForest, there are 3 default policies that could be chosen, specified by `--playout_policy`:

1. `v2` (default)   
DarkForest uses its own trained default policy. The training set is Tygem (Thanks [Ling Wang](mailto:1160071998@qq.com) for providing this). `./models/playout-model.bin` is the trained model. Check `patternv2.c` for the source code.

2. `simple`  
DarkForest uses simple default policy (trying to save when atari, trying to put others into atari, 3x3 pattern matching from Pachi, etc).

3. `pachi`   
DarkForest uses Pachi's default policy. The majority of codes are in `./pachi_tactics`.

Utilities
---------

Dump default policy:

```bash
th sample_one_pattern_v2.lua -p ../models/playout-policy.bin --temperature 0.5 --sgf_file [your_sgf_file] --move_from 230 --num_games 10 --num_moves 200 --save_prefix moves
```

Then it will dump the games played by playout policy and visualize them. If `save_prefix` is set, then the move sequence of each trial will also be saved. 
