"""Summarize stacks saved by `sm63 --profile FILE`: self and inclusive time per function.

usage: python3 tools/profile.py FILE [top-N]
"""
import subprocess
import sys
from collections import Counter, defaultdict

path = sys.argv[1]
top = int(sys.argv[2]) if len(sys.argv) > 2 else 25
stacks = [line.split() for line in open(path) if line.strip()]

# Symbolize every distinct (module, offset) with one addr2line call per module. Each
# address expands to its chain of inlined functions, innermost first.
by_module = defaultdict(set)
for s in stacks:
    for frame in s:
        mod, off = frame.rsplit(':', 1)
        by_module[mod].add(off)


def clean(fn):
    # Drop the parameter list (but keep "(anonymous namespace)").
    fn = fn.replace('(anonymous namespace)', '{anon}')
    fn = fn.split('(')[0]
    return fn.replace('{anon}', '(anon)')


names = {}
for mod, offs in by_module.items():
    offs = sorted(offs)
    out = subprocess.run(['addr2line', '-a', '-f', '-i', '-C', '-e', mod] + ['0x' + o for o in offs],
                         capture_output=True, text=True).stdout.splitlines()
    chains, cur = [], None
    i = 0
    while i < len(out):
        if out[i].startswith('0x') and (i + 1 >= len(out) or not out[i].startswith('0x0 ')):
            cur = []
            chains.append(cur)
            i += 1
            continue
        if cur is not None and i + 1 < len(out):
            cur.append(out[i])
        i += 2
    for off, chain in zip(offs, chains):
        chain = [clean(f) for f in chain if f != '??'] or [mod.rsplit('/', 1)[-1] + '+0x' + off]
        names[mod + ':' + off] = chain

SKIP = {'prof::onSignal', '__sigaction', '__restore_rt', '__libc_sigaction'}
expanded = []
for s in stacks:
    fns = [f for frame in s for f in names.get(frame, [frame])]  # e.g. linux-vdso, not a file
    # The innermost frames are the profiler's signal handler; skip past them.
    while fns and (fns[0] in SKIP or 'sigaction' in fns[0] or 'libc.so' in fns[0]):
        fns.pop(0)
    expanded.append(fns)
stacks = expanded

self_t, incl = Counter(), Counter()
for fns in stacks:
    if fns:
        self_t[fns[0]] += 1
    for f in set(fns):
        incl[f] += 1
n = len(stacks) or 1
print(f'{len(stacks)} samples')
print('-- self')
for f, c in self_t.most_common(top):
    print(f'{100 * c / n:5.1f}%  {f}')
print('-- inclusive')
for f, c in incl.most_common(top):
    print(f'{100 * c / n:5.1f}%  {f}')
