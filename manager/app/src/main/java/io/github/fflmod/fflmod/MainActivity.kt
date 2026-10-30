package io.github.fflmod.fflmod

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.provider.Settings
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView

/**
 * 管理アプリの画面（1画面）。
 * 「modを導入」で APK 一式を作り、正規版を削除して、mod の一式を入れる。「更新」は入っている mod の上から新しい版を入れる。
 * セーブの書き出し・取り込みはゲームの画面を開く。
 */
class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var versions: TextView
    private lateinit var result: TextView
    private lateinit var log: TextView
    private lateinit var install: Button
    private lateinit var update: Button
    private lateinit var export: Button
    private lateinit var importBtn: Button
    private val main = Handler(Looper.getMainLooper())
    private var state: GameState? = null
    /** 最新の mod の版。null はまだ読んでいない、"" は確認できなかった */
    private var latest: String? = null
    private var busy = false
    private var waitingUninstall = false
    private val logBuf = StringBuilder()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        val lp = { LinearLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT) }
        status = TextView(this).apply { textSize = 18f }
        root.addView(status, lp())
        versions = TextView(this).apply { textSize = 16f; setPadding(0, pad / 2, 0, 0) }
        root.addView(versions, lp())
        result = TextView(this).apply { textSize = 16f; setPadding(0, pad / 2, 0, pad / 2) }
        root.addView(result, lp())
        install = Button(this).apply { text = "modを導入"; setOnClickListener { onInstall() } }
        root.addView(install, lp())
        update = Button(this).apply { text = "modを更新"; setOnClickListener { onUpdate() } }
        root.addView(update, lp())
        export = Button(this).apply { text = "セーブを書き出す"; setOnClickListener { openSave("fflmod.intent.action.EXPORT_SAVE") } }
        root.addView(export, lp())
        importBtn = Button(this).apply { text = "セーブを取り込む"; setOnClickListener { openSave("fflmod.intent.action.IMPORT_SAVE") } }
        root.addView(importBtn, lp())
        /* ボタンの英字を大文字にしない（Android の既定は大文字にする） */
        for (b in listOf(install, update, export, importBtn)) b.isAllCaps = false
        log = TextView(this).apply { textSize = 12f; setTextIsSelectable(true) }
        root.addView(log, lp())
        setContentView(ScrollView(this).apply { addView(root) })
        Installer.listener = { r -> main.post { onInstallResult(r) } }
    }

    override fun onDestroy() {
        Installer.listener = null
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        if (!busy) refresh()
    }

    // ---- 状態の表示 ----

    private fun refresh() {
        Thread {
            val st = detect()
            main.post { show(st) }
        }.start()
        Thread {
            val v = try { Release.latestVersion() } catch (e: Exception) { "" }
            main.post { latest = v; showVersions() }
        }.start()
    }

    /** 入っている mod と最新の mod の版を並べて出す */
    private fun showVersions() {
        val st = state
        val have = when (st) {
            is GameState.Modded -> st.version ?: "（版を読めませんでした）"
            null -> "（調べられませんでした）"
            else -> "なし"
        }
        val l = latest
        val sb = StringBuilder("現在バージョン：").append(have).append("\n最新バージョン：")
        when {
            l == null -> sb.append("確認しています…")
            l.isEmpty() -> sb.append("確認できませんでした")
            else -> {
                sb.append(l)
                if (st is GameState.Modded && st.version != null)
                    sb.append(if (st.version == l) "\n最新版です。" else "\nアップデートがあります。「modを更新」から入れられます。")
            }
        }
        versions.text = sb
    }

    private fun detect(): GameState? = try {
        Game.detect(packageManager)
    } catch (e: Exception) {
        null
    }

    private fun show(st: GameState?) {
        state = st
        status.text = when (st) {
            null -> "ゲームの状態を調べられませんでした。"
            is GameState.NotInstalled -> "FFレジェンズが入っていません。"
            is GameState.Original ->
                if (st.ok) "正規版（1.2.0）が入っています。\nmodを導入できます。"
                else "正規版が入っていますが、対応していない版です。"
            is GameState.Modded ->
                "modが導入されています。"
            is GameState.Unknown -> "正規版とも fflmod とも違う署名のアプリが入っています。"
        }
        // 導入の途中から続ける（アプリが閉じられていたときなど）
        when (Store.phase(this)) {
            "uninstall" -> if (!waitingUninstall) {
                if (st is GameState.NotInstalled) { startInstall(); return }
                Store.setPhase(this, "")
                Store.setMessage(this, "正規版の削除が取り消されました。", Store.log(this))
            }
            "perm" -> {
                Store.setPhase(this, "")
                if (st is GameState.NotInstalled && canInstall()) { startInstall(); return }
            }
            "install" -> if (st is GameState.Modded && (Store.mode(this) != "update" || st.version == Store.version(this)))
                Store.finishInstall(this, true, null)
        }
        showVersions()
        result.text = Store.message(this)
        if (!busy) log.text = Store.log(this).ifEmpty {
            when (st) {
                is GameState.Original -> st.detail
                is GameState.Unknown -> "cert ${st.certSha256}"
                else -> ""
            }
        }
        updateButtons()
    }

    private fun updateButtons() {
        val st = state
        install.isEnabled = !busy && ((st is GameState.Original && st.ok) ||
                (st is GameState.NotInstalled && Store.builtApks(this) != null))
        update.isEnabled = !busy && st is GameState.Modded
        export.isEnabled = !busy && st is GameState.Modded
        importBtn.isEnabled = !busy && st is GameState.Modded
    }

    private fun addLog(s: String) = main.post {
        logBuf.append(s).append('\n')
        log.text = logBuf
    }

    private fun setBusy(b: Boolean) {
        busy = b
        updateButtons()
    }

    // ---- 導入 ----

    private fun onInstall() {
        val st = state
        if (st is GameState.NotInstalled) {
            Store.setMode(this, "install")
            startInstall()   // 作った一式が残っている（前回の導入が途中で終わった）
            return
        }
        if (st !is GameState.Original || !st.ok) return
        setBusy(true)
        logBuf.setLength(0)
        log.text = ""
        result.text = ""
        status.text = "modを入れたAPKを作っています…"
        Thread {
            val r = try {
                ModBuild.run(this) { addLog(it) }
            } catch (e: Exception) {
                addLog(e.toString())
                null
            }
            main.post {
                setBusy(false)
                if (r == null) {
                    Store.setMessage(this, "APKを作成できませんでした。", logBuf.toString())
                    refresh()
                } else {
                    Store.setVersion(this, r.version)
                    Store.setMode(this, "install")
                    Store.setMessage(this, "", logBuf.toString())
                    confirmUninstall()
                }
            }
        }.start()
    }

    // ---- 更新 ----

    private fun onUpdate() {
        val st = state as? GameState.Modded ?: return
        setBusy(true)
        logBuf.setLength(0)
        log.text = ""
        result.text = ""
        status.text = "modの更新を準備しています…"
        Thread {
            val p = try {
                Release.fetchLatest { addLog(it) }
            } catch (e: Exception) {
                addLog(e.toString())
                null
            }
            main.post {
                if (p == null) {
                    setBusy(false)
                    Store.setMessage(this, "更新できませんでした。", logBuf.toString())
                    refresh()
                } else if (st.version == p.version) {
                    AlertDialog.Builder(this)
                        .setTitle("同じ版です")
                        .setMessage("fflmod ${p.version} はすでに入っています。入れ直しますか？")
                        .setPositiveButton("入れ直す") { _, _ -> buildUpdate(p) }
                        .setNegativeButton("やめる") { _, _ -> setBusy(false); refresh() }
                        .setCancelable(false)
                        .show()
                } else {
                    buildUpdate(p)
                }
            }
        }.start()
    }

    private fun buildUpdate(p: Release.Patch) {
        Thread {
            var msg = "更新できませんでした。"
            val r = try {
                ModBuild.update(this, p) { addLog(it) }
            } catch (e: ModBuild.NeedReinstall) {
                msg = "この版は「modを更新」では入れられません。セーブを書き出してから、modを入れ直してください。"
                null
            } catch (e: Exception) {
                addLog(e.toString())
                null
            }
            main.post {
                setBusy(false)
                if (r == null) {
                    Store.setMessage(this, msg, logBuf.toString())
                    refresh()
                } else {
                    Store.setVersion(this, r.first.version)
                    Store.setMode(this, "update")
                    Store.setMessage(this, "", logBuf.toString())
                    startInstall(r.second)
                }
            }
        }.start()
    }

    private fun confirmUninstall() {
        AlertDialog.Builder(this)
            .setTitle("正規版を削除します")
            .setMessage("modを導入するには、いま入っている正規版をいったん削除する必要があります。削除すると、この端末にあるセーブデータも消えます。先にゲームのクラウドセーブで保存しておくと、導入後に Google データ エクスポートを使って取り込めます。")
            .setPositiveButton("続ける") { _, _ ->
                AlertDialog.Builder(this)
                    .setTitle("セーブデータは保存しましたか？")
                    .setMessage("削除したセーブデータは元に戻せません。")
                    .setPositiveButton("削除する") { _, _ -> uninstall() }
                    .setNegativeButton("やめる") { _, _ -> refresh() }
                    .setCancelable(false)
                    .show()
            }
            .setNegativeButton("やめる") { _, _ -> refresh() }
            .setCancelable(false)
            .show()
    }

    @Suppress("DEPRECATION")
    private fun uninstall() {
        Store.setPhase(this, "uninstall")
        waitingUninstall = true
        val i = Intent(Intent.ACTION_UNINSTALL_PACKAGE, Uri.parse("package:" + Game.PKG))
            .putExtra(Intent.EXTRA_RETURN_RESULT, true)
        startActivityForResult(i, REQ_UNINSTALL)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode != REQ_UNINSTALL) return
        setBusy(true)
        // 削除の画面が閉じてから、削除が終わるまで少し待つことがある
        Thread {
            var st = detect()
            var n = 0
            while (st !is GameState.NotInstalled && n++ < 10) {
                Thread.sleep(500)
                st = detect()
            }
            main.post {
                waitingUninstall = false
                setBusy(false)
                if (st is GameState.NotInstalled) {
                    state = st
                    startInstall()
                } else {
                    Store.setPhase(this, "")
                    Store.setMessage(this, "正規版の削除が取り消されました。", Store.log(this))
                    refresh()
                }
            }
        }.start()
    }

    private fun canInstall() = Build.VERSION.SDK_INT < 26 || packageManager.canRequestPackageInstalls()

    private fun startInstall(list: List<java.io.File>? = null) {
        val apks = list ?: Store.builtApks(this)
        if (apks == null) {
            Store.setPhase(this, "")
            Store.setMessage(this, "導入できませんでした。正規版は Google Play から入れ直せます。", "作ったAPKが見つかりません")
            refresh()
            return
        }
        if (!canInstall()) {
            Store.setPhase(this, "perm")
            Store.setMessage(this, "このアプリにアプリのインストールを許可してください。", Store.log(this))
            result.text = Store.message(this)
            startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:$packageName")))
            return
        }
        Store.setPhase(this, "install")
        setBusy(true)
        status.text = if (Store.mode(this) == "update") "modを更新しています…" else "modを導入しています…"
        Thread {
            try {
                Installer.install(this, apks)
            } catch (e: Exception) {
                main.post { onInstallResult(Installer.Result.Failure(e.toString())) }
            }
        }.start()
    }

    private fun onInstallResult(r: Installer.Result) {
        when (r) {
            is Installer.Result.NeedUser -> {
                startActivity(r.intent)
                return
            }
            is Installer.Result.Success -> Store.finishInstall(this, true, null)
            is Installer.Result.Failure -> Store.finishInstall(this, false, r.message)
        }
        setBusy(false)
        refresh()
    }

    // ---- セーブ ----

    private fun openSave(action: String) {
        try {
            startActivity(Intent(action).setClassName(Game.PKG, Game.PKG + ".FFLModSaveActivity"))
        } catch (e: Exception) {
            result.text = "ゲームの画面を開けませんでした。"
            log.text = e.toString()
        }
    }

    companion object {
        private const val REQ_UNINSTALL = 1
    }
}
