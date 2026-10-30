package io.github.fflmod.fflmod.core;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.Closeable;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.CRC32;
import java.util.zip.Deflater;
import java.util.zip.Inflater;

/**
 * 圧縮したままのデータを写せる、最小限の ZIP の読み書き（zip64 は扱わない）。
 * 200MB ほどあるデータの分割ファイルも、解いて圧縮し直さずに済む。
 */
public final class ZipRaw {
    private ZipRaw() {}

    public static final int STORED = 0, DEFLATED = 8;

    /** 中央ディレクトリの1件 */
    public static final class Entry {
        public String name;
        public int flags, method, time, date, crc, externalAttr;
        public long csize, size, localOffset;
    }

    public static final class Reader implements Closeable {
        private final RandomAccessFile f;
        public final List<Entry> entries = new ArrayList<>();

        public Reader(File file) throws IOException {
            f = new RandomAccessFile(file, "r");
            long len = f.length();
            int tail = (int) Math.min(len, 22 + 65535);
            byte[] t = new byte[tail];
            f.seek(len - tail);
            f.readFully(t);
            int e = -1;
            for (int i = tail - 22; i >= 0; i--) if (AxmlPatch.u32(t, i) == 0x06054b50) { e = i; break; }
            if (e < 0) throw new IOException("not a zip: " + file);
            int count = AxmlPatch.u16(t, e + 10);
            long cdSize = AxmlPatch.u32l(t, e + 12), cdOff = AxmlPatch.u32l(t, e + 16);
            if (count == 0xffff || cdOff == 0xffffffffL) throw new IOException("zip64 is not supported");
            byte[] cd = new byte[(int) cdSize];
            f.seek(cdOff);
            f.readFully(cd);
            int p = 0;
            for (int k = 0; k < count; k++) {
                if (AxmlPatch.u32(cd, p) != 0x02014b50) throw new IOException("bad central directory");
                Entry en = new Entry();
                en.flags = AxmlPatch.u16(cd, p + 8);
                en.method = AxmlPatch.u16(cd, p + 10);
                en.time = AxmlPatch.u16(cd, p + 12);
                en.date = AxmlPatch.u16(cd, p + 14);
                en.crc = AxmlPatch.u32(cd, p + 16);
                en.csize = AxmlPatch.u32l(cd, p + 20);
                en.size = AxmlPatch.u32l(cd, p + 24);
                int nl = AxmlPatch.u16(cd, p + 28), xl = AxmlPatch.u16(cd, p + 30), cl = AxmlPatch.u16(cd, p + 32);
                en.externalAttr = AxmlPatch.u32(cd, p + 38);
                en.localOffset = AxmlPatch.u32l(cd, p + 42);
                en.name = new String(cd, p + 46, nl, StandardCharsets.UTF_8);
                entries.add(en);
                p += 46 + nl + xl + cl;
            }
        }

        public Entry find(String name) {
            for (Entry e : entries) if (e.name.equals(name)) return e;
            return null;
        }

        /** データの始まりの位置（ローカルヘッダの後） */
        public long dataOffset(Entry e) throws IOException {
            byte[] h = new byte[30];
            f.seek(e.localOffset);
            f.readFully(h);
            if (AxmlPatch.u32(h, 0) != 0x04034b50) throw new IOException("bad local header: " + e.name);
            return e.localOffset + 30 + AxmlPatch.u16(h, 26) + AxmlPatch.u16(h, 28);
        }

        /** 圧縮したままのデータを out に写す */
        public void copyRaw(Entry e, OutputStream out) throws IOException {
            f.seek(dataOffset(e));
            byte[] buf = new byte[1 << 16];
            long left = e.csize;
            while (left > 0) {
                int n = f.read(buf, 0, (int) Math.min(buf.length, left));
                if (n < 0) throw new IOException("short read: " + e.name);
                out.write(buf, 0, n);
                left -= n;
            }
        }

        /** 中身を解いて返す（小さいファイル用） */
        public byte[] read(Entry e) throws IOException {
            ByteArrayOutputStream raw = new ByteArrayOutputStream();
            copyRaw(e, raw);
            byte[] c = raw.toByteArray();
            byte[] d;
            if (e.method == STORED) d = c;
            else if (e.method == DEFLATED) {
                Inflater inf = new Inflater(true);
                inf.setInput(c);
                d = new byte[(int) e.size];
                int n = 0;
                try {
                    while (n < d.length) {
                        int r = inf.inflate(d, n, d.length - n);
                        if (r == 0 && (inf.finished() || inf.needsInput())) break;
                        n += r;
                    }
                } catch (java.util.zip.DataFormatException x) {
                    throw new IOException(x);
                } finally {
                    inf.end();
                }
                if (n != d.length) throw new IOException("inflate size mismatch: " + e.name);
            } else throw new IOException("unknown method " + e.method + ": " + e.name);
            CRC32 crc = new CRC32();
            crc.update(d);
            if ((int) crc.getValue() != e.crc) throw new IOException("crc mismatch: " + e.name);
            return d;
        }

        @Override public void close() throws IOException { f.close(); }
    }

    /**
     * 書き出し。無圧縮のデータは位置をそろえる（zipalign と同じ 0xd935 の拡張欄。.so は 16KB、ほかは 4）。
     * apksig はこの拡張欄を読んで、署名のときも同じそろえを保つ。
     */
    public static final class Writer implements Closeable {
        public static final int ALIGN_SO = 16384, ALIGN_OTHER = 4;
        private final OutputStream out;
        private long pos;
        private final AxmlPatch.Buf central = new AxmlPatch.Buf();
        private int count;

        public Writer(File file) throws IOException {
            out = new BufferedOutputStream(new FileOutputStream(file), 1 << 16);
        }

        private void w(byte[] b) throws IOException { out.write(b); pos += b.length; }

        private void header(Entry e, byte[] name) throws IOException {
            byte[] extra = new byte[0];
            if (e.method == STORED) {
                int align = e.name.endsWith(".so") ? ALIGN_SO : ALIGN_OTHER;
                long start = pos + 30 + name.length;
                int pad = (int) Math.floorMod(-(start + 6), (long) align);
                AxmlPatch.Buf x = new AxmlPatch.Buf();
                x.u16(0xd935).u16(2 + pad).u16(align);
                for (int i = 0; i < pad; i++) x.u8(0);
                extra = x.toByteArray();
            }
            int flags = e.flags & 0x800;
            long off = pos;
            AxmlPatch.Buf h = new AxmlPatch.Buf();
            h.u32(0x04034b50).u16(20).u16(flags).u16(e.method).u16(e.time).u16(e.date)
                    .u32(e.crc).u32((int) e.csize).u32((int) e.size).u16(name.length).u16(extra.length)
                    .bytes(name).bytes(extra);
            w(h.toByteArray());
            central.u32(0x02014b50).u16(20).u16(20).u16(flags).u16(e.method).u16(e.time).u16(e.date)
                    .u32(e.crc).u32((int) e.csize).u32((int) e.size).u16(name.length).u16(0).u16(0)
                    .u16(0).u16(0).u32(e.externalAttr).u32((int) off).bytes(name);
            count++;
        }

        /** 元の ZIP から、圧縮したまま写す */
        public void copyFrom(Reader r, Entry e) throws IOException {
            byte[] name = e.name.getBytes(StandardCharsets.UTF_8);
            header(e, name);
            CountingOut c = new CountingOut(out);
            r.copyRaw(e, c);
            pos += c.n;
        }

        /** 新しいファイルを足す */
        public void add(String n, byte[] data, int method, int time, int date) throws IOException {
            Entry e = new Entry();
            e.name = n;
            e.method = method;
            e.time = time;
            e.date = date;
            CRC32 crc = new CRC32();
            crc.update(data);
            e.crc = (int) crc.getValue();
            e.size = data.length;
            byte[] body = data;
            if (method == DEFLATED) {
                Deflater d = new Deflater(Deflater.DEFAULT_COMPRESSION, true);
                d.setInput(data);
                d.finish();
                ByteArrayOutputStream bo = new ByteArrayOutputStream();
                byte[] buf = new byte[1 << 16];
                while (!d.finished()) bo.write(buf, 0, d.deflate(buf));
                d.end();
                body = bo.toByteArray();
            }
            e.csize = body.length;
            header(e, n.getBytes(StandardCharsets.UTF_8));
            w(body);
        }

        @Override public void close() throws IOException {
            byte[] cd = central.toByteArray();
            long cdOff = pos;
            w(cd);
            AxmlPatch.Buf eocd = new AxmlPatch.Buf();
            eocd.u32(0x06054b50).u16(0).u16(0).u16(count).u16(count).u32(cd.length).u32((int) cdOff).u16(0);
            w(eocd.toByteArray());
            out.close();
        }
    }

    private static final class CountingOut extends OutputStream {
        final OutputStream o;
        long n;
        CountingOut(OutputStream o) { this.o = o; }
        @Override public void write(int b) throws IOException { o.write(b); n++; }
        @Override public void write(byte[] b, int off, int len) throws IOException { o.write(b, off, len); n += len; }
    }
}
