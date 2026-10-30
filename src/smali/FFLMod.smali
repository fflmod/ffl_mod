.class public Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;
.super Ljava/lang/Object;
.source "FFLMod.java"


# static fields
.field private static ok:Z

.field private static tried:Z


# direct methods
.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method

.method public static native install()V
.end method

.method public static native nativeAxis(FFFF)V
.end method

.method public static native nativeKey(II)I
.end method

.method public static native nativeReset()V
.end method

# MainActivity が作られたとき（libjniproxy の読み込み直後）に FFLModApp から1回だけ呼ぶ。失敗してもゲームは通常どおり動く
.method public static init()V
    .locals 2

    sget-boolean v0, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->tried:Z

    if-nez v0, :goto_0

    const/4 v0, 0x1

    sput-boolean v0, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->tried:Z

    :try_start_0
    const-string v0, "fflmod"

    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V

    invoke-static {}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->install()V

    const/4 v0, 0x1

    sput-boolean v0, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->ok:Z
    :try_end_0
    .catchall {:try_start_0 .. :try_end_0} :catch_0

    :goto_0
    return-void

    :catch_0
    move-exception v1

    const-string v0, "FFLMod"

    invoke-static {v0, v0, v1}, Landroid/util/Log;->e(Ljava/lang/String;Ljava/lang/String;Ljava/lang/Throwable;)I

    goto :goto_0
.end method

.method public static reset()V
    .locals 1

    sget-boolean v0, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->ok:Z

    if-eqz v0, :cond_0

    invoke-static {}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->nativeReset()V

    :cond_0
    return-void
.end method

.method public static onKey(Landroid/view/KeyEvent;)Z
    .locals 4

    sget-boolean v0, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->ok:Z

    if-eqz v0, :not_handled

    invoke-virtual {p0}, Landroid/view/KeyEvent;->getKeyCode()I

    move-result v0

    invoke-virtual {p0}, Landroid/view/KeyEvent;->getAction()I

    move-result v1

    invoke-static {v0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->nativeKey(II)I

    move-result v2

    if-eqz v2, :not_handled

    const/4 v3, 0x2

    if-ne v2, v3, :handled

    # 戻るボタンと同じ扱い（MainActivity.onKeyDown のキーコード4の処理を再現）
    sget-object v0, Lcom/square_enix/android_googleplay/ffl_gp/MainActivity;->m_sActivity:Lcom/square_enix/android_googleplay/ffl_gp/MainActivity;

    if-eqz v0, :handled

    iget-boolean v1, v0, Lcom/square_enix/android_googleplay/ffl_gp/MainActivity;->keyAssign:Z

    if-eqz v1, :handled

    iget v1, v0, Lcom/square_enix/android_googleplay/ffl_gp/MainActivity;->keyEvent:I

    or-int/lit8 v1, v1, 0x2

    iput v1, v0, Lcom/square_enix/android_googleplay/ffl_gp/MainActivity;->keyEvent:I

    :handled
    const/4 v0, 0x1

    return v0

    :not_handled
    const/4 v0, 0x0

    return v0
.end method

.method public static onMotion(Landroid/view/MotionEvent;)Z
    .locals 6

    sget-boolean v0, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->ok:Z

    if-eqz v0, :no

    invoke-virtual {p0}, Landroid/view/MotionEvent;->getSource()I

    move-result v0

    # SOURCE_JOYSTICK
    const v1, 0x1000010

    and-int v2, v0, v1

    if-ne v2, v1, :no

    invoke-virtual {p0}, Landroid/view/MotionEvent;->getActionMasked()I

    move-result v2

    # ACTION_MOVE
    const/4 v3, 0x2

    if-ne v2, v3, :no

    const/4 v1, 0x0

    invoke-virtual {p0, v1}, Landroid/view/MotionEvent;->getAxisValue(I)F

    move-result v2

    const/4 v1, 0x1

    invoke-virtual {p0, v1}, Landroid/view/MotionEvent;->getAxisValue(I)F

    move-result v3

    const/16 v1, 0xf

    invoke-virtual {p0, v1}, Landroid/view/MotionEvent;->getAxisValue(I)F

    move-result v4

    const/16 v1, 0x10

    invoke-virtual {p0, v1}, Landroid/view/MotionEvent;->getAxisValue(I)F

    move-result v5

    invoke-static {v2, v3, v4, v5}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->nativeAxis(FFFF)V

    const/4 v0, 0x1

    return v0

    :no
    const/4 v0, 0x0

    return v0
.end method
