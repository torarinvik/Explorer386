# DOSBox Explorer Training Farm

Distributed training system for the Explorer neural network using multiple DOS games.

## Overview

The training farm allows parallel execution of multiple DOSBox instances, each running a different DOS game. The Explorer module in each instance collects observations and trains a neural network to understand DOS program behavior. Models from individual workers are periodically merged using Federated Averaging (FedAvg).

## Components

### nn_farm.py
Main orchestrator script that:
- Spawns multiple worker processes
- Assigns games to workers
- Periodically merges model weights using FedAvg
- Saves checkpoints and logs

### worker.py
Individual worker script that:
- Runs a single DOSBox instance
- Manages headless display via Xvfb
- Collects training metrics
- Saves model checkpoints

### merge_models.py
Utility for merging model weights:
- Supports average, weighted, and EMA strategies
- Can process multiple model files
- Standalone tool for post-training merging

### start_training.sh
Convenient launcher script with common defaults.

## Prerequisites

1. **DOSBox with Explorer**: Build from the parent repository with `enable_explorer=true`
2. **Python 3**: With PyTorch for model operations
3. **Xvfb**: For headless operation (optional, falls back to real display)

## Quick Start

```bash
# Start training with default settings
./start_training.sh /path/to/games

# With more workers and custom output
./start_training.sh /path/to/games -w 8 -o ./models

# With initial model checkpoint
./start_training.sh /path/to/games --model pretrained.pt
```

## Detailed Usage

### Using nn_farm.py directly

```bash
python3 nn_farm.py \
    --games-dir /path/to/games \
    --dosbox /path/to/dosbox \
    --workers 4 \
    --merge-interval 300 \
    --output ./training_output

# Run only a few games (deterministic subset)
python3 nn_farm.py \
    --games-dir /path/to/games \
    --dosbox /path/to/dosbox \
    --workers 3 \
    --max-games 3 \
    --seed 123 \
    --output ./training_output

# Filter by executable path (relative to --games-dir)
python3 nn_farm.py \
    --games-dir /path/to/games \
    --dosbox /path/to/dosbox \
    --workers 4 \
    --include-regex 'Alley Cat|Digger|Arkanoid' \
    --scan-mode per-dir \
    --output ./training_output
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--games-dir` | required | Directory with game subdirectories |
| `--dosbox` | `./dosbox` | Path to DOSBox executable |
| `--workers` | 4 | Number of parallel workers |
| `--merge-interval` | 300 | Seconds between model merges |
| `--output` | `./output` | Output directory for models/logs |
| `--model` | None | Starting model checkpoint |
| `--include-regex` | None | Only include executables whose relative path matches regex |
| `--exclude-regex` | None | Exclude executables whose relative path matches regex |
| `--max-games` | None | Limit to N executables (random subset) |
| `--seed` | None | Random seed for reproducible subset/assignment |
| `--scan-mode` | `auto` | How to discover executables: `auto`, `executables`, `per-dir` |

Note: `nn_farm.py` also accepts `--output-dir` (alias for `--output`) and `--initial-model` (alias for `--model`) for backwards compatibility.

### Using worker.py directly

For debugging or single-game training:

```bash
python3 worker.py \
    --game-path /path/to/game \
    --worker-id 0 \
    --dosbox-path ../../../build/dosbox \
    --training-steps 1000 \
    --timeout 300
```

### Merging models

```bash
# Average merge
python3 merge_models.py worker_*/model.pt -o merged.pt

# Weighted merge
python3 merge_models.py model1.pt model2.pt --strategy weighted --weights 1.0 2.0 -o merged.pt

# EMA merge
python3 merge_models.py *.pt --strategy ema --ema-decay 0.99 -o merged.pt
```

## Directory Structure

```
training_output/
├── checkpoints/
│   ├── merged_epoch_001.pt
│   ├── merged_epoch_002.pt
│   └── ...
├── workers/
│   ├── worker_0/
│   │   ├── worker_0.log
│   │   ├── worker_0_metrics.json
│   │   └── worker_0_model.pt
│   └── ...
└── farm.log
```

## Game Requirements

Games should be organized in subdirectories:
```
games/
├── game1/
│   ├── GAME.EXE
│   └── DATA/
├── game2/
│   └── PLAY.COM
└── ...
```

Each subdirectory should contain a runnable DOS game with an executable file (.exe, .com, or .bat).

## Model Architecture

The Explorer neural network is a policy network that:
- Takes game state observations as input (memory snapshots, I/O events)
- Outputs action probabilities for emulator control
- Is trained using PPO (Proximal Policy Optimization)

## Tips

1. **Start small**: Use 2-4 workers initially to validate setup
2. **Monitor logs**: Check `farm.log` and worker logs for issues
3. **GPU usage**: If LibTorch is built with CUDA, training will use GPU
4. **Memory**: Each DOSBox instance uses ~100-200MB RAM
5. **Disk space**: Checkpoints can accumulate; prune old ones periodically

## Troubleshooting

### "Xvfb not found"
Install with: `apt-get install xvfb` or run with `--no-headless`

### "DOSBox not found"
Rebuild DOSBox or specify path with `--dosbox /full/path/to/dosbox`

### Workers dying immediately
Check worker logs in `output_dir/workers/worker_N/worker_N.log`

### No model improvement
- Increase training steps per episode
- Try different games
- Check that Explorer instrumentation is enabled in DOSBox build
