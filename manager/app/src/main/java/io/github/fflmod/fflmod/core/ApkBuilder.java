package io.github.fflmod.fflmod.core;

import java.io.File;
import java.io.IOException;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/** fflmod の APK の組み立て（tools/fflmod_apk.py と同じ手順。元の署名を外し、ファイルを差し替え・追加する） */
public final class ApkBuilder {
    private ApkBuilder() {}

    /** 追加したファイルの日時（fflmod_apk.py の FIXED_TIME = 1981-01-01 01:01:02） */
    static final int FIXED_TIME = (1 << 11) | (1 << 5) | 1, FIXED_DATE = (1 << 9) | (1 << 5) | 1;

    static boolean drop(String n) {
        if (n.equals("META-INF/MANIFEST.MF") || n.equals("stamp-cert-sha256")) return true;
        return n.startsWith("META-INF/")
                && (n.endsWith(".SF") || n.endsWith(".RSA") || n.endsWith(".DSA") || n.endsWith(".EC"));
    }

    /** 足す dex の名前。Android は classes.dex, classes2.dex, … を番号の抜けがないところまでしか読まない */
    public static String nextDexName(ZipRaw.Reader r) {
        int n = 1;
        while (r.find("classes" + (n + 1) + ".dex") != null) n++;
        return "classes" + (n + 1) + ".dex";
    }

    /** src を dst に写す。replaceName を replaceData に差し替え、addName を末尾に足す（どちらも null 可） */
    public static void rebuild(File src, File dst, String replaceName, byte[] replaceData,
                               String addName, byte[] addData, int addMethod) throws IOException {
        try (ZipRaw.Reader r = new ZipRaw.Reader(src); ZipRaw.Writer w = new ZipRaw.Writer(dst)) {
            boolean replaced = replaceName == null;
            for (ZipRaw.Entry e : r.entries) {
                if (drop(e.name)) continue;
                if (e.name.equals(replaceName)) {
                    w.add(e.name, replaceData, e.method, e.time, e.date);
                    replaced = true;
                } else {
                    w.copyFrom(r, e);
                }
            }
            if (!replaced) throw new IOException(replaceName + " not found in " + src);
            if (addName != null) w.add(addName, addData, addMethod, FIXED_TIME, FIXED_DATE);
        }
    }

    public static void buildBase(File origBase, byte[] manifest, byte[] dex, File dst) throws IOException {
        String dexName;
        try (ZipRaw.Reader r = new ZipRaw.Reader(origBase)) { dexName = nextDexName(r); }
        rebuild(origBase, dst, "AndroidManifest.xml", manifest, dexName, dex, ZipRaw.DEFLATED);
    }

    public static void buildArm64(File origArm64, byte[] so, File dst) throws IOException {
        rebuild(origArm64, dst, null, null, "lib/arm64-v8a/libfflmod.so", so, ZipRaw.STORED);
    }

    public static void resignCopy(File src, File dst) throws IOException {
        rebuild(src, dst, null, null, null, null, 0);
    }

    /**
     * 照合用の要約。署名のファイル（META-INF/）を除く各ファイルの「名前 CRC 大きさ」を名前順に並べた SHA-256 の先頭16桁。
     * PC の道具で作ったものと同じ値になれば、中身は同じ。.so が 16KB 境界にあるかも調べる。
     */
    public static String summary(File apk) throws IOException {
        List<String> lines = new ArrayList<>();
        String align = "";
        try (ZipRaw.Reader r = new ZipRaw.Reader(apk)) {
            for (ZipRaw.Entry e : r.entries) {
                if (e.name.startsWith("META-INF/")) continue;
                lines.add(String.format(Locale.ROOT, "%s %08x %d", e.name, e.crc, e.size));
                if (e.name.endsWith(".so") && e.method == ZipRaw.STORED)
                    align += (r.dataOffset(e) % ZipRaw.Writer.ALIGN_SO == 0) ? "" : " NG:" + e.name;
            }
        }
        Collections.sort(lines);
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            for (String l : lines) md.update((l + "\n").getBytes("UTF-8"));
            byte[] d = md.digest();
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 8; i++) sb.append(String.format(Locale.ROOT, "%02x", d[i]));
            return sb + (align.isEmpty() ? "" : align);
        } catch (java.security.NoSuchAlgorithmException x) {
            throw new IOException(x);
        }
    }
}
