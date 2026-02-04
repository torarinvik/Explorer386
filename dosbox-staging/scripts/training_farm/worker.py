#!/usr/bin/env python3
"""
Training Worker Script

Runs a single DOSBox instance with Explorer instrumentation enabled.
This script is spawned by the training farm orchestrator (nn_farm.py).

Features:
- Runs DOSBox with specified game
- Manages Explorer neural network training
- Reports metrics to orchestrator
- Handles model checkpoints
- Supports headless operation via Xvfb

Usage:
    python worker.py --game-path /path/to/game --worker-id 0
    python worker.py --game-path /path/to/game --worker-id 1 --model model.pt
"""

import os
import sys
import json
import time
import argparse
import signal
import subprocess
import shutil
import tempfile
from pathlib import Path
from typing import Optional, Dict, List
from datetime import datetime


class ExplorerWorker:
    """Single DOSBox training worker"""
    
    def __init__(self, 
                 worker_id: int,
                 game_path: str,
                 dosbox_path: str,
                 output_dir: str,
                 model_path: Optional[str] = None,
                 headless: bool = True,
                 training_steps: int = 1000,
                 episode_timeout: float = 300.0):
        self.worker_id = worker_id
        self.game_path = Path(game_path)
        self.dosbox_path = Path(dosbox_path)
        self.output_dir = Path(output_dir)
        self.model_path = model_path
        self.headless = headless
        self.training_steps = training_steps
        self.episode_timeout = episode_timeout
        
        self.process: Optional[subprocess.Popen] = None
        self.xvfb_process: Optional[subprocess.Popen] = None
        self.display_num: Optional[int] = None
        self.start_time: Optional[float] = None
        self.metrics: Dict = {}
        
        # Create output directory
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.log_file = self.output_dir / f"worker_{worker_id}.log"
        self.metrics_file = self.output_dir / f"worker_{worker_id}_metrics.json"
        # Explorer training currently uses checkpoint files written by LibTorch:
        #   <checkpoint_base>.model, <checkpoint_base>.optimizer, <checkpoint_base>.stats
        # We keep a stable base name within each worker output directory.
        self.checkpoint_base = self.output_dir / "explorer_checkpoint"
        
    def log(self, message: str):
        """Write to log file"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        log_line = f"[{timestamp}] Worker {self.worker_id}: {message}"
        print(log_line)
        with open(self.log_file, 'a') as f:
            f.write(log_line + "\n")
    
    def find_executable(self) -> Optional[str]:
        """Find the game executable in the game directory"""
        exe_extensions = ['.exe', '.com', '.bat']
        candidates = []
        
        for ext in exe_extensions:
            for f in self.game_path.glob(f'*{ext}'):
                candidates.append(f)
            for f in self.game_path.glob(f'**/*{ext}'):
                candidates.append(f)
        
        # Prefer common executable names
        priority_names = ['game', 'main', 'start', 'run', 'play']
        
        for name in priority_names:
            for c in candidates:
                if name in c.stem.lower():
                    return str(c.relative_to(self.game_path))
        
        # Return first .exe or .com found
        for c in candidates:
            if c.suffix.lower() in ['.exe', '.com']:
                return str(c.relative_to(self.game_path))
        
        return None
    
    def setup_xvfb(self) -> bool:
        """Start Xvfb for headless display"""
        if not self.headless:
            return True

        try:
            # Try multiple display numbers. This avoids a common race where
            # multiple workers choose the same available display simultaneously.
            for display in range(100, 200):
                if os.path.exists(f"/tmp/.X{display}-lock"):
                    continue

                proc = subprocess.Popen(
                    ['Xvfb', f':{display}', '-screen', '0', '1024x768x24', '-nolisten', 'tcp'],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL
                )
                time.sleep(0.5)  # Give Xvfb time to start

                if proc.poll() is None:
                    self.xvfb_process = proc
                    self.display_num = display
                    self.log(f"Started Xvfb on display :{self.display_num}")
                    return True

                # Failed to start; try next display.
                try:
                    proc.kill()
                except Exception:
                    pass

            self.log("Could not start Xvfb on any display")
            return False

        except FileNotFoundError:
            self.log("Xvfb not found - running with real display")
            self.headless = False
            return True
    
    def create_dosbox_config(self) -> Path:
        """Create DOSBox configuration file"""
        config_path = self.output_dir / f"dosbox_worker_{self.worker_id}.conf"
        
        config_content = f"""
[sdl]
fullscreen=false
fulldouble=false
fullresolution=original
windowresolution=640x480
output=surface
autolock=false
sensitivity=100
waitonerror=true
priority=higher,normal
mapperfile=mapper-{self.worker_id}.map
usescancodes=true

[dosbox]
language=
machine=svga_s3
captures={self.output_dir}/capture
memsize=64

[render]
frameskip=0
aspect=false
scaler=none

[cpu]
core=auto
cputype=auto
cycles=max

[mixer]
nosound=true
rate=44100
blocksize=1024
prebuffer=25

[midi]
mpu401=intelligent
mididevice=none

[sblaster]
sbtype=sb16
sbbase=220
irq=7
dma=1
hdma=5
sbmixer=true
oplmode=auto
oplemu=default
oplrate=44100

[gus]
gus=false

[speaker]
pcspeaker=true
pcrate=44100
tandy=auto
tandyrate=44100
disney=true

[joystick]
joysticktype=auto
timed=true
autofire=false
swap34=false
buttonwrap=false

[serial]
serial1=dummy
serial2=dummy
serial3=disabled
serial4=disabled

[dos]
xms=true
ems=true
umb=true
keyboardlayout=auto

[ipx]
ipx=false

[explorer]
enable=true
"""

    # Training is enabled via environment variables (see explorer_training.cpp).
        
        config_content += f"""
[autoexec]
@echo off
mount c "{self.game_path}"
c:
"""
        
        # Find and run the game executable
        exe = self.find_executable()
        if exe:
            config_content += f"{exe}\n"
        
        config_content += "exit\n"
        
        with open(config_path, 'w') as f:
            f.write(config_content)
        
        self.log(f"Created config: {config_path}")
        return config_path
    
    def run(self) -> Dict:
        """Run the training session"""
        self.log(f"Starting training for game: {self.game_path.name}")
        self.start_time = time.time()
        
        # Setup virtual display
        if not self.setup_xvfb():
            return {'status': 'error', 'error': 'Failed to setup display'}
        
        # Create config
        config_path = self.create_dosbox_config()
        
        # Prepare environment
        env = os.environ.copy()
        if self.headless and self.display_num:
            env['DISPLAY'] = f':{self.display_num}'

        # Enable Explorer training via environment variables.
        env['EXPLORER_TRAINING'] = '1'
        env['EXPLORER_TRAINING_STEPS'] = str(self.training_steps)
        # Save frequently so the farm can pick up checkpoints quickly.
        env.setdefault('EXPLORER_TRAINING_SAVE_INTERVAL', '1')
        env['EXPLORER_TRAINING_CHECKPOINT'] = str(self.checkpoint_base)

        # If an initial checkpoint base is provided, copy it into place so Explorer
        # can auto-load it (StartTraining loads when <checkpoint>.model exists).
        if self.model_path:
            src_base = Path(self.model_path)
            for suffix in ('.model', '.optimizer', '.stats'):
                src = Path(str(src_base) + suffix)
                dst = Path(str(self.checkpoint_base) + suffix)
                if src.exists():
                    try:
                        shutil.copy(src, dst)
                    except Exception:
                        pass
        
        # Build command
        cmd = [str(self.dosbox_path), '-conf', str(config_path)]
        
        self.log(f"Running: {' '.join(cmd)}")
        
        try:
            # Start DOSBox
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                env=env,
                cwd=str(self.game_path)
            )
            
            # Wait for completion or timeout
            try:
                stdout, _ = self.process.communicate(timeout=self.episode_timeout)
                
                # Parse output for metrics
                output = stdout.decode('utf-8', errors='ignore')
                self.parse_metrics(output)
                
            except subprocess.TimeoutExpired:
                self.log("Training timeout - terminating")
                self.process.terminate()
                try:
                    stdout, _ = self.process.communicate(timeout=5)
                    output = stdout.decode('utf-8', errors='ignore') if stdout else ''
                    self.parse_metrics(output)
                except subprocess.TimeoutExpired:
                    self.process.kill()
            
            self.metrics['status'] = 'completed'
            self.metrics['duration'] = time.time() - self.start_time
            self.metrics['exit_code'] = self.process.returncode
            
        except Exception as e:
            self.log(f"Error running DOSBox: {e}")
            self.metrics['status'] = 'error'
            self.metrics['error'] = str(e)
        
        finally:
            self.cleanup()
        
        # Save metrics
        with open(self.metrics_file, 'w') as f:
            json.dump(self.metrics, f, indent=2)
        
        self.log(f"Training completed: {self.metrics.get('status')}")
        return self.metrics
    
    def parse_metrics(self, output: str):
        """Parse training metrics from DOSBox output"""
        lines = output.split('\n')
        
        for line in lines:
            # Look for Explorer metrics in output
            if 'EXPLORER:' in line or 'explorer:' in line.lower():
                try:
                    # Parse key=value pairs
                    parts = line.split()
                    for part in parts:
                        if '=' in part:
                            key, value = part.split('=', 1)
                            try:
                                self.metrics[key] = float(value)
                            except ValueError:
                                self.metrics[key] = value
                except Exception:
                    pass
            
            # Look for coverage data
            if 'coverage' in line.lower():
                try:
                    # Extract percentage
                    import re
                    match = re.search(r'(\d+\.?\d*)%', line)
                    if match:
                        self.metrics['coverage_percent'] = float(match.group(1))
                except Exception:
                    pass
            
            # Look for training step info
            if 'step' in line.lower() and ('loss' in line.lower() or 'reward' in line.lower()):
                try:
                    import re
                    step_match = re.search(r'step\s*[:\s]*(\d+)', line, re.IGNORECASE)
                    if step_match:
                        self.metrics['training_steps'] = int(step_match.group(1))
                except Exception:
                    pass
    
    def cleanup(self):
        """Clean up resources"""
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
        
        if self.xvfb_process and self.xvfb_process.poll() is None:
            self.xvfb_process.terminate()
            try:
                self.xvfb_process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.xvfb_process.kill()
    
    def handle_signal(self, signum, frame):
        """Handle termination signals"""
        self.log(f"Received signal {signum}")
        self.cleanup()
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description='DOSBox Explorer Training Worker')
    parser.add_argument('--game-path', required=True, help='Path to game directory')
    parser.add_argument('--worker-id', type=int, default=0, help='Worker ID')
    parser.add_argument('--dosbox-path', default='dosbox', help='Path to DOSBox executable')
    parser.add_argument('--output-dir', default='./worker_output', help='Output directory')
    parser.add_argument('--model', help='Path to initial model weights')
    parser.add_argument('--training-steps', type=int, default=1000, help='Training steps per episode')
    parser.add_argument('--timeout', type=float, default=300.0, help='Episode timeout in seconds')
    parser.add_argument('--no-headless', action='store_true', help='Run with visible display')
    
    args = parser.parse_args()
    
    # Find DOSBox executable
    dosbox_path = args.dosbox_path
    if not os.path.exists(dosbox_path):
        # Try common locations
        candidates = [
            '../../../build/dosbox',
            '../../build/dosbox',
            '/workspace/Explorer386/dosbox-staging/build/dosbox'
        ]
        for candidate in candidates:
            if os.path.exists(candidate):
                dosbox_path = candidate
                break
    
    if not os.path.exists(dosbox_path):
        print(f"Error: DOSBox not found at {dosbox_path}")
        sys.exit(1)
    
    # Create worker
    worker = ExplorerWorker(
        worker_id=args.worker_id,
        game_path=args.game_path,
        dosbox_path=dosbox_path,
        output_dir=args.output_dir,
        model_path=args.model,
        headless=not args.no_headless,
        training_steps=args.training_steps,
        episode_timeout=args.timeout
    )
    
    # Setup signal handlers
    signal.signal(signal.SIGTERM, worker.handle_signal)
    signal.signal(signal.SIGINT, worker.handle_signal)
    
    # Run training
    metrics = worker.run()
    
    # Print final metrics as JSON for parsing by orchestrator
    print(f"\n=== WORKER METRICS ===")
    print(json.dumps(metrics, indent=2))
    
    sys.exit(0 if metrics.get('status') == 'completed' else 1)


if __name__ == '__main__':
    main()
