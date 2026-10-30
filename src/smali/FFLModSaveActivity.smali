.class public Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;
.super Landroid/app/Activity;
.source "FFLModSaveActivity.java"

# セーブデータの書き出し・取り込み（v4.06）。管理アプリや adb から次のアクションで呼ぶ。
#   fflmod.intent.action.EXPORT_SAVE  内部の files/save.bin を、Android の保存画面で選んだ場所へ写す
#   fflmod.intent.action.IMPORT_SAVE  選んだファイル（save.bin そのもの、または Google データ エクスポートの zip）を
#                                     外部の files/save_import.bin に置き、プロセスを終える。次の起動で FFLMod が取り込む
# 透明な画面で、文言はダイアログと通知（トースト）だけ。ゲームと同じアプリの中で動くので save.bin を直接読める。


# instance fields
.field private pending:[B


# direct methods
.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Landroid/app/Activity;-><init>()V

    return-void
.end method

# セーブデータか：260000 バイトで先頭が "FJ00"
.method private static isSave([B)Z
    .locals 3

    const/4 v0, 0x0

    if-eqz p0, :no

    array-length v1, p0

    const v2, 0x3f7a0

    if-ne v1, v2, :no

    aget-byte v1, p0, v0

    const/16 v2, 0x46

    if-ne v1, v2, :no

    const/4 v1, 0x1

    aget-byte v1, p0, v1

    const/16 v2, 0x4a

    if-ne v1, v2, :no

    const/4 v1, 0x2

    aget-byte v1, p0, v1

    const/16 v2, 0x30

    if-ne v1, v2, :no

    const/4 v1, 0x3

    aget-byte v1, p0, v1

    if-ne v1, v2, :no

    const/4 v0, 0x1

    :no
    return v0
.end method

# 全部読む。limit を超えたら null
.method private static readAll(Ljava/io/InputStream;I)[B
    .locals 5
    .annotation system Ldalvik/annotation/Throws;
        value = {
            Ljava/io/IOException;
        }
    .end annotation

    new-instance v0, Ljava/io/ByteArrayOutputStream;

    invoke-direct {v0}, Ljava/io/ByteArrayOutputStream;-><init>()V

    const/high16 v1, 0x10000

    new-array v1, v1, [B

    const/4 v2, 0x0

    :loop
    invoke-virtual {p0, v1}, Ljava/io/InputStream;->read([B)I

    move-result v3

    if-ltz v3, :done

    add-int/2addr v2, v3

    if-le v2, p1, :ok

    const/4 v4, 0x0

    return-object v4

    :ok
    const/4 v4, 0x0

    invoke-virtual {v0, v1, v4, v3}, Ljava/io/ByteArrayOutputStream;->write([BII)V

    goto :loop

    :done
    invoke-virtual {v0}, Ljava/io/ByteArrayOutputStream;->toByteArray()[B

    move-result-object v4

    return-object v4
.end method

# メタデータ.html の最初の "yyyy-MM-ddTHH:mm:ssZ" を日本時間の "yyyy/MM/dd HH:mm" にする。無ければ null
.method private static metaDate(Ljava/lang/String;)Ljava/lang/String;
    .locals 6

    const/4 v5, 0x0

    if-eqz p0, :none

    :try_start_0
    const-string v0, "\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\dZ"

    invoke-static {v0}, Ljava/util/regex/Pattern;->compile(Ljava/lang/String;)Ljava/util/regex/Pattern;

    move-result-object v0

    invoke-virtual {v0, p0}, Ljava/util/regex/Pattern;->matcher(Ljava/lang/CharSequence;)Ljava/util/regex/Matcher;

    move-result-object v0

    invoke-virtual {v0}, Ljava/util/regex/Matcher;->find()Z

    move-result v1

    if-eqz v1, :none

    invoke-virtual {v0}, Ljava/util/regex/Matcher;->group()Ljava/lang/String;

    move-result-object v1

    const-string v3, "yyyy-MM-dd\'T\'HH:mm:ss\'Z\'"

    sget-object v4, Ljava/util/Locale;->US:Ljava/util/Locale;

    new-instance v2, Ljava/text/SimpleDateFormat;

    invoke-direct {v2, v3, v4}, Ljava/text/SimpleDateFormat;-><init>(Ljava/lang/String;Ljava/util/Locale;)V

    const-string v3, "UTC"

    invoke-static {v3}, Ljava/util/TimeZone;->getTimeZone(Ljava/lang/String;)Ljava/util/TimeZone;

    move-result-object v3

    invoke-virtual {v2, v3}, Ljava/text/SimpleDateFormat;->setTimeZone(Ljava/util/TimeZone;)V

    invoke-virtual {v2, v1}, Ljava/text/SimpleDateFormat;->parse(Ljava/lang/String;)Ljava/util/Date;

    move-result-object v1

    const-string v3, "yyyy/MM/dd HH:mm"

    new-instance v2, Ljava/text/SimpleDateFormat;

    invoke-direct {v2, v3, v4}, Ljava/text/SimpleDateFormat;-><init>(Ljava/lang/String;Ljava/util/Locale;)V

    const-string v3, "Asia/Tokyo"

    invoke-static {v3}, Ljava/util/TimeZone;->getTimeZone(Ljava/lang/String;)Ljava/util/TimeZone;

    move-result-object v3

    invoke-virtual {v2, v3}, Ljava/text/SimpleDateFormat;->setTimeZone(Ljava/util/TimeZone;)V

    invoke-virtual {v2, v1}, Ljava/text/SimpleDateFormat;->format(Ljava/util/Date;)Ljava/lang/String;

    move-result-object v5
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    :none
    return-object v5

    :catch_0
    move-exception v0

    const/4 v5, 0x0

    goto :none
.end method

# 名前の最後の "/" までを返す（なければ ""）
.method private static folder(Ljava/lang/String;)Ljava/lang/String;
    .locals 2

    const/16 v0, 0x2f

    invoke-virtual {p0, v0}, Ljava/lang/String;->lastIndexOf(I)I

    move-result v0

    add-int/lit8 v0, v0, 0x1

    const/4 v1, 0x0

    invoke-virtual {p0, v1, v0}, Ljava/lang/String;->substring(II)Ljava/lang/String;

    move-result-object v0

    return-object v0
.end method


# virtual methods
.method protected onCreate(Landroid/os/Bundle;)V
    .locals 2

    invoke-super {p0, p1}, Landroid/app/Activity;->onCreate(Landroid/os/Bundle;)V

    # 作り直されたとき（画面の向きなど）は、選択画面の結果を待つだけ
    if-eqz p1, :fresh

    return-void

    :fresh
    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->getIntent()Landroid/content/Intent;

    move-result-object v0

    invoke-virtual {v0}, Landroid/content/Intent;->getAction()Ljava/lang/String;

    move-result-object v0

    const-string v1, "fflmod.intent.action.EXPORT_SAVE"

    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v1

    if-eqz v1, :not_export

    invoke-direct {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->exportStart()V

    return-void

    :not_export
    const-string v1, "fflmod.intent.action.IMPORT_SAVE"

    invoke-virtual {v1, v0}, Ljava/lang/String;->equals(Ljava/lang/Object;)Z

    move-result v1

    if-eqz v1, :unknown

    invoke-direct {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->importStart()V

    return-void

    :unknown
    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->finish()V

    return-void
.end method

.method private toast(Ljava/lang/String;)V
    .locals 2

    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->getApplicationContext()Landroid/content/Context;

    move-result-object v0

    const/4 v1, 0x1

    invoke-static {v0, p1, v1}, Landroid/widget/Toast;->makeText(Landroid/content/Context;Ljava/lang/CharSequence;I)Landroid/widget/Toast;

    move-result-object v0

    invoke-virtual {v0}, Landroid/widget/Toast;->show()V

    return-void
.end method

.method private toastFinish(Ljava/lang/String;)V
    .locals 0

    invoke-direct {p0, p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toast(Ljava/lang/String;)V

    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->finish()V

    return-void
.end method

.method private saveFile()Ljava/io/File;
    .locals 3

    new-instance v0, Ljava/io/File;

    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->getFilesDir()Ljava/io/File;

    move-result-object v1

    const-string v2, "save.bin"

    invoke-direct {v0, v1, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V

    return-object v0
.end method

# ボタンが1つ（OK）のダイアログ。押したら onDialog(kind)
.method private okDialog(Ljava/lang/String;I)V
    .locals 4

    new-instance v0, Landroid/app/AlertDialog$Builder;

    const/4 v1, 0x5

    invoke-direct {v0, p0, v1}, Landroid/app/AlertDialog$Builder;-><init>(Landroid/content/Context;I)V

    invoke-virtual {v0, p1}, Landroid/app/AlertDialog$Builder;->setMessage(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;

    const/4 v1, 0x0

    invoke-virtual {v0, v1}, Landroid/app/AlertDialog$Builder;->setCancelable(Z)Landroid/app/AlertDialog$Builder;

    new-instance v1, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;

    invoke-direct {v1, p0, p2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;-><init>(Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;I)V

    const-string v2, "OK"

    invoke-virtual {v0, v2, v1}, Landroid/app/AlertDialog$Builder;->setPositiveButton(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;

    invoke-virtual {v0}, Landroid/app/AlertDialog$Builder;->show()Landroid/app/AlertDialog;

    return-void
.end method

# ---- 書き出し ----
.method private exportStart()V
    .locals 5

    invoke-direct {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->saveFile()Ljava/io/File;

    move-result-object v0

    invoke-virtual {v0}, Ljava/io/File;->exists()Z

    move-result v0

    if-nez v0, :has_save

    const-string v0, "セーブデータが見つかりません。ゲームで一度セーブしてから書き出してください。"

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void

    :has_save
    const-string v1, "yyyyMMdd_HHmm"

    sget-object v2, Ljava/util/Locale;->US:Ljava/util/Locale;

    new-instance v0, Ljava/text/SimpleDateFormat;

    invoke-direct {v0, v1, v2}, Ljava/text/SimpleDateFormat;-><init>(Ljava/lang/String;Ljava/util/Locale;)V

    new-instance v1, Ljava/util/Date;

    invoke-direct {v1}, Ljava/util/Date;-><init>()V

    invoke-virtual {v0, v1}, Ljava/text/SimpleDateFormat;->format(Ljava/util/Date;)Ljava/lang/String;

    move-result-object v0

    new-instance v1, Ljava/lang/StringBuilder;

    const-string v2, "fflmod_save_"

    invoke-direct {v1, v2}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    const-string v2, ".bin"

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    new-instance v1, Landroid/content/Intent;

    const-string v2, "android.intent.action.CREATE_DOCUMENT"

    invoke-direct {v1, v2}, Landroid/content/Intent;-><init>(Ljava/lang/String;)V

    const-string v2, "android.intent.category.OPENABLE"

    invoke-virtual {v1, v2}, Landroid/content/Intent;->addCategory(Ljava/lang/String;)Landroid/content/Intent;

    const-string v2, "application/octet-stream"

    invoke-virtual {v1, v2}, Landroid/content/Intent;->setType(Ljava/lang/String;)Landroid/content/Intent;

    const-string v2, "android.intent.extra.TITLE"

    invoke-virtual {v1, v2, v0}, Landroid/content/Intent;->putExtra(Ljava/lang/String;Ljava/lang/String;)Landroid/content/Intent;

    :try_start_0
    const/4 v2, 0x1

    invoke-virtual {p0, v1, v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->startActivityForResult(Landroid/content/Intent;I)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    return-void

    :catch_0
    move-exception v2

    const-string v2, "書き出しに失敗しました。保存先を変えてもう一度お試しください。"

    invoke-direct {p0, v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method

.method private exportTo(Landroid/net/Uri;)V
    .locals 5

    const/4 v0, 0x0

    const/4 v1, 0x0

    :try_start_0
    invoke-direct {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->saveFile()Ljava/io/File;

    move-result-object v2

    new-instance v0, Ljava/io/FileInputStream;

    invoke-direct {v0, v2}, Ljava/io/FileInputStream;-><init>(Ljava/io/File;)V

    const v2, 0x100000

    invoke-static {v0, v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->readAll(Ljava/io/InputStream;I)[B

    move-result-object v2

    invoke-virtual {v0}, Ljava/io/FileInputStream;->close()V

    if-eqz v2, :fail

    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->getContentResolver()Landroid/content/ContentResolver;

    move-result-object v3

    const-string v4, "w"

    invoke-virtual {v3, p1, v4}, Landroid/content/ContentResolver;->openOutputStream(Landroid/net/Uri;Ljava/lang/String;)Ljava/io/OutputStream;

    move-result-object v1

    if-eqz v1, :fail

    invoke-virtual {v1, v2}, Ljava/io/OutputStream;->write([B)V

    invoke-virtual {v1}, Ljava/io/OutputStream;->flush()V

    invoke-virtual {v1}, Ljava/io/OutputStream;->close()V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    const-string v0, "セーブデータを書き出しました。"

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void

    :catch_0
    move-exception v0

    :fail
    const-string v0, "書き出しに失敗しました。保存先を変えてもう一度お試しください。"

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method

# ---- 取り込み ----
.method private importStart()V
    .locals 3

    new-instance v0, Landroid/content/Intent;

    const-string v1, "android.intent.action.OPEN_DOCUMENT"

    invoke-direct {v0, v1}, Landroid/content/Intent;-><init>(Ljava/lang/String;)V

    const-string v1, "android.intent.category.OPENABLE"

    invoke-virtual {v0, v1}, Landroid/content/Intent;->addCategory(Ljava/lang/String;)Landroid/content/Intent;

    const-string v1, "*/*"

    invoke-virtual {v0, v1}, Landroid/content/Intent;->setType(Ljava/lang/String;)Landroid/content/Intent;

    :try_start_0
    const/4 v1, 0x2

    invoke-virtual {p0, v0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->startActivityForResult(Landroid/content/Intent;I)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    return-void

    :catch_0
    move-exception v1

    const-string v1, "ファイルを読み込めませんでした。"

    invoke-direct {p0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method

.method private importFrom(Landroid/net/Uri;)V
    .locals 4

    :try_start_0
    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->getContentResolver()Landroid/content/ContentResolver;

    move-result-object v0

    invoke-virtual {v0, p1}, Landroid/content/ContentResolver;->openInputStream(Landroid/net/Uri;)Ljava/io/InputStream;

    move-result-object v0

    if-eqz v0, :readfail

    const/high16 v1, 0x4000000

    invoke-static {v0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->readAll(Ljava/io/InputStream;I)[B

    move-result-object v1

    invoke-virtual {v0}, Ljava/io/InputStream;->close()V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    # 64MB を超えるものはセーブでも Takeout の zip でもない
    if-eqz v1, :notsave

    invoke-static {v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->isSave([B)Z

    move-result v2

    if-eqz v2, :not_bin

    iput-object v1, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->pending:[B

    const/4 v2, 0x0

    invoke-direct {p0, v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->confirm(Ljava/lang/String;)V

    return-void

    :not_bin
    # zip（先頭 "PK"）なら中を探す
    array-length v2, v1

    const/4 v3, 0x4

    if-lt v2, v3, :notsave

    const/4 v2, 0x0

    aget-byte v2, v1, v2

    const/16 v3, 0x50

    if-ne v2, v3, :notsave

    const/4 v2, 0x1

    aget-byte v2, v1, v2

    const/16 v3, 0x4b

    if-ne v2, v3, :notsave

    invoke-direct {p0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->scanZip([B)V

    return-void

    :notsave
    const-string v0, "このファイルはFFレジェンズのセーブデータではありません。"

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void

    :catch_0
    move-exception v0

    :readfail
    const-string v0, "ファイルを読み込めませんでした。"

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method

# Google データ エクスポートの zip から .../Saved Games/save_cloud/Data.bin（セーブの形のもの）と、同じフォルダの .html（メタデータ）を探す
.method private scanZip([B)V
    .locals 12

    # v0 zis, v1 entry, v2 name, v3 count, v4 browserOnly, v5 found, v6 foundFolder, v7 metas(HashMap), v8-v11 作業
    const/4 v3, 0x0

    const/4 v4, 0x1

    const/4 v5, 0x0

    const/4 v6, 0x0

    new-instance v7, Ljava/util/HashMap;

    invoke-direct {v7}, Ljava/util/HashMap;-><init>()V

    :try_start_0
    new-instance v8, Ljava/io/ByteArrayInputStream;

    invoke-direct {v8, p1}, Ljava/io/ByteArrayInputStream;-><init>([B)V

    new-instance v0, Ljava/util/zip/ZipInputStream;

    invoke-direct {v0, v8}, Ljava/util/zip/ZipInputStream;-><init>(Ljava/io/InputStream;)V

    :loop
    invoke-virtual {v0}, Ljava/util/zip/ZipInputStream;->getNextEntry()Ljava/util/zip/ZipEntry;

    move-result-object v1

    if-eqz v1, :end

    invoke-virtual {v1}, Ljava/util/zip/ZipEntry;->isDirectory()Z

    move-result v8

    if-nez v8, :loop

    invoke-virtual {v1}, Ljava/util/zip/ZipEntry;->getName()Ljava/lang/String;

    move-result-object v2

    add-int/lit8 v3, v3, 0x1

    const-string v8, "archive_browser.html"

    invoke-virtual {v2, v8}, Ljava/lang/String;->endsWith(Ljava/lang/String;)Z

    move-result v8

    if-nez v8, :not_browser_check

    const/4 v4, 0x0

    :not_browser_check
    const-string v8, "/Saved Games/save_cloud/"

    invoke-virtual {v2, v8}, Ljava/lang/String;->contains(Ljava/lang/CharSequence;)Z

    move-result v8

    if-eqz v8, :loop

    const-string v8, "/Data.bin"

    invoke-virtual {v2, v8}, Ljava/lang/String;->endsWith(Ljava/lang/String;)Z

    move-result v8

    if-eqz v8, :not_data

    if-nez v5, :loop

    const v8, 0x100000

    invoke-static {v0, v8}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->readAll(Ljava/io/InputStream;I)[B

    move-result-object v8

    invoke-static {v8}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->isSave([B)Z

    move-result v9

    if-eqz v9, :loop

    move-object v5, v8

    invoke-static {v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->folder(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v6

    goto :loop

    :not_data
    const-string v8, ".html"

    invoke-virtual {v2, v8}, Ljava/lang/String;->endsWith(Ljava/lang/String;)Z

    move-result v8

    if-eqz v8, :loop

    const v8, 0x100000

    invoke-static {v0, v8}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->readAll(Ljava/io/InputStream;I)[B

    move-result-object v8

    if-eqz v8, :loop

    new-instance v9, Ljava/lang/String;

    const-string v10, "UTF-8"

    invoke-direct {v9, v8, v10}, Ljava/lang/String;-><init>([BLjava/lang/String;)V

    invoke-static {v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->folder(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v10

    invoke-virtual {v7, v10, v9}, Ljava/util/HashMap;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    goto :loop

    :end
    invoke-virtual {v0}, Ljava/util/zip/ZipInputStream;->close()V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    if-eqz v5, :not_found

    iput-object v5, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->pending:[B

    invoke-virtual {v7, v6}, Ljava/util/HashMap;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v8

    check-cast v8, Ljava/lang/String;

    invoke-static {v8}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->metaDate(Ljava/lang/String;)Ljava/lang/String;

    move-result-object v8

    new-instance v9, Ljava/lang/StringBuilder;

    const-string v10, "Google データ エクスポートのクラウドセーブ"

    invoke-direct {v9, v10}, Ljava/lang/StringBuilder;-><init>(Ljava/lang/String;)V

    if-eqz v8, :no_date

    const-string v10, "（最終更新："

    invoke-virtual {v9, v10}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v9, v8}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    const-string v10, "）"

    invoke-virtual {v9, v10}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    :no_date
    const-string v10, "を取り込みます。"

    invoke-virtual {v9, v10}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v9}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v9

    invoke-direct {p0, v9}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->confirm(Ljava/lang/String;)V

    return-void

    :not_found
    if-lez v3, :zip_notfound

    if-eqz v4, :zip_notfound

    const-string v8, "このファイルにはセーブデータが入っていません。一緒に届いたもう一つのzipファイルを選んでください。"

    const/4 v9, 0x3

    invoke-direct {p0, v8, v9}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->okDialog(Ljava/lang/String;I)V

    return-void

    :zip_notfound
    const-string v8, "このzipファイルにはFFレジェンズのセーブデータが入っていません。"

    invoke-direct {p0, v8}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void

    :catch_0
    move-exception v8

    const-string v8, "ファイルを読み込めませんでした。"

    invoke-direct {p0, v8}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method

# 取り込みの確認。prefix は Takeout のときの1文（なければ null）
.method private confirm(Ljava/lang/String;)V
    .locals 4

    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V

    if-eqz p1, :no_prefix

    invoke-virtual {v0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    goto :rest

    :no_prefix
    const-string v1, "選んだセーブデータを取り込みます。"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    # 1.0-pre18: Takeout のときは前の1文と重なるので「選んだセーブデータを取り込みます。」を付けない
    :rest
    const-string v1, "今のセーブデータは上書きされます。必要な場合は、先に書き出しておいてください。取り込んだあと、ゲームを終了します。"

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    new-instance v0, Landroid/app/AlertDialog$Builder;

    const/4 v2, 0x5

    invoke-direct {v0, p0, v2}, Landroid/app/AlertDialog$Builder;-><init>(Landroid/content/Context;I)V

    const-string v2, "セーブデータの取り込み"

    invoke-virtual {v0, v2}, Landroid/app/AlertDialog$Builder;->setTitle(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;

    invoke-virtual {v0, v1}, Landroid/app/AlertDialog$Builder;->setMessage(Ljava/lang/CharSequence;)Landroid/app/AlertDialog$Builder;

    const/4 v2, 0x0

    invoke-virtual {v0, v2}, Landroid/app/AlertDialog$Builder;->setCancelable(Z)Landroid/app/AlertDialog$Builder;

    new-instance v2, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;

    const/4 v3, 0x1

    invoke-direct {v2, p0, v3}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;-><init>(Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;I)V

    const-string v3, "取り込む"

    invoke-virtual {v0, v3, v2}, Landroid/app/AlertDialog$Builder;->setPositiveButton(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;

    new-instance v2, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;

    const/4 v3, 0x2

    invoke-direct {v2, p0, v3}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;-><init>(Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;I)V

    const-string v3, "やめる"

    invoke-virtual {v0, v3, v2}, Landroid/app/AlertDialog$Builder;->setNegativeButton(Ljava/lang/CharSequence;Landroid/content/DialogInterface$OnClickListener;)Landroid/app/AlertDialog$Builder;

    invoke-virtual {v0}, Landroid/app/AlertDialog$Builder;->show()Landroid/app/AlertDialog;

    return-void
.end method

# 外部の files/save_import.bin に置く（一時ファイルに書いてから名前を変える）
.method private doImport()V
    .locals 5

    :try_start_0
    const/4 v0, 0x0

    invoke-virtual {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->getExternalFilesDir(Ljava/lang/String;)Ljava/io/File;

    move-result-object v0

    if-eqz v0, :fail

    invoke-virtual {v0}, Ljava/io/File;->mkdirs()Z

    new-instance v1, Ljava/io/File;

    const-string v2, "save_import.tmp"

    invoke-direct {v1, v0, v2}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V

    new-instance v2, Ljava/io/FileOutputStream;

    invoke-direct {v2, v1}, Ljava/io/FileOutputStream;-><init>(Ljava/io/File;)V

    iget-object v3, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->pending:[B

    invoke-virtual {v2, v3}, Ljava/io/FileOutputStream;->write([B)V

    invoke-virtual {v2}, Ljava/io/FileOutputStream;->getFD()Ljava/io/FileDescriptor;

    move-result-object v3

    invoke-virtual {v3}, Ljava/io/FileDescriptor;->sync()V

    invoke-virtual {v2}, Ljava/io/FileOutputStream;->close()V

    new-instance v2, Ljava/io/File;

    const-string v3, "save_import.bin"

    invoke-direct {v2, v0, v3}, Ljava/io/File;-><init>(Ljava/io/File;Ljava/lang/String;)V

    invoke-virtual {v2}, Ljava/io/File;->delete()Z

    invoke-virtual {v1, v2}, Ljava/io/File;->renameTo(Ljava/io/File;)Z

    move-result v3

    if-eqz v3, :fail
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    const-string v0, "取り込みの準備ができました。ゲームを終了します。次にゲームを起動したときに反映されます。"

    const/4 v1, 0x4

    invoke-direct {p0, v0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->okDialog(Ljava/lang/String;I)V

    return-void

    :catch_0
    move-exception v0

    :fail
    const-string v0, "取り込みに失敗しました。"

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method

# ダイアログのボタン。1=取り込む 2=やめる 3=目次の zip の OK 4=準備ができた OK
.method onDialog(I)V
    .locals 2

    const/4 v0, 0x1

    if-ne p1, v0, :not1

    invoke-direct {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->doImport()V

    return-void

    :not1
    const/4 v0, 0x2

    if-ne p1, v0, :not2

    const-string v1, "取り込みを取りやめました。"

    invoke-direct {p0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void

    :not2
    const/4 v0, 0x4

    if-ne p1, v0, :not4

    # ゲームが裏で動いていても古いセーブで上書きしないよう、アプリのプロセスごと終える
    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->finish()V

    invoke-static {}, Landroid/os/Process;->myPid()I

    move-result v0

    invoke-static {v0}, Landroid/os/Process;->killProcess(I)V

    return-void

    :not4
    invoke-virtual {p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->finish()V

    return-void
.end method

.method protected onActivityResult(IILandroid/content/Intent;)V
    .locals 3

    invoke-super {p0, p1, p2, p3}, Landroid/app/Activity;->onActivityResult(IILandroid/content/Intent;)V

    const/4 v0, -0x1

    if-ne p2, v0, :cancel

    if-eqz p3, :cancel

    invoke-virtual {p3}, Landroid/content/Intent;->getData()Landroid/net/Uri;

    move-result-object v0

    if-eqz v0, :cancel

    const/4 v1, 0x1

    if-ne p1, v1, :import

    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->exportTo(Landroid/net/Uri;)V

    return-void

    :import
    invoke-direct {p0, v0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->importFrom(Landroid/net/Uri;)V

    return-void

    :cancel
    const/4 v1, 0x1

    if-ne p1, v1, :cancel_import

    const-string v2, "書き出しを取りやめました。"

    goto :cancel_show

    :cancel_import
    const-string v2, "取り込みを取りやめました。"

    :cancel_show
    invoke-direct {p0, v2}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->toastFinish(Ljava/lang/String;)V

    return-void
.end method
