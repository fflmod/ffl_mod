package io.github.fflmod.fflmod

import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** GitHub Releases から差分一式（files.json、fflmod.dex、libfflmod.so）を取ってくる */
object Release {
    private const val API = "https://api.github.com/repos/fflmod/ffl_mod/releases"
    const val FORMAT = 2
    const val INTERFACE = 1

    /** 試験版（プレリリース）も最新の候補に入れるか。ふだんは正式版だけ（画面の長押しで切り替え、Store が覚える） */
    @Volatile var includePre = false

    class Patch(
        val tag: String,
        val version: String,
        val manifestFrom: String,
        val manifestTo: String,
        val dex: ByteArray,
        val so: ByteArray,
    )

    /** いちばん新しい Release（試験版は includePre のときだけ）の差分一式 */
    fun fetchLatest(log: (String) -> Unit): Patch {
        val rel = latestRelease()
        val tag = rel.getString("tag_name")
        log("Release $tag")
        val info = JSONObject(String(get(asset(rel, "files.json")!!), Charsets.UTF_8))
        if (info.getInt("format") != FORMAT) throw IOException("files.json の形式 ${info.getInt("format")} には対応していません")
        if (info.getInt("interface") != INTERFACE) throw IOException("interface ${info.getInt("interface")} には対応していません")
        val game = info.getJSONObject("game")
        val sha = game.getJSONObject("sha256")
        if (game.getString("package") != Game.PKG || game.getLong("versionCode") != Game.VERSION_CODE ||
            sha.getString("base.apk") != Game.SHA_BASE || sha.getString("split_config.arm64_v8a.apk") != Game.SHA_ARM64
        ) throw IOException("この Release は、管理アプリが対応しているゲームの版と違います")
        val files = info.getJSONObject("files")
        val data = HashMap<String, ByteArray>()
        for (name in listOf("fflmod.dex", "libfflmod.so")) {
            val meta = files.getJSONObject(name)
            val url = asset(rel, name) ?: throw IOException("$name が Release にありません")
            val d = get(url)
            if (d.size.toLong() != meta.getLong("size") || sha256(d) != meta.getString("sha256"))
                throw IOException("$name が files.json と一致しません")
            log("$name ${d.size} bytes")
            data[name] = d
        }
        val man = info.getJSONObject("manifest")
        return Patch(tag, info.getString("version"), man.getString("from_sha256"), man.getString("to_sha256"),
            data["fflmod.dex"]!!, data["libfflmod.so"]!!)
    }

    /** 最新の mod の版（タグの v を除いたもの）。一覧だけを読み、mod の中身はダウンロードしない */
    fun latestVersion(): String = latestRelease().getString("tag_name").removePrefix("v")

    /**
     * 公開した日時がいちばん新しい Release（試験版は includePre のときだけ）。GitHub の一覧は版の名前の順（pre10 が pre2 より後ろ）に
     * 並ぶので、先頭ではなく published_at で選ぶ（ISO 8601 の UTC なので文字列の比較で足りる）
     */
    private fun latestRelease(): JSONObject {
        val list = JSONArray(String(get("$API?per_page=100"), Charsets.UTF_8))
        var rel: JSONObject? = null
        for (i in 0 until list.length()) {
            val r = list.getJSONObject(i)
            if (r.optBoolean("draft") || asset(r, "files.json") == null) continue
            if (r.optBoolean("prerelease") && !includePre) continue
            if (rel == null || r.optString("published_at") > rel.optString("published_at")) rel = r
        }
        return rel ?: throw IOException("Release が見つかりません")
    }

    private fun asset(rel: JSONObject, name: String): String? {
        val a = rel.optJSONArray("assets") ?: return null
        for (i in 0 until a.length()) {
            val o = a.getJSONObject(i)
            if (o.getString("name") == name) return o.getString("browser_download_url")
        }
        return null
    }

    private fun get(url: String): ByteArray {
        var u = URL(url)
        repeat(5) {
            val c = u.openConnection() as HttpURLConnection
            c.connectTimeout = 15000
            c.readTimeout = 30000
            c.instanceFollowRedirects = false
            c.setRequestProperty("User-Agent", "fflmod-manager")
            c.setRequestProperty("Accept", if (url.startsWith(API)) "application/vnd.github+json" else "application/octet-stream")
            try {
                val code = c.responseCode
                if (code in 300..399) {
                    u = URL(u, c.getHeaderField("Location"))
                    return@repeat
                }
                if (code != 200) throw IOException("HTTP $code: $u")
                val out = ByteArrayOutputStream()
                c.inputStream.use { it.copyTo(out) }
                return out.toByteArray()
            } finally {
                c.disconnect()
            }
        }
        throw IOException("転送先が多すぎます: $url")
    }

    fun sha256(d: ByteArray): String = Game.hex(MessageDigest.getInstance("SHA-256").digest(d))
}
