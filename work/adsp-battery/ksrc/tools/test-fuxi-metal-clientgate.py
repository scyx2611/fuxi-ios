#!/usr/bin/env python3
"""Host ASan/UBSan tests for FuxiMetal ClientGate.hpp & stage-fuxi-metal.py U-026 policy."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
os.environ.setdefault('PYTHONDONTWRITEBYTECODE', '1')

ROOT = Path(__file__).resolve().parents[1]


def main():
    cxx = os.environ.get('CXX', 'clang++')
    with tempfile.TemporaryDirectory(prefix='fuxi-clientgate-') as tmp:
        bin_path = Path(tmp) / 'ClientGateTests'
        subprocess.run([
            cxx, '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
            '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
            str(ROOT / 'tools/fuxi-metal/ClientGateTests.cpp'),
            '-o', str(bin_path),
        ], check=True)
        run = subprocess.run([str(bin_path)], capture_output=True, text=True)
        assert run.returncode == 0, run.stdout + run.stderr
        assert 'PASS:' in run.stdout

    stage = subprocess.run(
        [sys.executable, '-B', str(ROOT / 'tools/test-fuxi-metal-stage.py'), '--policy-storage-only'],
        capture_output=True, text=True
    )
    assert stage.returncode == 0, stage.stdout + stage.stderr
    assert 'PASS:' in stage.stdout
    print('PASS: FuxiMetal ClientGate & stage-fuxi-metal U-026 permanent deny (SpringBoard/mediaserverd/*poster*)')


if __name__ == '__main__':
    main()
