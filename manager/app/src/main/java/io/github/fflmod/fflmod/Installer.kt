package io.github.fflmod.fflmod

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import java.io.File

/** 作った APK 一式を PackageInstaller でまとめて入れる */
object Installer {
    /** 結果を受け取る画面（開いていれば） */
    @Volatile
    var listener: ((Result) -> Unit)? = null

    sealed class Result {
        /** 入れてよいか利用者に聞く画面を出す必要がある */
        class NeedUser(val intent: Intent) : Result()
        object Success : Result()
        class Failure(val message: String) : Result()
    }

    fun install(ctx: Context, apks: List<File>) {
        val pi = ctx.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL)
        params.setAppPackageName(Game.PKG)
        val id = pi.createSession(params)
        pi.openSession(id).use { s ->
            for (f in apks) {
                s.openWrite(f.name, 0, f.length()).use { out ->
                    f.inputStream().use { it.copyTo(out, 1 shl 16) }
                    s.fsync(out)
                }
            }
            val intent = Intent(ctx, InstallReceiver::class.java)
            val flags = if (Build.VERSION.SDK_INT >= 31)
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE
            else PendingIntent.FLAG_UPDATE_CURRENT
            s.commit(PendingIntent.getBroadcast(ctx, id, intent, flags).intentSender)
        }
    }

    internal fun deliver(ctx: Context, r: Result) {
        val l = listener
        if (l != null) {
            l(r)
            return
        }
        // 画面が閉じているとき：確認の画面はここから出し、結果は次に開いたときに見せる
        when (r) {
            is Result.NeedUser -> ctx.startActivity(r.intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            is Result.Success -> Store.finishInstall(ctx, true, null)
            is Result.Failure -> Store.finishInstall(ctx, false, r.message)
        }
    }
}

class InstallReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)
        val r = when (status) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                @Suppress("DEPRECATION")
                val confirm: Intent? = if (Build.VERSION.SDK_INT >= 33)
                    intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
                else intent.getParcelableExtra(Intent.EXTRA_INTENT)
                if (confirm != null) Installer.Result.NeedUser(confirm)
                else Installer.Result.Failure("確認の画面を出せませんでした")
            }
            PackageInstaller.STATUS_SUCCESS -> Installer.Result.Success
            else -> Installer.Result.Failure(
                "status $status: " + (intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE) ?: "")
            )
        }
        Installer.deliver(ctx.applicationContext, r)
    }
}
