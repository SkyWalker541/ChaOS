#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""ChaOS: a stand-in RomM server, for `make check-romm` (tools/romm-check.c).

    tools/romm-mock.py PORT [--old]

Answers the parts of RomM's API ChaOS uses (src/romm.h), as RomM 5 does, or
with --old as RomM 4 does (no device pairing, a pairing code instead): a
user, two platforms with games (one of them in several files), firmware, and
save sync with the server's plan. Pairing is approved on the second poll.
State lives for the life of the process; POST /__state shows it."""
import io, json, sys, zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

OLD = '--old' in sys.argv
TOKEN = 'rmm_testtoken'
state = {'polls': 0, 'saves': {}, 'next_save': 1, 'uploads': [], 'downloaded': [], 'completed': [],
         'devices': []}
# a server save for rom 2, newer than anything the device has
state['saves'][41] = {'id': 41, 'rom_id': 2, 'file_name': 'Tetris.srm', 'content': b'SERVER-SAVE'}

PLATFORMS = [
    {'id': 1, 'slug': 'gb', 'fs_slug': 'gb', 'name': 'Game Boy', 'custom_name': '', 'rom_count': 2, 'firmware_count': 1},
    {'id': 2, 'slug': 'tg16', 'fs_slug': 'tg16', 'name': 'TurboGrafx-16', 'custom_name': 'PC Engine',
     'rom_count': 1, 'firmware_count': 0},
    {'id': 3, 'slug': 'ps2', 'fs_slug': 'ps2', 'name': 'PlayStation 2', 'custom_name': '', 'rom_count': 9, 'firmware_count': 0},
]
ROMS = [
    {'id': 1, 'platform_id': 1, 'name': 'Link\'s "Awakening"', 'fs_name': 'Zelda (USA).gb', 'fs_size_bytes': 12,
     'has_multiple_files': False, 'path_cover_small': '/assets/romm/resources/roms/1/1/cover/small.png',
     'files': [{'id': 7, 'file_name': 'Zelda manual.pdf', 'category': 'manual'},
               {'id': 8, 'file_name': 'Zelda (USA).gb', 'category': None}]},
    {'id': 2, 'platform_id': 1, 'name': 'Tetris', 'fs_name': 'Tetris.gb', 'fs_size_bytes': 9,
     'has_multiple_files': False, 'url_cover': 'https://images.igdb.com/t.jpg',
     'files': [{'id': 9, 'file_name': 'Tetris.gb'}]},
    {'id': 3, 'platform_id': 2, 'name': 'Ys Book I & II', 'fs_name': 'Ys I & II', 'fs_size_bytes': 30,
     'has_multiple_files': True, 'files': [{'id': 10, 'file_name': 'Ys.cue'}, {'id': 11, 'file_name': 'Ys.bin'}]},
]
CONTENT = {8: b'ZELDA-ROM-12', 9: b'TETRIS-GB'}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, obj=None, raw=None, ctype='application/json'):
        body = raw if raw is not None else json.dumps(obj).encode()
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def authed(self):
        if self.headers.get('Authorization') != 'Bearer ' + TOKEN:
            self.send(401, {'detail': 'Not authenticated'})
            return False
        return True

    def body(self):
        n = int(self.headers.get('Content-Length') or 0)
        return self.rfile.read(n) if n else b''

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        p = u.path
        if p.startswith('/assets/'):
            return self.send(200, raw=b'\x89PNG cover', ctype='image/png')
        if not self.authed():
            return
        if p == '/api/users/me':
            return self.send(200, {'id': 1, 'username': 'player'})
        if p == '/api/platforms':
            return self.send(200, PLATFORMS)
        if p == '/api/roms':
            pid = int((q.get('platform_ids') or q.get('platform_id') or ['0'])[0])
            off, lim = int(q.get('offset', ['0'])[0]), int(q.get('limit', ['50'])[0])
            items = [r for r in ROMS if r['platform_id'] == pid]
            return self.send(200, {'items': items[off:off + lim], 'total': len(items), 'limit': lim, 'offset': off})
        if p.startswith('/api/roms/') and '/content/' in p:
            rid = int(p.split('/')[3])
            rom = next(r for r in ROMS if r['id'] == rid)
            if rom['has_multiple_files']:
                b = io.BytesIO()
                with zipfile.ZipFile(b, 'w') as z:
                    z.writestr('Ys.cue', 'FILE "Ys.bin" BINARY\n')
                    z.writestr('Ys.bin', 'BIN')
                return self.send(200, raw=b.getvalue(), ctype='application/zip')
            fid = int(q.get('file_ids', ['0'])[0])
            return self.send(200, raw=CONTENT[fid], ctype='application/octet-stream')
        if p == '/api/firmware':
            if q.get('platform_id') == ['1']:
                return self.send(200, [{'id': 5, 'file_name': 'gb_bios.bin', 'file_size_bytes': 4, 'md5_hash': 'x'},
                                       {'id': 6, 'file_name': '../evil.bin', 'file_size_bytes': 4}])
            return self.send(200, [])
        if p.startswith('/api/firmware/') and '/content/' in p:
            return self.send(200, raw=b'BIOS', ctype='application/octet-stream')
        if p.startswith('/api/saves/') and p.endswith('/content'):
            sid = int(p.split('/')[3])
            return self.send(200, raw=state['saves'][sid]['content'], ctype='application/octet-stream')
        self.send(404, {'detail': 'Not Found'})

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        p = u.path
        raw = self.body()
        if p == '/__state':
            s = dict(state)
            s['saves'] = {k: {kk: (vv.decode() if isinstance(vv, bytes) else vv) for kk, vv in v.items()}
                          for k, v in state['saves'].items()}
            return self.send(200, s)
        if p == '/api/auth/device/init':
            if OLD:
                return self.send(404, {'detail': 'Not Found'})
            return self.send(200, {'device_code': 'dc-1', 'user_code': 'WXYZ-1234',
                                   'verification_path': '/pair/device',
                                   'verification_path_complete': '/pair/device?user_code=WXYZ-1234',
                                   'expires_in': 600, 'interval': 1})
        if p == '/api/auth/device/token':
            state['polls'] += 1
            if json.loads(raw).get('device_code') != 'dc-1':
                return self.send(400, {'detail': 'invalid_grant'})
            if state['polls'] < 2:
                return self.send(400, {'detail': 'authorization_pending'})
            return self.send(200, {'access_token': TOKEN, 'device_id': 'dev-brick-1', 'scopes': [],
                                   'expires_at': None})
        if p == '/api/client-tokens/exchange':
            if json.loads(raw).get('code') == 'ABCD1234':
                return self.send(200, {'raw_token': TOKEN, 'name': 'ChaOS', 'scopes': []})
            return self.send(400, {'detail': 'Invalid or expired pairing code'})
        if not self.authed():
            return
        if p == '/api/devices':
            state['devices'].append(json.loads(raw))
            return self.send(201, {'device_id': 'dev-brick-2', 'name': 'x'})
        if p == '/api/sync/negotiate':
            req = json.loads(raw)
            ops = []
            for s in req['saves']:
                ops.append({'action': 'upload', 'rom_id': s['rom_id'], 'save_id': None,
                            'file_name': s['file_name'], 'reason': 'device newer'})
            ops.append({'action': 'download', 'rom_id': 2, 'save_id': 41, 'file_name': 'Tetris.srm',
                        'reason': 'server newer'})
            ops.append({'action': 'download', 'rom_id': 999, 'save_id': 42, 'file_name': 'Other.srm',
                        'reason': 'not here'})
            state['negotiated'] = req
            return self.send(200, {'session_id': 77, 'operations': ops, 'total_upload': len(req['saves']),
                                   'total_download': 2, 'total_conflict': 0, 'total_no_op': 0})
        if p == '/api/saves':
            ctype = self.headers.get('Content-Type', '')
            ok = 'multipart/form-data' in ctype and b'name="saveFile"' in raw
            state['uploads'].append({'rom_id': q.get('rom_id', [''])[0], 'device_id': q.get('device_id', [''])[0],
                                     'ok': ok, 'has': raw.count(b'LOCAL-SAVE') > 0})
            return self.send(201 if ok else 400, {'id': 50})
        if p.startswith('/api/saves/') and p.endswith('/downloaded'):
            state['downloaded'].append(int(p.split('/')[3]))
            return self.send(200, {})
        if p.startswith('/api/sync/sessions/') and p.endswith('/complete'):
            state['completed'].append(json.loads(raw))
            return self.send(200, {'session': {'id': 77}})
        self.send(404, {'detail': 'Not Found'})


if __name__ == '__main__':
    ThreadingHTTPServer(('127.0.0.1', int(sys.argv[1])), H).serve_forever()
