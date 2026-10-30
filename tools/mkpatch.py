#!/usr/bin/env python3
"""配布用の差分一式を作る（GitHub Releases に上げるもの。ゲームのファイルそのものは入らない）。

使い方: mkpatch.py ビルドの作業フォルダ 出力フォルダ 版
  ビルドの作業フォルダ : tools/build.sh の 出力/work（fflmod.dex、lib/arm64-v8a/libfflmod.so）

出力:
  fflmod.dex    FFLMod、FFLModApp、FFLModInput、FFLModSaveActivity（自分のコード。APK の中では classes2.dex などの続きの番号で入る）
  libfflmod.so  本体（自分のコード）
  files.json    版、つなぎの番号、正規版と manifest と各ファイルのハッシュ
manifest は差分を配らず、管理アプリ（と applypatch.py）が axml_patch.py と同じ手順で書き換え、結果を files.json の値と照らす。
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fflmod_apk as F  # noqa: E402

FORMAT = 2


def main(work, out, version):
    os.makedirs(out, exist_ok=True)
    files = {
        'fflmod.dex': open(os.path.join(work, 'fflmod.dex'), 'rb').read(),
        'libfflmod.so': open(os.path.join(work, 'lib', 'arm64-v8a', 'libfflmod.so'), 'rb').read(),
    }
    assert ('installed v' + version + ':').encode() in files['libfflmod.so'], 'FFLMOD_VERSION is not ' + version
    for n, d in files.items():
        open(os.path.join(out, n), 'wb').write(d)
    info = {
        'format': FORMAT,
        'name': 'fflmod',
        'version': version,
        'interface': F.INTERFACE,
        'game': {
            'package': F.PKG,
            'versionCode': F.GAME_VERSION_CODE,
            'sha256': F.ORIG_SHA256,
        },
        'manifest': {
            'from_sha256': F.MANIFEST_SHA256['from'],
            'to_sha256': F.MANIFEST_SHA256['to'],
        },
        'files': {n: {'size': len(d), 'sha256': F.sha256(d)} for n, d in files.items()},
    }
    with open(os.path.join(out, 'files.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, ensure_ascii=False, indent=2)
        f.write('\n')
    for n, d in files.items():
        print('%-14s %8d bytes' % (n, len(d)))


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
