#!/usr/bin/env python3
"""A menu's key handler acts on what a row is, never on where it is.

The regression this project kept having (0.10.2: Transition Speed turned Menu
Color; 2.7.0: Shelf Order opened Arrange Shelves; 2026-11-25 again): a
handler said "row 4", a row was added above it or shown only sometimes, and
every row after it acted for the one above. Rows carry an id from their build
(menu_row.id, MENU_ID) and handlers read it with menu_id().

This fails any key handler that compares its row number against a fixed
number or a named row (sel == 2, sel == SM_SORT, switch (sel), sel - 1),
unless it takes the row's id first (sel = menu_id(), or an id array its
build filled beside the rows, c->ids[sel]). A list of like items
(games, networks, discs) may still index its own array with sel, and compare
it with variables (sel == c->held), since its build made the rows from that
array in the same order.

    python3 tools/menu-lint.py src/*.c
"""
import re, sys

SIG = re.compile(r'static menu_result (\w+)\s*\(app \*a, void \*ctx, in_button key, int (\w+)\)\s*$')
BAD = [
    (re.compile(r'\b{v}\s*[=!]=\s*(-?\d+|[A-Z][A-Z0-9_]*)\b'), "compares the row number with a fixed row"),
    (re.compile(r'\b(-?\d+|[A-Z][A-Z0-9_]*)\s*[=!]=\s*{v}\b'), "compares the row number with a fixed row"),
    (re.compile(r'switch\s*\(\s*(\([a-z_]+\)\s*)?{v}\s*\)'), "switches on the row number"),
    (re.compile(r'\b{v}\s*[-+]\s*(\d|\()'), "does arithmetic on the row number"),
    (re.compile(r'\b{v}\s*[<>]=?\s*[A-Z][A-Z0-9_]*\b'), "compares the row number with a named row"),
]
fails = 0
for path in sys.argv[1:]:
    lines = open(path, encoding='utf-8', errors='replace').read().split('\n')
    i = 0
    while i < len(lines):
        m = SIG.search(lines[i])
        if not m:
            i += 1
            continue
        name, var = m.group(1), m.group(2)
        depth, started, body, j = 0, False, [], i + 1
        while j < len(lines):
            l = lines[j]
            body.append((j + 1, l))
            depth += l.count('{') - l.count('}')
            if '{' in l: started = True
            if started and depth == 0: break
            j += 1
        text = '\n'.join(l for _, l in body)
        # by id: the row's id (menu_id), or an id array the same build filled
        # row by row (c->ids[n] = ...; rows[n++] = ...), read at the row
        by_id = (re.search(r'\b%s\s*=\s*[^;]*menu_id\(\)' % var, text) or
                 re.search(r'->(ids|what)\[\s*%s\s*\]' % var, text))
        if not by_id:
            for ln, l in body:
                code = re.sub(r'/\*.*?\*/|//.*$', '', l)
                for rx, why in BAD:
                    if re.compile(rx.pattern.format(v=re.escape(var))).search(code):
                        print(f"  FAIL {path}:{ln}: {name} {why}: {l.strip()}")
                        fails += 1
                        break
        i = j + 1
print("menu rows by id:", "ok" if not fails else f"{fails} found; use menu_row.id and menu_id()")
sys.exit(1 if fails else 0)
