.class public Lcom/square_enix/android_googleplay/ffl_gp/FFLModInput;
.super Landroidx/appcompat/view/WindowCallbackWrapper;
.source "FFLModInput.java"

# MainActivity の窓の入力の窓口（Window.Callback）を包み、キーとスティックを先に FFLMod へ渡す。
# FFLMod が使わなかった入力は、包む前の窓口（AppCompat → MainActivity）へそのまま渡す。


# direct methods
.method public constructor <init>(Landroid/view/Window$Callback;)V
    .locals 0

    invoke-direct {p0, p1}, Landroidx/appcompat/view/WindowCallbackWrapper;-><init>(Landroid/view/Window$Callback;)V

    return-void
.end method


# virtual methods
.method public dispatchKeyEvent(Landroid/view/KeyEvent;)Z
    .locals 1

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->onKey(Landroid/view/KeyEvent;)Z

    move-result v0

    if-eqz v0, :cond_super

    const/4 v0, 0x1

    return v0

    :cond_super
    invoke-super {p0, p1}, Landroidx/appcompat/view/WindowCallbackWrapper;->dispatchKeyEvent(Landroid/view/KeyEvent;)Z

    move-result v0

    return v0
.end method

.method public dispatchGenericMotionEvent(Landroid/view/MotionEvent;)Z
    .locals 1

    invoke-static {p1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLMod;->onMotion(Landroid/view/MotionEvent;)Z

    move-result v0

    if-eqz v0, :cond_super

    const/4 v0, 0x1

    return v0

    :cond_super
    invoke-super {p0, p1}, Landroidx/appcompat/view/WindowCallbackWrapper;->dispatchGenericMotionEvent(Landroid/view/MotionEvent;)Z

    move-result v0

    return v0
.end method
