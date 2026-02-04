#!/usr/bin/env python3
"""Neural Network Training Farm Orchestrator

Manages distributed training across multiple DOSBox+Explorer instances.
Each worker runs a game directory via worker.py and periodically produces a
model checkpoint. The farm merges worker models on an interval.

Usage:
    python3 nn_farm.py --games-dir /path/to/games --workers 4 --output-dir ./training_output
"""

from __future__ import annotations

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional

try:
    import torch

    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False
    print("Warning: PyTorch not available. Model merging will fall back to copying.")


@dataclass
class WorkerConfig:
    worker_id: int
    game_dir: str
    output_dir: str
    model_path: Optional[str] = None


@dataclass
class FarmConfig:
    games_dir: str
    output_dir: str
    dosbox_path: str
    num_workers: int = 4
    merge_interval: int = 300  # seconds
    checkpoint_interval: int = 3600  # seconds
    model_path: Optional[str] = None
    training_steps: int = 1000
    episode_timeout: float = 300.0
    headless: bool = True
    include_regex: Optional[str] = None
    exclude_regex: Optional[str] = None
    max_games: Optional[int] = None
    random_assign_prob: float = 0.3
    seed: Optional[int] = None


class Worker:
    """Manages a single worker.py subprocess."""

    def __init__(self, config: WorkerConfig, farm: FarmConfig, worker_script: Path):
        self.config = config
        self.farm = farm
        self.worker_script = worker_script
        self.process: Optional[subprocess.Popen] = None
        self.start_time: Optional[float] = None

    def start(self) -> None:
        os.makedirs(self.config.output_dir, exist_ok=True)

        cmd = [
            sys.executable,
            str(self.worker_script),
            "--game-path",
            self.config.game_dir,
            "--worker-id",
            str(self.config.worker_id),
            "--dosbox-path",
            self.farm.dosbox_path,
            "--output-dir",
            self.config.output_dir,
            "--training-steps",
            str(self.farm.training_steps),
            "--timeout",
            str(self.farm.episode_timeout),
        ]

        if self.config.model_path:
            cmd += ["--model", self.config.model_path]

        if not self.farm.headless:
            cmd += ["--no-headless"]

        log_path = os.path.join(self.config.output_dir, f"worker_{self.config.worker_id}.log")
        log_file = open(log_path, "w")

        self.process = subprocess.Popen(
            cmd,
            stdout=log_file,
            stderr=subprocess.STDOUT,
            cwd=self.config.output_dir,
        )
        self.start_time = time.time()
        print(f"[Worker {self.config.worker_id}] Started on {os.path.basename(self.config.game_dir)}")

    def is_running(self) -> bool:
        return self.process is not None and self.process.poll() is None

    def stop(self) -> None:
        if self.process and self.is_running():
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        print(f"[Worker {self.config.worker_id}] Stopped")

    def get_model_path(self) -> str:
        # Base path used by worker.py for Explorer checkpoints.
        return os.path.join(self.config.output_dir, "explorer_checkpoint")

    def get_status(self) -> Dict:
        return {
            "worker_id": self.config.worker_id,
            "game": os.path.basename(self.config.game_dir),
            "running": self.is_running(),
            "runtime_seconds": time.time() - self.start_time if self.start_time else 0,
            "model_exists": os.path.exists(self.get_model_path()),
        }


class TrainingFarm:
    def __init__(self, config: FarmConfig):
        self.config = config
        self.workers: List[Worker] = []
        self.games: List[str] = []
        self.running = False
        self.start_time: Optional[float] = None

        os.makedirs(config.output_dir, exist_ok=True)

        self.worker_script = Path(__file__).with_name("worker.py")
        if not self.worker_script.exists():
            raise ValueError(f"worker.py not found next to nn_farm.py: {self.worker_script}")

        self._load_games()

    def _load_games(self) -> None:
        games_path = Path(self.config.games_dir)
        if not games_path.exists():
            raise ValueError(f"Games directory not found: {self.config.games_dir}")

        include_re = re.compile(self.config.include_regex, re.IGNORECASE) if self.config.include_regex else None
        exclude_re = re.compile(self.config.exclude_regex, re.IGNORECASE) if self.config.exclude_regex else None
        exe_patterns = ("*.exe", "*.com", "*.bat", "*.EXE", "*.COM", "*.BAT")

        game_dirs: List[Path] = []
        for child in games_path.iterdir():
            if not child.is_dir():
                continue
            if include_re and not include_re.search(str(child)):
                continue
            if exclude_re and exclude_re.search(str(child)):
                continue

            has_exe = False
            for pat in exe_patterns:
                try:
                    next(child.rglob(pat))
                    has_exe = True
                    break
                except StopIteration:
                    continue
            if has_exe:
                game_dirs.append(child)

        game_dirs.sort(key=lambda p: p.name.lower())
        if self.config.max_games is not None:
            game_dirs = game_dirs[: max(0, self.config.max_games)]

        self.games = [str(p) for p in game_dirs]

        print(f"Found {len(self.games)} game directories in {self.config.games_dir}")
        if not self.games:
            raise ValueError("No game directories with executables found")

    def _assign_game(self, worker_id: int) -> str:
        if not self.games:
            raise ValueError("No games available")

        if random.random() < self.config.random_assign_prob:
            return random.choice(self.games)
        return self.games[worker_id % len(self.games)]

    def _create_worker(self, worker_id: int) -> Worker:
        game_dir = self._assign_game(worker_id)
        worker_output = os.path.join(self.config.output_dir, f"worker_{worker_id}")
        worker_cfg = WorkerConfig(
            worker_id=worker_id,
            game_dir=game_dir,
            output_dir=worker_output,
            model_path=self.config.model_path,
        )
        return Worker(worker_cfg, self.config, self.worker_script)

    def start(self) -> None:
        print(f"\n{'=' * 60}")
        print("Starting Training Farm")
        print(f"  Workers: {self.config.num_workers}")
        print(f"  Games: {len(self.games)}")
        print(f"  Output: {self.config.output_dir}")
        print(f"{'=' * 60}\n")

        self.running = True
        self.start_time = time.time()

        for i in range(self.config.num_workers):
            worker = self._create_worker(i)
            worker.start()
            self.workers.append(worker)
            time.sleep(0.5)

        self._save_config()

    def _save_config(self) -> None:
        config_path = os.path.join(self.config.output_dir, "farm_config.json")
        with open(config_path, "w") as f:
            json.dump(asdict(self.config), f, indent=2)

    def stop(self) -> None:
        print("\nStopping training farm...")
        self.running = False

        for worker in self.workers:
            worker.stop()

        self.merge_models()
        self._save_status()
        print("Training farm stopped.")

    def _load_model_state(self, path: str) -> Optional[Dict]:
        if not TORCH_AVAILABLE:
            return None
        obj = torch.load(path, map_location="cpu")
        if isinstance(obj, dict):
            if "model_state_dict" in obj and isinstance(obj["model_state_dict"], dict):
                return obj["model_state_dict"]
            if "state_dict" in obj and isinstance(obj["state_dict"], dict):
                return obj["state_dict"]
            return obj
        return None

    def _save_model_state(self, state: Dict, path: str, metadata: Optional[Dict] = None) -> None:
        if not TORCH_AVAILABLE:
            return
        torch.save({"model_state_dict": state, "metadata": metadata or {}}, path)

    def merge_models(self) -> Optional[str]:
        print("\nMerging models from workers...")

        bases = []
        for w in self.workers:
            base = w.get_model_path()
            if os.path.exists(base + ".model"):
                bases.append(base)

        if not bases:
            print("No worker checkpoints found to merge")
            return None

        # Select the newest checkpoint (by .model mtime) as the merged baseline.
        newest = max(bases, key=lambda b: os.path.getmtime(b + ".model"))
        merged_base = os.path.join(self.config.output_dir, "merged_checkpoint")

        for suffix in (".model", ".optimizer", ".stats"):
            src = newest + suffix
            dst = merged_base + suffix
            if os.path.exists(src):
                shutil.copy(src, dst)

        with open(os.path.join(self.config.output_dir, "merged_checkpoint.json"), "w") as f:
            json.dump(
                {
                    "merged_at": datetime.now().isoformat(),
                    "strategy": "newest_checkpoint_copy",
                    "source": newest,
                },
                f,
                indent=2,
            )

        print(f"Copied newest checkpoint {newest} -> {merged_base}")
        return merged_base

    def _save_status(self) -> None:
        status = {
            "timestamp": datetime.now().isoformat(),
            "runtime_seconds": time.time() - self.start_time if self.start_time else 0,
            "workers": [w.get_status() for w in self.workers],
        }
        status_path = os.path.join(self.config.output_dir, "status.json")
        with open(status_path, "w") as f:
            json.dump(status, f, indent=2)

    def _save_checkpoint(self) -> None:
        merged = self.merge_models()
        if not merged or not os.path.exists(merged):
            return
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        checkpoint_dir = os.path.join(self.config.output_dir, "checkpoints")
        os.makedirs(checkpoint_dir, exist_ok=True)
        checkpoint_path = os.path.join(checkpoint_dir, f"checkpoint_{timestamp}.pt")
        shutil.copy(merged, checkpoint_path)
        print(f"Saved checkpoint: {checkpoint_path}")

    def _print_status(self) -> None:
        runtime = time.time() - self.start_time if self.start_time else 0
        active = sum(1 for w in self.workers if w.is_running())
        print(f"\r[Farm] Runtime: {runtime:.0f}s | Active workers: {active}/{len(self.workers)}", end="")
        sys.stdout.flush()

    def run(self) -> None:
        self.start()

        last_merge = time.time()
        last_checkpoint = time.time()

        try:
            while self.running:
                time.sleep(10)

                for i, worker in enumerate(self.workers):
                    if not worker.is_running():
                        print(f"\n[Worker {i}] Died, restarting with new game...")
                        worker.stop()
                        new_worker = self._create_worker(i)
                        new_worker.start()
                        self.workers[i] = new_worker

                now = time.time()
                if self.config.merge_interval > 0 and (now - last_merge) >= self.config.merge_interval:
                    merged_base = self.merge_models()
                    if merged_base and os.path.exists(merged_base + ".model"):
                        self.config.model_path = merged_base
                    last_merge = now

                if self.config.checkpoint_interval > 0 and (now - last_checkpoint) >= self.config.checkpoint_interval:
                    self._save_checkpoint()
                    last_checkpoint = now

                self._save_status()
                self._print_status()

        except KeyboardInterrupt:
            print("\nInterrupted by user")
        finally:
            self.stop()


def main() -> None:
    parser = argparse.ArgumentParser(description="Neural Network Training Farm")
    parser.add_argument("--games-dir", "-g", required=True, help="Directory containing DOS game subdirectories")
    parser.add_argument(
        "--output-dir",
        "--output",
        "-o",
        dest="output_dir",
        default="training_output",
        help="Output directory for logs/models",
    )
    parser.add_argument("--dosbox", "-d", default="./dosbox", help="Path to DOSBox binary")
    parser.add_argument("--workers", "-w", type=int, default=4, help="Number of parallel workers")
    parser.add_argument("--model", "--initial-model", "-m", dest="model", help="Initial model to start from")
    parser.add_argument("--merge-interval", type=int, default=300, help="Seconds between model merges")
    parser.add_argument("--checkpoint-interval", type=int, default=3600, help="Seconds between checkpoints")
    parser.add_argument("--training-steps", type=int, default=1000, help="Training steps per episode")
    parser.add_argument("--timeout", type=float, default=300.0, help="Episode timeout in seconds")
    parser.add_argument("--no-headless", action="store_true", help="Run workers with visible display")
    parser.add_argument("--include", "--include-regex", dest="include", help="Regex to filter game directories")
    parser.add_argument("--exclude-regex", dest="exclude", help="Regex to exclude game directories")
    parser.add_argument("--max-games", type=int, help="Limit number of game directories used")
    parser.add_argument("--seed", type=int, help="Random seed")
    parser.add_argument(
        "--scan-mode",
        default="dirs",
        choices=["dirs"],
        help="Game discovery mode (only 'dirs' is supported)",
    )

    args = parser.parse_args()

    if not os.path.exists(args.dosbox):
        print(f"Error: DOSBox not found at {args.dosbox}")
        sys.exit(1)

    cfg = FarmConfig(
        games_dir=args.games_dir,
        output_dir=args.output_dir,
        dosbox_path=os.path.abspath(args.dosbox),
        num_workers=args.workers,
        merge_interval=args.merge_interval,
        checkpoint_interval=args.checkpoint_interval,
        model_path=args.model,
        training_steps=args.training_steps,
        episode_timeout=args.timeout,
        headless=not args.no_headless,
        include_regex=args.include,
        exclude_regex=args.exclude,
        max_games=args.max_games,
        seed=args.seed,
    )

    if cfg.seed is not None:
        random.seed(cfg.seed)

    farm = TrainingFarm(cfg)
    farm.run()


if __name__ == "__main__":
    main()
