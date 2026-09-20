import sys
sys.stdout.reconfigure(encoding='utf-8')
BS = chr(92)  # backslash
Q = chr(34)   # double quote
lines = open('bench_e8.cpp', encoding='utf-8').read().split('\n')
for ln in range(160, 176):
    line = lines[ln-1]
    in_str = False
    esc = False
    events = []
    for i, ch in enumerate(line):
        if in_str:
            if esc:
                esc = False
            elif ch == BS:
                esc = True
            elif ch == Q:
                in_str = False
                events.append((i+1, 'close'))
        else:
            if ch == Q:
                in_str = True
                events.append((i+1, 'open'))
    status = 'BALANCED' if not in_str else 'UNTERMINATED-AT-EOL'
    print(ln, status, events[-4:] if len(events) > 4 else events)
