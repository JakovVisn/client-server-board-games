"""
Append-only markdown training log for VirusWar RL.

Written automatically by train.py callbacks.
Use log_note() to add manual entries programmatically.
The file can also be edited directly — it is plain markdown.
"""

import os
from datetime import datetime


class TrainingLogger:
    def __init__(self, log_path: str):
        self._path = log_path
        os.makedirs(os.path.dirname(os.path.abspath(log_path)), exist_ok=True)
        if not os.path.exists(log_path):
            with open(log_path, "w", encoding="utf-8") as f:
                f.write("# VirusWar RL Training Log\n")

    # ------------------------------------------------------------------

    def log_session_start(self, loaded_from: str | None, opponent: str,
                          target_kl, clip_range: float,
                          n_epochs: int, ent_coef: float) -> None:
        date = datetime.now().strftime("%Y-%m-%d %H:%M")
        self._append(
            f"\n---\n"
            f"\n## Session {date}\n"
            f"\n- **Loaded:** `{loaded_from or 'scratch'}`"
            f"\n- **Opponent:** `{opponent or 'any'}`"
            f"\n- **Hyperparams:** target_kl={target_kl}  "
            f"clip_range={clip_range}  n_epochs={n_epochs}  ent_coef={ent_coef}\n"
        )

    def log_rollout(self, step: int, win_rate: float, ep_rew_mean: float,
                    ep_len_mean: float = 0.0, entropy: float | None = None,
                    server_rt_ms: float = 0.0) -> None:
        len_str = f"  len={ep_len_mean:.1f}" if ep_len_mean > 0 else ""
        ent_str = f"  ent={entropy:.2f}" if entropy is not None else ""
        rt_str = f"  server_rt={server_rt_ms:.1f}ms" if server_rt_ms > 0 else ""
        self._append(
            f"- `{self._ts()}` train rollout"
            f"  step={step:,}  win={win_rate:.1%}  rew={ep_rew_mean:+.3f}"
            f"{len_str}{ent_str}{rt_str}"
        )

    def log_promotion(self, step: int, win_rate: float, path: str) -> None:
        self._append(
            f"- `{self._ts()}` train promote"
            f"  step={step:,}  win_rate={win_rate:.1%}  path={path}"
        )

    def log_session_end(self, step: int, path: str) -> None:
        self._append(
            f"- `{self._ts()}` train session_end  step={step:,}  path={path}"
        )

    def log_note(self, note: str) -> None:
        """Programmatic manual note — also usable by Claude via direct file edit."""
        ts = datetime.now().strftime("%Y-%m-%d %H:%M")
        self._append(f"\n> **Note** `{ts}`: {note}\n")

    # ------------------------------------------------------------------

    def _append(self, text: str) -> None:
        with open(self._path, "a", encoding="utf-8") as f:
            f.write(text + "\n")

    @staticmethod
    def _ts() -> str:
        return datetime.now().strftime("%Y-%m-%d %H:%M:%S")