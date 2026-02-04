#!/bin/bash
#
# DOSBox Explorer Training Farm Launcher
#
# This script provides an easy way to start distributed training
# for the Explorer neural network using multiple DOS games.
#
# Usage:
#   ./start_training.sh /path/to/games [options]
#   ./start_training.sh --help
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOSBOX_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
DOSBOX_BIN="${DOSBOX_DIR}/build/dosbox"

# Default settings
NUM_WORKERS=4
MERGE_INTERVAL=300
OUTPUT_DIR="./training_output"
TRAINING_STEPS=1000
EPISODE_TIMEOUT=300
INCLUDE_REGEX=""
EXCLUDE_REGEX=""
MAX_GAMES=""
SEED=""
SCAN_MODE=""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

print_help() {
    echo "DOSBox Explorer Training Farm"
    echo ""
    echo "Usage: $0 <games-directory> [options]"
    echo ""
    echo "Arguments:"
    echo "  games-directory    Directory containing DOS games (each game in subdirectory)"
    echo ""
    echo "Options:"
    echo "  -w, --workers N    Number of parallel workers (default: $NUM_WORKERS)"
    echo "  -m, --merge N      Merge interval in seconds (default: $MERGE_INTERVAL)"
    echo "  -o, --output DIR   Output directory (default: $OUTPUT_DIR)"
    echo "  -s, --steps N      Training steps per episode (default: $TRAINING_STEPS)"
    echo "  -t, --timeout N    Episode timeout in seconds (default: $EPISODE_TIMEOUT)"
    echo "  --dosbox PATH      Path to DOSBox executable"
    echo "  --model PATH       Initial model to start from"
    echo "  --include REGEX    Only run executables matching regex (path relative to games dir)"
    echo "  --exclude REGEX    Exclude executables matching regex (path relative to games dir)"
    echo "  --max-games N      Limit to N executables total (randomized subset)"
    echo "  --seed N           Random seed for reproducible subset/assignment"
    echo "  --scan-mode MODE   auto|executables|per-dir (default: auto)"
    echo "  -h, --help         Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 /workspace/Games1_Extracted -w 8"
    echo "  $0 /path/to/games --workers 4 --merge 600 --output ./models"
}

# Parse arguments
GAMES_DIR=""
INITIAL_MODEL=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -w|--workers)
            NUM_WORKERS="$2"
            shift 2
            ;;
        -m|--merge)
            MERGE_INTERVAL="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -s|--steps)
            TRAINING_STEPS="$2"
            shift 2
            ;;
        -t|--timeout)
            EPISODE_TIMEOUT="$2"
            shift 2
            ;;
        --dosbox)
            DOSBOX_BIN="$2"
            shift 2
            ;;
        --model)
            INITIAL_MODEL="$2"
            shift 2
            ;;
        --include)
            INCLUDE_REGEX="$2"
            shift 2
            ;;
        --exclude)
            EXCLUDE_REGEX="$2"
            shift 2
            ;;
        --max-games)
            MAX_GAMES="$2"
            shift 2
            ;;
        --seed)
            SEED="$2"
            shift 2
            ;;
        --scan-mode)
            SCAN_MODE="$2"
            shift 2
            ;;
        -h|--help)
            print_help
            exit 0
            ;;
        -*)
            echo -e "${RED}Unknown option: $1${NC}"
            print_help
            exit 1
            ;;
        *)
            if [[ -z "$GAMES_DIR" ]]; then
                GAMES_DIR="$1"
            else
                echo -e "${RED}Unexpected argument: $1${NC}"
                print_help
                exit 1
            fi
            shift
            ;;
    esac
done

# Validate inputs
if [[ -z "$GAMES_DIR" ]]; then
    echo -e "${RED}Error: Games directory is required${NC}"
    print_help
    exit 1
fi

if [[ ! -d "$GAMES_DIR" ]]; then
    echo -e "${RED}Error: Games directory does not exist: $GAMES_DIR${NC}"
    exit 1
fi

if [[ ! -f "$DOSBOX_BIN" ]]; then
    echo -e "${RED}Error: DOSBox not found at: $DOSBOX_BIN${NC}"
    echo "Build DOSBox first or specify path with --dosbox"
    exit 1
fi

# Check for Python
if ! command -v python3 &> /dev/null; then
    echo -e "${RED}Error: Python 3 is required${NC}"
    exit 1
fi

# Check for PyTorch
if ! python3 -c "import torch" 2>/dev/null; then
    echo -e "${YELLOW}Warning: PyTorch not found. Model merging will be limited.${NC}"
fi

# Count games (follow symlinks so a curated gameset can use symlinked dirs)
GAME_COUNT=$(find -L "$GAMES_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l)
echo -e "${GREEN}Found $GAME_COUNT game directories${NC}"

if [[ $GAME_COUNT -eq 0 ]]; then
    echo -e "${RED}Error: No game directories found in $GAMES_DIR${NC}"
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"

# Build command
CMD="python3 ${SCRIPT_DIR}/nn_farm.py"
CMD="$CMD --games-dir $GAMES_DIR"
CMD="$CMD --dosbox $DOSBOX_BIN"
CMD="$CMD --workers $NUM_WORKERS"
CMD="$CMD --merge-interval $MERGE_INTERVAL"
CMD="$CMD --output $OUTPUT_DIR"
CMD="$CMD --training-steps $TRAINING_STEPS"
CMD="$CMD --timeout $EPISODE_TIMEOUT"

if [[ -n "$INCLUDE_REGEX" ]]; then
    CMD="$CMD --include-regex \"$INCLUDE_REGEX\""
fi

if [[ -n "$EXCLUDE_REGEX" ]]; then
    CMD="$CMD --exclude-regex \"$EXCLUDE_REGEX\""
fi

if [[ -n "$MAX_GAMES" ]]; then
    CMD="$CMD --max-games $MAX_GAMES"
fi

if [[ -n "$SEED" ]]; then
    CMD="$CMD --seed $SEED"
fi

if [[ -n "$SCAN_MODE" ]]; then
    CMD="$CMD --scan-mode $SCAN_MODE"
fi

if [[ -n "$INITIAL_MODEL" && -f "$INITIAL_MODEL" ]]; then
    CMD="$CMD --model $INITIAL_MODEL"
fi

# Print configuration
echo ""
echo -e "${GREEN}=== DOSBox Explorer Training Farm ===${NC}"
echo ""
echo "Configuration:"
echo "  Games directory:   $GAMES_DIR"
echo "  Game count:        $GAME_COUNT"
echo "  Workers:           $NUM_WORKERS"
echo "  Merge interval:    ${MERGE_INTERVAL}s"
echo "  Output directory:  $OUTPUT_DIR"
echo "  DOSBox path:       $DOSBOX_BIN"
if [[ -n "$INITIAL_MODEL" ]]; then
    echo "  Initial model:     $INITIAL_MODEL"
fi
echo ""
echo "Starting training farm..."
echo ""

# Run the training farm
exec $CMD
