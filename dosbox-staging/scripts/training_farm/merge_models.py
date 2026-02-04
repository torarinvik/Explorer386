#!/usr/bin/env python3
"""
Model Weight Merger Utility

Merges model weights from multiple training workers.
Supports various merging strategies:
- Average: Simple averaging of weights
- Weighted: Weighted average based on training steps/coverage
- EMA: Exponential moving average for smoother updates

Usage:
    python merge_models.py model1.pt model2.pt -o merged.pt
    python merge_models.py worker_*/model.pt --strategy weighted -o merged.pt
"""

import os
import sys
import argparse
import glob
from pathlib import Path
from typing import List, Dict, Optional
from collections import OrderedDict

try:
    import torch
    TORCH_AVAILABLE = True
except ImportError:
    TORCH_AVAILABLE = False
    print("Error: PyTorch is required for model merging")
    sys.exit(1)


def load_model(path: str) -> Dict[str, torch.Tensor]:
    """Load model state dict from file"""
    if not os.path.exists(path):
        raise FileNotFoundError(f"Model not found: {path}")
    
    state = torch.load(path, map_location='cpu')
    
    # Handle different save formats
    if isinstance(state, dict):
        if 'model_state_dict' in state:
            return state['model_state_dict']
        elif 'state_dict' in state:
            return state['state_dict']
    return state


def save_model(state: Dict[str, torch.Tensor], path: str, metadata: Optional[Dict] = None):
    """Save model state dict to file"""
    save_data = {
        'model_state_dict': state,
        'metadata': metadata or {}
    }
    torch.save(save_data, path)
    print(f"Saved merged model to: {path}")


def merge_average(models: List[Dict[str, torch.Tensor]]) -> Dict[str, torch.Tensor]:
    """Simple averaging of model weights"""
    if not models:
        raise ValueError("No models to merge")
    
    merged = OrderedDict()
    
    # Get all keys from first model
    keys = list(models[0].keys())
    
    for key in keys:
        # Stack tensors from all models
        tensors = [m[key].float() for m in models if key in m]
        if tensors:
            merged[key] = torch.stack(tensors).mean(dim=0)
    
    return merged


def merge_weighted(models: List[Dict[str, torch.Tensor]], 
                   weights: List[float]) -> Dict[str, torch.Tensor]:
    """Weighted averaging of model weights"""
    if not models:
        raise ValueError("No models to merge")
    if len(models) != len(weights):
        raise ValueError("Number of weights must match number of models")
    
    # Normalize weights
    total = sum(weights)
    weights = [w / total for w in weights]
    
    merged = OrderedDict()
    keys = list(models[0].keys())
    
    for key in keys:
        weighted_sum = None
        for model, weight in zip(models, weights):
            if key in model:
                tensor = model[key].float() * weight
                if weighted_sum is None:
                    weighted_sum = tensor
                else:
                    weighted_sum = weighted_sum + tensor
        if weighted_sum is not None:
            merged[key] = weighted_sum
    
    return merged


def merge_ema(models: List[Dict[str, torch.Tensor]], 
              decay: float = 0.99) -> Dict[str, torch.Tensor]:
    """Exponential moving average merge (ordered by modification time)"""
    if not models:
        raise ValueError("No models to merge")
    
    merged = OrderedDict()
    
    # Start with first model
    for key, value in models[0].items():
        merged[key] = value.float().clone()
    
    # EMA update with subsequent models
    for model in models[1:]:
        for key in merged.keys():
            if key in model:
                merged[key] = decay * merged[key] + (1 - decay) * model[key].float()
    
    return merged


def merge_max_coverage(models: List[Dict[str, torch.Tensor]],
                       coverages: List[int]) -> Dict[str, torch.Tensor]:
    """Use model with maximum coverage contribution"""
    if not models or not coverages:
        raise ValueError("No models or coverage data")
    
    max_idx = coverages.index(max(coverages))
    return models[max_idx]


def get_model_stats(state: Dict[str, torch.Tensor]) -> Dict:
    """Get statistics about a model"""
    stats = {
        'num_parameters': 0,
        'num_layers': len(state),
        'layer_shapes': {}
    }
    
    for key, value in state.items():
        stats['num_parameters'] += value.numel()
        stats['layer_shapes'][key] = list(value.shape)
    
    return stats


def main():
    parser = argparse.ArgumentParser(description='Merge model weights from multiple training workers')
    parser.add_argument('models', nargs='+', help='Model files to merge (supports glob patterns)')
    parser.add_argument('-o', '--output', default='merged_model.pt', help='Output file path')
    parser.add_argument('-s', '--strategy', choices=['average', 'weighted', 'ema'], 
                        default='average', help='Merge strategy')
    parser.add_argument('--weights', type=float, nargs='+', help='Weights for weighted merge')
    parser.add_argument('--ema-decay', type=float, default=0.99, help='EMA decay factor')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')
    
    args = parser.parse_args()
    
    # Expand glob patterns
    model_paths = []
    for pattern in args.models:
        expanded = glob.glob(pattern)
        if expanded:
            model_paths.extend(expanded)
        elif os.path.exists(pattern):
            model_paths.append(pattern)
        else:
            print(f"Warning: No files matching '{pattern}'")
    
    # Remove duplicates while preserving order
    model_paths = list(dict.fromkeys(model_paths))
    
    if not model_paths:
        print("Error: No model files found")
        sys.exit(1)
    
    print(f"Found {len(model_paths)} models to merge")
    
    # Load all models
    models = []
    for path in model_paths:
        try:
            state = load_model(path)
            models.append(state)
            if args.verbose:
                stats = get_model_stats(state)
                print(f"  {path}: {stats['num_parameters']:,} params, {stats['num_layers']} layers")
        except Exception as e:
            print(f"Warning: Could not load {path}: {e}")
    
    if not models:
        print("Error: No models could be loaded")
        sys.exit(1)
    
    print(f"Loaded {len(models)} models successfully")
    
    # Perform merge
    print(f"Merging with strategy: {args.strategy}")
    
    if args.strategy == 'average':
        merged = merge_average(models)
    elif args.strategy == 'weighted':
        if args.weights:
            if len(args.weights) != len(models):
                print(f"Error: {len(args.weights)} weights provided for {len(models)} models")
                sys.exit(1)
            weights = args.weights
        else:
            # Default to equal weights
            weights = [1.0] * len(models)
        merged = merge_weighted(models, weights)
    elif args.strategy == 'ema':
        merged = merge_ema(models, decay=args.ema_decay)
    else:
        print(f"Unknown strategy: {args.strategy}")
        sys.exit(1)
    
    # Save merged model
    metadata = {
        'merged_from': model_paths,
        'strategy': args.strategy,
        'num_models': len(models)
    }
    save_model(merged, args.output, metadata)
    
    # Print final stats
    if args.verbose:
        stats = get_model_stats(merged)
        print(f"\nMerged model statistics:")
        print(f"  Parameters: {stats['num_parameters']:,}")
        print(f"  Layers: {stats['num_layers']}")


if __name__ == '__main__':
    main()
