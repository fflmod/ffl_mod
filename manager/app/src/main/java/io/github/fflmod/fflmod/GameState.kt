package io.github.fflmod.fflmod

import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import java.io.File
import java.io.FileInputStream
import java.security.MessageDigest
import java.util.zip.ZipFile

/** 端末に入っているゲームの状態 */
sealed class GameState {
    /** ゲームが入っていない */
    object NotInstalled : GameState()

    /** 正規版（スクウェア・エニックスの署名）。ok なら対応している版で、ファイルも対応表と一致した */
    data class Original(val versionCode: Long, val ok: Boolean, val detail: String) : GameState()

    /** fflmod の署名。version は libfflmod.so から読んだ版（読めなければ null） */
    data class Modded(val version: String?) : GameState()

    /** どちらでもない署名 */
    data class Unknown(val certSha256: String) : GameState()
}

object Game {
    const val PKG = "com.square_enix.android_googleplay.ffl_gp"
    const val VERSION_CODE = 120L

    /** 正規版（日本の Google Play）の署名の証明書 */
    const val CERT_ORIGINAL = "8575d97a796e08f9fe8887c3d83ab78c5275ba4a691494ed47f566abdac5e495"

    /** fflmod の署名の証明書（keys/fflmod.jks） */
    const val CERT_FFLMOD = "422e4baa786b2df10a77728658da824ed4f2fad4ab98ee577d285c5345795e20"

    /** 正規版 1.2.0 の SHA-256 */
    const val SHA_BASE = "e3753f48234d2d080007c444e1a9b28c5553b4b057e8021bed19e4c96e8fccac"
    const val SHA_ARM64 = "f535938cdc428533f9bef13c1082c561324b0d6fd95820bc13ec8d9ff524c507"

    fun detect(pm: PackageManager): GameState {
        val info = try {
            packageInfo(pm)
        } catch (e: PackageManager.NameNotFoundException) {
            return GameState.NotInstalled
        }
        val cert = certSha256(info) ?: return GameState.Unknown("?")
        return when (cert) {
            CERT_ORIGINAL -> checkOriginal(info)
            CERT_FFLMOD -> GameState.Modded(modVersion(info))
            else -> GameState.Unknown(cert)
        }
    }

    @Suppress("DEPRECATION")
    private fun packageInfo(pm: PackageManager): PackageInfo =
        if (Build.VERSION.SDK_INT >= 28)
            pm.getPackageInfo(PKG, PackageManager.GET_SIGNING_CERTIFICATES)
        else
            pm.getPackageInfo(PKG, PackageManager.GET_SIGNATURES)

    @Suppress("DEPRECATION")
    private fun certSha256(info: PackageInfo): String? {
        val sigs = if (Build.VERSION.SDK_INT >= 28) {
            val si = info.signingInfo ?: return null
            if (si.hasMultipleSigners()) si.apkContentsSigners else si.signingCertificateHistory
        } else {
            info.signatures
        }
        val first = sigs?.firstOrNull() ?: return null
        return hex(MessageDigest.getInstance("SHA-256").digest(first.toByteArray()))
    }

    @Suppress("DEPRECATION")
    private fun versionCode(info: PackageInfo): Long =
        if (Build.VERSION.SDK_INT >= 28) info.longVersionCode else info.versionCode.toLong()

    /** ゲームの APK 一式の場所（base と分割ファイル） */
    fun apkFiles(pm: PackageManager): List<File> {
        val ai = pm.getApplicationInfo(PKG, 0)
        val list = mutableListOf(File(ai.sourceDir))
        ai.splitSourceDirs?.forEach { list.add(File(it)) }
        return list
    }

    private fun checkOriginal(info: PackageInfo): GameState {
        val vc = versionCode(info)
        if (vc != VERSION_CODE) return GameState.Original(vc, false, "versionCode $vc")
        val ai = info.applicationInfo ?: return GameState.Original(vc, false, "no applicationInfo")
        val base = File(ai.sourceDir)
        val arm64 = ai.splitSourceDirs?.map { File(it) }?.firstOrNull { it.name.contains("arm64_v8a") }
            ?: return GameState.Original(vc, false, "arm64 split not found")
        val hb = sha256(base)
        val ha = sha256(arm64)
        val ok = hb == SHA_BASE && ha == SHA_ARM64
        return GameState.Original(vc, ok, "base $hb\narm64 $ha")
    }

    /** 入っている arm64 の分割ファイルの libfflmod.so から "installed v1.0-pre1:" の版を読む */
    private fun modVersion(info: PackageInfo): String? = try {
        val ai = info.applicationInfo
        val arm64 = ai?.splitSourceDirs?.firstOrNull { it.contains("arm64_v8a") }
        if (arm64 == null) null else ZipFile(arm64).use { z ->
            val e = z.getEntry("lib/arm64-v8a/libfflmod.so") ?: return@use null
            val bytes = z.getInputStream(e).use { it.readBytes() }
            val s = String(bytes, Charsets.ISO_8859_1)
            Regex("installed v([0-9A-Za-z.-]+):").find(s)?.groupValues?.get(1)
        }
    } catch (e: Exception) {
        null
    }

    fun sha256(f: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        FileInputStream(f).use { input ->
            val buf = ByteArray(1 shl 16)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        return hex(md.digest())
    }

    fun hex(b: ByteArray): String = b.joinToString("") { "%02x".format(it) }
}
