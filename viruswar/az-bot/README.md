# AZ bot

An AlphaZero-style agent for Virus War: Monte Carlo tree search guided by a policy/value network, trained by pure self-play on the `viruswar_sim` simulator. Training needs no server; only `play.py` connects to it.

The project's strongest player: it wins every game against the random bot and the frozen [RL bot](../rl-bot), and beat a human 16 games out of 17.

## How it works

- **Network** (`net.py`): a stem convolution, 6 residual blocks of 64 channels, then a policy head (one logit per cell) and a value head (expected result, −1 … +1). The input is the board from the moving player's view as 6 planes (my pieces, my captures, opponent's pieces, opponent's captures, empty, sub-moves left).
- **Search** (`mcts.py`): PUCT tree search with one node per sub-move, so a 3-move turn is three tree levels. The value's sign flips only where the turn passes to the other player. Training games add Dirichlet noise at the root of every move.
- **Self-play** (`self_play.py`, `parallel.py`): each iteration plays 40 games on 10 worker processes, 200 simulations per move. The first 20 sub-moves are sampled from the visit counts, later ones take the most-visited move. The policy target is always the full visit distribution.
- **Training** (`train.py`): a random 25% of each game's positions go to a replay buffer (up to 200k positions), are augmented with board symmetries (`symmetry.py`) and trained with loss = policy + 0.25 × value.
- **Promotion:** every 5 iterations the newest checkpoint plays 100 games against the newest promoted model (50 random openings, both sides of each) and is promoted at 55% or more.
- **Diagnostics** (`diagnostics.py`): every iteration logs value-head accuracy and policy sharpness on games not yet trained on, plus `gap`: how much worse the value head does on those games than on its training data. A growing gap means memorising.

All settings live in `.env`; [`.env.example`](.env.example) lists them with their defaults.

## Results

Run 4 (2026-10-13/14): about 22k self-play games in about 30 hours on an M4 Pro, 40+ promotions, final `gap` ≈ +0.04 (no memorising). Ladder of the final model `promoted_0000021400`, 200 games per opponent (`ladder.py`, 2026-10-14):

| Opponent | Win rate | 95% interval |
|---|---|---|
| Random moves | 100% | 98–100% |
| Untrained seed | 100% | 98–100% |
| After 4.6k games | 100% | 98–100% |
| After 10.2k games | 97% | 94–99% |
| After 14.2k games | 77% | 71–82% |
| Previous promoted (21.0k games) | 62% | 55–68% |
| RL bot, latest | 100% | 98–100% |

The steady rise against older models shows real progress, not luck in 100-game pits. Progress was slowing (about +210 Elo over the last 7k games), so training stopped.

### Earlier runs

Runs 1–3 never produced a useful model; each failure led to one of the settings above.

| Run | What went wrong | Fix |
|---|---|---|
| 1 (June 2026, 500 games) | The value head memorised game results: about 20 gradient passes per position and almost no root noise. | Root noise on every move, less sample reuse |
| 2 (200 games) | It memorised again: training value loss 0.50 against 1.18 on fresh games, and lost 91% of pits to the untrained seed. Positions from one game are nearly identical and share one result. | Keep 25% of each game's positions, value loss weight 0.25 |
| 3 | Memorising was fixed (`gap` ≈ 0.05), but it still lost to the seed: after move 20 the policy target was the played move only, so the prior collapsed and blinded the search. | Always train on the full visit distribution |

The archived runs are in `models_run*/` (gitignored).

## Running it

Run from this folder, with the shared venv `viruswar/.venv` and the `viruswar_sim` symlink (see [Getting started](../../README.md#getting-started)). The venv is installed from `viruswar/requirements.txt`; `requirements.txt` here lists only what the AZ bot needs.

```bash
cp .env.example .env

../.venv/bin/python train.py                  # trains; resumes from the newest checkpoint
../.venv/bin/python play.py --bot azbot:azbot:latest --bot azbot_prev:azbot_prev:prev:1
../.venv/bin/python ladder.py                 # newest promoted model vs its history; stop training first
../.venv/bin/python evaluate.py               # per-model value/policy diagnostics
```

- **`train.py`** saves a checkpoint and `train_state.pt` (replay buffer and optimizer) every 5 iterations, so a stopped run continues where it left off. It runs `AZ_WORKERS` processes (10 keep an M4 Pro fully busy).
- **`play.py`** serves league bots on the server, 400 simulations per move. Roles: `latest` (newest promoted model), `prev:N` (N promotions older), `random` (a random one, picked again on each reload). It reloads when a model is promoted. Each account needs a `login:password:bot:N` line in the server's `auth.txt` (see [docs/PROTOCOL.md](../../docs/PROTOCOL.md#accounts-authtxt)).
- **`ladder.py`** also accepts RL bot models; see its docstring.

## Files

```
models/
  checkpoints/   az_NNNNNNNNNN.pt (every 5 iterations) + train_state.pt
  promoted/      promoted_NNNNNNNNNN.pt, read by play.py; promoted_0000000000 is the untrained seed
  train_az.log  play_az.log  eval_az.log  ladder_az.log   event logs, one line per event
  ladder_az.md                                          ladder result tables
```

The number in a model's name is the count of self-play games it was trained on.
