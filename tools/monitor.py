"""Observe training over SWD without halting the Cortex-M7."""
import argparse
import json
import time
from stlink import ROOT, STLink, symbols

FIELDS = ('sequence magic version stage fatal_count heartbeat_ms mode episodes successes timeouts '
          'total_steps state steps epsilon_permille last_steps last_return eval_steps frames '
          'spi_errors update_cycles render_us training_ms recent_success_percent demo_runs core_hz').split()

def sample(link, address):
    for _ in range(100):
        first = link.words(address, 1)[0]
        if first & 1:
            continue
        values = link.words(address, len(FIELDS))
        last = link.words(address, 1)[0]
        if first == values[0] == last and not last & 1:
            data = dict(zip(FIELDS, values))
            if data['magic'] != 0x514C3735 or data['version'] != 1:
                raise RuntimeError('Board firmware does not match Q-learning diagnostics version 1')
            if data['last_return'] & 0x80000000:
                data['last_return'] -= 1 << 32
            return data
    raise RuntimeError('Unable to obtain a coherent diagnostic snapshot')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--restart', action='store_true', help='Clear the Q table and restart training')
    parser.add_argument('--output', type=__import__('pathlib').Path, default=ROOT/'logs/monitor.json')
    args = parser.parse_args()
    syms = symbols(); rows = []
    with STLink() as link:
        if args.restart:
            link.command(f'write_memory {syms["g_command"]:#x} 32 {{1}}')
            time.sleep(0.1)
        start = time.monotonic()
        while True:
            data = sample(link, syms['g_diag'])
            data['elapsed_s'] = round(time.monotonic()-start, 3)
            rows.append(data)
            print(json.dumps(data), flush=True)
            if data['fatal_count'] or data['spi_errors']:
                raise RuntimeError('Firmware reported a fault or SPI error')
            if data['elapsed_s'] >= args.seconds:
                break
            time.sleep(2)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({'samples': rows, 'final': rows[-1]}, indent=2), encoding='utf-8')

if __name__ == '__main__':
    main()
