package io.github.fflmod.fflmod

import android.content.Context
import android.os.Build
import android.os.StatFs
import io.github.fflmod.fflmod.core.ApkBuilder
import io.github.fflmod.fflmod.core.AxmlPatch
import io.github.fflmod.fflmod.core.Signer
import io.github.fflmod.fflmod.core.ZipRaw
import java.io.File
import java.io.IOException

/**
 * 入っている正規版と Release の差分一式から、fflmod の APK 一式を作る（入れるのは Installer）。
 * 出来上がりは アプリ専用のフォルダ/build（Android/data/io.github.fflmod.fflmod/files/build）に置く。
 */
object ModBuild {
    class Result(val dir: File, val version: String, val summaries: List<String>)

    /** 作業に要る空き容量の余裕 */
    private const val MARGIN = 50L shl 20

    fun run(ctx: Context, log: (String) -> Unit): Result {
        val st = Game.detect(ctx.packageManager)
        if (st !is GameState.Original || !st.ok) throw IOException("対応している正規版が入っていません")
        val apks = Game.apkFiles(ctx.packageManager)
        val out = File(ctx.getExternalFilesDir(null), "build")
        out.deleteRecursively()
        if (!out.mkdirs()) throw IOException("作業用のフォルダを作れません")
        // 作る間は署名済みの一式＋作業中の未署名のファイル一つ、入れるときは署名済みの一式＋インストールの作業領域への写しが要る
        val need = 2 * apks.sumOf { it.length() } + MARGIN
        val free = StatFs(out.path).availableBytes
        if (free < need) throw IOException("空き容量が足りません（あと ${(need - free + (1 shl 20) - 1) shr 20}MB 必要です）")

        log("差分一式を取得しています…")
        val p = Release.fetchLatest(log)

        val base = apks.first()
        val man = ZipRaw.Reader(base).use { r ->
            r.read(r.find("AndroidManifest.xml") ?: throw IOException("AndroidManifest.xml がありません"))
        }
        if (Release.sha256(man) != p.manifestFrom) throw IOException("正規版の manifest が想定と違います")
        val man2 = AxmlPatch.patch(man)
        if (Release.sha256(man2) != p.manifestTo) throw IOException("manifest の書き換えの結果が想定と違います")

        val signer = ctx.assets.open("fflmod_cert.der").use { cert ->
            Signer(ctx.assets.open("fflmod_key.pk8").use { it.readBytes() }, cert)
        }
        val v1 = Build.VERSION.SDK_INT >= 26
        val tmp = File(out, "unsigned.tmp")
        val summaries = ArrayList<String>()
        for (f in apks) {
            val name = f.name
            log("$name を作っています…")
            when {
                f == base -> ApkBuilder.buildBase(f, man2, p.dex, tmp)
                name == "split_config.arm64_v8a.apk" -> ApkBuilder.buildArm64(f, p.so, tmp)
                else -> ApkBuilder.resignCopy(f, tmp)
            }
            val dst = File(out, name)
            signer.sign(tmp, dst, v1)
            tmp.delete()
            if (f == base || name == "split_config.arm64_v8a.apk") {
                val s = "$name ${ApkBuilder.summary(dst)}"
                summaries.add(s)
                log(s)
            }
        }
        File(out, "report.txt").writeText(
            "fflmod ${p.version} (${p.tag})\nsigning v1=$v1 v2=true\n" + summaries.joinToString("\n") + "\n"
        )
        return Result(out, p.version, summaries)
    }

    /** 更新で manifest から作れないとき（manifest の書き換えが版によって変わった） */
    class NeedReinstall : IOException("この版は更新では入れられません")

    /** 更新：入っている mod の base・arm64 の中の mod のファイルだけを差し替えて署名する。ほかの分割ファイルはそのまま使う。
     *  戻り値は入れるファイルの一覧（作ったものと、入っている分割ファイル） */
    fun update(ctx: Context, p: Release.Patch, log: (String) -> Unit): Pair<Result, List<File>> {
        val st = Game.detect(ctx.packageManager)
        if (st !is GameState.Modded) throw IOException("fflmod が入っていません")
        val apks = Game.apkFiles(ctx.packageManager)
        val base = apks.first()
        val arm64 = apks.firstOrNull { it.name == "split_config.arm64_v8a.apk" }
            ?: throw IOException("split_config.arm64_v8a.apk がありません")
        val out = File(ctx.getExternalFilesDir(null), "build")
        out.deleteRecursively()
        if (!out.mkdirs()) throw IOException("作業用のフォルダを作れません")
        // 作るのは base と arm64 だけ。入れるときに一式がインストールの作業領域へ写される
        val need = 2 * (base.length() + arm64.length()) + apks.sumOf { it.length() } + MARGIN
        val free = StatFs(out.path).availableBytes
        if (free < need) throw IOException("空き容量が足りません（あと ${(need - free + (1 shl 20) - 1) shr 20}MB 必要です）")

        var dexName: String? = null
        ZipRaw.Reader(base).use { r ->
            val man = r.read(r.find("AndroidManifest.xml") ?: throw IOException("AndroidManifest.xml がありません"))
            if (Release.sha256(man) != p.manifestTo) throw NeedReinstall()
            val mark = "FFLModApp".toByteArray()
            for (e in r.entries) {
                if (!e.name.matches(Regex("classes[2-9][0-9]*\\.dex"))) continue
                if (indexOf(r.read(e), mark) >= 0) dexName = e.name
            }
        }
        val dn = dexName ?: throw IOException("mod の dex が見つかりません")

        val signer = ctx.assets.open("fflmod_cert.der").use { cert ->
            Signer(ctx.assets.open("fflmod_key.pk8").use { it.readBytes() }, cert)
        }
        val v1 = Build.VERSION.SDK_INT >= 26
        val tmp = File(out, "unsigned.tmp")
        val summaries = ArrayList<String>()
        val made = ArrayList<File>()
        for ((src, name, data) in listOf(Triple(base, dn, p.dex), Triple(arm64, "lib/arm64-v8a/libfflmod.so", p.so))) {
            log("${src.name} を作っています…")
            ApkBuilder.rebuild(src, tmp, name, data, null, null, 0)
            val dst = File(out, src.name)
            signer.sign(tmp, dst, v1)
            tmp.delete()
            val s = "${src.name} ${ApkBuilder.summary(dst)}"
            summaries.add(s)
            log(s)
            made.add(dst)
        }
        File(out, "report.txt").writeText(
            "fflmod ${p.version} (${p.tag}) update\nsigning v1=$v1 v2=true\n" + summaries.joinToString("\n") + "\n"
        )
        val list = ArrayList<File>(made)
        for (f in apks) if (f != base && f != arm64) list.add(f)
        return Pair(Result(out, p.version, summaries), list)
    }

    private fun indexOf(h: ByteArray, n: ByteArray): Int {
        outer@ for (i in 0..h.size - n.size) {
            for (j in n.indices) if (h[i + j] != n[j]) continue@outer
            return i
        }
        return -1
    }
}
