"""核對 Fuxi Android 的 RTP 檔與四種實機回饋的 HAP525 匯流排紀錄。"""
import argparse
import hashlib
import json
from pathlib import Path
import re

WRITE = re.compile(r'spmi_write_begin: opc=48 sid=07 addr=0x([0-9a-f]+) len=(\d+) buf=0x\[([0-9a-f-]+)\]')
PLAY = re.compile(r'\s(\d+\.\d+): qcom_haptics_play: play enable: ([01])')


def verify(trace, waveform):
    samples = bytearray()
    play = []
    rate = None
    fifo_play = False
    prefetched = None
    vmax = None
    for line in trace.splitlines():
        if match := PLAY.search(line):
            play.append((float(match[1]), int(match[2])))
        if match := WRITE.search(line):
            addr = int(match[1], 16)
            data = bytes.fromhex(match[3].replace('-', ' '))
            if len(data) != int(match[2]):
                raise ValueError('Incomplete SPMI payload')
            if addr in (0xf120, 0xf12c):
                if len(data) != (4 if addr == 0xf120 else 1):
                    raise ValueError('Unexpected FIFO transfer width')
                samples.extend(data)
            elif addr == 0xf048 and not fifo_play:
                vmax = data[0]
            elif addr == 0xf124 and not fifo_play:
                rate = data[0] & 15
            elif addr == 0xf04c and data[0] & 0x80:
                if data != b'\x80' or rate != 10 or fifo_play:
                    raise ValueError('Expected the 24 kHz FIFO source')
                prefetched = len(samples)
                fifo_play = True
    if samples != waveform or not fifo_play or [v for _, v in play[:2]] != [1, 0]:
        raise ValueError('Waveform or playback transitions differ')
    if 'FIFO HW status: filled = 0, available = 2048, full = 0, empty = 1' not in trace:
        raise ValueError('FIFO empty report missing')
    return {'play_ms': round((play[1][0] - play[0][0]) * 1000, 3),
            'vmax_code': vmax, 'prefilled_bytes': prefetched, 'refilled_bytes': len(samples) - prefetched}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('wave', type=Path)
    parser.add_argument('traces', type=Path)
    args = parser.parse_args()
    waveform = args.wave.read_bytes()
    if len(waveform) != 376 or waveform[-4:] != bytes(4):
        raise ValueError('Expected the complete Fuxi click with its silent tail')
    times = {name: verify((args.traces / f'{name}.trace').read_text(encoding='utf8'), waveform)
             for name in ('click', 'tick', 'long-press', 'switch-on')}
    print(json.dumps({'sha256': hashlib.sha256(waveform).hexdigest(), 'samples': len(waveform),
                      'rate_hz': 24000, 'hardware': times}, indent=2))


if __name__ == '__main__':
    main()
