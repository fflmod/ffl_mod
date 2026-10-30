.class final Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;
.super Ljava/lang/Object;
.source "FFLModSaveActivity.java"

# interfaces
.implements Landroid/content/DialogInterface$OnClickListener;

# FFLModSaveActivity のダイアログのボタン。押されたら a.onDialog(kind)


# instance fields
.field private final a:Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;

.field private final kind:I


# direct methods
.method constructor <init>(Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;I)V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    iput-object p1, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;->a:Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;

    iput p2, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;->kind:I

    return-void
.end method


# virtual methods
.method public onClick(Landroid/content/DialogInterface;I)V
    .locals 2

    iget-object v0, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;->a:Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;

    iget v1, p0, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity$Click;->kind:I

    invoke-virtual {v0, v1}, Lcom/square_enix/android_googleplay/ffl_gp/FFLModSaveActivity;->onDialog(I)V

    return-void
.end method
