#!/usr/bin/env python3
"""バイナリ AndroidManifest.xml に fflmod の変更を入れる。

使い方: axml_patch.py 入力 出力
  1. <application> に android:name="…FFLModApp" を足す
  2. <application> の中の最後に、セーブの書き出し・取り込みの画面を足す
     <activity android:theme="@android:style/Theme.Translucent.NoTitleBar" android:name="…FFLModSaveActivity"
               android:exported="true" [android:excludeFromRecents="true"] android:launchMode="3"
               android:configChanges="0x4a0"/>
- 属性名はどれも元の文字列表にあるもの（リソース ID 表の範囲）を使う。足す文字列はクラス名の値だけで、文字列表の末尾に置く。
- 要素の属性はリソース ID の順に並べる決まりに従う。ほかのバイトは変えない。
"""
import struct
import sys

PKG = 'com.square_enix.android_googleplay.ffl_gp'
APP_CLASS = PKG + '.FFLModApp'
SAVE_CLASS = PKG + '.FFLModSaveActivity'

R_THEME, R_NAME, R_EXPORTED, R_EXCLUDE, R_LAUNCH, R_CONFIG = (
    0x01010000, 0x01010003, 0x01010010, 0x01010017, 0x0101001d, 0x0101001f)
THEME_TRANSLUCENT_NOTITLE = 0x0103000f
T_REF, T_STRING, T_DEC, T_HEX, T_BOOL = 0x01, 0x03, 0x10, 0x11, 0x12


def u16(b, o): return struct.unpack_from('<H', b, o)[0]
def u32(b, o): return struct.unpack_from('<I', b, o)[0]


class Pool:
    def __init__(self, b, o):
        self.hsz, size = u16(b, o + 2), u32(b, o + 4)
        cnt, scnt, self.flags, sstart, ststart = struct.unpack_from('<IIIII', b, o + 8)
        offs = struct.unpack_from('<%dI' % cnt, b, o + self.hsz)
        self.styles = list(struct.unpack_from('<%dI' % scnt, b, o + self.hsz + 4 * cnt))
        self.utf8 = bool(self.flags & 0x100)
        raw = b[o + sstart:o + (ststart if scnt else size)]
        self.style_raw = b[o + ststart:o + size] if scnt else b''
        self.strs = [self._read(raw, off) for off in offs]
        self.size = size

    def _read(self, raw, off):
        if self.utf8:
            n = raw[off]; off += 2 if n & 0x80 else 1
            n = raw[off]
            if n & 0x80: n = ((n & 0x7f) << 8) | raw[off + 1]; off += 2
            else: off += 1
            return raw[off:off + n].decode('utf-8')
        n = u16(raw, off); off += 2
        if n & 0x8000: n = ((n & 0x7fff) << 16) | u16(raw, off); off += 2
        return raw[off:off + 2 * n].decode('utf-16-le')

    def _enc(self, s):
        if self.utf8:
            e = s.encode('utf-8')
            assert len(s) < 0x80 and len(e) < 0x80
            return bytes([len(s), len(e)]) + e + b'\0'
        assert len(s) < 0x8000
        return struct.pack('<H', len(s)) + s.encode('utf-16-le') + b'\0\0'

    def add(self, s):
        assert s not in self.strs, 'already patched? ' + s
        self.strs.append(s)
        return len(self.strs) - 1

    def build(self):
        raw, offs = b'', []
        for s in self.strs:
            offs.append(len(raw)); raw += self._enc(s)
        while len(raw) % 4: raw += b'\0'
        cnt, scnt = len(self.strs), len(self.styles)
        sstart = self.hsz + 4 * cnt + 4 * scnt
        ststart = sstart + len(raw) if scnt else 0
        body = struct.pack('<%dI' % cnt, *offs) + struct.pack('<%dI' % scnt, *self.styles) + raw + self.style_raw
        return struct.pack('<HHIIIIII', 0x0001, self.hsz, self.hsz + len(body), cnt, scnt, self.flags, sstart, ststart) + body


def attr(ns, name, raw, typ, data):
    return struct.pack('<IIIHBBI', ns, name, raw, 8, 0, typ, data)


def patch(src):
    b = bytes(src)
    assert u16(b, 0) == 0x0003, 'not AXML'
    o = u16(b, 2)
    pool = Pool(b, o)
    pe = o + pool.size
    assert u16(b, pe) == 0x0180, 'resource map expected'
    rm = u32(b, pe + 4)
    resmap = list(struct.unpack_from('<%dI' % ((rm - 8) // 4), b, pe + 8))
    # 属性名は元の文字列表にあるものだけを使う。excludeFromRecents は版によって無い（1.2.0 には無い）ので、無ければ付けない
    idx = {rid: resmap.index(rid) for rid in (R_THEME, R_NAME, R_EXPORTED, R_EXCLUDE, R_LAUNCH, R_CONFIG) if rid in resmap}
    for rid in (R_THEME, R_NAME, R_EXPORTED, R_LAUNCH, R_CONFIG):
        assert rid in idx, 'attribute %x not in manifest' % rid
    ns = pool.strs.index('http://schemas.android.com/apk/res/android')
    s_app, s_act = pool.strs.index('application'), pool.strs.index('activity')
    app_val = pool.add(APP_CLASS)
    save_val = pool.add(SAVE_CLASS)
    NONE = 0xffffffff

    rest = bytearray(b[pe:])
    # 1. <application android:name>
    i = rm
    while i < len(rest):
        t, hs, sz = u16(rest, i), u16(rest, i + 2), u32(rest, i + 4)
        if t == 0x0102 and u32(rest, i + 20) == s_app:
            app_line = u32(rest, i + 8)
            ext = i + hs
            astart, asize, acnt = u16(rest, ext + 8), u16(rest, ext + 10), u16(rest, ext + 12)
            id_i, cls_i, sty_i = u16(rest, ext + 14), u16(rest, ext + 16), u16(rest, ext + 18)
            assert asize == 20
            attrs = [bytes(rest[ext + astart + 20 * k: ext + astart + 20 * (k + 1)]) for k in range(acnt)]
            assert all(u32(a, 4) != idx[R_NAME] for a in attrs), 'application already has android:name'
            rid = lambda a: resmap[u32(a, 4)] if u32(a, 4) < len(resmap) else NONE
            pos = 0
            while pos < acnt and rid(attrs[pos]) < R_NAME: pos += 1
            attrs.insert(pos, attr(ns, idx[R_NAME], app_val, T_STRING, app_val))
            fix = lambda v: v + 1 if v and v - 1 >= pos else v
            ext_new = struct.pack('<IIHHHHHH', u32(rest, ext), u32(rest, ext + 4), astart, 20, acnt + 1,
                                  fix(id_i), fix(cls_i), fix(sty_i))
            node = bytes(rest[i:i + hs])
            node = node[:4] + struct.pack('<I', sz + 20) + node[8:]
            rest[i:i + sz] = node + ext_new + bytes(rest[ext + 20:ext + astart]) + b''.join(attrs)
            break
        i += sz
    else:
        raise SystemExit('<application> not found')
    # 2. </application> の直前に <activity .../> を差し込む
    i = rm
    while i < len(rest):
        t, hs, sz = u16(rest, i), u16(rest, i + 2), u32(rest, i + 4)
        if t == 0x0103 and u32(rest, i + 20) == s_app:
            line = u32(rest, i + 8)
            attrs = [
                attr(ns, idx[R_THEME], NONE, T_REF, THEME_TRANSLUCENT_NOTITLE),
                attr(ns, idx[R_NAME], save_val, T_STRING, save_val),
                attr(ns, idx[R_EXPORTED], NONE, T_BOOL, 0xffffffff),
                attr(ns, idx[R_LAUNCH], NONE, T_DEC, 3),
                attr(ns, idx[R_CONFIG], NONE, T_HEX, 0x4a0),
            ]
            if R_EXCLUDE in idx:
                attrs.insert(3, attr(ns, idx[R_EXCLUDE], NONE, T_BOOL, 0xffffffff))
            start = struct.pack('<HHIII', 0x0102, 16, 16 + 20 + 20 * len(attrs), line, NONE) + \
                struct.pack('<IIHHHHHH', NONE, s_act, 20, 20, len(attrs), 0, 0, 0) + b''.join(attrs)
            end = struct.pack('<HHIII', 0x0103, 16, 24, line, NONE) + struct.pack('<II', NONE, s_act)
            rest[i:i] = start + end
            break
        i += sz
    else:
        raise SystemExit('</application> not found')
    out = bytearray(b[:o]) + pool.build() + rest
    struct.pack_into('<I', out, 4, len(out))
    return bytes(out)


if __name__ == '__main__':
    src, dst = sys.argv[1:3]
    data = patch(open(src, 'rb').read())
    open(dst, 'wb').write(data)
    print('manifest patched: %s, %s (%d bytes)' % (APP_CLASS, SAVE_CLASS, len(data)))
