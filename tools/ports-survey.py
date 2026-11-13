#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ChaOS: which libraries PortMaster's ports need that a Brick under ChaOS
hasn't got - the list the port library pack (tools/build-libpack.sh) is
made from.

    tools/ports-survey.py PORTS_JSON HAVE.txt OUT_DIR [--jobs N] [--max-mb N] [--have-dir D]...

PORTS_JSON is PortMaster's catalog; HAVE.txt the library file names the
Brick has (its root's /lib and /usr/lib, and ChaOS's port libraries), a
name a line. Each --have-dir is a folder of those libraries themselves
(the root's lib folders, ChaOS's port libraries unpacked), read for what
they need in turn - a port's own copy of a library, on its library path
first, can need what the Brick's doesn't (Doom Engines' libsndfile wants
an older FLAC). For every port that may run on ChaOS (src/pmcat.c's rule)
it reads the zip's central directory with an HTTP range request, then
fetches only the members that look like programs or libraries, again by
range, and reads each ELF's DT_NEEDED. A library is had if the Brick has
it or the port carries a file of that name. Members alike in CRC and size
across ports are fetched once.

Writes OUT_DIR/needed.json (each port: what it needs and lacks) and
OUT_DIR/missing.txt (each missing library, by how many ports want it).
Progress is kept in OUT_DIR/cache, so a run stopped picks up again.
"""
import concurrent.futures as cf
import hashlib
import json
import os
import struct
import sys
import time
import urllib.request
import zlib

BAD = {'analog_2', '2gb', 'opengl', '!trimui', 'ultra', 'armhf'}
DATA_EXT = {'.png', '.jpg', '.jpeg', '.ogg', '.wav', '.mp3', '.txt', '.md', '.json', '.xml',
            '.gptk', '.pck', '.wad', '.pak', '.dat', '.cfg', '.ini', '.lua', '.py', '.sh',
            '.ttf', '.otf', '.zip', '.love', '.apk', '.win', '.ogv', '.webm', '.mp4', '.bmp',
            '.gif', '.tga', '.csv', '.html', '.css', '.js', '.yml', '.yaml', '.squashfs',
            '.droid', '.port', '.svg', '.mod', '.xm', '.it', '.s3m', '.mid', '.sf2', '.sf3',
            '.flac', '.opus', '.m4a', '.glsl', '.vert', '.frag', '.shader', '.po', '.mo',
            '.pdf', '.license', '.gz', '.xz', '.7z', '.rar', '.tar', '.bin.png', '.rom',
            '.iso', '.cue', '.bik', '.dll', '.exe', '.pdb', '.jar', '.class', '.rpy',
            '.rpyc', '.rpa', '.pyc', '.pyo', '.data', '.unity3d', '.assets', '.resS'}


def may_run(attr):
    reqs = attr.get('reqs') or []
    for r in reqs:
        if r in BAD or ('|' in r and 'analog_1' not in r):
            return False
    arch = attr.get('arch') or []
    return not arch or 'aarch64' in arch


def get_range(url, start, end, tries=4):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={'Range': 'bytes=%d-%d' % (start, end)})
            with urllib.request.urlopen(req, timeout=120) as r:
                return r.read()
        except Exception:
            if i == tries - 1:
                raise
            time.sleep(2 + i * 3)


def central(url, size):
    tail = get_range(url, max(0, size - 65558), size - 1)
    at = tail.rfind(b'PK\x05\x06')
    if at < 0:
        raise ValueError('no end of central directory')
    count, cd_size, cd_off = struct.unpack('<HII', tail[at + 10:at + 20])
    if cd_off == 0xFFFFFFFF or count == 0xFFFF:
        # zip64: its locator sits just before
        loc = tail.rfind(b'PK\x06\x07')
        z64 = struct.unpack('<Q', tail[loc + 8:loc + 16])[0]
        rec = get_range(url, z64, z64 + 55)
        count, cd_size, cd_off = struct.unpack('<QQQ', rec[32:56])
    cd = get_range(url, cd_off, cd_off + cd_size - 1)
    out, p = [], 0
    while p + 46 <= len(cd) and cd[p:p + 4] == b'PK\x01\x02':
        (method, crc, csize, usize, nl, xl, cl, ext_attr, loff) = (
            struct.unpack('<H', cd[p + 10:p + 12])[0], *struct.unpack('<III', cd[p + 16:p + 28]),
            *struct.unpack('<HHH', cd[p + 28:p + 34]), struct.unpack('<I', cd[p + 38:p + 42])[0],
            struct.unpack('<I', cd[p + 42:p + 46])[0])
        name = cd[p + 46:p + 46 + nl].decode('utf-8', 'replace')
        extra = cd[p + 46 + nl:p + 46 + nl + xl]
        # zip64 sizes and offset
        q = 0
        while q + 4 <= len(extra):
            hid, hl = struct.unpack('<HH', extra[q:q + 4])
            if hid == 1:
                vals, r = [], q + 4
                for v, is64 in ((usize, usize == 0xFFFFFFFF), (csize, csize == 0xFFFFFFFF),
                                (loff, loff == 0xFFFFFFFF)):
                    if is64:
                        vals.append(struct.unpack('<Q', extra[r:r + 8])[0])
                        r += 8
                    else:
                        vals.append(v)
                usize, csize, loff = vals
            q += 4 + hl
        out.append(dict(name=name, method=method, crc=crc, csize=csize, usize=usize,
                        mode=(ext_attr >> 16) & 0o170777, off=loff))
        p += 46 + nl + xl + cl
    return out


def looks_like_program(m):
    n = m['name']
    if n.endswith('/'):
        return False
    base = n.rsplit('/', 1)[-1]
    low = base.lower()
    if '.so' in low:
        return True
    ext = os.path.splitext(low)[1]
    if ext in DATA_EXT:
        return False
    if m['mode'] & 0o111:
        return True
    return ext in ('', '.aarch64', '.arm64', '.elf', '.bin', '.x86_64') or low.endswith('64')


def member(url, m):
    head = get_range(url, m['off'], m['off'] + 29)
    nl, xl = struct.unpack('<HH', head[26:30])
    start = m['off'] + 30 + nl + xl
    raw = get_range(url, start, start + m['csize'] - 1) if m['csize'] else b''
    if m['method'] == 0:
        return raw
    if m['method'] == 8:
        return zlib.decompressobj(-15).decompress(raw)
    raise ValueError('method %d' % m['method'])


def elf_needed(b):
    """(soname, needed, machine) of an ELF64 little-endian, or None"""
    if len(b) < 64 or b[:4] != b'\x7fELF' or b[4] != 2 or b[5] != 1:
        return None
    machine = struct.unpack('<H', b[18:20])[0]
    phoff, = struct.unpack('<Q', b[32:40])
    phentsize, phnum = struct.unpack('<HH', b[54:58])
    loads, dyn = [], None
    for i in range(phnum):
        o = phoff + i * phentsize
        if o + 56 > len(b):
            break
        ptype, _, poff, pvaddr, _, pfilesz = struct.unpack('<IIQQQQ', b[o:o + 40])
        if ptype == 1:
            loads.append((pvaddr, poff, pfilesz))
        elif ptype == 2:
            dyn = (poff, pfilesz)
    if not dyn:
        return ('', [], machine)

    def v2o(va):
        for vaddr, off, sz in loads:
            if vaddr <= va < vaddr + sz:
                return va - vaddr + off
        return None

    entries, strtab, needed, soname = [], None, [], None
    for i in range(dyn[1] // 16):
        tag, val = struct.unpack('<qQ', b[dyn[0] + i * 16:dyn[0] + i * 16 + 16])
        if tag == 0:
            break
        if tag == 5:
            strtab = v2o(val)
        entries.append((tag, val))
    if strtab is None:
        return ('', [], machine)

    def s(o):
        e = b.index(b'\0', strtab + o)
        return b[strtab + o:e].decode('utf-8', 'replace')

    for tag, val in entries:
        if tag == 1:
            needed.append(s(val))
        elif tag == 14:
            soname = s(val)
    return (soname or '', needed, machine)


def survey_port(key, port, cache_dir, max_bytes):
    path = os.path.join(cache_dir, key + '.json')
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    src = port['source']
    url, size = src['url'], src['size']
    members = central(url, size)
    names = {m['name'].rsplit('/', 1)[-1] for m in members if not m['name'].endswith('/')}
    progs = []
    for m in members:
        if not looks_like_program(m) or m['usize'] > max_bytes:
            continue
        mk = '%08x-%d' % (m['crc'], m['usize'])
        mp = os.path.join(cache_dir, 'm-' + mk + '.json')
        if os.path.exists(mp):
            with open(mp) as f:
                info = json.load(f)
        else:
            try:
                data = member(url, m)
                info = elf_needed(data)
                info = list(info) if info else None
            except Exception as e:
                info = {'error': str(e)}
            with open(mp, 'w') as f:
                json.dump(info, f)
        if isinstance(info, list) and info[2] == 183:   # aarch64
            progs.append({'name': m['name'], 'soname': info[0], 'needed': info[1]})
    out = {'zip': key, 'title': port['attr'].get('title', key), 'carries': sorted(names),
           'programs': progs}
    with open(path + '.tmp', 'w') as f:
        json.dump(out, f)
    os.replace(path + '.tmp', path)
    return out


def main():
    args = sys.argv[1:]
    jobs, max_mb = 8, 120
    if '--jobs' in args:
        i = args.index('--jobs'); jobs = int(args[i + 1]); del args[i:i + 2]
    if '--max-mb' in args:
        i = args.index('--max-mb'); max_mb = int(args[i + 1]); del args[i:i + 2]
    have_dirs = []
    while '--have-dir' in args:
        i = args.index('--have-dir'); have_dirs.append(args[i + 1]); del args[i:i + 2]
    cat_path, have_path, out = args
    have_needs = {}
    for d in have_dirs:
        for root, _, files in os.walk(d):
            for fn in files:
                fp = os.path.join(root, fn)
                if os.path.islink(fp) or '.so' not in fn:
                    continue
                try:
                    with open(fp, 'rb') as f:
                        info = elf_needed(f.read())
                except Exception:
                    info = None
                if info:
                    have_needs[fn] = info[1]
                    if info[0]:
                        have_needs[info[0]] = info[1]
    cat = json.load(open(cat_path))['ports']
    have = {l.strip() for l in open(have_path) if l.strip()}
    cache = os.path.join(out, 'cache')
    os.makedirs(cache, exist_ok=True)
    todo = [(k, v) for k, v in cat.items() if may_run(v['attr'])]
    results, failed = {}, {}
    with cf.ThreadPoolExecutor(jobs) as ex:
        fut = {ex.submit(survey_port, k, v, cache, max_mb << 20): k for k, v in todo}
        for i, f in enumerate(cf.as_completed(fut)):
            k = fut[f]
            try:
                results[k] = f.result()
            except Exception as e:
                failed[k] = str(e)
            if i % 25 == 0:
                print('%d/%d' % (i + 1, len(todo)), file=sys.stderr, flush=True)
    wants = {}
    report = {}
    for k, r in results.items():
        carried = set(r['carries'])
        # What each library the port carries needs, by its file name and
        # its soname: followed only from a program that loads it, so a
        # library shipped and never used doesn't count
        libs = {}
        for p in r['programs']:
            base = p['name'].rsplit('/', 1)[-1]
            libs[base] = p['needed']
            if p['soname']:
                libs[p['soname']] = p['needed']
        lacks = set()
        for p in r['programs']:
            base = p['name'].rsplit('/', 1)[-1]
            if '.so' in base or p['soname']:
                continue                       # a library: reached from a program
            seen, todo = set(), list(p['needed'])
            while todo:
                n = todo.pop()
                if n in seen:
                    continue
                seen.add(n)
                if n in libs:                  # the port's own copy, first
                    todo.extend(libs[n])
                elif n in carried:
                    continue
                elif n in have:
                    todo.extend(have_needs.get(n, []))
                else:
                    lacks.add(n)
        report[k] = {'title': r['title'], 'lacks': sorted(lacks),
                     'programs': len(r['programs'])}
        for n in lacks:
            wants.setdefault(n, []).append(k)
    with open(os.path.join(out, 'needed.json'), 'w') as f:
        json.dump({'ports': report, 'failed': failed}, f, indent=1)
    with open(os.path.join(out, 'missing.txt'), 'w') as f:
        for n, ks in sorted(wants.items(), key=lambda x: (-len(x[1]), x[0])):
            f.write('%4d  %s  (%s)\n' % (len(ks), n, ', '.join(sorted(ks)[:6]) + (' ...' if len(ks) > 6 else '')))
    print('surveyed %d, failed %d, missing libraries %d' % (len(results), len(failed), len(wants)))


if __name__ == '__main__':
    main()
