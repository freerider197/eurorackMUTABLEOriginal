#!/usr/bin/env python3
"""Worst-case stack use of the Tomline firmware, from its disassembly.

The main loop, the SysTick interrupt (4 kHz) and the ADC DMA interrupt
(31 kHz) all share one stack. In the worst case the DMA interrupt fires while
SysTick is running, and SysTick fires while the main loop is at its deepest
point, so the three depths add up (plus 32 bytes per interrupt, pushed by the
hardware, and 4 bytes of alignment padding).

Usage: python3 tomline/tools/stack_usage.py build/tomline/tomline.elf
Exits with an error if the worst case does not fit in the stack reserved by
the linker script (_estack - _sstack) with a 25 % margin.
"""

import re
import subprocess
import sys

OBJDUMP = 'arm-none-eabi-objdump'
NM = 'arm-none-eabi-nm'

FUNC_RE = re.compile(r'^([0-9a-f]+) <(.+)>:$')
PUSH_RE = re.compile(r'\b(?:push|stmdb(?:\.w)?\s+sp!,)\s*\{([^}]*)\}')
SUB_SP_RE = re.compile(r'\bsub(?:\.w)?\s+sp,\s*(?:sp,\s*)?#(\d+)')
CALL_RE = re.compile(r'\b(?:bl|blx)\s+[0-9a-f]+ <([^>+]+)>')
TAIL_RE = re.compile(r'\bb(?:\.w|\.n)?\s+[0-9a-f]+ <([^>+]+)>')
EXCEPTION_FRAME = 32 + 4
# Calls through a function pointer, which the disassembly does not show.
INDIRECT_CALLS = {
  'DMA1_Channel1_IRQHandler': ['Process(unsigned short*)'],
}


def count_registers(text):
  n = 0
  for part in text.split(','):
    part = part.strip()
    if '-' in part:
      a, b = part.split('-')
      n += int(b.strip()[1:]) - int(a.strip()[1:]) + 1
    elif part:
      n += 1
  return n


def parse(elf):
  out = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', '-C', elf],
                       capture_output=True, text=True, check=True).stdout
  frames, calls, current = {}, {}, None
  for line in out.splitlines():
    m = FUNC_RE.match(line)
    if m:
      current = m.group(2)
      frames[current] = 0
      calls[current] = set()
      continue
    if current is None:
      continue
    m = PUSH_RE.search(line)
    if m:
      frames[current] += 4 * count_registers(m.group(1))
    m = SUB_SP_RE.search(line)
    if m:
      frames[current] += int(m.group(1))
    m = CALL_RE.search(line) or TAIL_RE.search(line)
    if m and m.group(1) != current:
      calls[current].add(m.group(1))
  for caller, callees in INDIRECT_CALLS.items():
    calls.setdefault(caller, set()).update(callees)
  return frames, calls


def depth(name, frames, calls, memo, stack=()):
  if name in memo:
    return memo[name]
  if name in stack:
    return 0  # recursion: not expected in this firmware
  best, path = 0, []
  for callee in calls.get(name, ()):
    d, p = depth(callee, frames, calls, memo, stack + (name,))
    if d > best:
      best, path = d, p
  memo[name] = (frames.get(name, 0) + best, [name] + path)
  return memo[name]


def find(frames, *candidates):
  for c in candidates:
    for name in frames:
      if name == c or name.startswith(c + '('):
        return name
  raise SystemExit('function not found: %s' % (candidates,))


def symbol(elf, name):
  out = subprocess.run([NM, elf], capture_output=True, text=True,
                       check=True).stdout
  for line in out.splitlines():
    parts = line.split()
    if len(parts) == 3 and parts[2] == name:
      return int(parts[0], 16)
  raise SystemExit('symbol not found: ' + name)


def main():
  elf = sys.argv[1]
  frames, calls = parse(elf)
  memo = {}
  total = 0
  # Init() runs before the interrupts start, so only the loop counts.
  main = find(frames, 'main')
  calls[main] = {find(frames, 'tomline::Ui::DoEvents')}
  for label, names in (
      ('main loop', ('main',)),
      ('SysTick interrupt', ('SysTick_Handler',)),
      ('ADC DMA interrupt', ('DMA1_Channel1_IRQHandler',))):
    d, path = depth(find(frames, *names), frames, calls, memo)
    extra = 0 if label == 'main loop' else EXCEPTION_FRAME
    total += d + extra
    print('%-18s %4d bytes  %s' % (label, d + extra, ' > '.join(path)))
  size = symbol(elf, '_estack') - symbol(elf, '_sstack')
  print('Worst case         %4d bytes, stack reserved: %d bytes' % (total, size))
  if total * 5 // 4 > size:
    print('FAIL: the stack can overflow into other variables')
    sys.exit(1)
  print('OK: %d bytes of margin' % (size - total))


if __name__ == '__main__':
  main()
