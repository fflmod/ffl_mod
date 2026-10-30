#!/bin/sh
# fflmod のビルド。正規版の APK から、署名済みの base.apk と split_config.arm64_v8a.apk を作る。
#
# 使い方: tools/build.sh 正規版のフォルダ 出力フォルダ [local]（split_config.ja など他の分割ファイルがあれば署名だけ付け直す）
#   正規版のフォルダに - を渡すと、出力/work に libfflmod.so と fflmod.dex を作るだけで終わる（Release 用。ゲームのファイルは要らない）
#   正規版のフォルダ: base.apk と split_config.arm64_v8a.apk（日本の Google Play、1.2.0）
#   local を付けると手元だけの追加（src/local.c、リポジトリには含めない）を入れる
# 必要なもの: clang/lld（aarch64）、python3、java、
#   APKTOOL（apktool の jar。smali の組み立てに使う）、SIGNER（uber-apk-signer の jar）、
#   KS（鍵、既定 keys/fflmod.jks）、KS_PASS（既定 fflmod）
set -eu
mkdir -p "$2"
if [ "$1" = - ]; then ORIG=-; else ORIG=$(cd "$1" && pwd); fi
OUT=$(cd "$2" && pwd)
MODE=${3:-public}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
APKTOOL=${APKTOOL:-$ROOT/tools/apktool.jar}
KS=${KS:-$ROOT/keys/fflmod.jks}
SIGNER=${SIGNER:-$ROOT/tools/signer.jar}
KS_PASS=${KS_PASS:-fflmod}
mkdir -p "$OUT/work/lib/arm64-v8a" "$OUT/work/smalic" "$OUT/unsigned"

# 1. libfflmod.so（ソースのファイル名を固定して作る。名前が記号表に残るため）
#    つなぐ相手（libc/libdl/liblog）は端末のものを使うので、ここでは名前だけの空の代役を作ってつなぐ
mkdir -p "$OUT/work/stubs"
for n in c:c dl:dl log:log; do
  clang --target=aarch64-linux-gnu -shared -nostdlib -fuse-ld=lld -Wl,-soname,lib${n%%:*}.so \
    -o "$OUT/work/stubs/lib${n%%:*}.so" "$ROOT/src/stubs/${n#*:}.c"
done
DEF=""
[ "$MODE" = local ] && DEF="-DFFLMOD_LOCAL"
(cd "$ROOT/src" && clang --target=aarch64-linux-gnu -O2 -fPIC -shared -nostdlib -ffreestanding \
  -fno-stack-protector -fuse-ld=lld -Wl,-z,now -Wl,-z,max-page-size=16384 -Wl,-soname,libfflmod.so -L"$OUT/work/stubs" -lc -ldl -llog -I. $DEF \
  -o "$OUT/work/lib/arm64-v8a/libfflmod.so" fflmod.c)

# 2. fflmod.dex（FFLMod、FFLModApp、FFLModInput、FFLModSaveActivity）。APK の中では続きの番号の classesN.dex になる
javac -cp "$APKTOOL" -d "$OUT/work/smalic" "$ROOT/tools/smalic/SmaliC.java"
java -cp "$APKTOOL:$OUT/work/smalic" SmaliC "$OUT/work/fflmod.dex" \
  "$ROOT"/src/smali/*.smali

if [ "$ORIG" = - ]; then
  ls -l "$OUT/work/fflmod.dex" "$OUT/work/lib/arm64-v8a/libfflmod.so"
  exit 0
fi

# 3. AndroidManifest.xml に Application クラスとセーブの書き出し・取り込みの画面を足す
unzip -p "$ORIG/base.apk" AndroidManifest.xml > "$OUT/work/AndroidManifest.orig.xml"
python3 -I "$ROOT/tools/axml_patch.py" "$OUT/work/AndroidManifest.orig.xml" "$OUT/work/AndroidManifest.xml"

# 4. 組み立てと署名
python3 -I - "$ROOT/tools" "$ORIG" "$OUT" <<'EOF'
import sys
sys.path.insert(0, sys.argv[1])
import fflmod_apk as F
orig, out = sys.argv[2], sys.argv[3]
for n, h in F.ORIG_SHA256.items():
    assert F.sha256_file(orig + '/' + n) == h, n + ' is not the supported original'
w = out + '/work/'
man0, man1 = open(w + 'AndroidManifest.orig.xml', 'rb').read(), open(w + 'AndroidManifest.xml', 'rb').read()
assert F.sha256(man0) == F.MANIFEST_SHA256['from'], 'manifest of the original differs'
assert F.sha256(man1) == F.MANIFEST_SHA256['to'], 'patched manifest differs from MANIFEST_SHA256 (axml_patch.py changed?)'
F.build_base(orig + '/base.apk', open(w + 'AndroidManifest.xml', 'rb').read(), open(w + 'fflmod.dex', 'rb').read(), out + '/unsigned/base.apk')
F.build_arm64(orig + '/split_config.arm64_v8a.apk', open(w + 'lib/arm64-v8a/libfflmod.so', 'rb').read(), out + '/unsigned/split_config.arm64_v8a.apk')
# 言語・画面密度・データの分割ファイルは中身を変えず、署名だけ付け直す（新しく入れるときは全部同じ鍵が要る）
import glob, os
for p in sorted(glob.glob(orig + '/split_*.apk')):
    n = os.path.basename(p)
    if n != 'split_config.arm64_v8a.apk':
        F.rebuild(p, out + '/unsigned/' + n)
        print('resign', n)
EOF
# 位置そろえは fflmod_apk.py で済ませてある（.so は 16KB）。署名だけ行い、zipalign はしない（4KB にそろえ直されるため）
java -jar "$SIGNER" -a "$OUT/unsigned" -o "$OUT/signed" --ks "$KS" --ksAlias fflmod \
  --ksPass "$KS_PASS" --ksKeyPass "$KS_PASS" --skipZipAlign > "$OUT/work/signer.log" 2>&1
for f in "$OUT"/unsigned/*.apk; do
  n=$(basename "$f" .apk)
  cp "$OUT/signed/$n-signed.apk" "$OUT/$n.apk"
done
(cd "$OUT" && md5sum *.apk)
