package io.github.fflmod.fflmod.core;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * バイナリ AndroidManifest.xml に fflmod の変更を入れる（tools/axml_patch.py と同じ手順。結果は1バイトも違わない）。
 * 1. &lt;application&gt; に android:name="…FFLModApp" を足す
 * 2. &lt;application&gt; の中の最後に、セーブの書き出し・取り込みの画面を足す
 */
public final class AxmlPatch {
    public static final String PKG = "com.square_enix.android_googleplay.ffl_gp";
    public static final String APP_CLASS = PKG + ".FFLModApp";
    public static final String SAVE_CLASS = PKG + ".FFLModSaveActivity";

    private static final long R_THEME = 0x01010000L, R_NAME = 0x01010003L, R_EXPORTED = 0x01010010L,
            R_EXCLUDE = 0x01010017L, R_LAUNCH = 0x0101001dL, R_CONFIG = 0x0101001fL;
    private static final int THEME_TRANSLUCENT_NOTITLE = 0x0103000f;
    private static final int T_REF = 0x01, T_STRING = 0x03, T_DEC = 0x10, T_HEX = 0x11, T_BOOL = 0x12;
    private static final int NONE = 0xffffffff;

    private AxmlPatch() {}

    static int u16(byte[] b, int o) { return (b[o] & 0xff) | (b[o + 1] & 0xff) << 8; }
    static int u32(byte[] b, int o) { return u16(b, o) | u16(b, o + 2) << 16; }
    static long u32l(byte[] b, int o) { return u32(b, o) & 0xffffffffL; }

    /** 小さな書き込み用 */
    static final class Buf extends ByteArrayOutputStream {
        Buf u16(int v) { write(v); write(v >>> 8); return this; }
        Buf u32(int v) { u16(v); u16(v >>> 16); return this; }
        Buf u8(int v) { write(v); return this; }
        Buf bytes(byte[] b) { write(b, 0, b.length); return this; }
        Buf bytes(byte[] b, int from, int to) { write(b, from, to - from); return this; }
    }

    static final class Pool {
        final int hsz, size, flags;
        final boolean utf8;
        final List<String> strs = new ArrayList<>();
        final int[] styles;
        final byte[] styleRaw;

        Pool(byte[] b, int o) {
            hsz = u16(b, o + 2);
            size = u32(b, o + 4);
            int cnt = u32(b, o + 8), scnt = u32(b, o + 12);
            flags = u32(b, o + 16);
            int sstart = u32(b, o + 20), ststart = u32(b, o + 24);
            styles = new int[scnt];
            for (int i = 0; i < scnt; i++) styles[i] = u32(b, o + hsz + 4 * cnt + 4 * i);
            utf8 = (flags & 0x100) != 0;
            int rawStart = o + sstart, rawEnd = o + (scnt != 0 ? ststart : size);
            byte[] raw = java.util.Arrays.copyOfRange(b, rawStart, rawEnd);
            styleRaw = scnt != 0 ? java.util.Arrays.copyOfRange(b, o + ststart, o + size) : new byte[0];
            for (int i = 0; i < cnt; i++) strs.add(read(raw, u32(b, o + hsz + 4 * i)));
        }

        private String read(byte[] raw, int off) {
            if (utf8) {
                int n = raw[off] & 0xff;
                off += (n & 0x80) != 0 ? 2 : 1;
                n = raw[off] & 0xff;
                if ((n & 0x80) != 0) { n = ((n & 0x7f) << 8) | (raw[off + 1] & 0xff); off += 2; } else off += 1;
                return new String(raw, off, n, StandardCharsets.UTF_8);
            }
            int n = u16(raw, off);
            off += 2;
            if ((n & 0x8000) != 0) { n = ((n & 0x7fff) << 16) | u16(raw, off); off += 2; }
            return new String(raw, off, 2 * n, StandardCharsets.UTF_16LE);
        }

        private byte[] enc(String s) {
            Buf o = new Buf();
            if (utf8) {
                byte[] e = s.getBytes(StandardCharsets.UTF_8);
                if (s.length() >= 0x80 || e.length >= 0x80) throw new IllegalStateException("string too long");
                return o.u8(s.length()).u8(e.length).bytes(e).u8(0).toByteArray();
            }
            if (s.length() >= 0x8000) throw new IllegalStateException("string too long");
            return o.u16(s.length()).bytes(s.getBytes(StandardCharsets.UTF_16LE)).u16(0).toByteArray();
        }

        int add(String s) {
            if (strs.contains(s)) throw new IllegalStateException("already patched? " + s);
            strs.add(s);
            return strs.size() - 1;
        }

        int index(String s) {
            int i = strs.indexOf(s);
            if (i < 0) throw new IllegalStateException("string not found: " + s);
            return i;
        }

        byte[] build() {
            Buf raw = new Buf();
            int[] offs = new int[strs.size()];
            for (int i = 0; i < offs.length; i++) { offs[i] = raw.size(); raw.bytes(enc(strs.get(i))); }
            while (raw.size() % 4 != 0) raw.u8(0);
            int cnt = strs.size(), scnt = styles.length;
            int sstart = hsz + 4 * cnt + 4 * scnt;
            int ststart = scnt != 0 ? sstart + raw.size() : 0;
            Buf body = new Buf();
            for (int v : offs) body.u32(v);
            for (int v : styles) body.u32(v);
            body.bytes(raw.toByteArray()).bytes(styleRaw);
            Buf h = new Buf();
            h.u16(0x0001).u16(hsz).u32(hsz + body.size()).u32(cnt).u32(scnt).u32(flags).u32(sstart).u32(ststart);
            return h.bytes(body.toByteArray()).toByteArray();
        }
    }

    private static byte[] attr(int ns, int name, int raw, int typ, int data) {
        return new Buf().u32(ns).u32(name).u32(raw).u16(8).u8(0).u8(typ).u32(data).toByteArray();
    }

    private static byte[] splice(byte[] a, int from, int to, byte[] ins) {
        byte[] r = new byte[a.length - (to - from) + ins.length];
        System.arraycopy(a, 0, r, 0, from);
        System.arraycopy(ins, 0, r, from, ins.length);
        System.arraycopy(a, to, r, from + ins.length, a.length - to);
        return r;
    }

    public static byte[] patch(byte[] b) {
        if (u16(b, 0) != 0x0003) throw new IllegalStateException("not AXML");
        int o = u16(b, 2);
        Pool pool = new Pool(b, o);
        int pe = o + pool.size;
        if (u16(b, pe) != 0x0180) throw new IllegalStateException("resource map expected");
        int rm = u32(b, pe + 4);
        int nres = (rm - 8) / 4;
        long[] resmap = new long[nres];
        for (int i = 0; i < nres; i++) resmap[i] = u32l(b, pe + 8 + 4 * i);
        Map<Long, Integer> idx = new HashMap<>();
        for (long rid : new long[] { R_THEME, R_NAME, R_EXPORTED, R_EXCLUDE, R_LAUNCH, R_CONFIG })
            for (int i = 0; i < nres; i++) if (resmap[i] == rid) { idx.put(rid, i); break; }
        for (long rid : new long[] { R_THEME, R_NAME, R_EXPORTED, R_LAUNCH, R_CONFIG })
            if (!idx.containsKey(rid)) throw new IllegalStateException("attribute " + Long.toHexString(rid) + " not in manifest");
        int ns = pool.index("http://schemas.android.com/apk/res/android");
        int sApp = pool.index("application"), sAct = pool.index("activity");
        int appVal = pool.add(APP_CLASS);
        int saveVal = pool.add(SAVE_CLASS);
        int nameIdx = idx.get(R_NAME);

        byte[] rest = java.util.Arrays.copyOfRange(b, pe, b.length);
        // 1. <application android:name>
        boolean done = false;
        for (int i = rm; i < rest.length; ) {
            int t = u16(rest, i), hs = u16(rest, i + 2), sz = u32(rest, i + 4);
            if (t == 0x0102 && u32(rest, i + 20) == sApp) {
                int ext = i + hs;
                int astart = u16(rest, ext + 8), asize = u16(rest, ext + 10), acnt = u16(rest, ext + 12);
                int idI = u16(rest, ext + 14), clsI = u16(rest, ext + 16), styI = u16(rest, ext + 18);
                if (asize != 20) throw new IllegalStateException("attribute size " + asize);
                List<byte[]> attrs = new ArrayList<>();
                for (int k = 0; k < acnt; k++) {
                    int a0 = ext + astart + 20 * k;
                    byte[] a = java.util.Arrays.copyOfRange(rest, a0, a0 + 20);
                    if (u32(a, 4) == nameIdx) throw new IllegalStateException("application already has android:name");
                    attrs.add(a);
                }
                int pos = 0;
                while (pos < acnt) {
                    long n = u32l(attrs.get(pos), 4);
                    long rid = n < nres ? resmap[(int) n] : 0xffffffffL;
                    if (rid >= R_NAME) break;
                    pos++;
                }
                attrs.add(pos, attr(ns, nameIdx, appVal, T_STRING, appVal));
                Buf seg = new Buf();
                seg.bytes(rest, i, i + 4).u32(sz + 20).bytes(rest, i + 8, i + hs);
                seg.u32(u32(rest, ext)).u32(u32(rest, ext + 4)).u16(astart).u16(20).u16(acnt + 1)
                        .u16(fix(idI, pos)).u16(fix(clsI, pos)).u16(fix(styI, pos));
                seg.bytes(rest, ext + 20, ext + astart);
                for (byte[] a : attrs) seg.bytes(a);
                rest = splice(rest, i, i + sz, seg.toByteArray());
                done = true;
                break;
            }
            i += sz;
        }
        if (!done) throw new IllegalStateException("<application> not found");
        // 2. </application> の直前に <activity .../> を差し込む
        done = false;
        for (int i = rm; i < rest.length; ) {
            int t = u16(rest, i), sz = u32(rest, i + 4);
            if (t == 0x0103 && u32(rest, i + 20) == sApp) {
                int line = u32(rest, i + 8);
                List<byte[]> attrs = new ArrayList<>();
                attrs.add(attr(ns, idx.get(R_THEME), NONE, T_REF, THEME_TRANSLUCENT_NOTITLE));
                attrs.add(attr(ns, nameIdx, saveVal, T_STRING, saveVal));
                attrs.add(attr(ns, idx.get(R_EXPORTED), NONE, T_BOOL, 0xffffffff));
                attrs.add(attr(ns, idx.get(R_LAUNCH), NONE, T_DEC, 3));
                attrs.add(attr(ns, idx.get(R_CONFIG), NONE, T_HEX, 0x4a0));
                if (idx.containsKey(R_EXCLUDE)) attrs.add(3, attr(ns, idx.get(R_EXCLUDE), NONE, T_BOOL, 0xffffffff));
                int n = attrs.size();
                Buf ins = new Buf();
                ins.u16(0x0102).u16(16).u32(16 + 20 + 20 * n).u32(line).u32(NONE);
                ins.u32(NONE).u32(sAct).u16(20).u16(20).u16(n).u16(0).u16(0).u16(0);
                for (byte[] a : attrs) ins.bytes(a);
                ins.u16(0x0103).u16(16).u32(24).u32(line).u32(NONE).u32(NONE).u32(sAct);
                rest = splice(rest, i, i, ins.toByteArray());
                done = true;
                break;
            }
            i += sz;
        }
        if (!done) throw new IllegalStateException("</application> not found");
        Buf out = new Buf();
        out.bytes(b, 0, o).bytes(pool.build()).bytes(rest);
        byte[] r = out.toByteArray();
        int len = r.length;
        r[4] = (byte) len; r[5] = (byte) (len >>> 8); r[6] = (byte) (len >>> 16); r[7] = (byte) (len >>> 24);
        return r;
    }

    private static int fix(int v, int pos) { return v != 0 && v - 1 >= pos ? v + 1 : v; }
}
