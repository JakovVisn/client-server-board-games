# RL bot (frozen)

A PPO agent for Virus War. It learned to play but plateaued far below the [AZ bot](../az-bot), so development stopped in June 2026. The code stays as a working baseline and for the comparison below.

## How it works

- **Model:** MaskablePPO (sb3-contrib) with a residual CNN: 64 channels, 4 residual blocks, a 256-unit feature layer. Illegal moves are masked out.
- **Input:** the board from the moving player's view as 6 planes of 10×10 (my pieces, my captures, opponent's pieces, opponent's captures, empty, sub-moves left). Each sub-move is one action.
- **Reward:** +1 for a win (plus up to 0.5 for captured cells), −1 for a loss, nothing in between.
- **Training:** league self-play through the real server. `train.py` runs several envs (10 in the run below), each logged in with an `rltrain_*` account. Matchmaking pairs them with league bots that `play.py` serves from the agent's own promoted models (latest, three previous, a random one). A model is promoted when its rollout win rate stays above a threshold for several rollouts in a row (the run used 62% and 5; `.env.example` has 55% and 3).
- **Playing:** `play.py` plays the policy's most likely legal move, without search.

## Result

Trained June 14–24, 2026: about 48M steps (one step = one sub-move), 10 promotions. Latest promoted model (`promoted_0048354848`), 200 games per opponent on paired random openings ([`az-bot/ladder.py`](../az-bot/ladder.py), 2026-10-15):

| Opponent | Win rate | 95% interval |
|---|---|---|
| Random moves | 95.5% | 92–98% |
| Its own model from 28.8M steps (`promoted_0028775768`) | 53% | 46–60% |
| AZ seed: MCTS with an untrained network | 74% | 68–80% |
| AZ after ~4.6k self-play games | 0% | 0–2% |
| AZ after ~21.4k self-play games (best) | 0% | 0–2% |

## Why it was frozen

- **It plateaued.** The last 20M steps (40% of all training) brought no measurable gain against its own older model. League self-play kept it level with itself instead of pushing it further.
- **No search.** The policy plays straight from the network. In Virus War one sub-move can cut off a territory, and AZ checks about 100 continuations before each move. An AZ model after a few hours of training won all 200 games. The best AZ model wins in about 54 sub-moves; RL bot vs random moves lasts about 180.
- **Expensive training.** Every training move goes through the TCP server, so 48M steps took 10 days. AZ trains offline on the `viruswar_sim` simulator and needed about 22k games for a far stronger model.

What did work: the network clearly learned the game. It beats random play and MCTS guided by an untrained network.

## Running it

Run from this folder, with the server running, the shared venv `viruswar/.venv` and the `viruswar_sim` symlink (see [Getting started](../../README.md#getting-started)):

```bash
cp .env.example .env       # training accounts and PPO settings
../.venv/bin/python play.py --bot rlbot:rlbot:latest    # league bot for humans or other bots
../.venv/bin/python train.py --steps 10000000           # resumes from the newest checkpoint in models/
```

The accounts must exist in the server's `auth.txt` (see [docs/PROTOCOL.md](../../docs/PROTOCOL.md#accounts-authtxt)): league bots as `login:password:bot:N`, training accounts as `login:password:bot`.

To measure a model offline, without the server, use the AZ ladder:

```bash
cd ../az-bot
../.venv/bin/python ladder.py --model ../rl-bot/models/promoted/promoted_0048354848.zip \
    --opponents random models/promoted/promoted_0000021400.pt
```
