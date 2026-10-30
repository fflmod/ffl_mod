.class public Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;
.super Landroid/app/Application;
.source "FFLModApp.java"

# interfaces
.implements Landroid/app/Application$ActivityLifecycleCallbacks;

# AndroidManifest の <application android:name> で指定する。
# ゲームの classes.dex には手を付けず、MainActivity の出入りを知らせてもらって
#   作られたとき → FFLMod.init()（MainActivity.<clinit> で libjniproxy が読み込み済み）と入力の横取り
#   再開したとき → 入力の横取りが外れていたら付け直す
#   一時停止したとき → FFLMod.reset()（押しっぱなしの解除）
# を行う。


# direct methods
.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Landroid/app/Application;-><init>()V

    return-void
.end method

.method private static isMain(Landroid/app/Activity;)Z
    .locals 1

    instance-of v0, p0, Lcom/square_enix/android_googleplay/ffl_gp/MainActivity;

    return v0
.end method

# 窓の入力の窓口を FFLModInput で包む（すでに包んであれば何もしない）
.method private static wrap(Landroid/app/Activity;)V
    .locals 3

    :try_start_0
    invoke-virtual {p0}, Landroid/app/Activity;->getWindow()Landroid/view/Window;

    move-result-object v0

    if-eqz v0, :done

    invoke-virtual {v0}, Landroid/view/Window;->getCallback()Landroid/view/Window$Callback;

    move-result-object v1

    if-eqz v1, :done

    instance-of v2, v1, Lcom/square_enix/android_googleplay/ffl_gp/FFLModInput;

    if-nez v2, :done

    new-instance v2, Lcom/square_enix/android_googleplay/ffl_gp/FFLModInput;

    invoke-direct {v2, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModInput;-><init>(Landroid/view/Window$Callback;)V

    invoke-virtual {v0, v2}, Landroid/view/Window;->setCallback(Landroid/view/Window$Callback;)V
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    :done
    return-void

    :catch_0
    move-exception v1

    const-string v0, "FFLMod"

    invoke-static {v0, v0, v1}, Landroid/util/Log;->e(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I

    goto :done
.end method


# virtual methods
.method public onCreate()V
    .locals 0

    invoke-super {p0}, Landroid/app/Application;->onCreate()V

    invoke-virtual {p0, p0}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;->registerActivityLifecycleCallbacks(Landroid/app/Application$ActivityLifecycleCallbacks;)V

    return-void
.end method

.method public onActivityCreated(Landroid/app/Activity;Landroid/os/Bundle;)V
    .locals 1

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;->isMain(Landroid/app/Activity;)Z

    move-result v0

    if-eqz v0, :cond_0

    invoke-static {}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->init()V

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;->wrap(Landroid/app/Activity;)V

    :cond_0
    return-void
.end method

.method public onActivityResumed(Landroid/app/Activity;)V
    .locals 1

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;->isMain(Landroid/app/Activity;)Z

    move-result v0

    if-eqz v0, :cond_0

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;->wrap(Landroid/app/Activity;)V

    :cond_0
    return-void
.end method

.method public onActivityPaused(Landroid/app/Activity;)V
    .locals 1

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModApp;->isMain(Landroid/app/Activity;)Z

    move-result v0

    if-eqz v0, :cond_0

    invoke-static {}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->reset()V

    :cond_0
    return-void
.end method

.method public onActivityStarted(Landroid/app/Activity;)V
    .locals 0

    return-void
.end method

.method public onActivityStopped(Landroid/app/Activity;)V
    .locals 0

    return-void
.end method

.method public onActivitySaveInstanceState(Landroid/app/Activity;Landroid/os/Bundle;)V
    .locals 0

    return-void
.end method

.method public onActivityDestroyed(Landroid/app/Activity;)V
    .locals 0

    return-void
.end method
