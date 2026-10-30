package io.github.fflmod.fflmod

import android.content.Context
import java.io.File

/**
 * 導入の途中の状態と、最後の結果の表示を覚えておく（ほかのアプリから戻ったときや、アプリが閉じられたあとも続けられるように）。
 * phase：""（何もしていない）、"uninstall"（APK を作り終え、正規版の削除を待っている）、"install"（インストールの確認を待っている）
 */
object Store {
    private fun sp(ctx: Context) = ctx.getSharedPreferences("fflmod", Context.MODE_PRIVATE)

    fun phase(ctx: Context): String = sp(ctx).getString("phase", "") ?: ""
    fun setPhase(ctx: Context, p: String) = sp(ctx).edit().putString("phase", p).apply()

    fun version(ctx: Context): String = sp(ctx).getString("version", "") ?: ""
    fun setVersion(ctx: Context, v: String) = sp(ctx).edit().putString("version", v).apply()

    fun message(ctx: Context): String = sp(ctx).getString("message", "") ?: ""
    fun log(ctx: Context): String = sp(ctx).getString("log", "") ?: ""
    fun setMessage(ctx: Context, message: String, log: String) =
        sp(ctx).edit().putString("message", message).putString("log", log).apply()

    /** 作った APK 一式の場所 */
    fun buildDir(ctx: Context) = File(ctx.getExternalFilesDir(null), "build")

    /** 作り終えた一式（report.txt があるときだけ） */
    fun builtApks(ctx: Context): List<File>? {
        val d = buildDir(ctx)
        if (!File(d, "report.txt").isFile || !File(d, "base.apk").isFile) return null
        return d.listFiles { f -> f.name.endsWith(".apk") }?.sortedBy { if (it.name == "base.apk") "" else it.name }
    }

    /** "install"（正規版から導入）か "update"（mod の更新） */
    fun mode(ctx: Context): String = sp(ctx).getString("mode", "install") ?: "install"
    fun setMode(ctx: Context, m: String) = sp(ctx).edit().putString("mode", m).apply()

    fun finishInstall(ctx: Context, ok: Boolean, reason: String?) {
        setPhase(ctx, "")
        val update = mode(ctx) == "update"
        if (ok) {
            buildDir(ctx).deleteRecursively()
            setMessage(ctx, if (update) "fflmod ${version(ctx)} に更新しました。"
                else "fflmod ${version(ctx)} を導入しました。\nセーブを取り込むときは「セーブを取り込む」を押してください。", "")
        } else {
            setMessage(ctx, if (update) "更新できませんでした。" else "導入できませんでした。正規版は Google Play から入れ直せます。",
                reason ?: "")
        }
    }
}
