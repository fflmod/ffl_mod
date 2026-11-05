#!/usr/bin/env python3
"""追加ジョブの絵の描き替え（髪と目）を、fflmod に埋め込むデータ（sprite_patch.bin）にする。

使い方: gen_sprite_patch.py 絵の作業フォルダ src/sprite_patch.bin
  絵の作業フォルダ: final/<ジョブ>_<キャラ>.pkl（確定版。(色替えの絵, 描き替えた絵, 記録) の19コマ）、
                    files/fldchr*.dat（ゲームの絵）、recolor_tab.h（公開ソースと同じ色替えパレット）

形式（数はすべて little endian）:
  'FSP1', u32 件数
  件数 × { u8 ジョブ, u8 キャラ, u8 2枚目なら1, u8 足す色の数, u16 幅, u16 高さ,
           u32 元の点の番号（幅×高さ）の FNV-1a, u32 データの位置, u32 データの長さ }
  データ: 足す色 × { u8 番号, u8 R, G, B, A }、続いて 点のかたまり × { u32 位置（y×幅＋x）, u16 長さ, 番号 × 長さ }
mod は、色替えで読んだ元の絵の点の番号の FNV-1a がこの値と同じときだけ、色を足して点を書き換える。
"""
import os, pickle, re, struct, sys
import numpy as np
D = sys.argv[1]
sys.path.insert(0, os.path.join(D, 'tools2')); import icp

JOB = {'ranger': 41, 'dk': 43, 'dancer': 45, 'ninja': 47, 'majin': 49,
       'dragoon': 40, 'bard': 42, 'memorist': 44, 'knight': 46, 'sage': 48}
CHARA = {'sol': 0, 'aig': 1, 'dusk': 2, 'sera': 3, 'nacht': 4, 'alba': 5, 'diana': 6, 'grave': 7}
SHEETS = (('', 14), ('_1', 5))

def fnv(b):
    h = 0x811c9dc5
    for c in b: h = ((h ^ c) * 0x01000193) & 0xffffffff
    return h

def recolor_tab():
    s = open(os.path.join(D, 'recolor_tab.h'), encoding='utf-8').read()
    t = {}
    for m, c, b, p in re.findall(r'\{ (\d+), (\d+), (\d+), /\*[^*]*\*/\s*"((?:\\x[0-9a-f]{2})+)" \}', s):
        raw = bytes(int(x, 16) for x in re.findall(r'\\x([0-9a-f]{2})', p))
        assert len(raw) == 1024
        t[int(m), int(c)] = (int(b), [tuple(raw[i*4:i*4+4]) for i in range(256)])
    return t

def main(out):
    tab = recolor_tab(); assert len(tab) == 40
    ents, blob = [], bytearray()
    for f in sorted(os.listdir(os.path.join(D, 'final'))):
        if not f.endswith('.pkl'): continue
        j, c = f[:-4].split('_'); model, chara = JOB[j], CHARA[c]
        base, pal = tab[model, chara]
        cur, res, log = pickle.load(open(os.path.join(D, 'final', f), 'rb'))
        sheets = [icp.load(os.path.join(D, 'files', 'fldchr%d_%d%s.dat' % (model, base, s))) for s, _ in SHEETS]
        used = set()
        for x in sheets: used |= set(x['idx'])
        P = np.array(pal, np.int32)
        free = [i for i in range(1, 256) if i not in used]
        newpal = list(pal); added = []
        def index_of(col):
            col = tuple(int(v) for v in col)
            if col[3] == 0: return 0
            for i in sorted(used) + [a for a, _ in added]:
                if tuple(newpal[i]) == col: return i
            i = free.pop(0); newpal[i] = col; added.append((i, col)); return i
        k = 0
        per = []
        for (s, n), x in zip(SHEETS, sheets):
            w, h = x['w'], x['h']
            A = np.frombuffer(x['idx'], np.uint8).reshape(h, w)
            B = A.copy()
            for kk in range(n):
                cy, cx = (kk // 10) * 50, (kk % 10) * 50
                assert (P[A[cy:cy+50, cx:cx+50]] == cur[k]).all(), (f, k, 'cur differs from base+pal')
                d = (cur[k] != res[k]).any(2)
                for y, xx in zip(*np.where(d)):
                    B[cy+y, cx+xx] = index_of(res[k][y, xx])
                k += 1
            per.append((s, x, A, B))
        NP = np.array(newpal, np.int32)
        k = 0
        for s, x, A, B in per:   # 確かめ：足した色と書き換えた番号で、描き替えた絵と同じになる
            n = 14 if s == '' else 5
            for kk in range(n):
                cy, cx = (kk // 10) * 50, (kk % 10) * 50
                got = NP[B[cy:cy+50, cx:cx+50]]
                exp = res[k].copy(); exp[exp[..., 3] == 0] = NP[0]
                got[got[..., 3] == 0] = NP[0]
                assert (got == exp).all(), (f, k, 'roundtrip')
                k += 1
        for sub, (s, x, A, B) in enumerate(per):
            w, h = x['w'], x['h']
            a, b = A.ravel(), B.ravel()
            diff = np.where(a != b)[0]
            if len(diff) == 0 and not added: continue
            data = bytearray()
            for i, col in added: data += struct.pack('<B4B', i, *col)
            i = 0
            while i < len(diff):
                j = i
                while j + 1 < len(diff) and diff[j+1] == diff[j] + 1 and j - i < 65534: j += 1
                p0 = int(diff[i]); ln = j - i + 1
                data += struct.pack('<IH', p0, ln) + bytes(b[p0:p0+ln])
                i = j + 1
            ents.append((model, chara, sub, len(added), w, h, fnv(x['idx']), len(blob), len(data)))
            blob += data
        print(f, model, chara, 'base', base, 'added colours', len(added), 'free left', len(free))
    hdr = b'FSP1' + struct.pack('<I', len(ents))
    off0 = len(hdr) + len(ents) * 20
    tbl = b''.join(struct.pack('<4B2H3I', m, c, s, na, w, h, hh, off0 + o, ln) for m, c, s, na, w, h, hh, o, ln in ents)
    open(out, 'wb').write(hdr + tbl + bytes(blob))
    print(len(ents), 'entries', len(hdr + tbl + blob), 'bytes')

main(sys.argv[2])
