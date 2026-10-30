#!/usr/bin/env python3
"""差分一式を正規版に当てる（管理アプリと同じ手順を PC で行う。確かめ用）。

使い方: applypatch.py 正規版のフォルダ 差分のフォルダ 出力フォルダ
  正規版の base.apk と split_config.arm64_v8a.apk のハッシュ、差分一式の各ファイルのハッシュを
  files.json と照らしてから、未署名の base.apk と split_config.arm64_v8a.apk を 出力/unsigned に作る。
  署名は tools/signer.jar で行う（管理アプリでは apksig）。
"""
import json
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import axml_patch  # noqa: E402
import fflmod_apk as F  # noqa: E402

FORMAT = 2


def main(orig, patch, out):
    info = json.load(open(os.path.join(patch, 'files.json'), encoding='utf-8'))
    if info.get('format') != FORMAT:
        sys.exit('unknown format')
    if info['interface'] != F.INTERFACE:
        sys.exit('interface %s is not supported (want %s)' % (info['interface'], F.INTERFACE))
    for n, h in info['game']['sha256'].items():
        if F.sha256_file(os.path.join(orig, n)) != h:
            sys.exit('%s is not the supported original' % n)
    data = {}
    for n, meta in info['files'].items():
        d = open(os.path.join(patch, n), 'rb').read()
        if len(d) != meta['size'] or F.sha256(d) != meta['sha256']:
            sys.exit('%s is broken or not the released file' % n)
        data[n] = d
    with zipfile.ZipFile(os.path.join(orig, 'base.apk')) as z:
        man = z.read('AndroidManifest.xml')
    if F.sha256(man) != info['manifest']['from_sha256']:
        sys.exit('manifest differs from the expected original')
    man = axml_patch.patch(man)
    if F.sha256(man) != info['manifest']['to_sha256']:
        sys.exit('manifest patch failed')
    os.makedirs(os.path.join(out, 'unsigned'), exist_ok=True)
    F.build_base(os.path.join(orig, 'base.apk'), man, data['fflmod.dex'], os.path.join(out, 'unsigned', 'base.apk'))
    F.build_arm64(os.path.join(orig, 'split_config.arm64_v8a.apk'), data['libfflmod.so'],
                  os.path.join(out, 'unsigned', 'split_config.arm64_v8a.apk'))
    print('fflmod %s applied' % info['version'])


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
