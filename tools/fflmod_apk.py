#!/usr/bin/env python3
"""fflmod の APK の組み立て（mkpatch / applypatch / build から使う共通部品）。

正規版の APK から元の署名を外し、ファイルを差し替え・追加して、未署名の APK を作る。
署名は呼び出し側で uber-apk-signer（tools/signer.jar）を使って行う。
"""
import hashlib
import struct
import zipfile

PKG = 'com.square_enix.android_googleplay.ffl_gp'
APP_CLASS = PKG + '.FFLModApp'
GAME_VERSION_CODE = 120
INTERFACE = 1          # fflmod.dex（FFLMod の Java 側）と libfflmod.so の組み合わせの番号

# 正規版（日本の Google Play、1.2.0）の SHA-256
ORIG_SHA256 = {
    'base.apk': 'e3753f48234d2d080007c444e1a9b28c5553b4b057e8021bed19e4c96e8fccac',
    'split_config.arm64_v8a.apk': 'f535938cdc428533f9bef13c1082c561324b0d6fd95820bc13ec8d9ff524c507',
}

# AndroidManifest.xml の SHA-256。from は正規版、to は axml_patch.py で書き換えた後。
# 書き換えは管理アプリも同じ手順で行い、結果を to と照らす（配布物に manifest の差分は入れない）
MANIFEST_SHA256 = {
    'from': 'e641dcfabb3f1824c5011e2ddb501098db2b1e5e6b5a696c6f23b0c5a8e7f0d2',
    'to': 'f72703da0e7f3e626fb06245955636af8f0671ebafa1b0be86ce17096cfffc1d',
}

# 元の署名とソーススタンプ。再署名するので外す
DROP = ('META-INF/MANIFEST.MF', 'stamp-cert-sha256')
DROP_SUFFIX = ('.SF', '.RSA', '.DSA', '.EC')
FIXED_TIME = (1981, 1, 1, 1, 1, 2)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    with open(path, 'rb') as f:
        return sha256(f.read())


def _drop(name):
    return name in DROP or (name.startswith('META-INF/') and name.endswith(DROP_SUFFIX))


# 無圧縮のファイルのデータの位置をそろえる（zipalign と同じ）。.so は 16KB（16KB ページの端末で APK から直接読み込むため）
ALIGN_SO = 16384
ALIGN_OTHER = 4


def _write(zo, ni, data):
    if ni.compress_type == zipfile.ZIP_STORED:
        align = ALIGN_SO if ni.filename.endswith('.so') else ALIGN_OTHER
        start = zo.fp.tell() + 30 + len(ni.filename.encode('utf-8'))
        # 0xd935 は Android の zipalign が使う「そろえ」の拡張欄（ID 2 + 大きさ 2 + そろえの値 2 + 詰め物）
        pad = (-(start + 6)) % align
        ni.extra = struct.pack('<HHH', 0xd935, 2 + pad, align) + b'\0' * pad
    zo.writestr(ni, data)


def rebuild(src, dst, replace=None, add=None):
    """src の中身を dst に写す。replace={名前: バイト列} で差し替え、add=[(名前, バイト列, 圧縮)] を末尾に足す。"""
    replace = dict(replace or {})
    with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, 'w') as zo:
        for info in zi.infolist():
            if _drop(info.filename):
                continue
            data = replace.pop(info.filename, None)
            if data is None:
                data = zi.read(info.filename)
            ni = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            ni.compress_type = info.compress_type
            ni.external_attr = info.external_attr
            _write(zo, ni, data)
        assert not replace, 'not found in %s: %s' % (src, list(replace))
        for name, data, method in (add or []):
            ni = zipfile.ZipInfo(name, date_time=FIXED_TIME)
            ni.compress_type = method
            _write(zo, ni, data)


def next_dex_name(apk):
    """追加する dex の名前。Android は classes.dex, classes2.dex, … を番号の抜けがないところまでしか読まないので、
    続きの番号にする（1.1.7 は classes2.dex まであったので classes3.dex、1.2.0 は classes.dex だけなので classes2.dex）"""
    with zipfile.ZipFile(apk) as z:
        names = set(z.namelist())
    n = 1
    while ('classes%d.dex' % (n + 1)) in names:
        n += 1
    return 'classes%d.dex' % (n + 1)


def build_base(orig_base, manifest, dex, dst):
    """fflmod.dex は APK の中では next_dex_name の名前で入れる"""
    rebuild(orig_base, dst, replace={'AndroidManifest.xml': manifest},
            add=[(next_dex_name(orig_base), dex, zipfile.ZIP_DEFLATED)])


def build_arm64(orig_arm64, so, dst):
    rebuild(orig_arm64, dst, add=[('lib/arm64-v8a/libfflmod.so', so, zipfile.ZIP_STORED)])


def entries(path):
    """比較用：{名前: (CRC, 大きさ)}。署名のファイルは除く"""
    with zipfile.ZipFile(path) as z:
        return {i.filename: (i.CRC, i.file_size) for i in z.infolist() if not i.filename.startswith('META-INF/')}
