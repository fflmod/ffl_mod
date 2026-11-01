/*
 * fflmod: FFレジェンズ 光と闇の戦士（Android 版 1.2.0）向けの改造。
 * もとはコントローラー入力のブリッジ（padhook v2.1）として始まった。
 *
 * 1) キー入力の流し込み
 *    フィールド・メニューの多くは携帯電話版由来のキー処理を持ち、GameClass::KeyState /
 *    KeyTrigger / KeyRepeat でキー状態を問い合わせている。これらを GOT（関数の行き先表）で
 *    差し替え、元の結果にコントローラー入力を OR して返す。
 *
 * 2) 汎用カーソル（v2.0〜）
 *    キー処理を持たない画面（戦闘・ダイアログ・コンフィグなど）向け。
 *    MenuWidget::Update を差し替えて、そのフレームに有効なボタン（MenuItem）の位置を集める。
 *    方向キーで一番近いボタンへカーソルを移し、決定でそのボタンの中心にタッチを送る。
 *    カーソル枠は MenuWidget::Draw の直後に描く。
 *    ゲームが方向キーを問い合わせている画面（＝キー処理が生きている画面）では自動で引っ込む。
 *
 * ゲームのコード自体は書き換えない。
 */

typedef unsigned long size_t;
typedef unsigned long uintptr_t;
typedef unsigned char u8;

/* bionic */
extern void *dlopen(const char *name, int flags);
extern void *dlsym(void *handle, const char *sym);
extern int mprotect(void *addr, size_t len, int prot);
extern int __android_log_print(int prio, const char *tag, const char *fmt, ...);
extern int open(const char *path, int flags, ...);
extern long read(int fd, void *buf, size_t n);
extern long write(int fd, const void *buf, size_t n);
extern int close(int fd);
extern int mkdir(const char *path, unsigned mode);
extern int rename(const char *from, const char *to);

extern int vsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
/* v3.40t8: ログは logcat に加えて files/fflmod_log.txt にも書く（起動ごとに作り直す。logcat はほかのアプリで流れるため） */
static void plog(int prio, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#define LOGI(...) plog(4, __VA_ARGS__)
#define LOGE(...) plog(6, __VA_ARGS__)

#define RTLD_NOW 2
#define PROT_READ 1
#define PROT_WRITE 2

/* 版。公開に向けて 1.0 の系統にそろえた（1.0-pre1・1.0-pre2 は開発時の v4.07 と同じ中身。1.0-pre3 から本当のワイド表示） */
#define FFLMOD_VERSION "1.1.8-pre1"
static int g_curMap = -1;
static void slow_magic_apply(void);     /* v3.32t: LoadMap で読み込み中のマップ番号 */

/* libjniproxy.so (ffl_gp 1.2.0, arm64-v8a) 内の位置。基準は KeyState のシンボル。
   v4.07: 1.1.7 の位置から、区間ごとのずれ（コード +0x10/+0x28/+0x68、.data.rel.ro・.got +0x3070、.data +0x6070）で直した。
   コメントの中の位置も 1.2.0 のもの */
#define SYM_KEYSTATE      "_ZN9GameClass8KeyStateEi"
#define OFF_KEYSTATE_FUNC 0x3ec7e8UL
#define OFF_KEYSTATE_GOT  0x45f328UL
#define OFF_TRIGGER_FUNC  0x3dd0b8UL  /* GameClass::KeyTrigger(int)        */
#define OFF_TRIGGER_GOT   0x45e288UL
#define OFF_REPEAT_FUNC   0x3ec800UL  /* GameClass::KeyRepeat(int)         */
#define OFF_REPEAT_GOT    0x45e370UL
#define OFF_APPRUN_FUNC   0x3adc20UL  /* App::run(MtxGraphics3D&)          */
#define OFF_APPRUN_GOT    0x45ee00UL
#define OFF_WUPDATE_FUNC  0x4134a4UL  /* MenuWidget::Update()              */
#define OFF_WUPDATE_GOT   0x45e308UL
#define OFF_WDRAW_FUNC    0x4138e4UL  /* MenuWidget::Draw(MtxGraphics3D&)  */
#define OFF_WDRAW_GOT     0x45e408UL
#define OFF_FOGEN_FUNC    0x3713f8UL  /* MtxGraphics3D::SetFogColor(float x4)  */
#define OFF_FOGEN_GOT     0x45d928UL
#define OFF_DRAWPRIM_FUNC 0x372230UL  /* MtxGraphics3D::DrawPrimitive(MtxPrimitive&) */
#define OFF_DRAWPRIM_GOT  0x45d978UL
#define OFF_VTX_FUNC      0x377890UL  /* MtxPrimitive::Vertex3f(float x3)            */
#define OFF_VTX_GOT       0x45dc48UL
#define OFF_FOGON_FUNC    0x371334UL  /* MtxGraphics3D::SetFogEnable(bool)           */
#define OFF_FOGON_GOT     0x45d910UL
#define OFF_DRAWFAR_FUNC  0x3c97dcUL  /* FieldClass::DrawFar(MtxGraphics3D*)         */
#define OFF_DRAWFAR_GOT   0x45f7a8UL
#define OFF_MOVEW_FUNC    0x3fa9d4UL  /* MenuClass::MoveWidget()                */
#define OFF_MOVEW_GOT     0x45fec8UL
#define OFF_DRAWW_FUNC    0x405ed8UL  /* MenuClass::DrawWidget(MtxGraphics3D*)  */
#define OFF_DRAWW_GOT     0x460288UL
/* MenuClass のボタン表: +0x8a4 から 0x18 バイト × 32。{id, x, y, w, h, flags}。id==0 で終端 */
#define MC_TABLE    0x8a4
#define MC_STATE    0xbd0
#define MC_MODE     0x10    /* MenuClass: 画面の種類（MenuFunc の分岐。4=魔法・技 7=隊列） */
#define MENU_MAGIC  4
#define G_MENUCMD_N 0x34430 /* GameClass: 魔法・技の画面の右の列（CreateMemberMenuCommand）の数 */
#define OFF_GETTOUCHINFO  0x37ff24UL  /* GetTouchInfo()                    */
#define OFF_TP_SETDATA    0x37f7bcUL  /* MtxTouchPanel::SetData(int,float,float,int) */
#define OFF_FILLRECT      0x372c4cUL  /* MtxGraphics3D::FillRect(int,int,int,int)    */

/* MenuWidget / MenuItem / MtxTouchPanel / MtxGraphics3D の中身（解析で判明した位置） */
#define W_HEAD      0x20   /* MenuWidget: 項目リストの先頭ノード。ノードは +0 次, +0x10 項目 */
#define I_ACTIVE    0x0c   /* MenuItem: 更新対象 */
#define I_VISIBLE   0x0d   /* MenuItem: 描画対象 */
#define I_TOUCHABLE 0x14   /* MenuItem: タッチ受付 */
#define I_POS       0x28   /* MenuItem: x, y (float) */
#define I_SIZE      0x30   /* MenuItem: w, h (float) */
#define TP_INSCALE  0x66c  /* MtxTouchPanel: 入力座標の倍率 (x, y) */
#define TP_SCALE    0x674  /* MtxTouchPanel: GetPosition の倍率 (x, y) */
#define TP_OFFSET   0x67c  /* MtxTouchPanel: GetPosition のずれ (x, y) */
#define G_COLOR     0x4ac  /* MtxGraphics3D: 描画色 0xRRGGBBAA */

/* ゲーム内部のキー状態ビット */
#define K_LEFT   0x00010000
#define K_UP     0x00020000
#define K_RIGHT  0x00040000
#define K_DOWN   0x00080000
#define K_DIRS   (K_LEFT | K_UP | K_RIGHT | K_DOWN)
#define K_A      0x00100000  /* 決定（0x100020 で判定される） */
#define K_MENU   0x00200000  /* 携帯版のソフトキー相当。表示切替など */
#define K_BACK   0x00400000  /* 戻る（Android の戻るボタンと同じビット） */
#define K_PREV   0x00000082  /* 携帯版の1/7キー相当。キャラ切替（前） */
#define K_NEXT   0x00000208  /* 携帯版の3/9キー相当。キャラ切替（次） */
#define K_XMENU  0x01000000  /* 改造専用：フィールドでメニューを開く（ゲームは見ない） */
#define K_BAUTO  0x02000000  /* 改造専用：戦闘の AUTO（マイナス／Z） */
#define K_BPAUSE 0x04000000  /* 改造専用：戦闘の PAUSE（プラス／P） */

/* Android の KeyEvent キーコード */
#define KC_DPAD_UP 19
#define KC_DPAD_DOWN 20
#define KC_DPAD_LEFT 21
#define KC_DPAD_RIGHT 22
#define KC_DPAD_CENTER 23
#define KC_E 33
#define KC_TAB 61
#define KC_LEFT_BRACKET 71
#define KC_RIGHT_BRACKET 72
#define KC_BACKSLASH 73
#define KC_Q 45
#define KC_SPACE 62
#define KC_ENTER 66
#define KC_DEL 67
#define KC_PAGE_UP 92
#define KC_PAGE_DOWN 93
#define KC_BUTTON_A 96
#define KC_BUTTON_B 97
#define KC_BUTTON_X 99
#define KC_BUTTON_Y 100
#define KC_BUTTON_L1 102
#define KC_BUTTON_R1 103
#define KC_BUTTON_L2 104
#define KC_BUTTON_R2 105
#define KC_BUTTON_THUMBL 106
#define KC_BUTTON_THUMBR 107
#define KC_BUTTON_START 108
#define KC_BUTTON_SELECT 109
#define KC_ESCAPE 111
#define KC_C 31
#define KC_M 41
#define KC_V 50
#define KC_X 52
#define KC_Z 54
#define KC_P 44

/* キーリピート（ゲームは30fps） */
#define REPEAT_DELAY 12
#define REPEAT_INTERVAL 3

/* ---- UIスレッドが書くもの ---- */
static volatile int g_keyBits;
static volatile int g_axisBits;
static volatile int g_latch;

static int g_installed;
static uintptr_t g_base;

/* 上下しか見ていない画面（左右を前後に読み替える）。libjniproxy 内の関数範囲 */
static const struct { uintptr_t start, size; } k_listOnly[] = {
    { 0x3fc4e8, 1908 },  /* MenuClass::MenuSelectFunc        */
    { 0x405744,  484 },  /* MenuClass::HelpFuncColumn        */
    { 0x406c50, 1224 },  /* MenuClass::ShopFuncItemBuySelect */
};

/* ---- 以下はゲーム（GL）スレッドだけが触る ---- */
static int g_prev, g_cur, g_trig, g_rep;
static int g_hold[32];
static unsigned g_frame;
static int g_logFd = -1;
static volatile int g_logLock;
static long g_logBytes;
static void plog(int prio, const char *fmt, ...)
{
    char buf[600];
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int n = vsnprintf(buf + 16, sizeof(buf) - 18, fmt, ap);
    __builtin_va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(buf) - 19) n = (int)sizeof(buf) - 19;
    __android_log_print(prio, "FFLMod", "%s", buf + 16);
    if (g_logFd < 0 || g_logBytes > (4L << 20)) return;
    /* 先頭に「フレーム番号 I/E 」を付ける */
    char h[16]; int k = 0; unsigned f = g_frame; char t[12]; int m = 0;
    do { t[m++] = (char)('0' + f % 10); f /= 10; } while (f && m < 10);
    while (m) h[k++] = t[--m];
    h[k++] = ' '; h[k++] = prio >= 6 ? 'E' : 'I'; h[k++] = ' ';
    char *p = buf + 16 - k;
    for (int i = 0; i < k; i++) p[i] = h[i];
    buf[16 + n] = '\n';
    int len = k + n + 1;
    while (__atomic_exchange_n(&g_logLock, 1, __ATOMIC_ACQUIRE)) { }
    if (write(g_logFd, p, (size_t)len) == len) g_logBytes += len;
    __atomic_store_n(&g_logLock, 0, __ATOMIC_RELEASE);
}
static void *g_btl;          /* v3.09: 戦闘データ（BattleScaduleWin で覚え、EndBattle で忘れる） */
static int g_winLog;
#define BTL_SCHEDULE 0x6dfc   /* BattleClass: 進行の種類（3=勝利 4=敗北 5=逃走 6=終了 7=テレポ） */

/* 汎用カーソル */
typedef struct {
    void *item;      /* ボタン（MenuItem） */
    void *widget;    /* 所属する MenuWidget */
    int sub;         /* リスト内の何番目か（リストでなければ -1） */
    int list;        /* 種類: 0=普通のボタン 1=マス目リストのマス 2=MenuClassのボタン 3=部品リストの中身 */
    float x, y, w, h;  /* 画面上の位置（収集時点） */
    float rx, ry;      /* ボタン左上からの相対位置（描画用） */
    uintptr_t vt;      /* 部品の種類（ログ用） */
    int id;            /* MenuClass のボタン番号（それ以外は0） */
    unsigned sig;      /* マス目リストの中身の指紋（収集時に計算） */
} Cand;
#define MAXC 160
#define MAXW 24
static Cand g_cand[2][MAXC];
static int g_ncand[2];
static int g_cb;                 /* 今フレーム集めている側 */
static void *g_wseen[2][MAXW];
static int g_nw[2];
static int g_sinceDir = 100;     /* ゲームが方向キーを最後に問い合わせてからのフレーム数 */
static int g_sinceA = 100;       /* ゲームが決定キーを最後に問い合わせてからのフレーム数 */
static int g_force[2];           /* キー処理があっても汎用カーソルを優先する画面か */
static int g_memberSel;          /* メインメニューから入ったメンバー選択 */
static unsigned g_cmdSig;        /* 戦闘コマンド欄の指紋（誰の番かが変わったか） */
static unsigned g_curFrame;      /* カーソルが最後に有効だったフレーム */
static uintptr_t vt_slidepad;
static int g_mcState = -1, g_mcCount;    /* JobsMenuSlidePad（全画面のスワイプ受け。候補にしない） */
static int g_cursorMode;
/* フィールドの台本ボタン（地図・乗る・降りる）を A / Y で押す（v3.01） */
#define FB_BASE   0xfbd8          /* FieldClass: 台本ボタンの入れ物。先頭が部品、+0x58 から8個のボタン、+0x98 から8個の番号 */
#define BTN_RIDE  0               /* 乗る／降りる */
#define BTN_MAP   1               /* 地図 */
static void *g_field;            /* FieldClass（MoveCharaControl で覚える） */
static float g_wideE, g_wideS;   /* 1.0-pre3: ワイド表示の左右の余白（論理座標）と倍率。g_wideE が 0 ならワイドにしない */
static int wide_shift_begin(void *g);
static void wide_shift_end(void *g);
/* 1.0-pre4: フィールドの右上のボタン（台本ボタンの入れ物）。ワイド表示では右の余白の分だけ右へずらす */
static int is_field_hud(void *w);
static void hud_shift(void *w, float dx);
static int g_btnPress = -1;      /* 押した扱いにするボタン番号（-1=なし） */
static int g_btnWait;
static int g_blockA, g_blockMenu;
#define BTN_YES    322            /* MenuClass のボタン：はい */
#define BTN_BACK   390            /* 戻る */
#define BTN_DELETE 401            /* 削除 */
static void *game_inst(void);
static int script_button_ok(int id);
static int dash_button_pressed(void *w);
static Cand g_curC;              /* 今カーソルがある場所（item==0 なら未選択） */
static float g_tapX, g_tapY;
static int g_tapWait;
/* 部品ごとに最後に見たフレーム（交互に更新される部品で「新しく現れた」と誤判定しないため） */
#define WHIST 48
static struct { void *w; unsigned last; } g_whist[WHIST];
static int g_hadPrev;
static uintptr_t vt_list, vt_itemlist;  /* JobsMenuListControl / JobsMenuItemListControl の vtable */

typedef int (*KeyFn)(void *self, int mask);
typedef long (*AppRunFn)(void *self, void *g);
typedef void (*WUpdateFn)(void *w);
typedef void (*WDrawFn)(void *w, void *g);
typedef void *(*GetTouchInfoFn)(void);
typedef void (*SetDataFn)(void *tp, int action, float x, float y, int id);
typedef void (*FillRectFn)(void *g, int x, int y, int w, int h);

static KeyFn g_origState, g_origTrigger, g_origRepeat;
static AppRunFn g_origAppRun;
static WUpdateFn g_origWUpdate;
static WDrawFn g_origWDraw;
static WUpdateFn g_origMoveW;
typedef void (*FogEnFn)(void *g, float r, float gg, float b, float a);
static FogEnFn g_origFogEn;
/* ---- 描画の不具合対策（BlueStacks の Android 11/13 向け） ----
   本来フォグは色だけに効くが、BlueStacks ではアルファにも効いてしまう。
   ・元のアルファのまま → 城などの透明部分が四角く浮く
   ・アルファ0 → 遠くの地面まで透けて、奥の雲が見える
   そこで地形（DrawMapChips）を描く間だけアルファ1、それ以外はアルファ0にする。
   （v2.15t の検証で、この方式と継ぎ目埋めの組み合わせで線・四角・雲がすべて消えることを確認） */
static const int k_gfxFix = 0;    /* 0=フォグと継ぎ目の対策を入れない（v3.05 で差し戻し） */
static const int g_fogMode = 2;
static const int g_seamFix = 1;
static float g_fogR, g_fogG, g_fogB, g_fogA;
static int g_fogKnown;
static void hook_FogEn(void *g, float r, float gg, float b, float a)
{
    g_fogR = r; g_fogG = gg; g_fogB = b; g_fogA = a; g_fogKnown = 1;
    g_origFogEn(g, r, gg, b, g_fogMode == 0 ? a : 0.0f);
}

/* DrawMapChips 内の DrawPrimitive 呼び出しの戻り先（1枚目＝テクスチャ108、2枚目＝109） */
#define RA_CHIPS_PRIM1 0x3bf8e0UL
#define RA_CHIPS_PRIM2 0x3bf8f4UL
typedef void (*DrawPrimFn)(void *g, void *prim);
static DrawPrimFn g_origDrawPrim;
static void hook_DrawPrim(void *g, void *prim)
{
    uintptr_t ra;
    __asm__ volatile("mov %0, x30" : "=r"(ra));
    uintptr_t off = ra - g_base;
    int terrain = (g_fogMode == 2 && off == RA_CHIPS_PRIM1) ||
                  (g_fogMode == 3 && (off == RA_CHIPS_PRIM1 || off == RA_CHIPS_PRIM2));
    if (!terrain || !g_fogKnown) { g_origDrawPrim(g, prim); return; }
    g_origFogEn(g, g_fogR, g_fogG, g_fogB, 1.0f);
    g_origDrawPrim(g, prim);
    g_origFogEn(g, g_fogR, g_fogG, g_fogB, 0.0f);
}

/* 地形タイルの頂点を外側へ少し広げ、行と行の間にすき間（線）ができないようにする。
   DrawMapChips は1枚の四角を6回の Vertex3f で積む。呼び出し位置で角が分かる */
#define SEAM_EPS 0.5f
typedef void (*Vtx3fFn)(void *prim, float x, float y, float z);
static Vtx3fFn g_origVtx;
static void hook_Vtx(void *prim, float x, float y, float z)
{
    uintptr_t ra;
    __asm__ volatile("mov %0, x30" : "=r"(ra));
    if (g_seamFix) {
        switch (ra - g_base) {
        case 0x3bf73c: x -= SEAM_EPS; y -= SEAM_EPS; break;  /* 左・手前 */
        case 0x3bf760: x += SEAM_EPS; y -= SEAM_EPS; break;  /* 右・手前 */
        case 0x3bf784: x -= SEAM_EPS; y += SEAM_EPS; break;  /* 左・奥 */
        case 0x3bf7a8: x += SEAM_EPS; y -= SEAM_EPS; break;  /* 右・手前 */
        case 0x3bf7cc: x += SEAM_EPS; y += SEAM_EPS; break;  /* 右・奥 */
        case 0x3bf7f0: x -= SEAM_EPS; y += SEAM_EPS; break;  /* 左・奥 */
        }
    }
    g_origVtx(prim, x, y, z);
}
static WDrawFn g_origDrawW;
static GetTouchInfoFn p_GetTouchInfo;
static SetDataFn p_SetData;
static FillRectFn p_FillRect;

#define RD(T, p, off) (*(volatile T *)((uintptr_t)(p) + (off)))
#define RDP(p, off) ((void *)RD(uintptr_t, p, off))

static int remap_for_caller(int bits, uintptr_t ra)
{
    uintptr_t off = ra - g_base;
    for (unsigned i = 0; i < sizeof(k_listOnly) / sizeof(k_listOnly[0]); i++) {
        if (off - k_listOnly[i].start < k_listOnly[i].size) {
            if (bits & K_LEFT)  bits |= K_UP;
            if (bits & K_RIGHT) bits |= K_DOWN;
            break;
        }
    }
    return bits;
}

/* カーソル中はゲーム側に方向と決定を見せない（二重操作を防ぐ） */
static int visible_bits(int bits)
{
    return g_cursorMode ? (bits & ~(K_DIRS | K_A)) : bits;
}

/* このフレームに方向キーを問い合わせた呼び出し元（libjniproxy 内の位置） */
static uintptr_t g_dirCallers[2][4];
static int g_nDirCallers[2];
static int g_dcb;

static void note_poll(int mask, uintptr_t ra)
{
    if (mask & K_DIRS) {
        g_sinceDir = 0;
        uintptr_t off = ra - g_base;
        int n = g_nDirCallers[g_dcb];
        int dup = 0;
        for (int i = 0; i < n; i++) if (g_dirCallers[g_dcb][i] == off) { dup = 1; break; }
        if (!dup && n < 4) g_dirCallers[g_dcb][g_nDirCallers[g_dcb]++] = off;
    }
    if (mask & K_A)
        g_sinceA = 0;
}

/* ---------- 汎用カーソル ---------- */

static int ok_size(float w, float h) { return w >= 4.0f && h >= 4.0f && w < 4000.0f && h < 4000.0f; }

static void add_cand(void *w, void *it, int sub, int list, float ix, float iy, float lx, float ly, float lw, float lh,
                     float clipW, float clipH)
{
    int n = g_ncand[g_cb];
    if (n >= MAXC || !ok_size(lw, lh)) return;
    float cx = lx + lw * 0.5f, cy = ly + lh * 0.5f;
    if (clipW > 0 && (cx < 0 || cy < 0 || cx > clipW || cy > clipH)) return;  /* スクロールで見えていない */
    Cand *c = &g_cand[g_cb][n];
    for (int k = 0; k < n; k++)
        if (g_cand[g_cb][k].item == it && g_cand[g_cb][k].sub == sub) return;
    c->item = it; c->widget = w; c->sub = sub; c->list = list; c->id = 0; c->sig = 0;
    c->vt = list == 2 ? 0 : (uintptr_t)RDP(it, 0);
    c->rx = lx; c->ry = ly; c->w = lw; c->h = lh;
    c->x = ix + lx; c->y = iy + ly;
    g_ncand[g_cb] = n + 1;
}

static void collect_item(void *w, void *it)
{
    float ix = RD(float, it, I_POS), iy = RD(float, it, I_POS + 4);
    float iw = RD(float, it, I_SIZE), ih = RD(float, it, I_SIZE + 4);
    if (!ok_size(iw, ih)) return;
    uintptr_t vp = (uintptr_t)RDP(it, 0);
    if (vt_slidepad && vp == vt_slidepad + 0x10) return;   /* 全画面のスワイプ受けは押しても意味がない */
    if (iw >= 600.0f && ih >= 360.0f) return;             /* 画面全体を覆う部品も同様 */
    if (vt_list && vp == vt_list + 0x10) {
        /* マス目状のリスト（戦闘コマンドなど）：1マスずつ候補にする */
        int cnt = RD(int, it, 0xc0), rows = RD(int, it, 0xa4), cols = RD(int, it, 0xa8);
        float cw = RD(float, it, 0xd0), ch = RD(float, it, 0xd4);
        float sx = RD(float, it, 0x50), sy = RD(float, it, 0x54);
        if (cols > 0 && rows > 0 && cw > 1.0f && ch > 1.0f && cnt > 0) {
            int m = cnt < rows * cols ? cnt : rows * cols;
            if (m > 64) m = 64;
            unsigned sig = (unsigned)(uintptr_t)it ^ (unsigned)cnt;
            uintptr_t ent = (uintptr_t)RDP(it, 0xc8);
            if (ent)
                for (int i = 0; i < cnt && i < 8; i++)
                    sig = sig * 31u + (unsigned)(uintptr_t)RDP(ent, i * 24 + 8);
            if (sig == 0) sig = 1;
            int before = g_ncand[g_cb];
            for (int i = 0; i < m; i++)
                add_cand(w, it, i, 1, ix, iy, (i % cols) * cw + sx, (i / cols) * ch + sy, cw, ch, iw, ih);
            for (int k = before; k < g_ncand[g_cb]; k++) g_cand[g_cb][k].sig = sig;
            return;
        }
    } else if (vt_itemlist && vp == vt_itemlist + 0x10) {
        /* 部品を並べたリスト：中の部品を1つずつ候補にする */
        int cnt = RD(int, it, 0xac);
        void *arr = RDP(it, 0xb0);
        float sx = RD(float, it, 0x50), sy = RD(float, it, 0x54);
        if (arr && cnt > 0) {
            if (cnt > 64) cnt = 64;
            int added = 0;
            for (int k = 0; k < cnt; k++) {
                void *s = RDP(arr, k * 8);
                if (!s || !RD(u8, s, I_ACTIVE)) continue;
                add_cand(w, it, k, 3, ix, iy, RD(float, s, I_POS) + sx, RD(float, s, I_POS + 4) + sy,
                         RD(float, s, I_SIZE), RD(float, s, I_SIZE + 4), iw, ih);
                added++;
            }
            if (added) return;
        }
    }
    add_cand(w, it, -1, 0, ix, iy, 0, 0, iw, ih, 0, 0);
}

static void collect_widget(void *w)
{
    int nw = g_nw[g_cb];
    for (int i = 0; i < nw; i++)
        if (g_wseen[g_cb][i] == w)
            return;
    if (nw < MAXW)
        g_wseen[g_cb][g_nw[g_cb]++] = w;
    void *node = RDP(w, W_HEAD);
    for (int guard = 0; node && guard < 256; guard++) {
        void *it = RDP(node, 0x10);
        if (it && RD(u8, it, I_ACTIVE) && RD(u8, it, I_VISIBLE) && RD(u8, it, I_TOUCHABLE))
            collect_item(w, it);
        node = RDP(node, 0);
    }
}

static void wupdate_inner(void *w);
static void hook_WUpdate(void *w)
{
    /* 1.0-pre6: フィールドのボタンはワイド表示で右の余白の分だけ右へ置く（更新・カーソルの候補集めの間だけ位置をずらす） */
    int sh = g_wideE > 0.0f && is_field_hud(w);
    if (sh) hud_shift(w, g_wideE);
    wupdate_inner(w);
    if (sh) hud_shift(w, -g_wideE);
}

static void wupdate_inner(void *w)
{
    g_origWUpdate(w);
    dash_button_pressed(w);   /* v4.01: ダッシュボタンは台本へ渡さない */
    collect_widget(w);  /* 更新直後の、確実に有効な状態で読む */
    /* 台本ボタンを押した扱いにする（GetPushScriptButtonID が見る +0x40 / +0x44） */
    if (g_btnPress >= 0 && g_field && w == RDP(g_field, FB_BASE)) {
        *(volatile int *)((uintptr_t)w + 0x40) = 1;
        *(volatile int *)((uintptr_t)w + 0x44) = 0x10000 | g_btnPress;
        g_btnPress = -1;
    }
}

static float fabsf_(float v) { return v < 0 ? -v : v; }

static const Cand *find_cand(const Cand *c, int n, void *item, int sub)
{
    if (!item) return 0;
    for (int i = 0; i < n; i++) if (c[i].item == item && c[i].sub == sub) return &c[i];
    return 0;
}

/* 方向に一番近い項目 */
static const Cand *nav(const Cand *c, int n, const Cand *from, int dir)
{
    float fx = from->x + from->w * 0.5f, fy = from->y + from->h * 0.5f;
    const Cand *best = 0;
    float bestScore = 1e30f;
    for (int i = 0; i < n; i++) {
        if (&c[i] == from) continue;
        float cx = c[i].x + c[i].w * 0.5f, cy = c[i].y + c[i].h * 0.5f;
        float dx = cx - fx, dy = cy - fy, primary, secondary;
        switch (dir) {
        case K_LEFT:  primary = -dx; secondary = dy; break;
        case K_RIGHT: primary =  dx; secondary = dy; break;
        case K_UP:    primary = -dy; secondary = dx; break;
        default:      primary =  dy; secondary = dx; break;
        }
        if (primary < 2.0f) continue;
        float s = primary + 2.5f * fabsf_(secondary);
        /* v3.17: 押した方向と直角の向きで重なる候補（同じ行・同じ列）を優先する */
        int ov = (dir == K_LEFT || dir == K_RIGHT)
            ? (c[i].y < from->y + from->h - 2.0f && c[i].y + c[i].h > from->y + 2.0f)
            : (c[i].x < from->x + from->w - 2.0f && c[i].x + c[i].w > from->x + 2.0f);
        if (ov) s -= 1e6f;
        if (s < bestScore) { bestScore = s; best = &c[i]; }
    }
    if (best)
        return best;
    /* その方向に何もなければ反対の端へ回り込む（同じ列・行を優先） */
    bestScore = 1e30f;
    for (int i = 0; i < n; i++) {
        if (&c[i] == from) continue;
        float cx = c[i].x + c[i].w * 0.5f, cy = c[i].y + c[i].h * 0.5f;
        float along, secondary;
        switch (dir) {
        case K_LEFT:  along = -cx; secondary = cy - fy; break;   /* 左へ回り込む＝一番右へ */
        case K_RIGHT: along =  cx; secondary = cy - fy; break;
        case K_UP:    along = -cy; secondary = cx - fx; break;   /* 上へ回り込む＝一番下へ */
        default:      along =  cy; secondary = cx - fx; break;   /* 下へ回り込む＝一番上へ */
        }
        float s = along + 2.5f * fabsf_(secondary);
        if (s < bestScore) { bestScore = s; best = &c[i]; }
    }
    return best;
}

/* マス目リストのうち、マスの数が一番多いものの先頭（戦闘コマンドなど） */
static const Cand *biggest_list_first(const Cand *c, int n, void *widget, int *outCnt)
{
    const Cand *first = 0;
    int bestCnt = 0;
    for (int i = 0; i < n; i++) {
        if (widget && c[i].widget != widget) continue;
        if (c[i].list != 1 || c[i].sub < 0) continue;
        /* 1.0-pre23: 一覧の見えているいちばん上の行（ゲームが記憶の行に合わせてスクロールして開くと、1行目は見えない） */
        int cnt = 0, top = 1;
        for (int k = 0; k < n; k++) {
            if (c[k].item != c[i].item) continue;
            cnt++;
            if (c[k].list == 1 && c[k].sub >= 0 && c[k].sub < c[i].sub) top = 0;
        }
        if (top && cnt > bestCnt) { bestCnt = cnt; first = &c[i]; }
    }
    if (outCnt) *outCnt = bestCnt;
    return first;
}

/* 1.0-pre13: ゲームが一覧の最初の選択を決めたとき（JobsMenuListControl::SetFirstSelectID）。
   コンフィグの「バトルカーソル位置：記憶」で、戦闘コマンドや魔法の一覧を前回の場所から始めるのに使われる。
   汎用カーソルも、その一覧が出たらそこから始める */
#define OFF_FIRSTSEL_FUNC 0x3f5234UL
#define OFF_FIRSTSEL_GOT  0x45e540UL
typedef void (*FirstSelFn)(void *list, int idx);
static FirstSelFn g_origFirstSel;
static void *g_fsList;
static int g_fsIdx;
static unsigned g_fsFrame;
static void hook_FirstSel(void *list, int idx)
{
    g_origFirstSel(list, idx);
    g_fsList = list; g_fsIdx = idx; g_fsFrame = g_frame;
}

/* 直前（2秒以内）にゲームが選んだ行が候補にあれば、それを返す（1回だけ使う） */
static const Cand *first_sel(const Cand *c, int n, const Cand *cur)
{
    if (!g_fsList) return cur;
    if (g_frame - g_fsFrame > 60) { g_fsList = 0; return cur; }
    for (int k = 0; k < n; k++)
        if (c[k].item == g_fsList && c[k].sub == g_fsIdx) { g_fsList = 0; return &c[k]; }
    return cur;
}

/* 初期位置：リストの先頭 → なければ左上 */
static const Cand *default_in(const Cand *c, int n, void *widget)
{
    const Cand *best = 0;
    {
        /* マス目リストがあれば、マスの数が一番多いもの（戦闘コマンド）の先頭へ。
           右下の ▼（AUTO／PAUSE／ESCAPE）も同じ種類のリストなので、数で見分ける */
        const Cand *first = 0;
        int bestCnt = 0;
        for (int i = 0; i < n; i++) {
            if (widget && c[i].widget != widget) continue;
            if (c[i].list != 1 || c[i].sub != 0) continue;
            int cnt = 0;
            for (int k = 0; k < n; k++) if (c[k].item == c[i].item) cnt++;
            if (cnt > bestCnt) { bestCnt = cnt; first = &c[i]; }
        }
        if (first) return first;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            if (widget && c[i].widget != widget) continue;
            if (pass == 0 && c[i].list != 2) continue;          /* まずは MenuClass のボタンから */
            if (!best || c[i].y < best->y - 4.0f || (fabsf_(c[i].y - best->y) <= 4.0f && c[i].x < best->x))
                best = &c[i];
        }
        if (best) break;
    }
    return best;
}

static void send_touch(int action, float gx, float gy)
{
    void *tp = p_GetTouchInfo();
    if (!tp) return;
    float isx = RD(float, tp, TP_INSCALE), isy = RD(float, tp, TP_INSCALE + 4);
    float sx = RD(float, tp, TP_SCALE),    sy = RD(float, tp, TP_SCALE + 4);
    float ox = RD(float, tp, TP_OFFSET),   oy = RD(float, tp, TP_OFFSET + 4);
    if (isx == 0.0f) isx = 1.0f;
    if (isy == 0.0f) isy = 1.0f;
    if (sx == 0.0f) sx = 1.0f;
    if (sy == 0.0f) sy = 1.0f;
    float rx = gx / sx + ox, ry = gy / sy + oy;   /* GetPosition の逆変換 */
    p_SetData(tp, action, rx / isx, ry / isy, 0);  /* SetData の倍率の逆変換 */
}

/* MenuClass のスクロール欄を1行ぶん送る。送れたら1（もう端なら0）。
   0xbe4=スクロール量 0xbe8=行数 0xbf4=1行の高さ 0xbf8=欄の高さ 0xc08=慣性 */
static int scroll_mc_list(void *mc, int dir)
{
    int e4 = RD(int, mc, 0xbe4), cnt = RD(int, mc, 0xbe8), f4 = RD(int, mc, 0xbf4), f8 = RD(int, mc, 0xbf8);
    if (f4 <= 0) return 0;
    int max = f4 * cnt - f8;
    if (max < 0) max = 0;
    int nv;
    if (dir > 0) {
        if (e4 >= max) return 0;
        nv = (e4 / f4 + 1) * f4;
        if (nv > max) nv = max;
    } else {
        if (e4 <= 0) return 0;
        nv = ((e4 + f4 - 1) / f4 - 1) * f4;
        if (nv < 0) nv = 0;
    }
    *(volatile int *)((uintptr_t)mc + 0xbe4) = nv;
    *(volatile float *)((uintptr_t)mc + 0xc08) = 0.0f;
    return 1;
}

/* マス目状のリスト（魔法・アビリティの一覧など）で、見えていない行へ送る */
typedef void (*SetViewItemFn)(void *list, int idx);
static SetViewItemFn p_SetViewItem;
static void *g_pendItem;          /* 送り終わったらカーソルを乗せるマス */
static int g_pendSub, g_pendWait;

typedef struct { float x, y; } Vec2f;
typedef Vec2f (*ScrollMinFn)(void *item);
typedef void (*ILSetScrollFn)(void *list, Vec2f *v);
typedef void (*ILSyncFn)(void *list);
static ScrollMinFn p_ScrollMin;
static ILSetScrollFn p_ILSetScroll;
static ILSyncFn p_ILSync;
static int grid_scroll(const Cand *c, int n, const Cand *cur, int dir)
{
    if (!p_SetViewItem || cur->list != 1 || cur->sub < 0) return 0;
    void *it = cur->item;
    int cols = RD(int, it, 0xa8), cnt = RD(int, it, 0xc0);
    if (cols <= 0 || cnt <= 0) return 0;
    int idx = cur->sub + (dir == K_DOWN ? cols : -cols);
    if (dir == K_DOWN && idx >= cnt) {
        if (cur->sub / cols < (cnt - 1) / cols) idx = cnt - 1;   /* 最後の行が途中までのとき */
        else return 0;
    }
    if (idx < 0) return 0;
    for (int i = 0; i < n; i++) if (c[i].item == it && c[i].sub == idx) return 0;  /* もう見えている */
    p_SetViewItem(it, idx);
    g_pendItem = it; g_pendSub = idx; g_pendWait = 30;
    return 1;
}

/* ---------- 戦闘のボタン（AUTO / PAUSE / ESCAPE）と L / R ---------- */
#define VT_ITEMLIST 0x459348UL   /* JobsMenuItemListControl：戦闘右下の ▶・AUTO・PAUSE・ESCAPE */
#define LR_WAIT 3                /* L / R を押してから同時押しを待つフレーム数（約0.1秒） */
static int g_btlBtn;             /* 最後に見た画面が戦闘のボタンありだったか（押せる所がない間は保つ） */
static int g_pendL, g_pendR;     /* 同時押し待ちの残りフレーム */
static int g_fireL, g_fireR;     /* 待ち終わった L / R（戦闘の処理が読むまで数フレーム有効） */
static int g_btlReq;             /* 1=AUTO 2=PAUSE 3=ESCAPE をタップしたい */

static void battle_keys(const Cand *c, int n)
{
    /* 右側に縦に並ぶ部品リストの中身（▶ 以外の3つ）を上から AUTO, PAUSE, ESCAPE とみなす */
    const Cand *bt[3] = { 0, 0, 0 };
    int nb = 0;
    for (int i = 0; i < n; i++) {
        if (c[i].vt != g_base + VT_ITEMLIST || c[i].sub < 1 || c[i].sub > 3 || c[i].x < 400.0f) continue;
        if (nb && c[i].item != bt[0]->item) continue;
        if (nb < 3) bt[nb++] = &c[i];
    }
    if (nb == 3) {
        for (int a = 0; a < 3; a++)
            for (int b2 = a + 1; b2 < 3; b2++)
                if (bt[b2]->y < bt[a]->y) { const Cand *t = bt[a]; bt[a] = bt[b2]; bt[b2] = t; }
        g_btlBtn = 1;
    } else {
        nb = 0;
        if (n > 0) g_btlBtn = 0;
    }

    if (g_fireL > 0) g_fireL--;
    if (g_fireR > 0) g_fireR--;
    if (g_trig & K_BAUTO)  g_btlReq = 1;
    if (g_trig & K_BPAUSE) g_btlReq = 2;
    if (g_btlBtn) {
        int tl = (g_trig & K_PREV) != 0, tr = (g_trig & K_NEXT) != 0;
        if ((tl && tr) || (tl && g_pendR) || (tr && g_pendL)) {
            g_btlReq = 3; g_pendL = g_pendR = 0;
        } else if (tl) {
            g_pendL = LR_WAIT;
        } else if (tr) {
            g_pendR = LR_WAIT;
        } else {
            if (g_pendL && --g_pendL == 0) g_fireL = 3;
            if (g_pendR && --g_pendR == 0) g_fireR = 3;
        }
    } else {
        g_pendL = g_pendR = g_fireL = g_fireR = 0;
    }

    if (!g_btlReq || g_tapWait || !p_SetData || !p_GetTouchInfo) return;
    int req = g_btlReq;
    g_btlReq = 0;
    if (nb == 3) {
        const Cand *b = bt[req - 1];
        g_tapX = b->x + b->w * 0.5f;
        g_tapY = b->y + b->h * 0.5f;
    } else if (req == 2 && n == 0 && g_btlBtn) {
        g_tapX = 320.0f; g_tapY = 192.0f;          /* PAUSE 中：画面の真ん中で解除 */
    } else {
        return;
    }
    send_touch(0, g_tapX, g_tapY);
    g_tapWait = 2;
}

/* v3.20: ジョブ画面で A を押したマス（ボタン番号 20000+）。ダイアログのあと一覧が出直したら戻す */
static int g_jobId;
/* v4.01: ページを切り替えたあと汎用カーソルを移すジョブのマス（ボタン番号。0=なし） */
static int g_jobWant, g_jobWantWait;
static int job_wrap(void *mc, int k, int dir);
#define JOB_TOUCH_ID0 20000
#define ARROW_ID_L    20900
#define ARROW_ID_R    20901
/* v3.23: 店で A を押した品物の行（ボタン番号 20000+）。確認のあと一覧が出直したら戻す */
static int g_shopId;
static void local_field_poll(void);
static void local_event_patch(unsigned char *h, int cnt);
static void summon_sync(void);

static void *game_inst(void);

/* マス目リストのマスに文字が無いか（その一覧に文字のあるマスが1つも無いときは 0 を返して外さない） */
static int cell_blank(void *it, int sub)
{
    u8 *ent = (u8 *)RDP(it, 0xc8);
    int cnt = RD(int, it, 0xc0);
    if (!ent || sub < 0 || sub >= cnt || cnt > 256) return 0;
    int any = 0;
    for (int i = 0; i < cnt && !any; i++) { const char *m = (const char *)RDP(ent, i * 24 + 8); if (m && *m) any = 1; }
    if (!any) return 0;
    const char *m = (const char *)RDP(ent, sub * 24 + 8);
    return !m || !*m;
}

static void cursor_tick(void)
{
    /* 前フレームに集めた候補を使い、次の収集先を空にする */
    int b = g_cb;
    g_cb ^= 1;
    g_ncand[g_cb] = 0;
    g_nw[g_cb] = 0;
    int force = g_force[b];
    g_force[g_cb] = 0;
    const Cand *cAll = g_cand[b];
    int nAll = g_ncand[b];
    /* v3.11: 戦闘の右下の欄（◀・AUTO・PAUSE・ESCAPE）はカーソルの行き先にしない。
       先頭の ◀ が右端（x>=560）にある縦長のマス（高さ90以上）を持つ一覧を、その欄とみなす。
       AUTO などのボタン操作（battle_keys）は外す前の候補 cAll を使う */
    static Cand s_candF[MAXC];
    const void *barItem = 0;
    for (int i = 0; i < nAll; i++)
        if ((cAll[i].sub == 0 && cAll[i].x >= 560.0f && cAll[i].h >= 90.0f) ||
            /* 1.0-pre14: ▶ のつまみを消したので、右下の部品リスト（JobsMenuItemListControl）そのものでも見分ける */
            (cAll[i].list == 3 && cAll[i].vt == g_base + VT_ITEMLIST && cAll[i].x >= 400.0f && cAll[i].y >= 240.0f)) {
            barItem = cAll[i].item; break;
        }
    /* v4.04: 魔法・技の画面（方向キーを MagicFunc が読む）では、文字の入っていないマス（コマンド一覧の空き行など）を外す。
       マス目リストの中身は +0xc8 から 0x18 バイトずつ、+8 が文字。その一覧に文字のあるマスが1つもなければ外さない */
    int magicScr = 0;
    for (int i = 0; i < g_nDirCallers[g_dcb]; i++)
        if (g_dirCallers[g_dcb][i] - 0x3ff258 < 3184) { magicScr = 1; break; }
    int n = 0;
    for (int i = 0; i < nAll; i++) {
        if (barItem && cAll[i].item == barItem) continue;
        /* 1.0-pre16: 魔法・技の画面（MenuClass +0x10 == 4）の右の列のボタン（番号 20000＋何番目のコマンドか）のうち、
           コマンドの数（GameClass +0x34430）以上の空いた行は外す（ゲームも押されても受け付けない） */
        /* 1.0-pre23: コマンドを選ぶ段階（MagicFunc の状態 MenuClass +0x40 == 0）だけ。魔法を選ぶ段階は、魔法のマスも 20000＋番号 */
        if (cAll[i].list == 2 && cAll[i].widget && cAll[i].x >= 360.0f && RD(int, cAll[i].widget, MC_MODE) == MENU_MAGIC &&
            RD(int, cAll[i].widget, 0x40) == 0) {
            unsigned k = (unsigned)((cAll[i].id & 0xffff) - 20000);
            void *gm = k < 64 ? game_inst() : 0;
            if (gm && (int)k >= RD(int, gm, G_MENUCMD_N)) continue;
        }
        if (magicScr && cAll[i].list == 1 && cAll[i].item && cell_blank(cAll[i].item, cAll[i].sub)) continue;
        s_candF[n++] = cAll[i];
    }
    const Cand *c = s_candF;

    if (g_sinceDir < 1000) g_sinceDir++;
    if (g_sinceA < 1000) g_sinceA++;

    /* 前フレームの方向キー問い合わせ元 */
    int db = g_dcb;
    g_dcb ^= 1;
    g_nDirCallers[g_dcb] = 0;
    int ndc = g_nDirCallers[db];
    int mcCount = g_mcCount;
    g_mcCount = -1;
    /* ゲーム側のキー処理が中途半端な画面（隊列・ジョブ・ステータス）は汎用カーソルに任せる */
    {
        static const struct { uintptr_t start, size; } k_forceFuncs[] = {
            { 0x400c74, 1884 },  /* MenuClass::FormationFunc（隊列） */
            { 0x3fcc5c, 2396 },  /* MenuClass::JobFunc（ジョブ） */
            { 0x400b7c,  248 },  /* MenuClass::StatusFunc（ステータス） */
            { 0x3fe0c0, 4504 },  /* MenuClass::ItemFunc（アイテム：使う相手の選択でゲーム側の上下が効かない） */
            { 0x3ff258, 3184 },  /* MenuClass::MagicFunc（魔法：かける相手の選択でゲーム側の上下が効かない） */
            { 0x3ffec8, 3252 },  /* MenuClass::EquipFunc（装備：下のボタンへ行けるように） */
        };
        for (int i = 0; i < ndc && !force; i++)
            for (unsigned k = 0; k < sizeof(k_forceFuncs) / sizeof(k_forceFuncs[0]); k++)
                if (g_dirCallers[db][i] - k_forceFuncs[k].start < k_forceFuncs[k].size) { force = 1; break; }
    }
    /* メインメニューの処理だけが方向キーを見ていて、メニュー側のボタンが3つ以下：
       メインメニューから入ったメンバー選択（上下はゲーム側で効かない）とみなす */
    if (ndc > 0 && mcCount >= 0 && mcCount <= 3) {
        int allMain = 1;
        for (int i = 0; i < ndc; i++)
            if (g_dirCallers[db][i] - 0x3fc4e8 >= 1908) { allMain = 0; break; }
        if (allMain) force = 1;
        g_memberSel = allMain;
    } else {
        g_memberSel = 0;
    }

    int equip = 0;
    for (int i = 0; i < ndc; i++)
        if (g_dirCallers[db][i] - 0x3ffec8 < 3252) { equip = 1; break; }
    int inWin = g_btl && RD(int, g_btl, BTL_SCHEDULE) == 3;   /* 戦闘終了画面：カーソルを出さず、A は画面中央をタップ */
    if (inWin != g_winLog) { g_winLog = inWin; }
    g_cursorMode = (n > 0 && !inWin && (g_sinceDir > 3 || force) && p_SetData && p_GetTouchInfo);
    if (g_trig) {
    }

    if (g_tapWait > 0 && --g_tapWait == 0)
        send_touch(2, g_tapX, g_tapY);           /* 離す */
    /* セーブ・ロード画面と、その確認：B で「戻る」（390）を押す。ゲーム側はこの画面で B を見ないため */
    /* v3.08: ゲームがセーブ画面を処理中（GameClass::IsSaveSceneExec と同じ印 GameClass+0x4e0c8）のときだけ。
       アイテム一覧やショップも 20000 番台を使うので、印なしだとそこでも押してしまっていた */
    u8 *gmS = (u8 *)game_inst();
    if ((g_trig & K_BACK) && gmS && RD(u8, gmS, 0x4e0c8) && g_tapWait == 0 && p_SetData && p_GetTouchInfo) {
        const Cand *back = 0; int other = 0;
        for (int k = 0; k < n; k++) {
            if (c[k].list != 2) continue;
            int id = c[k].id;
            if (id == BTN_BACK) back = &c[k];
            else if (id == BTN_DELETE || id == BTN_YES || (unsigned)(id - 20000) < 10) other = 1;
        }
        if (back && other) {
            g_tapX = back->x + back->w * 0.5f; g_tapY = back->y + back->h * 0.5f;
            send_touch(0, g_tapX, g_tapY);
            g_tapWait = 2;
        }
    }
    battle_keys(cAll, nAll);

    /* 新しく現れた部品（ターゲット選択やダイアログ）があれば、そちらへ移す。
       直近1秒以内に見た部品は「新しい」とみなさない。
       ゲーム側のキー処理で動いている間も記録しておき、切り替わった瞬間に誤判定しないようにする */
    void *newW = 0;
    for (int i = 0; i < g_nw[b]; i++) {
        void *w = g_wseen[b][i];
        int slot = -1, oldest = 0;
        for (int k = 0; k < WHIST; k++) {
            if (g_whist[k].w == w) { slot = k; break; }
            if (g_whist[k].last < g_whist[oldest].last) oldest = k;
        }
        int isNew = (slot < 0) || (g_frame - g_whist[slot].last > 30);
        if (slot < 0) { slot = oldest; g_whist[slot].w = w; }
        g_whist[slot].last = g_frame;
        if (isNew && !newW)
            for (int k = 0; k < n; k++) if (c[k].widget == w) { newW = w; break; }
    }

    /* v3.18: 画面の切り替わり＝方向キーを読むゲームの処理（呼び出し元）が変わった瞬間。
       部品は画面をまたいで使い回されるので、部品が新しいかどうかでは判定できない */
    static uintptr_t s_scrKey;
    uintptr_t scrKey = ndc > 0 ? g_dirCallers[db][0] : 0;
    int scrChanged = scrKey && scrKey != s_scrKey;
    uintptr_t prevScrKey = s_scrKey;
    if (scrKey) s_scrKey = scrKey;
    /* v3.19: メインメニュー・メンバー選択（MenuSelectFunc）から別の画面へ入った瞬間は、
       直前のカーソル位置を引き継がない（魔法・技の分類が、選んだ人の行の高さに合ってしまう） */
    int fromSelect = scrChanged && prevScrKey - 0x3fc4e8 < 1908 && !(scrKey - 0x3fc4e8 < 1908);
    if (fromSelect) g_jobId = 0;   /* v3.20: メンバー選択から入り直した：ジョブ画面は左上から */

    /* v3.23: 経験値をそろえる指示ファイル（フィールドを歩ける間だけ、1秒に1回） */
    if (s_scrKey == 0x3b7aac && !g_btl && g_frame % 30 == 0) { local_field_poll(); summon_sync(); }

    if (!g_cursorMode) {
        /* 位置は消さずに覚えておく（一瞬カーソルが無効になっても戻れるように） */
        /* 押せる候補がなく、ゲームも決定キーを見ていない（「ロードが完了しました」など）：
           画面中央をタップして先へ進める */
        /* v3.13: サイドバーを外した後の候補で判定する（戦闘中の会話は ◀ のバーしか残らない） */
        if ((n == 0 || inWin) && (g_trig & K_A) && g_sinceA > 2 && g_tapWait == 0 && p_SetData && p_GetTouchInfo) {
            g_tapX = 320.0f; g_tapY = 192.0f;
            send_touch(0, g_tapX, g_tapY);
            g_tapWait = 2;
        }
        return;
    }


    const Cand *cur = 0;

    /* 戦闘コマンド欄の中身（＝誰の番か）が変わったら、「たたかう」に戻す。
       中身は各マスの文字列ポインタから作った指紋で見分ける */
    {
        int bigCnt = 0;
        const Cand *big = biggest_list_first(c, n, 0, &bigCnt);
        unsigned sig = (big && bigCnt >= 3) ? big->sig : 0;
        if (sig && sig != g_cmdSig)
            cur = first_sel(c, n, big);  /* コマンド欄が出た／中身が変わった（記憶の設定なら前回のコマンドから） */
        g_cmdSig = sig;
    }

    int recent = g_curC.item && (g_frame - g_curFrame) <= 30 && !fromSelect;   /* 直前（1秒以内）までカーソルがあった */
    /* 新しい画面に「はい」（MenuClass のボタン 322）があれば、ほかの部品と同時に出てもそこから始める */
    if (!cur && newW)
        for (int k = 0; k < n; k++)
            if (c[k].list == 2 && c[k].id == BTN_YES) { cur = &c[k]; break; }
    /* v3.17: メンバー選択（魔法・技の相手など）は、新しく出たら直前の画面に関係なく右側の一覧の先頭から */
    if (!cur && newW && g_memberSel) {
        for (int i = 0; i < n; i++) {
            if (c[i].list != 3) continue;
            if (!cur || c[i].x > cur->x + 4.0f || (fabsf_(c[i].x - cur->x) <= 4.0f && c[i].y < cur->y))
                cur = &c[i];
        }
    }
    if (!cur && newW && !g_memberSel) {
        cur = default_in(c, n, newW);
        /* 新しく出た部品より大きなマス目リスト（戦闘コマンド）が画面にあれば、そちらを優先。
           行動がたまったときに右下の ▼ が新しく出て、カーソルが吸い寄せられるのを防ぐ */
        int bigCnt = 0;
        const Cand *big = biggest_list_first(c, n, 0, &bigCnt);
        if (big && big->widget != newW) {
            int curCnt = 0;
            if (cur && cur->list == 1)
                for (int k = 0; k < n; k++) if (c[k].item == cur->item) curCnt++;
            if (bigCnt > curCnt) cur = big;
        }
        cur = first_sel(c, n, cur);
    }
    if (!cur)
        cur = recent ? find_cand(c, n, g_curC.item, g_curC.sub) : 0;
    if (!cur && recent && g_curC.sub >= 0) {
        /* v3.10: 乗っていた行が消えた（ターゲットの敵が倒れた、アイテムを使い切った）：
           同じ一覧が残っていれば、その中で番号が一番近い行へ（一覧の外の ◀ などへ飛ばさない） */
        int bd = 1 << 30;
        for (int i = 0; i < n; i++) {
            if (c[i].item != g_curC.item || c[i].sub < 0) continue;
            int d = c[i].sub - g_curC.sub; if (d < 0) d = -d * 2 + 1; else d = d * 2;   /* 同じ距離なら前の行 */
            if (d < bd) { bd = d; cur = &c[i]; }
        }
    }
    if (!cur && recent) {
        /* 直前まで乗っていたボタンが作り直された（隊列の入れ替えなど）：一番近い位置のボタンへ */
        float lx = g_curC.x + g_curC.w * 0.5f, ly = g_curC.y + g_curC.h * 0.5f, bd = 1e30f;
        for (int i = 0; i < n; i++) {
            float dx = c[i].x + c[i].w * 0.5f - lx, dy = c[i].y + c[i].h * 0.5f - ly;
            float d = dx * dx + dy * dy;
            if (d < bd) { bd = d; cur = &c[i]; }
        }
        if (bd > 40.0f * 40.0f) cur = 0;   /* 近くに何もなければ通常の初期位置へ */
    }
    if (!cur)
    {
        /* メンバー選択：右側のリスト（いちばん右の列）の先頭から始める */
        if (g_memberSel) {
            for (int i = 0; i < n; i++) {
                if (c[i].list != 3) continue;
                if (!cur || c[i].x > cur->x + 4.0f || (fabsf_(c[i].x - cur->x) <= 4.0f && c[i].y < cur->y))
                    cur = &c[i];
            }
        }
        if (!cur) {
            cur = default_in(c, n, 0);
        }
    }
    g_hadPrev = 1;
    if (!cur) { g_curC.item = 0; return; }

    /* 装備画面：画面に入ったときと状態が変わったときは、ゲームの指と同じボタンへ。
       状態 1=部位を選ぶ（指は mc+0x2c、ボタン 10000+）、2=品物を選ぶ（指は game+0x17658、ボタン 20000+） */
    {
        static int eqState = -1, eqTry;
        void *mc = 0;
        if (equip)
            for (int i = 0; i < n; i++)
                if (c[i].list == 2 && (unsigned)((c[i].id & 0xffff) - 10000) < 5) { mc = c[i].widget; break; }
        if (!mc) {
            eqState = -1;
        } else {
            int st = RD(int, mc, 0x40);
            if (st != eqState) {
                void *game = game_inst();
                int want = st == 1 ? 10000 + RD(int, mc, 0x2c) : (st == 2 && game) ? 20000 + RD(int, game, 0x17658) : -1;
                const Cand *t = 0;
                for (int i = 0; i < n && want >= 0; i++)
                    if (c[i].list == 2 && c[i].widget == mc && (c[i].id & 0xffff) == want) { t = &c[i]; break; }
                if (t) cur = t;
                if (t || want < 0 || ++eqTry > 30) {
                    eqState = st; eqTry = 0;
                }
            }
        }
    }

    /* v3.18: 画面が切り替わった瞬間の初期位置。
       メンバー選択（魔法・技・装備・ステータスなど）＝右側の一覧の先頭、隊列＝左の一覧の先頭 */
    if (scrChanged) {
        int formation = scrKey - 0x400c74 < 1884;
        const Cand *t = 0;
        if (g_memberSel || formation) {
            for (int i = 0; i < n; i++) {
                if (c[i].list != 3) continue;
                if (!t) { t = &c[i]; continue; }
                int better = g_memberSel
                    ? (c[i].x > t->x + 4.0f || (fabsf_(c[i].x - t->x) <= 4.0f && c[i].y < t->y))
                    : (c[i].x < t->x - 4.0f || (fabsf_(c[i].x - t->x) <= 4.0f && c[i].y < t->y));
                if (better) t = &c[i];
            }
        }
        if (t) {
            cur = t;
        }
    }

    /* v3.18: アビリティ画面（MenuClass のボタン 10000 と「整列」394）。
       状態 MenuClass+0x40: 0=スロットを選ぶ（◀▶あり、ゲームがキーを処理し汎用カーソルは出ない）、
       1=一覧を選ぶ。1 になったら一覧の先頭「はずす」(20000) へ */
    {
        static int abState = -1, abTry;
        static unsigned abSeen;
        if (g_frame - abSeen > 2) abState = -1;   /* 汎用カーソルが出ていない間（状態0）は見えないので、戻ってきたら最初から */
        abSeen = g_frame;
        void *mc = 0; int has394 = 0;
        for (int i = 0; i < n; i++) {
            if (c[i].list != 2) continue;
            if ((c[i].id & 0xffff) == 10000) mc = c[i].widget;
            if (c[i].id == 394) has394 = 1;
        }
        if (!mc || !has394 || equip) {
            abState = -1;
        } else {
            int st = RD(int, mc, 0x40);
            if (st != abState) {
                int want = st == 1 ? 20000 : -1;
                const Cand *t = 0;
                for (int i = 0; i < n && want >= 0; i++)
                    if (c[i].list == 2 && c[i].widget == mc && (c[i].id & 0xffff) == want) { t = &c[i]; break; }
                if (t) cur = t;
                if (t || want < 0 || ++abTry > 30) {
                    abState = st; abTry = 0;
                }
            }
        }
    }

    /* v3.20: ジョブ画面（方向キーを読む処理が 3fd38c/3fd414/3fd460、またはその上のダイアログ 3fced8）。
       一覧（ボタン 20000+、127x57）が消えて出直したら、A を押したマスへ戻す */
    int jobCtx = s_scrKey == 0x3fd3b4 || s_scrKey == 0x3fd43c || s_scrKey == 0x3fd488 || s_scrKey == 0x3fcf00;
    {
        static unsigned jbSeen;
        int grid = 0;
        for (int i = 0; i < n; i++)
            if (c[i].list == 2 && (c[i].id & 0xffff) == 20000 && c[i].w < 200.0f && c[i].h < 80.0f) { grid = 1; break; }
        if (grid && jobCtx) {
            if (g_frame - jbSeen > 2 && g_jobId) {
                const Cand *t = 0;
                for (int i = 0; i < n; i++)
                    if (c[i].list == 2 && (c[i].id & 0xffff) == g_jobId) { t = &c[i]; break; }
                if (t) cur = t;
            }
            jbSeen = g_frame;
        }
    }

    /* v3.23: 店（方向キーを読む処理が 406cb4/406cd4）。品物の行（ボタン 20000+、横長）が
       確認のあいだ消えて出直したら、A を押した行へ戻す。店に入り直したら忘れる */
    int shopCtx = s_scrKey == 0x406cdc || s_scrKey == 0x406cfc;
    {
        int prevShop = prevScrKey == 0x406cdc || prevScrKey == 0x406cfc;
        if (scrChanged && shopCtx && !prevShop) g_shopId = 0;
        static unsigned shSeen;
        int rows = 0;
        for (int i = 0; i < n; i++)
            if (c[i].list == 2 && (unsigned)((c[i].id & 0xffff) - 20000) < 64 && c[i].w > 400.0f && c[i].h < 60.0f) { rows = 1; break; }
        if (rows && shopCtx) {
            if (g_frame - shSeen > 2 && g_shopId) {
                const Cand *t = 0;
                for (int i = 0; i < n; i++)
                    if (c[i].list == 2 && (c[i].id & 0xffff) == g_shopId) { t = &c[i]; break; }
                if (t) cur = t;
            }
            shSeen = g_frame;
        }
    }

    /* v3.21: セーブ画面（ファイルの枠＝ボタン 20000+ の横長の行、削除 401）。
       入ったときは、ゲームの指のファイルから始める。
       DrawSaveSlot は GameClass+0x4e0f0 と枠の番号が同じときに指を描き、
       枠の位置は「番号×88 − スクロール量（GameClass+0x35044）」 */
    {
        static unsigned svSeen;
        static int svTry = -1;
        void *mc = 0; int has401 = 0; float top = 1e30f;
        for (int i = 0; i < n; i++) {
            if (c[i].list != 2) continue;
            unsigned id = (unsigned)((c[i].id & 0xffff) - 20000);
            if (id < 32 && c[i].w > 400.0f) { mc = c[i].widget; if (c[i].y < top) top = c[i].y; }
            if (c[i].id == 401) has401 = 1;
        }
        if (!mc || !has401) {
            svTry = -1;
        } else {
            if (g_frame - svSeen > 2) svTry = 0;
            svSeen = g_frame;
            void *game = svTry >= 0 ? game_inst() : 0;
            if (svTry >= 0 && game) {
                int slot = RD(int, game, 0x4e0f0), scr = RD(int, game, 0x35044);
                float py = top + (float)(slot * 88 - scr) + 44.0f;
                const Cand *t = 0;
                for (int i = 0; i < n && slot >= 0; i++) {
                    unsigned id = (unsigned)((c[i].id & 0xffff) - 20000);
                    if (c[i].list != 2 || c[i].widget != mc || id >= 32 || c[i].w <= 400.0f) continue;
                    if (py >= c[i].y && py < c[i].y + c[i].h) { t = &c[i]; break; }
                }
                if (t) cur = t;
                svTry = -1;
            } else if (svTry >= 0 && ++svTry > 15) {
                LOGI("save: no game instance");
                svTry = -1;
            }
        }
    }

    /* 送り終わったマスが見えたら、そこへカーソルを移す */
    if (g_pendItem) {
        const Cand *p = find_cand(c, n, g_pendItem, g_pendSub);
        if (p) { cur = p; g_pendItem = 0; }
        else if (--g_pendWait <= 0) g_pendItem = 0;
    }
    /* v4.01: ジョブ画面のページを切り替えた：指定のマスへ */
    if (g_jobWant) {
        const Cand *t = 0;
        for (int i = 0; i < n; i++)
            if (c[i].list == 2 && (c[i].id & 0xffff) == g_jobWant && c[i].w < 200.0f && c[i].h < 80.0f) { t = &c[i]; break; }
        if (t) { cur = t; g_jobWant = 0; g_jobWantWait = 0; }
        else if (++g_jobWantWait > 15) { g_jobWant = 0; g_jobWantWait = 0; }
    }
    static const int dirs[4] = { K_UP, K_DOWN, K_LEFT, K_RIGHT };
    for (int i = 0; i < 4; i++) {
        if (g_rep & dirs[i]) {
            if (i < 2 && grid_scroll(c, n, cur, dirs[i])) break;
            /* v4.01: ジョブ画面の3段目の端で左右：反対側のページの端へ回り込む */
            if (i >= 2 && jobCtx && cur->list == 2 && (unsigned)((cur->id & 0xffff) - JOB_TOUCH_ID0) < 64 &&
                cur->w < 200.0f && job_wrap(cur->widget, (cur->id & 0xffff) - JOB_TOUCH_ID0, dirs[i] == K_LEFT ? -1 : 1))
                break;   /* 一覧を送った：カーソルは送り終わってから動く */
            const Cand *nx = nav(c, n, cur, dirs[i]);
            /* MenuClass のスクロール欄（店の品物など）の端で上下を押したら、
               欄の外へ出る前に欄そのものを1行ぶん送る。カーソルは同じ段に残り、次の品物を指す */
            if (i < 2 && cur->list == 2 && (RD(int, cur->item, 0x14) & 8) &&
                !(nx && nx->list == 2 && nx->widget == cur->widget && (RD(int, nx->item, 0x14) & 8) &&
                  (dirs[i] == K_DOWN ? nx->y > cur->y : nx->y < cur->y)) &&   /* 折り返しは欄の端扱い */
                scroll_mc_list(cur->widget, dirs[i] == K_DOWN ? 1 : -1))
                nx = 0;
            /* v3.41: 部品リストの中身（JobsMenuItemListControl、ステータス画面の名前一覧など）の端で上下を押したら、
               まだ送れる分があるうちは外へ出ずに一覧を1段送る。送り位置は +0x54（0〜CalcScrollMin の y） */
            if (i < 2 && cur->list == 3 && cur->item && cur->vt == g_base + VT_ITEMLIST && p_ScrollMin && p_ILSetScroll && p_ILSync &&
                !(nx && nx->list == 3 && nx->item == cur->item && (dirs[i] == K_DOWN ? nx->y > cur->y : nx->y < cur->y))) {
                Vec2f mn = p_ScrollMin(cur->item);
                float y = RD(float, cur->item, 0x54), step = cur->h > 1.0f ? cur->h : 1.0f;
                float ny = dirs[i] == K_DOWN ? y - step : y + step;
                if (ny < mn.y) ny = mn.y;
                if (ny > 0.0f) ny = 0.0f;
                if (ny != y) {
                    Vec2f v = { RD(float, cur->item, 0x50), ny };
                    p_ILSetScroll(cur->item, &v);
                    p_ILSync(cur->item);
                    g_pendItem = cur->item; g_pendSub = cur->sub + (dirs[i] == K_DOWN ? 1 : -1); g_pendWait = 10;
                    nx = 0;
                }
            }
            if (nx) cur = nx;
            break;
        }
    }
    g_curC = *cur;
    g_curFrame = g_frame;
    if ((g_trig & K_A) && g_tapWait == 0) {
        if (jobCtx && cur->list == 2 && (unsigned)((cur->id & 0xffff) - 20000) < 64 && cur->w < 200.0f) {
            g_jobId = cur->id & 0xffff;
        }
        if (shopCtx && cur->list == 2 && (unsigned)((cur->id & 0xffff) - 20000) < 64 && cur->w > 400.0f && cur->h < 60.0f) {
            g_shopId = cur->id & 0xffff;
        }
        g_tapX = cur->x + cur->w * 0.5f;
        g_tapY = cur->y + cur->h * 0.5f;
        send_touch(0, g_tapX, g_tapY);           /* 押す */
        g_tapWait = 2;
    }
}

static unsigned g_drawnFrame;   /* カーソル枠を描いたフレーム */

static void draw_frame(void *g, int x, int y, int ww, int hh)
{
    g_drawnFrame = g_frame;
    unsigned ph = g_frame % 32;
    unsigned a = 0x90 + (ph < 16 ? ph : 31 - ph) * 6;   /* ゆっくり明滅 */
    int t = 4;
    int saved = RD(int, g, G_COLOR);
    *(volatile int *)((uintptr_t)g + G_COLOR) = (int)(0xFFD84000u | a);
    p_FillRect(g, x - t, y - t, ww + 2 * t, t);
    p_FillRect(g, x - t, y + hh, ww + 2 * t, t);
    p_FillRect(g, x - t, y, t, hh);
    p_FillRect(g, x + ww, y, t, hh);
    *(volatile int *)((uintptr_t)g + G_COLOR) = saved;
}

static void wdraw_inner(void *w, void *g);
static void hook_WDraw(void *w, void *g)
{
    int sh = g_wideE > 0.0f && is_field_hud(w);
    if (sh) hud_shift(w, g_wideE);
    wdraw_inner(w, g);
    if (sh) hud_shift(w, -g_wideE);
    if (g_wideE > 0.0f && !sh && w) {
        /* 調査用：640 の右端の外にはみ出す部品を描く呼び元を1回ずつ記録する（戦闘の AUTO などの欄） */
        float mx = -1e9f;
        void *node = RDP(w, W_HEAD);
        for (int guard = 0; node && guard < 64; guard++) {
            void *it = RDP(node, 0x10);
            if (it && RD(float, it, I_POS) > mx) mx = RD(float, it, I_POS);
            node = RDP(node, 0);
        }
        if (mx > 600.0f) {
            static uintptr_t seen[16];
            static int n;
            uintptr_t ra = (uintptr_t)__builtin_return_address(0) - g_base;
            int dup = 0;
            for (int i = 0; i < n; i++) if (seen[i] == ra) dup = 1;
            if (!dup && n < 16) { seen[n++] = ra; LOGI("wide: widget %p drawn from %lx, item x up to %d", w, ra, (int)mx); }
        }
    }
}

static void wdraw_inner(void *w, void *g)
{
    g_origWDraw(w, g);
    if (!g_cursorMode || !g_curC.item || g_curC.list == 2 || g_curC.widget != w || !p_FillRect || !g)
        return;
    void *node = RDP(w, W_HEAD);
    for (int guard = 0; node && guard < 256; guard++) {
        void *it = RDP(node, 0x10);
        if (it == g_curC.item) {
            draw_frame(g, (int)(RD(float, it, I_POS) + g_curC.rx), (int)(RD(float, it, I_POS + 4) + g_curC.ry),
                       (int)g_curC.w, (int)g_curC.h);
            return;
        }
        node = RDP(node, 0);
    }
}

/* MenuClass 独自のボタン表（セーブ画面の はい／いいえ／戻る、メンバー選択など） */
static void collect_menuclass(void *mc)
{
    int st = RD(int, mc, MC_STATE);
    g_mcState = st;
    g_mcCount = 0;
    for (int i = 0; i < 32 && RD(int, (uintptr_t)mc + MC_TABLE + i * 0x18, 0); i++) g_mcCount++;
    if ((unsigned)(st - 2) <= 3) return;   /* MoveWidget がボタンを受け付けない状態 */
    int e4 = RD(int, mc, 0xbe4), f0 = RD(int, mc, 0xbf0), f4 = RD(int, mc, 0xbf4), f8 = RD(int, mc, 0xbf8);
    for (int i = 0; i < 32; i++) {
        uintptr_t e = (uintptr_t)mc + MC_TABLE + i * 0x18;
        int id = RD(int, e, 0);
        if (!id) break;
        if (id == ARROW_ID_L || id == ARROW_ID_R) continue;   /* v4.01: ジョブ画面の ◀ ▶ はタッチ用（パッドは L2/R2） */
        int x = RD(int, e, 4), y = RD(int, e, 8), w = RD(int, e, 0xc), h = RD(int, e, 0x10), fl = RD(int, e, 0x14);
        if (fl & 8) {               /* スクロールする欄の中のボタン */
            if (f4 <= 0) continue;
            y += (e4 / f4) * f4 - e4;
            float cy = y + h * 0.5f;
            if (cy < f0 || cy >= f0 + f8) continue;
        }
        int before = g_ncand[g_cb];
        add_cand(mc, (void *)e, -1, 2, (float)x, (float)y, 0, 0, (float)w, (float)h, 0, 0);
        if (g_ncand[g_cb] > before) g_cand[g_cb][before].id = id;
        if ((unsigned)((id & 0xffff) - 20000) < 10)
            g_force[g_cb] = 1;    /* メンバー選択のボタン：ゲーム側の上下が効かない画面なので汎用カーソルを使う */
    }
}

static void hook_MoveW(void *mc)
{
    g_origMoveW(mc);
    int nw = g_nw[g_cb];
    int seen = 0;
    for (int i = 0; i < nw; i++) if (g_wseen[g_cb][i] == mc) { seen = 1; break; }
    if (seen) return;
    if (nw < MAXW) g_wseen[g_cb][g_nw[g_cb]++] = mc;
    collect_menuclass(mc);
}

static void hook_DrawW(void *mc, void *g)
{
    g_origDrawW(mc, g);
    if (g_cursorMode && g_curC.item && g_curC.list == 2 && g_curC.widget == mc && p_FillRect && g)
        draw_frame(g, (int)g_curC.x, (int)g_curC.y, (int)g_curC.w, (int)g_curC.h);
}

/* ---------- 光専用・闇専用ジョブの共有（v2.19、v4.01 で作り替え） ----------
   ジョブ画面のマスは 4+4+5 の13個で、キャラ絵の置き場も14個しかない。そこでジョブ一覧を2ページに分ける。
     1ページ目: 元のとおり（すっぴん・基本7・自分側の専用5）
     2ページ目: すっぴん・基本7・もう片方の専用5（覚えているものだけ）
   1・2段目（共通の8つ）は両方のページで同じで、3段目だけが入れ替わる。
   切り替えは L2 / R2（キーボードは Tab）、赤魔道士の左と召喚士の右に描く ◀ ▶ のタップ、
   3段目の端での左右（左端で左→反対側の右端、右端で右→反対側の左端。10個が輪になる）。
   ◀ ▶ と端の回り込みは、反対側に覚えたジョブが1つ以上あるときだけ。
   2ページ目を作るときだけメンバーの陣営（+0x32）を一時的に反対にする。CreateJobList は
   ジョブデータ +0x10（1=光 2=闇 3=共通）とメンバーの陣営の AND で選ぶので、共通ジョブはそのまま残る */
#define GAME_PTR_GOT  0x45ca58UL      /* GameClass のインスタンスへ（3回たどる） */
#define G_JOBCOUNT    0x4ddc8         /* GameClass: ジョブ数 */
#define G_JOBDATA     0x4ddd0         /* GameClass: ジョブデータ（0x98 バイトずつ） */
#define JD_SIZE       0x98
#define JD_SIDE       0x10            /* ジョブデータ: 1=光 2=闇 3=共通 */
#define MS_SIDE       0x32            /* MEMBER_STATUS: 1=光 2=闇 */
#define MS_JOB        0x34
#define MS_CANJOB     0xbc
#define MC_MEMBER     0x38            /* MenuClass: 選んでいるメンバー */
#define MC_JOBMODE    0x40
#define MC_JOBCUR     0x28            /* ジョブ画面のカーソル位置 */
#define MC_JOBNOW     0x74            /* 今のジョブの一覧上の位置 */
#define MC_JOBLIST    0x370           /* 一覧（ジョブ番号 int） */
#define MC_JOBN       0x3c0           /* 一覧の数 */
#define MC_TOUCHED    0xba8           /* MenuClass: このフレームに離したボタンの番号（MoveWidget が書く） */
#define OFF_CJL_FUNC  0x3f907cUL      /* MenuClass::CreateJobList(MEMBER_STATUS*) */
#define OFF_CJL_GOT   0x460388UL
#define OFF_JOBF_FUNC 0x3fcc5cUL      /* MenuClass::JobFunc() */
#define OFF_JOBF_GOT  0x460408UL
#define OFF_DJOB_FUNC 0x40a70cUL      /* MenuClass::DrawJob(MtxGraphics3D*) */
#define OFF_DJOB_GOT  0x4605f8UL
#define OFF_LCI_FUNC  0x3f3544UL      /* GameClass::LoadCharaImage(int, int, int, bool) */
#define OFF_LCI_GOT   0x45ea18UL
#define OFF_LICP_FUNC 0x37da74UL      /* LoadICPDataBuffer(bytearray&, int&) */
#define OFF_LICP_GOT  0x45dea0UL
#define OFF_RW_FUNC   0x3fae04UL      /* MenuClass::ResetWidget(bool, bool) */
#define OFF_RW_GOT    0x460390UL
#define MC_MENU       0x10            /* MenuClass: 今のメニュー */
#define MC_TOUCH      0x8a4           /* MenuClass: タッチ範囲の表 {id,x,y,w,h,flags} 0x18 バイト×32、id==0 で終端 */
#define JOB_TOUCH_ID  20000
/* ◀ ▶（2段目の左右の空き。マスは x 66〜574、2段目は y 219〜276） */
#define ARROW_L_CX    34
#define ARROW_R_CX    606
#define ARROW_CY      247
#define ARROW_TX_L    4
#define ARROW_TX_R    578
#define ARROW_TY      219
#define ARROW_TW      60
#define ARROW_TH      57

typedef void (*CJLFn)(void *mc, u8 *member);
typedef void (*JobFuncFn)(void *mc);
typedef void (*DrawJobFn)(void *mc, void *g);
typedef u8 *(*GetMemberFn)(void *game, int pos);
typedef void (*ResetWidgetFn)(void *mc, int a, int b);
typedef void (*ReserveSEFn)(void *snd, int id);
typedef void (*DrawStringFn)(void *game, const char *s, int x, int y, int size);
typedef void (*SetColorFn)(void *game, int rgba);
typedef int (*LoadCharaFn)(void *game, int slot, int model, int chara, int sub);
typedef u8 *(*LoadICPFn)(void *ba, int *fmt);
typedef int (*IsLearnJobFn)(void *game, int job);

static CJLFn g_origCJL;
static JobFuncFn g_origJobF;
static DrawJobFn g_origDJob;
static LoadCharaFn g_origLCI;
static LoadICPFn g_origLICP;
static GetMemberFn p_GetMember;
static ResetWidgetFn p_ResetWidget;
static ReserveSEFn p_ReserveSE;
static DrawStringFn p_DrawString;
static SetColorFn p_SetColor;
static IsLearnJobFn p_IsLearnJob;

static int g_jobPage;                 /* 0=1ページ目 1=2ページ目 */
static int g_jobToggling;             /* 切り替えのために自分で一覧を作り直している最中 */
static volatile int g_pageLatch;      /* L2 / R2 / Tab が押された（キー入力側で立てる） */
static int g_pageTrig;                /* このフレームに切り替え要求があるか */
static int g_jobMenuId = -1;          /* ジョブ画面のメニュー番号（一覧を作ったときに覚える） */
static int g_inJobF;                  /* JobFunc の中か */
static int g_wrapReq;                 /* 3段目の端の回り込み: -1=左端で左 1=右端で右（汎用カーソルが立てる） */
static ResetWidgetFn g_origRW;

static void *game_inst(void)
{
    uintptr_t p = RD(uintptr_t, g_base, GAME_PTR_GOT);
    if (!p) return 0;
    p = RD(uintptr_t, p, 0);
    if (!p) return 0;
    return (void *)RD(uintptr_t, p, 0);
}

/* 2ページ目を持てるメンバーか（ジョブを変えられる光／闇の戦士） */
static int page_member(const u8 *m)
{
    return m && (m[MS_CANJOB] & 1) && (m[MS_SIDE] == 1 || m[MS_SIDE] == 2);
}

static int job_side(void *game, int job)
{
    int n = RD(int, game, G_JOBCOUNT);
    u8 *jd = (u8 *)RDP(game, G_JOBDATA);
    if (!jd || job < 0 || job >= n) return 0;
    return jd[job * JD_SIZE + JD_SIDE];
}

/* その陣営の専用ジョブを1つ以上覚えているか */
static int side_has_jobs(void *game, int side)
{
    if (!p_IsLearnJob) return 0;
    int n = RD(int, game, G_JOBCOUNT);
    for (int j = 1; j < n && j < 64; j++)
        if (job_side(game, j) == side && p_IsLearnJob(game, j)) return 1;
    return 0;
}

/* 今表示していない側の陣営 */
static int hidden_side(const u8 *m)
{
    return g_jobPage ? m[MS_SIDE] : 3 - m[MS_SIDE];
}

/* 一覧のうち共通ジョブの数（＝3段目の先頭の位置） */
static int common_count(void *mc, void *game)
{
    int cnt = RD(int, mc, MC_JOBN), k = 0;
    for (int i = 0; i < cnt && i < 20; i++)
        if (job_side(game, RD(int, mc, MC_JOBLIST + i * 4)) == 3) k++;
    return k;
}

/* ジョブ画面の、選んでいるメンバー（2ページ目を持てるときだけ） */
static u8 *job_screen_member(void *mc, void *game)
{
    if (!game || !p_GetMember || RD(int, mc, MC_MENU) != g_jobMenuId) return 0;
    u8 *m = p_GetMember(game, RD(int, mc, MC_MEMBER));
    return page_member(m) ? m : 0;
}

/* ページを切り替えられるか（2ページ目から1ページ目へはいつでも戻れる） */
static int can_toggle(void *game, const u8 *m)
{
    return g_jobPage == 1 || side_has_jobs(game, 3 - m[MS_SIDE]);
}

/* ◀ ▶ を出すか */
static int arrows_on(void *mc, void *game)
{
    u8 *m = job_screen_member(mc, game);
    return m && can_toggle(game, m);
}

/* タッチ表の終わりに ◀ ▶ を足す（ResetWidget で表が作り直されるたび） */
static void add_arrow_touch(void *mc)
{
    u8 *t = (u8 *)mc + MC_TOUCH;
    int i = 0;
    while (i < 32 && *(int *)(t + i * 0x18)) {
        int id = *(int *)(t + i * 0x18);
        if (id == ARROW_ID_L || id == ARROW_ID_R) return;   /* もう入っている */
        i++;
    }
    if (i + 2 > 32) { LOGE("job arrows: touch table full"); return; }
    static const int ent[2][5] = {
        { ARROW_ID_L, ARROW_TX_L, ARROW_TY, ARROW_TW, ARROW_TH },
        { ARROW_ID_R, ARROW_TX_R, ARROW_TY, ARROW_TW, ARROW_TH },
    };
    for (int k = 0; k < 2; k++) {
        int *e = (int *)(t + (i + k) * 0x18);
        for (int f = 0; f < 5; f++) e[f] = ent[k][f];
        e[5] = 0;
    }
    if (i + 2 < 32) *(int *)(t + (i + 2) * 0x18) = 0;
}

static void hook_RW(void *mc, int a, int b)
{
    g_origRW(mc, a, b);
    void *game = game_inst();
    if (game && arrows_on(mc, game)) add_arrow_touch(mc);
}

/* 2ページ目：メンバーの陣営を反対にして作る（共通ジョブは陣営3なのでそのまま入る） */
static void build_page1(void *mc, u8 *m)
{
    u8 side = m[MS_SIDE];
    m[MS_SIDE] = (u8)(3 - side);
    g_origCJL(mc, m);
    m[MS_SIDE] = side;
}

static void hook_CJL(void *mc, u8 *m)
{
    void *game = game_inst();
    g_jobMenuId = RD(int, mc, MC_MENU);
    if (!game || !page_member(m)) { g_jobPage = 0; g_origCJL(mc, m); return; }
    if (!g_jobToggling) {
        /* ゲーム側からの作成（ジョブ画面に入った・キャラを切り替えた）：今のジョブがあるページを開く */
        g_jobPage = job_side(game, m[MS_JOB]) == 3 - m[MS_SIDE];
    }
    if (g_jobPage == 1 && !side_has_jobs(game, 3 - m[MS_SIDE])) g_jobPage = 0;   /* もう片方を1つも覚えていない */
    if (g_jobPage == 0) { g_origCJL(mc, m); return; }
    build_page1(mc, m);
}

/* 汎用カーソルから：3段目の端で左右を押した。回り込めるなら要求を立てて 1 を返す */
static int job_wrap(void *mc, int k, int dir)
{
    void *game = game_inst();
    u8 *m = job_screen_member(mc, game);
    if (!m || g_wrapReq) return 0;
    int mode = RD(int, mc, MC_JOBMODE);
    if (mode != 0 && mode != 1) return 0;
    int cnt = RD(int, mc, MC_JOBN), rs = common_count(mc, game);
    if (cnt <= rs || k < rs || k >= cnt) return 0;            /* 3段目のマスではない */
    if (dir < 0 ? k != rs : k != cnt - 1) return 0;           /* 端ではない */
    if (!side_has_jobs(game, hidden_side(m))) return 0;
    g_wrapReq = dir;
    return 1;
}

static void hook_JobF(void *mc)
{
    int req = g_pageTrig;
    g_pageTrig = 0;
    int wrap = g_wrapReq;
    g_wrapReq = 0;
    int touched = RD(int, mc, MC_TOUCHED);
    if (touched == ARROW_ID_L || touched == ARROW_ID_R) {
        *(volatile int *)((uintptr_t)mc + MC_TOUCHED) = 0;   /* ゲーム側には見せない */
        req = 1;
    }
    if ((req || wrap) && p_GetMember && p_ResetWidget) {
        void *game = game_inst();
        int mode = RD(int, mc, MC_JOBMODE);
        u8 *m = game ? p_GetMember(game, RD(int, mc, MC_MEMBER)) : 0;
        if (game && (mode == 0 || mode == 1) && page_member(m) && can_toggle(game, m) &&
            (!wrap || side_has_jobs(game, hidden_side(m)))) {
            int before = g_jobPage, old = RD(int, mc, MC_JOBCUR);
            g_jobPage ^= 1;
            g_jobToggling = 1;
            hook_CJL(mc, m);
            g_jobToggling = 0;
            if (g_jobPage != before) {
                int cnt = RD(int, mc, MC_JOBN), rs = common_count(mc, game);
                int cur;
                if (wrap < 0) cur = cnt - 1;                  /* 左端で左：反対側の右端 */
                else if (wrap > 0) cur = rs < cnt ? rs : cnt - 1;   /* 右端で右：反対側の左端 */
                else cur = old < cnt ? old : cnt - 1;         /* L2/R2・◀▶：同じ位置（なければ右端） */
                if (cur < 0) cur = 0;
                *(volatile int *)((uintptr_t)mc + MC_JOBCUR) = cur;
                if (g_origRW) hook_RW(mc, 1, 1); else p_ResetWidget(mc, 1, 1);
                g_jobWant = JOB_TOUCH_ID + cur;
                if (p_ReserveSE) p_ReserveSE((void *)((uintptr_t)game + 0xb0), 1);
            }
        }
    }
    g_inJobF = 1;
    g_origJobF(mc);
    g_inJobF = 0;
}

/* v4.02: ナハトの左右と同じ矢印アイコン（MenuClass::DrawLR と同じ呼び方：左 8／右 10、+1 が点滅の相、
   GameClass::DrawIcon(g, icon, x, y, 9, 0, 255, -256)。アイコンは 16×16） */
typedef void (*DrawIconFn)(void *game, void *g, int icon, float x, float y, int a, int b, int alpha, int col);
static DrawIconFn p_DrawIcon;
static void draw_arrow(void *mc, void *game, int cx, int cy, int dir)
{
    if (!p_DrawIcon || !game) return;
    void *g = RDP(game, 0x172f8);
    if (!g) return;
    int ph = (RD(int, mc, 0x18) & 0xc) == 0;
    p_DrawIcon(game, g, (dir < 0 ? 8 : 10) | ph, (float)(cx - 8), (float)(cy - 8), 9, 0, 255, -256);
}

/* ページの目印。画面下の中央（表示切替と戻るの間） */
#define PAGE_MARK_X 320   /* 文字列の中心 */
#define PAGE_MARK_Y 352
static void hook_DJob(void *mc, void *g)
{
    g_origDJob(mc, g);
    if (!p_GetMember || !p_DrawString || !p_SetColor) return;
    void *game = game_inst();
    if (!game) return;
    u8 *m = p_GetMember(game, RD(int, mc, MC_MEMBER));
    if (!page_member(m)) return;
    p_SetColor(game, -1);
    p_DrawString(game, g_jobPage ? "< 2/2 >  L2/R2" : "< 1/2 >  L2/R2", PAGE_MARK_X, PAGE_MARK_Y, 18);
    p_SetColor(game, -1);
    if (can_toggle(game, m)) {
        draw_arrow(mc, game, ARROW_L_CX, ARROW_CY, -1);
        draw_arrow(mc, game, ARROW_R_CX, ARROW_CY, 1);
    }
}

/* ---------- 編成画面でのゲスト入れ替え（v2.20） ----------
   編成画面でカーソルの枠に合わせて R2（キーボードは ]）を押すと次のキャラ、L2（[）で前のキャラに替える。
   回る順番はメイン8人と控えのゲスト11人。今パーティにいる人は飛ばす。空き枠も回る順番に入る。
   枠への出し入れはシナリオと同じ FieldClass::SetPartyMember を使う（同じ人の二重登録を断り、
   先頭が替わればフィールドで動かすキャラも替える）。替えたあと、枠の絵と表示の部品を作り直す。
   入れ替え元を選んでいる途中（+0x40 != 0）は何もしない。 */
#define OFF_FORM_FUNC 0x400c74UL      /* MenuClass::FormationFunc() */
#define OFF_FORM_GOT  0x460438UL
#define G_FIELD       0x350d0         /* GameClass: FieldClass */
#define MC_FORMCUR    0x24            /* 編成画面のカーソル（0〜7） */
#define MC_FORMSEL    0x40            /* 0=選んでいない 1=入れ替え元を選んだ */
#define MC_FORMFLAG   0x5f0
#define MS_ROW        0x134           /* MEMBER_STATUS: 1=後列 */
#define BENCH_EMPTY   (-1)

typedef void (*FormFn)(void *mc);
typedef void (*SetPartyMemberFn)(void *field, int slot, int id, int row);
typedef int (*GetMemberIDFn)(void *game, int slot);
typedef int *(*GetMemberIDListFn)(void *game);
typedef void (*ReadImgAllFn)(void *mc, int *ids, int n, int flag);
typedef void (*InitItemCtrlFn)(void *mc, int kind);

static FormFn g_origForm;
static SetPartyMemberFn p_SetPartyMember;
static GetMemberIDFn p_GetMemberID;
static GetMemberIDListFn p_GetMemberIDList;
static ReadImgAllFn p_ReadImgAll;
static InitItemCtrlFn p_InitItemCtrl;
static volatile int g_benchLatch;     /* +1=R2 / ] , -1=L2 / [ */
static int g_benchTrig;

/* 回る順番: メイン8人 → 控え（エルゴ, バルバラ, フレイ, エドアルド(人), グラム, アルジイ, マトーヤ,
   ガウェイン, ジンナイ(片腕), ソピアー, 仮面の男） → 空き */
static const signed char k_benchOrder[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 19, 12, 13, 14, 15, 16, 17, 18, 21, BENCH_EMPTY };
static int styx_ready(void *game);     /* v3.40t: スティクス（21）はできてから */
#define BENCH_N ((int)sizeof(k_benchOrder))

static int bench_in_party(void *game, int id, int except)
{
    for (int s = 0; s < 8; s++)
        if (s != except && p_GetMemberID(game, s) == id) return 1;
    return 0;
}

/* v4.02: 編成の20人並び（下の「編成の一覧を仲間全員に」）。タップの横取りと控えの行の点滅 */
#define MC_FORMPICK   0x28            /* 入れ替え元に選んだ枠（v4.02: 8 以上は控えの行） */
#define MC_FORMWIDGET 0xc10           /* 編成の一覧の部品 */
static int form_tap(void *mc);
static void form_after(void *mc);
static void hook_Form(void *mc)
{
    g_benchTrig = 0;                  /* v4.02: L2/R2 で回す入れ替えはやめた */
    form_tap(mc);
    g_origForm(mc);
    form_after(mc);
}

/* ---------- ジョブポイントの調整（v2.20b） ----------
   ジョブが17個になり、物語で配られるジョブポイントの合計では端数が出る。
   第4章で導師（光）・魔人（闇）の力を受け継ぐ場面の「1人6ポイント」を8ポイントにする。
   6ポイントの配布は物語中この2回だけ（スクリプトの命令 0x73 の1人ずつ配る形）。 */
#define OFF_ADDJP_FUNC 0x3e7024UL     /* GameClass::AddJp(int member, int jp) */
#define OFF_ADDJP_GOT  0x45fc48UL
#define RA_SCRIPT_JP1  0x3d4728UL     /* MoveScript の1人ずつ配る命令から呼ばれたときの戻り先 */
typedef int (*AddJpFn)(void *game, int member, int jp);
static AddJpFn g_origAddJp;
static int hook_AddJp(void *game, int member, int jp)
{
    uintptr_t ra;
    __asm__ volatile("mov %0, x30" : "=r"(ra));
    if (ra - g_base == RA_SCRIPT_JP1 && jp == 6) {
        jp = 8;
    }
    return g_origAddJp(game, member, jp);
}

/* ---------- ゲストの強化（v2.21） ----------
   光と闇の戦士8人が1つのパーティにそろったとき（終章 mpk11_2 の「では、行くか」で闇の4人が加わる場面。
   第4章は光と闇を同じ枠に入れ替えるだけで8人はそろわない）から、
   控えのゲストに技とアビリティを足す。物語の途中（8人がそろう前）は元の性能のまま。
   キャラのデータに直接書くので、強化した状態はセーブにも残る。
   一度強化したかどうかは「バルバラがドラゴンソウルを覚えているか」で見分ける。 */
#define G_CHAPTER     0x19fe0         /* GameClass: 章の番号（mpk%d_%d の最初の数） */
#define G_MEMBERS     0x1a180         /* GameClass: キャラのデータ 0x4c4 バイトずつ */
#define G_PASSIVE_TBL 0x34410         /* GameClass: アビリティのデータ表（0x30 バイトずつ） */
#define MS_MAGIC      0x5c            /* MEMBER_STATUS: 覚えている技（256個分の印） */
#define MS_PASSIVE    0x9c            /* MEMBER_STATUS: 覚えているアビリティ（128個分の印） */
#define PROTO_MEM     102             /* プロトメモリーズ */
#define PROTO_ADD_OFF 0x12            /* 8つの能力値に足す量 */
#define MARK_MEMBER   9               /* バルバラ */
#define MARK_MAGIC    108             /* ドラゴンソウル */

typedef void (*InitSlotFn)(void *game, u8 *member, int b);
typedef void (*SetJobStatusFn)(void *game, u8 *member, int b);
static InitSlotFn p_InitSlot;
static SetJobStatusFn p_SetJobStatus;

typedef struct { signed char id; unsigned char magic[24]; unsigned char addp[3]; unsigned char delp[2]; unsigned char wb7; } GuestUp;
static const GuestUp k_guestUp[] = {
    /* エルゴ: 白黒魔ＬＶ８ 魔法剣 連続魔（ジョブの一覧）。白魔法・黒魔法はＬＶ８のものまで全部（下の wb7）。
       魔法剣の技は元の魔法を覚えていれば使える（boot_data の対応表）ので足さない。
       v4.03: 覚えている印は ＬＶ８（56）にし、ＬＶ３（51）と v4.02 までの ＬＶ７（55）は外す */
    { 8,  {137, 138, 139, 140, 141, 142, 143, 144, 145, 146}, {56}, {51, 55}, 1 },   /* v4.04: 魔法剣の10個も覚えさせる */
    /* バルバラ: 竜剣 咆哮 ホーリーブレス ドラゴンソウル */
    { 9,  {105, 106, 107, 108},        {0},          {0},   0 },
    /* フレイ: 素早く狙う 急所を狙う みだれうち / 援護 Ｗアイテム */
    { 10, {151, 152, 153},             {74, 75},     {0},   0 },
    /* エドアルド（人）: レクイエム 素早さの歌 天使の歌 破滅の歌 */
    { 19, {122, 123, 124, 125},        {0},          {0},   0 },
    /* グラム: 全力斬り ダークソード / 後列斬り ＨＰ吸収 / 盟約の武具を外す */
    { 12, {99, 100},                   {77, 79},     {105}, 0 },
    /* マトーヤ: ワンダーステップ トランス */
    { 14, {133, 134},                  {0},          {0},   0 },
    /* ガウェイン: ホーリーソード / 両手持ち ＭＰ変換 */
    { 15, {225},                       {69, 68},     {0},   0 },
    /* ジンナイ（片腕）: 忍殺。投げるはジョブの一覧に元からあるので、v4.02 までに足した印（82）は外す */
    { 16, {232},                       {0},          {82},  0 },
    /* ソピアー: ホーリー アレイズ / バ系8つ（属性耐性はジョブの一覧） */
    { 17, {23, 24, 234, 235, 236, 237, 238, 239, 240, 241}, {0}, {0}, 0 },
    /* 仮面の男: メテオ アルテマ / 白魔法ＬＶ６ バーサク リジェネ ヘイスト / エン系8つ（ジョブの一覧は黄昏の章からのジョブ19）。
       v4.02 までの印の書き換え（黒魔法ＬＶ６→ＬＶ８）は元に戻す */
    { 18, {47, 48, 16, 17, 18, 243, 244, 245, 246, 247, 248, 249, 250}, {34}, {36}, 0 },
};
#define ALGY 13

/* ゲストのアビリティは、覚えている印ではなくジョブに付いている一覧（最大6個）で決まる。
   強化後はゲスト専用ジョブの一覧を書き換え、強化前のセーブでは元に戻す */
#define JD_INNATE 0x25
static u8 *member_at(void *game, int id);
static void refresh_member(void *game, u8 *m);
typedef struct { unsigned char job, member, orig[6], up[6]; } GuestJob;
static const GuestJob k_guestJob[] = {
    { 20, 8,  {51},             {56, 59, 60} },         /* 赤魔道士（エルゴ）: 白黒魔ＬＶ３ → 白黒魔ＬＶ８ 魔法剣 連続魔（v4.03） */
    { 22, 10, {72, 73, 103},    {72, 73, 103, 74, 75} },/* レンジャー（フレイ）: ＋援護 Ｗアイテム */
    { 24, 12, {76, 78, 105},    {76, 78, 77, 79} },     /* 暗黒騎士（グラム）: 盟約の武具 → 後列斬り ＨＰ吸収 */
    { 27, 15, {66, 67, 21},     {66, 67, 21, 69, 68} }, /* ナイト（ガウェイン）: ＋両手持ち ＭＰ変換 */
    { 29, 17, {26, 34, 70},     {26, 34, 71, 70} },     /* 導師（ソピアー）: 白魔法ＬＶ８ 黒魔法ＬＶ６ ＋属性耐性 祝福（v4.03） */
    { 19, 18, {36, 23, 84},     {36, 24, 85, 84} },     /* ？？？？（仮面の男、黄昏の章から）: 黒魔法ＬＶ８ 白魔法ＬＶ５→６ ＋属性強化 精神波（v4.03） */
};
/* v4.04: ガウェインはナイト編から（強化の前から）全部位を替えられる */
static const struct { unsigned char member, clear; } k_eqUnlockAlways[] = { { 15, 0x1f } };
/* v4.03: 装備を替えられない部位の印（MEMBER_STATUS +0x58、bit=部位 0右手 1左手 2頭 3体 4装飾。chara_set.dat で付く）。
   強化後はグラムとガウェインの印を外す。ジンナイ（片腕）の右手はそのまま */
#define MS_EQLOCK 0x58
static const struct { unsigned char member, clear; } k_eqUnlock[] = { { 12, 0x0f } };
static int same6(const u8 *a, const unsigned char *b) { for (int i = 0; i < 6; i++) if (a[i] != b[i]) return 0; return 1; }
static void guest_jobs(void *game, int on)
{
    u8 *jd = (u8 *)RDP(game, G_JOBDATA);
    int n = RD(int, game, G_JOBCOUNT);
    if (!jd) return;
    for (unsigned k = 0; k < sizeof(k_guestJob) / sizeof(k_guestJob[0]); k++) {
        const GuestJob *g = &k_guestJob[k];
        if (g->job >= n) continue;
        u8 *a = jd + g->job * JD_SIZE + JD_INNATE;
        const unsigned char *want = on ? g->up : g->orig;
        if (same6(a, want)) continue;
        if (!same6(a, g->orig) && !same6(a, g->up)) {
            static unsigned warned;
            if (!(warned & (1u << k))) { warned |= 1u << k; LOGE("guest: job %d list unexpected, skipped", g->job); }
            continue;
        }
        for (int i = 0; i < 6; i++) a[i] = want[i];
        refresh_member(game, member_at(game, g->member));
    }
}

static u8 *member_at(void *game, int id) { return (u8 *)((uintptr_t)game + G_MEMBERS + (uintptr_t)id * 0x4c4); }
static int bit_get(const u8 *b, int i) { return (b[i >> 3] >> (i & 7)) & 1; }
static int bit_set(u8 *b, int i, int on)
{
    int was = bit_get(b, i);
    if (on) b[i >> 3] |= (u8)(1 << (i & 7)); else b[i >> 3] &= (u8)~(1 << (i & 7));
    return was != on;
}

static int heroes_together(void *game)
{
    if (!p_GetMemberID) return 0;
    int seen = 0;
    for (int s = 0; s < 8; s++) {
        int id = p_GetMemberID(game, s);
        if (id >= 0 && id < 8) seen |= 1 << id;
    }
    return seen == 0xff;
}

static void refresh_member(void *game, u8 *m)
{
    p_InitSlot(game, m, 0);
    p_SetJobStatus(game, m, 0);
}

static int g_guestOn;                 /* v3.40t: guest_tick が決めた「強化してよい」（スティクスも同じ条件） */
static void guest_tick(void)
{
    if (!p_InitSlot || !p_SetJobStatus) return;
    void *game = game_inst();
    if (!game) return;
    int chap = RD(int, game, G_CHAPTER);
    int done = bit_get(member_at(game, MARK_MEMBER) + MS_MAGIC, MARK_MAGIC);
    static int lastChap = -999, lastTogether = -1;
    int tog = heroes_together(game);
    if (chap != lastChap || tog != lastTogether) {   /* 章や8人そろいが変わったときだけ1行 */
        lastChap = chap; lastTogether = tog;
    }
    int on = done || ((chap == 11 || chap == 12 || chap == 14 || chap == 15) && tog);

    /* プロトメモリーズ: 強化後は＋59、それまでは元の＋39 */
    u8 *ptbl = (u8 *)RDP(game, G_PASSIVE_TBL);
    if (ptbl) {
        u8 *pv = ptbl + PROTO_MEM * 0x30 + PROTO_ADD_OFF;
        u8 want = on ? 59 : 39;
        if ((*pv == 39 || *pv == 59) && *pv != want) {
            *pv = want;
            refresh_member(game, member_at(game, ALGY));
        }
    }
    guest_jobs(game, on);
    g_guestOn = on;
    for (unsigned k = 0; k < sizeof(k_eqUnlockAlways) / sizeof(k_eqUnlockAlways[0]); k++) {
        volatile unsigned *lk = (volatile unsigned *)(member_at(game, k_eqUnlockAlways[k].member) + MS_EQLOCK);
        if (*lk & k_eqUnlockAlways[k].clear) {
            LOGI("guest: member %d equip lock %x -> %x", k_eqUnlockAlways[k].member, *lk, *lk & ~(unsigned)k_eqUnlockAlways[k].clear);
            *lk &= ~(unsigned)k_eqUnlockAlways[k].clear;
        }
    }
    if (!on) return;
    for (unsigned k = 0; k < sizeof(k_eqUnlock) / sizeof(k_eqUnlock[0]); k++) {
        volatile unsigned *lk = (volatile unsigned *)(member_at(game, k_eqUnlock[k].member) + MS_EQLOCK);
        if (*lk & k_eqUnlock[k].clear) {
            LOGI("guest: member %d equip lock %x -> %x", k_eqUnlock[k].member, *lk, *lk & ~(unsigned)k_eqUnlock[k].clear);
            *lk &= ~(unsigned)k_eqUnlock[k].clear;
        }
    }

    for (unsigned g = 0; g < sizeof(k_guestUp) / sizeof(k_guestUp[0]); g++) {
        const GuestUp *u = &k_guestUp[g];
        u8 *m = member_at(game, u->id);
        int ch = 0;
        for (int i = 0; i < 24 && u->magic[i]; i++) ch += bit_set(m + MS_MAGIC, u->magic[i], 1);
        if (u->wb7) {
            for (int i = 1; i <= 24; i++) ch += bit_set(m + MS_MAGIC, i, 1);   /* 白魔法 ＬＶ１〜８ */
            for (int i = 25; i <= 48; i++) ch += bit_set(m + MS_MAGIC, i, 1);  /* 黒魔法 ＬＶ１〜８ */
        }
        for (int i = 0; i < 3 && u->addp[i]; i++) ch += bit_set(m + MS_PASSIVE, u->addp[i], 1);
        for (int i = 0; i < 2 && u->delp[i]; i++) ch += bit_set(m + MS_PASSIVE, u->delp[i], 0);
        if (ch) {
            refresh_member(game, m);
        }
    }
    /* v4.04: エルゴの確かめ用（ジョブ、固有欄 +0x2f4/+0x2f6、魔法剣の印と使える印 +0x2b4、魔法剣の段の印 +0x28c+12*2）。変わったときだけ */
    {
        u8 *e = member_at(game, 8);
        int fx = RD(short, e, 0x2f4);
        unsigned sig = (unsigned)e[MS_JOB] * 131u + (unsigned)fx;
        for (int i = 0; i < 6; i++) sig = sig * 31u + (unsigned)RD(unsigned short, e, 0x2f6 + 2 * i);
        int learn = bit_get(e + MS_MAGIC, 137), use = (e[0x2b4 + 137 / 8] >> (137 % 8)) & 1;
        int lvm = RD(short, e, 0x28c + 12 * 2);
        sig = sig * 31u + (unsigned)(learn * 2 + use) + (unsigned)lvm * 7u;
        static unsigned lastSig;
        if (sig != lastSig) {
            lastSig = sig;
            LOGI("guest: ergo job %d fixed %d [%d %d %d %d %d %d] mgsword learn %d use %d lvmask %x",
                 e[MS_JOB], fx, RD(short, e, 0x2f6), RD(short, e, 0x2f8), RD(short, e, 0x2fa), RD(short, e, 0x2fc),
                 RD(short, e, 0x2fe), RD(short, e, 0x300), learn, use, lvm & 0xffff);
        }
    }
}

/* ---------- フィールドで歩くキャラの切り替え（v2.22） ----------
   L/R（キーボードは Q/E）で、フィールドを歩くキャラをパーティの並び順に前後へ切り替える。
   見た目だけで、並び順・戦闘に出る5人・物語の判定には関わらない。
   選んだキャラは覚えておき、ゲームが歩くキャラを決め直す（マップ移動・メニューを閉じた）ときも、
   まだパーティにいればそのキャラのままにする */
#define OFF_MOVEPL_FUNC 0x3b4eb8UL     /* FieldClass::MovePlayer() */
#define OFF_MOVEPL_GOT  0x45f1e0UL
#define OFF_SETCTRL_FUNC 0x3bd944UL    /* FieldClass::SetControlMember(int) */
#define OFF_SETCTRL_GOT  0x45f128UL
typedef void (*FieldFn)(void *field);
typedef void (*SetCtrlFn)(void *field, int pos);
typedef int (*IsCtrlChgFn)(void *field, int pos);
static FieldFn g_origMovePl;
static SetCtrlFn g_origSetCtrl;
static IsCtrlChgFn p_IsCtrlChg;
static int g_leaderId = -1;       /* 選んだキャラ（-1 なら選んでいない） */
static int g_leaderPos = -1;      /* 今歩いているキャラの枠 */

static int slot_of(void *game, int id)
{
    for (int s = 0; s < 8; s++) if (p_GetMemberID(game, s) == id) return s;
    return -1;
}

static void hook_SetCtrl(void *field, int pos)
{
    void *game = game_inst();
    if (game && p_GetMemberID && g_leaderId >= 0) {
        int s = slot_of(game, g_leaderId);
        if (s >= 0) pos = s;
        else g_leaderId = -1;     /* パーティから外れた：元の決まりに戻す */
    }
    g_leaderPos = pos;
    g_origSetCtrl(field, pos);
}

static void hook_MovePl(void *field)
{
    g_btlBtn = 0;   /* フィールドを歩いている：戦闘ではない */
    int dir = (g_trig & K_NEXT) ? 1 : (g_trig & K_PREV) ? -1 : 0;
    void *game = dir ? game_inst() : 0;
    if (game && p_GetMemberID && p_IsCtrlChg && p_IsCtrlChg(field, 0)) {
        int cur = g_leaderPos >= 0 ? g_leaderPos : 0;
        for (int k = 1; k <= 8; k++) {
            int s = ((cur + dir * k) % 8 + 8) % 8;
            int id = p_GetMemberID(game, s);
            if (id < 0) continue;
            if (s != cur) {
                g_leaderId = id;
                g_leaderPos = s;
                g_origSetCtrl(field, s);
                if (p_ReserveSE) p_ReserveSE((void *)((uintptr_t)game + 0xb0), 1);
            }
            break;
        }
    }
    g_origMovePl(field);
}

/* ---------- 光専用・闇専用ジョブの見た目（色替え） ----------
   本来ない組み合わせ（闇の戦士の竜騎士など）を読むとき、元にするキャラの絵を読み、
   絵をほどいた直後にパレット（256色）だけを差し替える。絵の形はそのまま。
   ほどいたデータはテクスチャが保持し、作り直し（ReCreate）にも同じものが使われる */
#include "recolor_tab.h"
static const char *g_recolorPal;

/* v3.40t: スティクス（fldchr120_0）の戦闘の絵。
   ・本体（256×512）：パレットが fldchr120_0 と同じなら、空の 150〜299 行目に B1〜B12 を書く（元は何も描かれていない）
   ・2枚目（_1、256×64）：fldchr120_0_1 は無いので、セーラの fldchr16_0_1 を読ませ、パレットと絵を丸ごと差し替える */
#include "styx_tab.h"
#define FLD16_1_PAL_FNV 0x48e74d7eu
static int g_styxSub;
static unsigned pal_fnv(const u8 *r)
{
    unsigned h = 0x811c9dc5u;
    for (int i = 0; i < 1024; i++) h = (h ^ r[4 + i]) * 0x01000193u;
    return h;
}

static int hook_LCI(void *game, int slot, int model, int chara, int sub)
{
    if (model == 120) {
        static int n;
        if (n++ < 30) LOGI("styx: LoadCharaImage from %x slot %d chara %d sub %d",
                           (unsigned)((uintptr_t)__builtin_return_address(0) - g_base), slot, chara, sub & 1);
    }
    if (model == 120 && chara == 0 && sub) {
        g_styxSub = 1;
        int r = g_origLCI(game, slot, 16, 0, sub);
        if (g_styxSub) LOGE("styx: sub image not decoded (slot %d)", slot);
        g_styxSub = 0;
        LOGI("styx: sub image via fldchr16_0_1 (slot %d) -> %d", slot, r);
        return r;
    }
    for (unsigned i = 0; i < sizeof(k_recolor) / sizeof(k_recolor[0]); i++) {
        if (k_recolor[i].model == model && k_recolor[i].chara == chara) {
            g_recolorPal = k_recolor[i].pal;
            int r = g_origLCI(game, slot, model, k_recolor[i].base, sub);
            g_recolorPal = 0;
            return r;
        }
    }
    return g_origLCI(game, slot, model, chara, sub);
}

#include "jump_tab.h"
/* 竜騎士編の洞窟のチップ画像（mc9_0）は、スマホ版でジャンプ台（0x36 番）の絵が消されている。
   読み込んだ直後に、同じ画像の砂の上に宝石を描いたものを書き戻す */
static void fix_jump_tile(u8 *r)
{
    unsigned h = 0x811c9dc5u;
    for (int i = 0; i < 1024; i++) h = (h ^ r[4 + i]) * 0x01000193u;
    if (h != MC9_PAL_FNV) return;
    u8 *idx = r + 4 + 1024;
    const int x0 = (0x36 % 16) * 32, y0 = (0x36 / 16) * 32;
    for (int y = 0; y < 32; y++)                       /* 空のままのときだけ書く */
        for (int x = 0; x < 32; x++)
            if (r[4 + idx[(y0 + y) * MC9_W + x0 + x] * 4 + 3] != 0) return;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            idx[(y0 + y) * MC9_W + x0 + x] = jump_tile_idx[y * 32 + x];
}

static u8 *hook_LICP(void *ba, int *fmt)
{
    u8 *r = g_origLICP(ba, fmt);
    const char *pal = g_recolorPal;
    if (r && g_styxSub) {
        g_styxSub = 0;
        unsigned h = pal_fnv(r);
        if (h == FLD16_1_PAL_FNV) {
            for (int i = 0; i < 1024; i++) r[4 + i] = (u8)styx_pal[i];
            for (int i = 0; i < 256 * STYX_EXT_H; i++) r[4 + 1024 + i] = styx_ext[i];
            LOGI("styx: sub image replaced");
        } else LOGE("styx: sub image palette %08x, not replaced", h);
    } else if (r && !pal && pal_fnv(r) == STYX_PAL_FNV) {
        u8 *idx = r + 4 + 1024 + STYX_ROW0 * 256;
        int blank = 1;
        for (int i = 0; i < 256 * STYX_ROWS; i++) if (idx[i] != 0) { blank = 0; break; }
        if (blank) {
            for (int i = 0; i < 256 * STYX_ROWS; i++) idx[i] = styx_rows[i];
            LOGI("styx: battle rows written");
        } else LOGI("styx: rows not blank, kept");
    } else if (r && pal) {
        g_recolorPal = 0;
        for (int i = 0; i < 1024; i++) r[4 + i] = (u8)pal[i];
    } else if (r) {
        fix_jump_tile(r);
    }
    return r;
}

/* ---------- 戦闘の R（前のキャラ） ----------
   ゲームの L は「今のキャラを行動待ちの列の最後へ回し、先頭のキャラのコマンドを開く」。
   R は列の最後のキャラを先頭へ移してから L の処理を動かし、最後に入った今のキャラを先頭へ移す */
#define OFF_BTLMENU_FUNC 0x3861d8UL   /* BattleClass::BattleMenuExec() */
#define OFF_BTLMENU_GOT  0x45e338UL
#define RA_BTL_PREV      0x3862fcUL   /* BattleMenuExec が L（0x82）を調べる所 */
#define BQ_HEAD 0x7fe8
#define BQ_TAIL 0x7fec
#define BQ_LIST 0x7ff0
typedef void (*BtlMenuFn)(void *battle);
static BtlMenuFn g_origBtlMenu;
static int g_rForce, g_rUsed;

static void bq_last_to_front(void *b)
{
    int h = RD(int, b, BQ_HEAD), t = RD(int, b, BQ_TAIL);
    int t1 = (t + 12) % 13, h1 = (h + 12) % 13;
    uintptr_t e = RD(uintptr_t, b, BQ_LIST + t1 * 8);
    *(volatile uintptr_t *)((uintptr_t)b + BQ_LIST + t1 * 8) = 0;
    *(volatile int *)((uintptr_t)b + BQ_TAIL) = t1;
    *(volatile uintptr_t *)((uintptr_t)b + BQ_LIST + h1 * 8) = e;
    *(volatile int *)((uintptr_t)b + BQ_HEAD) = h1;
}

static void bq_first_to_back(void *b)
{
    int h = RD(int, b, BQ_HEAD), t = RD(int, b, BQ_TAIL);
    uintptr_t e = RD(uintptr_t, b, BQ_LIST + h * 8);
    *(volatile uintptr_t *)((uintptr_t)b + BQ_LIST + h * 8) = 0;
    *(volatile int *)((uintptr_t)b + BQ_HEAD) = (h + 1) % 13;
    *(volatile uintptr_t *)((uintptr_t)b + BQ_LIST + t * 8) = e;
    *(volatile int *)((uintptr_t)b + BQ_TAIL) = (t + 1) % 13;
}

static void hook_BtlMenu(void *b)
{
    int pre = 0;
    uintptr_t oldCur = 0;
    if (g_fireR > 0) {
        int h = RD(int, b, BQ_HEAD), t = RD(int, b, BQ_TAIL);
        if (RD(u8, b, 0x6e34) && RD(int, b, 0x6e18) >= 1 &&
            h >= 0 && h < 13 && t >= 0 && t < 13 && h != t) {
            g_fireR = 0;
            oldCur = RD(uintptr_t, b, 0x6e20);
            bq_last_to_front(b);
            pre = 1;
            g_rForce = 1;
            g_rUsed = 0;
        }
    }
    g_origBtlMenu(b);
    if (!pre) return;
    g_rForce = 0;
    if (!g_rUsed) {
        bq_first_to_back(b);                   /* L の処理まで進まなかった：元に戻す */
        return;
    }
    int t = RD(int, b, BQ_TAIL);
    if (RD(uintptr_t, b, BQ_LIST + ((t + 12) % 13) * 8) == oldCur)
        bq_last_to_front(b);
}

/* ---------- 1.0-pre3: 本当のワイド表示 ----------
   ゲームは MainScene::SetViewPort で 640x384 を縦横同じ倍率 s で真ん中に置き、Set2DModeMatrix(640,384) で描く。
   横長の画面では：
   ① 描く範囲（glViewport）は窓いっぱいにする（hook_ViewPort。縦長の画面は元のまま）
   ② 640x384 の画面は ortho(-e, 640+e, 384, 0) で描く（e = 左右の余白を論理座標で）→ 縦横同じ倍率のまま真ん中に。
      文字（DrawString は論理座標で中央寄せしてから画素に直す）も絵もゆがまない
   ③ MainScene::SetViewPort の後で、タッチの換算（+0x674 倍率、+0x67c ずれ）をこの置き方に合わせ、
      App の絵の補正（+0x188/+0x18c、引き伸ばしの打ち消し）を 1 にする
   ④ フィールドはゲームが画面の比率から横幅を出す（W/s）ので、そのまま広く描かせる。遠景（DrawFar）だけは幅 640 の絵なので、
      横幅 640 の座標で描いて画面いっぱいに伸ばす
   ⑤ 640 の幅いっぱいに描く絵と塗り（戦闘の背景、メニューの背景、暗転など。左端 x<=0 かつ右端 >=640）は、
      真ん中を軸に横へ (640+2e)/640 倍に伸ばして左右の余白を埋める */
#define OFF_VIEWPORT_FUNC 0x370f3cUL   /* MtxGraphics3D::SetViewPort(int,int,int,int) */
#define OFF_VIEWPORT_GOT  0x45d8c0UL
#define OFF_S2DM_FUNC     0x371b98UL   /* MtxGraphics3D::Set2DModeMatrix(float,float) */
#define OFF_S2DM_GOT      0x45d960UL
#define OFF_MSVP_FUNC     0x3f6680UL   /* MainScene::SetViewPort(MtxGraphics3D&) */
#define OFF_MSVP_GOT      0x460360UL
#define OFF_DIEX_FUNC     0x373aa4UL   /* MtxGraphics3D::DrawImageEx(MtxTexture&, 8 float, int, int, int)（DrawImage もここを通る） */
#define OFF_DIEX_GOT      0x45d3a0UL
#define OFF_FILL4_FUNC    0x372c4cUL   /* MtxGraphics3D::FillRect(int,int,int,int) */
#define OFF_FILL4_GOT     0x45d590UL
#define OFF_FILL8_FUNC    0x372d44UL   /* MtxGraphics3D::FillRect(int,int,int,int,int,int,int,int) */
#define OFF_FILL8_GOT     0x45d9f0UL
#define OFF_DRAWFAR_FUNC  0x3c97dcUL   /* FieldClass::DrawFar(MtxGraphics3D*) */
#define OFF_DRAWFAR_GOT   0x45f7a8UL
#define OFF_FSET2D        0x3b5a58UL   /* FieldClass::Set2DMode(MtxGraphics3D*)（直に呼ぶ） */
#define OFF_SETMMODE      0x370c98UL   /* MtxGraphics3D::SetMatrixMode(int) */
#define OFF_LOADIDENT     0x370cd4UL   /* MtxGraphics3D::LoadIdentiry() */
#define OFF_GORTHO        0x370d04UL   /* MtxGraphics3D::Ortho(l,r,b,t,n,f)（今の行列に掛けて読み込む） */
#define APP_PTR_GOT       0x45ca58UL   /* App のインスタンスへ（2回たどる） */
#define APP_IMGFIX_X      0x188        /* App: 絵の横の補正（min/sx） */
#define APP_IMGFIX_Y      0x18c
#define GFX_VP            0x4c0        /* MtxGraphics3D: 描く範囲 x, y, w, h（float） */
#define GFX_PROJ          0x5c         /* MtxGraphics3D: 射影の行列（float 16、列優先） */
#define FIELD_W           0xc          /* FieldClass: フィールドの横幅（float） */
#define WIDE_HOOKS        9            /* フックの表の末尾にあるワイド表示のフックの数 */
#define GFX_SCREENRATIO   0x5b4        /* MtxGraphics3D: 論理座標 / 画素（SetScissor が使う） */
#define OFF_MINIMAP_FUNC  0x3caf38UL   /* FieldClass::DrawMiniMap(MtxGraphics3D*) */
#define OFF_MINIMAP_GOT   0x45f370UL
#define OFF_ESC_X1        0x391350UL   /* BattleClass::BattleScaduleEscape：逃げる行き先 x 680.0（mov w8, #0x442a0000）×2 */
#define OFF_ESC_X2        0x391408UL
#define INSN_MOV_680      0x52a88548U  /* movz w8, #0x442a, lsl #16 */
#define INSN_MOV_800      0x52a88908U  /* movz w8, #0x4448, lsl #16（800.0） */
/* 戦闘の AUTO/PAUSE/ESCAPE の欄（BattleClass の JobsMenuItemListControl、[battle+0x6e40]）。
   部品は ▶ のつまみ（2,2 54x105）とボタン3つ（56,2/37/72 101x35）。
   1.0-pre10: つまみを消し、欄を x 536・幅 104 にして、ボタンを欄の左端に置く（ボタンの画面上の位置は今までと同じ） */
static const struct { unsigned off, orig, now; } k_panelPatch[] = {
    { 0x3842cc, 0x5290000a, 0x5280000a },   /* BattleMain：閉じたときの x 586.0 → 536.0（movz w10,#0x8000 → #0） */
    { 0x3842d4, 0x72a8824a, 0x72a880ca },   /*   movk w10,#0x4412 → #0x4406 */
    { 0x3842ec, 0x5290000a, 0x5280000a },
    { 0x3842f4, 0x72a8824a, 0x72a880ca },
    { 0x384288, 0x52a87e0a, 0x52a880ca },   /* BattleMain：開いたときの x 480.0 → 536.0 */
    { 0x384834, 0x52a87e0a, 0x52a880ca },
    { 0x382914, 0x52804941, 0x52804301 },   /* BattleSceneInit：欄の SetSize の x 586 → 536 */
    { 0x38291c, 0x52801403, 0x52800d03 },   /*   幅 160 → 104 */
    { 0x382958, 0x52800041, 0x52800001 },   /* つまみの部品：種類 2 → 0（JobsMenuButton::Draw が描かない） */
    { 0x382970, 0x528006c7, 0x52800007 },   /*   幅 54 → 0（触っても当たらない） */
    { 0x3829e8, 0x52800705, 0x52800005 },   /* ボタン3つの x 56 → 0 */
    { 0x382a30, 0x52800705, 0x52800005 },
    { 0x382a78, 0x52800705, 0x52800005 },
};
typedef void (*ViewPortFn)(void *g, int x, int y, int w, int h);
typedef void (*S2DMFn)(void *g, float w, float h);
typedef void (*MSVPFn)(void *scene, void *g);
typedef void (*DIExFn)(void *g, void *tex, float x, float y, float w, float h, float u, float v, float uw, float vh,
                       int flags, int color, int mode);
typedef void (*Fill4Fn)(void *g, int x, int y, int w, int h);
typedef void (*Fill8Fn)(void *g, int x, int y, int w, int h, int r, int gg, int b, int a);
typedef void (*DrawFarFn)(void *field, void *g);
typedef void (*MiniMapFn)(void *field, void *g);
typedef void (*FSet2DFn)(void *field, void *g);
typedef void (*SetMModeFn)(void *g, int mode);
typedef void (*LoadIdentFn)(void *g);
typedef void (*GOrthoFn)(void *g, float l, float r, float b, float t, float n, float f);
static ViewPortFn g_origViewPort;
static S2DMFn g_origS2DM;
static MSVPFn g_origMSVP;
static DIExFn g_origDIEx;
static Fill4Fn g_origFill4;
static Fill8Fn g_origFill8;
static DrawFarFn g_origDrawFar;
static MiniMapFn g_origMiniMap;
static float g_wideM0, g_wideM12;   /* ワイドの 2D の射影の行列の [0] と [12]（今その射影かを見分ける） */

static void hook_ViewPort(void *g, int x, int y, int w, int h)
{
    /* 横に余白があるとき（横長の画面）だけ窓いっぱいにする。縦に余白があるとき（縦長）は元のまま */
    if (x <= 0 || y != 0 || w <= 0 || h <= 0) { g_origViewPort(g, x, y, w, h); return; }
    g_origViewPort(g, 0, 0, w + 2 * x, h);
}

static void hook_MSVP(void *scene, void *g)
{
    g_origMSVP(scene, g);
    float W = RD(float, g, GFX_VP + 8), H = RD(float, g, GFX_VP + 12);
    float e = 0.0f, s = 1.0f;
    if (W > 0.0f && H > 0.0f && W * 384.0f > H * 640.0f + 1.0f) {
        s = H / 384.0f;
        e = (W / s - 640.0f) * 0.5f;
    }
    if (e != g_wideE) {
        static int told;
        if (told < 4) { told++; LOGI("wide: screen %dx%d, margin %d (logical)", (int)W, (int)H, (int)e); }
    }
    g_wideE = e;
    g_wideS = s;
    if (e <= 0.0f) return;
    void *tp = p_GetTouchInfo ? p_GetTouchInfo() : 0;
    if (tp) {
        *(volatile float *)((uintptr_t)tp + TP_SCALE) = 1.0f / s;
        *(volatile float *)((uintptr_t)tp + TP_SCALE + 4) = 1.0f / s;
        *(volatile float *)((uintptr_t)tp + TP_OFFSET) = e * s;
        *(volatile float *)((uintptr_t)tp + TP_OFFSET + 4) = 0.0f;
    }
    uintptr_t v = *(volatile uintptr_t *)(g_base + APP_PTR_GOT);
    void *app = v ? *(void **)v : 0;
    if (app) {
        *(volatile float *)((uintptr_t)app + APP_IMGFIX_X) = 1.0f;
        *(volatile float *)((uintptr_t)app + APP_IMGFIX_Y) = 1.0f;
    }
    /* 1.0-pre4: 切り抜き（SetScissor）は「論理座標 / 倍率 + 描く範囲の左上」で画素に直すので、この置き方に合わせる
       （描く範囲の左上 +0x4c0 は、ほかには MainScene::SetViewPort がタッチのずれに使うだけ。上で書き直している） */
    *(volatile float *)((uintptr_t)g + GFX_VP) = e * s;
    *(volatile float *)((uintptr_t)g + GFX_VP + 4) = 0.0f;
    *(volatile float *)((uintptr_t)g + GFX_SCREENRATIO) = 1.0f / s;
    *(volatile float *)((uintptr_t)g + GFX_SCREENRATIO + 4) = 1.0f / s;
}

/* 2D の射影を ortho(-e - shift, 640 + e - shift, 384, 0) にする（shift > 0 で中身が右へずれる） */
static void wide_proj(void *g, float shift)
{
    float e = g_wideE;
    ((SetMModeFn)(g_base + OFF_SETMMODE))(g, 1);
    ((LoadIdentFn)(g_base + OFF_LOADIDENT))(g);
    ((GOrthoFn)(g_base + OFF_GORTHO))(g, -e - shift, 640.0f + e - shift, 384.0f, 0.0f, 0.0f, 1024.0f);
    ((SetMModeFn)(g_base + OFF_SETMMODE))(g, 0);
}

static void hook_S2DM(void *g, float w, float h)
{
    float e = g_wideE;
    if (e <= 0.0f || w != 640.0f || h != 384.0f) { g_origS2DM(g, w, h); return; }
    wide_proj(g, 0.0f);
    ((LoadIdentFn)(g_base + OFF_LOADIDENT))(g);
    g_wideM0 = RD(float, g, GFX_PROJ);
    g_wideM12 = RD(float, g, GFX_PROJ + 48);
}

/* 今ワイドの 2D の射影で描いているか */
static int wide_2d(void *g)
{
    return g_wideE > 0.0f && RD(float, g, GFX_PROJ) == g_wideM0 && RD(float, g, GFX_PROJ + 48) == g_wideM12;
}

/* 1.0-pre9: 背景を伸ばすのは、ワイドの 2D の射影で、しかも動かしたり拡大したりしていない（モデルビューの行列が単位行列の）ときだけ。
   世界地図は 384x384 の窓の中で地図の絵を拡大して描くので、絵の大きさが 640 を超えても伸ばしてはいけない */
#define GFX_MODELVIEW 0x1c
static int wide_2d_plain(void *g)
{
    if (!wide_2d(g)) return 0;
    const float *m = (const float *)((uintptr_t)g + GFX_MODELVIEW);
    return m[0] == 1.0f && m[5] == 1.0f && m[12] == 0.0f && m[13] == 0.0f && m[1] == 0.0f && m[4] == 0.0f;
}

static int wide_shift_begin(void *g)
{
    if (g_wideE <= 0.0f || !g || !wide_2d(g)) return 0;
    wide_proj(g, g_wideE);
    /* 1.0-pre5: 切り抜き（SetScissor）も同じだけ右へずらす（描く範囲の左上 +0x4c0 を e·s → 2e·s） */
    *(volatile float *)((uintptr_t)g + GFX_VP) = 2.0f * g_wideE * g_wideS;
    return 1;
}

static void wide_shift_end(void *g)
{
    wide_proj(g, 0.0f);
    *(volatile float *)((uintptr_t)g + GFX_VP) = g_wideE * g_wideS;
}

static int is_field_hud(void *w)
{
    return g_field && w && w == RDP(g_field, FB_BASE);
}

/* 1.0-pre6: フィールドのボタン（メニュー・ダッシュ・台本ボタン）の位置を横に dx ずらす。
   1.0-pre7: 全画面のスワイプ受け（スライドの移動パッド）は、左右の余白まで広げる（パッドは受けの範囲で切り抜かれるため） */
static void hud_shift(void *w, float dx)
{
    void *node = RDP(w, W_HEAD);
    for (int guard = 0; node && guard < 64; guard++) {
        void *it = RDP(node, 0x10);
        node = RDP(node, 0);
        if (!it) continue;
        if ((vt_slidepad && (uintptr_t)RDP(it, 0) == vt_slidepad + 0x10) || RD(float, it, I_SIZE) >= 600.0f) {
            *(volatile float *)((uintptr_t)it + I_POS) = RD(float, it, I_POS) - dx;
            *(volatile float *)((uintptr_t)it + I_SIZE) = RD(float, it, I_SIZE) + 2.0f * dx;
            continue;
        }
        *(volatile float *)((uintptr_t)it + I_POS) = RD(float, it, I_POS) + dx;
    }
}

/* 1.0-pre6: MenuWidget::Update（1.2.0）は、タッチが画面 (0,0,640,384) の中かを IsPointIn で確かめる。
   ワイド表示では左右の余白も画面の中として扱う（右へ置いたボタンと、余白から始めるスライド移動のため） */
#define OFF_ISPOINTIN_FUNC 0x363a10UL   /* vector2::IsPointIn(float,float,float,float) */
#define OFF_ISPOINTIN_GOT  0x45d1d0UL
typedef int (*IsPointInFn)(void *v, float x, float y, float w, float h);
static IsPointInFn g_origIsPointIn;
/* 1.0-pre9: フィールドの画面の覆い（北の洞窟などの暗がり。640x384 を四隅の4枚に分けて描く）は、
   640 幅を画面いっぱいに引き伸ばして描く（左右の余白まで暗くする） */
#define OFF_SCRFILTER_FUNC 0x3ca2f8UL   /* FieldClass::DrawScreenFilter(MtxGraphics3D*) */
#define OFF_SCRFILTER_GOT  0x45f7d0UL
typedef void (*ScrFilterFn)(void *field, void *g);
static ScrFilterFn g_origScrFilter;
static void hook_ScrFilter(void *field, void *g)
{
    if (g_wideE <= 0.0f || !wide_2d(g)) { g_origScrFilter(field, g); return; }
    ((SetMModeFn)(g_base + OFF_SETMMODE))(g, 1);
    ((LoadIdentFn)(g_base + OFF_LOADIDENT))(g);
    ((GOrthoFn)(g_base + OFF_GORTHO))(g, 0.0f, 640.0f, 384.0f, 0.0f, 0.0f, 1024.0f);
    ((SetMModeFn)(g_base + OFF_SETMMODE))(g, 0);
    g_origScrFilter(field, g);
    wide_proj(g, 0.0f);
}

static int hook_IsPointIn(void *v, float x, float y, float w, float h)
{
    if (g_wideE > 0.0f && x == 0.0f && y == 0.0f && w == 640.0f && h == 384.0f) {
        x = -g_wideE;
        w = 640.0f + 2.0f * g_wideE;
    }
    return g_origIsPointIn(v, x, y, w, h);
}

static void hook_MiniMap(void *field, void *g)
{
    int sh = wide_shift_begin(g);
    g_origMiniMap(field, g);
    if (sh) wide_shift_end(g);
}

static void flush_insn(volatile unsigned *ip);

/* 1.0-pre4: 逃げるときの行き先（640 のすぐ外の 680）を、広げた画面でも見えない 800 にする */
static void wide_escape_patch(void)
{
    uintptr_t a[2] = { g_base + OFF_ESC_X1, g_base + OFF_ESC_X2 };
    for (int i = 0; i < 2; i++) {
        volatile unsigned *ip = (volatile unsigned *)a[i];
        if (*ip == INSN_MOV_800) continue;
        if (*ip != INSN_MOV_680) { LOGE("wide: escape insn %08x, expected %08x, not patched", *ip, INSN_MOV_680); return; }
    }
    for (int i = 0; i < 2; i++) {
        volatile unsigned *ip = (volatile unsigned *)a[i];
        if (mprotect((void *)(a[i] & ~0xfffUL), 0x1000, PROT_READ | PROT_WRITE | 4) != 0) { LOGE("wide: escape mprotect failed"); return; }
        *ip = INSN_MOV_800;
        flush_insn(ip);
    }
    LOGI("wide: escape target 680 -> 800");
}

/* 戦闘の AUTO/PAUSE/ESCAPE の欄をアイテムの下に固定し、▶ のつまみを消す（k_panelPatch） */
static void wide_panel_patch(void)
{
    int n = (int)(sizeof k_panelPatch / sizeof k_panelPatch[0]);
    for (int i = 0; i < n; i++) {
        unsigned v = *(volatile unsigned *)(g_base + k_panelPatch[i].off);
        if (v != k_panelPatch[i].orig && v != k_panelPatch[i].now) {
            LOGE("wide: panel insn %x = %08x, not patched", k_panelPatch[i].off, v);
            return;
        }
    }
    for (int i = 0; i < n; i++) {
        volatile unsigned *ip = (volatile unsigned *)(g_base + k_panelPatch[i].off);
        if (mprotect((void *)((uintptr_t)ip & ~0xfffUL), 0x1000, PROT_READ | PROT_WRITE | 4) != 0) { LOGE("wide: panel mprotect failed"); return; }
        *ip = k_panelPatch[i].now;
        flush_insn(ip);
    }
    LOGI("wide: battle panel fixed at 536, tab removed");
}

static void wide_note(const char *what, uintptr_t id, float x, float y, float w, float h)
{
    static uintptr_t seen[48];
    static int n;
    for (int i = 0; i < n; i++) if (seen[i] == id) return;
    if (n >= 48) return;
    seen[n++] = id;
    LOGI("wide: %s %lx (%d,%d %dx%d) widened", what, id, (int)x, (int)y, (int)w, (int)h);
}

static void hook_DIEx(void *g, void *tex, float x, float y, float w, float h, float u, float v, float uw, float vh,
                      int flags, int color, int mode)
{
    if ((flags & 7) == 0 && wide_2d_plain(g)) {
        if (x <= 0.5f && x + w >= 639.5f) {
            float k = (640.0f + 2.0f * g_wideE) / 640.0f;
            wide_note("image", (uintptr_t)tex, x, y, w, h);
            x = 320.0f + (x - 320.0f) * k;
            w *= k;
        } else if (y <= 0.5f && y + h >= 383.5f && (x <= 0.5f || x + w >= 639.5f)) {
            /* 画面の高さいっぱいで左右どちらかの端に接する絵は、その端の余白まで伸ばす */
            wide_note("image edge", (uintptr_t)tex, x, y, w, h);
            if (x <= 0.5f) { x -= g_wideE; w += g_wideE; }
            else w += g_wideE;
        }
    }
    g_origDIEx(g, tex, x, y, w, h, u, v, uw, vh, flags, color, mode);
}

static void hook_Fill4(void *g, int x, int y, int w, int h)
{
    if (wide_2d_plain(g)) {
        int e = (int)(g_wideE + 1.0f);
        if (x <= 0 && x + w >= 640) {
            wide_note("fill", 4, x, y, w, h);
            x -= e;
            w += 2 * e;
        } else if (y <= 0 && y + h >= 384 && (x <= 0 || x + w >= 640)) {
            /* メニューの背景（左半分と右半分の色の帯）など：接している端の余白まで伸ばす */
            wide_note("fill edge", 4, x, y, w, h);
            if (x <= 0) { x -= e; w += e; } else w += e;
        }
    }
    g_origFill4(g, x, y, w, h);
}

static void hook_Fill8(void *g, int x, int y, int w, int h, int r, int gg, int b, int a)
{
    if (wide_2d_plain(g)) {
        int e = (int)(g_wideE + 1.0f);
        if (x <= 0 && x + w >= 640) {
            wide_note("fill", 8, x, y, w, h);
            x -= e;
            w += 2 * e;
        } else if (y <= 0 && y + h >= 384 && (x <= 0 || x + w >= 640)) {
            /* メニューの背景（左半分と右半分の色の帯）など：接している端の余白まで伸ばす */
            wide_note("fill edge", 8, x, y, w, h);
            if (x <= 0) { x -= e; w += e; } else w += e;
        }
    }
    g_origFill8(g, x, y, w, h, r, gg, b, a);
}

static void hook_DrawFar(void *field, void *g)
{
    volatile float *fw = (volatile float *)((uintptr_t)field + FIELD_W);
    float w0 = *fw;
    if (g_wideE <= 0.0f || w0 <= 640.5f) { g_origDrawFar(field, g); return; }
    FSet2DFn set2d = (FSet2DFn)(g_base + OFF_FSET2D);
    *fw = 640.0f;
    set2d(field, g);
    g_origDrawFar(field, g);
    *fw = w0;
    set2d(field, g);
}

/* ---------- v3.08: 戦闘終了画面の目印 ----------
   BattleClass::BattleScaduleWin（勝利処理：ギル・経験値の表示からレベルアップの表示まで）が動いたフレームを覚える */
#define OFF_BTLWIN_FUNC 0x390560UL
#define OFF_BTLWIN_GOT  0x45e4a0UL
typedef void (*BtlWinFn)(void *btl);
static BtlWinFn g_origBtlWin;
static void hook_BtlWin(void *btl)
{
    g_btl = btl;
    g_origBtlWin(btl);
}

/* ---------- v3.08: メニュー画面（MenuClass::MenuSelectFunc）の上下を1行（2つ）ずつに ----------
   10個のボタンは左上から横へ2列で並ぶが、ゲームは上で +9（1つ前）、下で +1（1つ後）しかしない。
   改造は左右を上下として見せているので、上下も左右と同じ動きになっていた。
   上下を押しているフレームだけ、この2つの足し算の数を +8 / +2 に差し替える（ゲームの移動は1回のまま） */
#define OFF_MENU_UP_ADD   0x3fc804UL   /* add w8, w8, #9 */
#define OFF_MENU_DOWN_ADD 0x3fcb38UL   /* add w8, w8, #1 */
#define INSN_ADD9 0x11002508U
#define INSN_ADD1 0x11000508U
#define INSN_ADD8 0x11002108U
#define INSN_ADD2 0x11000908U
static int g_menuStepOk, g_menuStepVert;

static void flush_insn(volatile unsigned *ip)
{
    __asm__ volatile("dc cvau, %0\n\tdsb ish\n\tic ivau, %0\n\tdsb ish\n\tisb" :: "r"(ip) : "memory");
}

static void menu_step_init(void)
{
    volatile unsigned *up = (volatile unsigned *)(g_base + OFF_MENU_UP_ADD);
    volatile unsigned *dn = (volatile unsigned *)(g_base + OFF_MENU_DOWN_ADD);
    if (*up != INSN_ADD9 || *dn != INSN_ADD1) {
        LOGE("menu step: insn %08x %08x, expected %08x %08x, not patched", *up, *dn, INSN_ADD9, INSN_ADD1);
        return;
    }
    uintptr_t p1 = (uintptr_t)up & ~0xfffUL, p2 = (uintptr_t)dn & ~0xfffUL;
    if (mprotect((void *)p1, 0x1000, PROT_READ | PROT_WRITE | 4) != 0 ||
        (p2 != p1 && mprotect((void *)p2, 0x1000, PROT_READ | PROT_WRITE | 4) != 0)) {
        LOGE("menu step: mprotect RWX failed, not patched");
        return;
    }
    g_menuStepOk = 1;
}

static void menu_step_tick(int cur)
{
    if (!g_menuStepOk) return;
    int vert = (cur & (K_UP | K_DOWN)) && !(cur & (K_LEFT | K_RIGHT));
    if (vert == g_menuStepVert) return;
    volatile unsigned *up = (volatile unsigned *)(g_base + OFF_MENU_UP_ADD);
    volatile unsigned *dn = (volatile unsigned *)(g_base + OFF_MENU_DOWN_ADD);
    *up = vert ? INSN_ADD8 : INSN_ADD9;
    *dn = vert ? INSN_ADD2 : INSN_ADD1;
    flush_insn(up);
    flush_insn(dn);
    g_menuStepVert = vert;
}

/* ---------- v3.07: レベルアップしたメンバーを戦闘後に全回復 ----------
   GameClass::LevelUp(slot) は勝利処理（BattleScaduleWin）からだけ呼ばれ、上がると 1 を返す。
   戦闘の終わり（BattleClass::EndBattle）で戦闘中の HP・MP・状態がメンバーへ書き戻されるので、
   その後で SetMemberFullRecover と同じ回復（今の HP/MP ← 最大、状態 ← 0）をする */
#define OFF_LEVELUP_FUNC  0x3e6828UL   /* GameClass::LevelUp(int) */
#define OFF_LEVELUP_GOT   0x45e908UL
#define OFF_ENDBTL_FUNC   0x384b80UL   /* BattleClass::EndBattle() */
#define OFF_ENDBTL_GOT    0x45e278UL
#define MEMBER_BASE  0x1a180           /* GameClass: MEMBER_STATUS[24]、1人 0x4c4 */
#define MEMBER_SIZE  0x4c4
#define MS_HPMP      0x118             /* 今の HP, MP */
#define MS_STATUS    0x120             /* 状態異常 */
#define MS_MAXHPMP   0x1f0             /* 最大 HP, MP */
typedef int (*LevelUpFn)(void *game, int slot);
typedef void (*EndBtlFn)(void *btl);
static LevelUpFn g_origLevelUp;
static EndBtlFn g_origEndBtl;
static u8 *g_lvUp[8];
static int g_nLvUp;

static int hook_LevelUp(void *game, int slot)
{
    int r = g_origLevelUp(game, slot);
    if ((r & 1) && slot >= 0 && slot <= 7) {
        uintptr_t G = (uintptr_t)game;
        int sel = RD(int, G, 0x21424);
        int id = RD(int, G, 0x21428 + sel * 32 + slot * 4);
        if (id >= 0 && id <= 0x17) {
            u8 *m = (u8 *)(G + MEMBER_BASE + id * MEMBER_SIZE);
            int dup = 0;
            for (int i = 0; i < g_nLvUp; i++) if (g_lvUp[i] == m) dup = 1;
            if (!dup && g_nLvUp < 8) g_lvUp[g_nLvUp++] = m;
        }
    }
    return r;
}

static void hook_EndBtl(void *btl)
{
    g_origEndBtl(btl);
    g_btl = 0;
    for (int i = 0; i < g_nLvUp; i++) {
        u8 *m = g_lvUp[i];
        int hp = RD(int, m, MS_MAXHPMP), mp = RD(int, m, MS_MAXHPMP + 4);
        *(volatile int *)(m + MS_HPMP) = hp;
        *(volatile int *)(m + MS_HPMP + 4) = mp;
        *(volatile int *)(m + MS_STATUS) = 0;
    }
    g_nLvUp = 0;
}

/* ---------- v3.07: 戦闘の経験値・ギル・AP を 1.5 倍 ----------
   AddEnemyDropData は敵ごとに「値 × 倍率 ÷ 100」を足す。倍率は GameClass+0x169d4（経験値）/d8（ギル）/dc（AP）、
   LoadBootData で 100 が入るだけでほかに書く所はない。毎フレーム確かめて 150 にする */
#define GAME_RATE_EXP 0x169d4
#define RATE_EXP 150
#define RATE_GIL 150
#define RATE_AP  150
static void reward_rate_tick(void)
{
    u8 *gm = (u8 *)game_inst();
    if (!gm) return;
    static const int want[3] = { RATE_EXP, RATE_GIL, RATE_AP };
    for (int k = 0; k < 3; k++) {
        volatile int *r = (volatile int *)(gm + GAME_RATE_EXP + 4 * k);
        if (*r != want[k]) {
            *r = want[k];
        }
    }
}


/* ---------- v3.15: 報酬の倍率を切り上げに ----------
   BattleClass::AddEnemyDropData は敵1体ごとに 値×倍率÷100 を切り捨てて BattleClass+0x7e04 の
   {EXP, Gil, AP} に足す（上限 9999999/9999999/999）。
   倍率を一時的に100にして元の処理で素の値を足させ、残り(倍率-100)%ぶんを切り上げで足す。 */
#define OFF_ADDDROP_FUNC 0x39c4f8UL
#define OFF_ADDDROP_GOT  0x45e648UL
#define BTL_REWARD       0x7e04
typedef void (*AddDropFn)(void *bc, void *act);
static AddDropFn g_origAddDrop;
static void hook_AddDrop(void *bc, void *act)
{
    u8 *gm = (u8 *)game_inst();
    if (!gm || !bc) { g_origAddDrop(bc, act); return; }
    volatile int *rate = (volatile int *)(gm + GAME_RATE_EXP);
    volatile int *acc = (volatile int *)((uintptr_t)bc + BTL_REWARD);
    int r[3], a0[3];
    static const int cap[3] = { 9999999, 9999999, 999 };
    for (int k = 0; k < 3; k++) { r[k] = rate[k]; a0[k] = acc[k]; rate[k] = 100; }
    g_origAddDrop(bc, act);
    for (int k = 0; k < 3; k++) {
        rate[k] = r[k];
        int base = acc[k] - a0[k];
        if (base > 0 && r[k] > 100) {
            long extra = ((long)base * (r[k] - 100) + 99) / 100;
            long v = (long)acc[k] + extra;
            acc[k] = (int)(v > cap[k] ? cap[k] : v);
        }
    }
}

/* ---------- v3.06: フィールドの横幅を 640 に固定（飛空艇で右上が黒く欠ける対策） ----------
   FieldClass::SceneDraw は毎フレーム、画面の比率からフィールドの横幅 field+0xc を計算し直す
   （ワイド化した画面だと 682 になり、幅 640 の空の絵が右端まで届かない）。
   その書き込み命令 stur d0,[x0,#0xc] を NOP にして、Initialize で入る 640x384 のままにする */
#define OFF_FIELDW_STORE 0x3b03a4UL
#define INSN_FIELDW_STORE 0xfc00c000U   /* stur d0, [x0, #0xc] */
#define INSN_NOP          0xd503201fU

static void patch_field_width(void)
{
    volatile unsigned *ip = (volatile unsigned *)(g_base + OFF_FIELDW_STORE);
    if (*ip == INSN_NOP) return;
    if (*ip != INSN_FIELDW_STORE) { LOGE("field width: insn %08x, expected %08x, not patched", *ip, INSN_FIELDW_STORE); return; }
    uintptr_t page = (uintptr_t)ip & ~0xfffUL;
    if (mprotect((void *)page, 0x1000, PROT_READ | PROT_WRITE | 4 /* PROT_EXEC */) != 0) {
        LOGE("field width: mprotect RWX failed, not patched");
        return;
    }
    *ip = INSN_NOP;
    __asm__ volatile("dc cvau, %0\n\tdsb ish\n\tic ivau, %0\n\tdsb ish\n\tisb" :: "r"(ip) : "memory");
    if (mprotect((void *)page, 0x1000, PROT_READ | 4 /* PROT_EXEC */) != 0)
        LOGE("field width: mprotect RX failed (page left RWX)");
}

/* ---------- ダッシュ（フィールドで B を押している間、操作キャラの歩きを2倍速に） ----------
   1歩のフレーム数（キャラ +0x68）は SetCharaAction で歩き出すときに決まる。
   操作キャラを動かす MoveCharaControl の中で歩き出したときだけ、それを半分にする */
#define OFF_MOVECTRL_FUNC 0x3b780cUL   /* FieldClass::MoveCharaControl(int*) */
#define OFF_MOVECTRL_GOT  0x45f3d8UL
#define OFF_SETACT_FUNC   0x3b9d30UL   /* FieldClass::SetCharaAction(int*, int x8) */
#define OFF_SETACT_GOT    0x45f508UL
typedef void (*MoveCtrlFn)(void *field, int *chara);
typedef void (*SetActFn)(void *field, int *chara, int a, int b, int c, int d, int e, int f, int g, int h);
static MoveCtrlFn g_origMoveCtrl;
static SetActFn g_origSetAct;
static int *g_ctrlChara;          /* MoveCharaControl で動かしている最中のキャラ */
static int *g_plChara;            /* v4.01: 最後に MoveCharaControl で動かしたキャラ（止まっているかを見る） */
static int g_dashOn;              /* v4.01: ダッシュボタンで ON（B を押していなくても走る） */
static int *g_dashChara;          /* ダッシュで1歩を短くしたキャラ */
static int g_dashOrig;            /* そのときの元のフレーム数（0=戻し済み） */
static void dash_restore(void);

static void hook_MoveCtrl(void *field, int *chara)
{
    int *saved = g_ctrlChara;
    if (chara == g_dashChara) dash_restore();
    g_field = field;
    g_ctrlChara = chara;
    g_plChara = chara;
    g_origMoveCtrl(field, chara);
    g_ctrlChara = saved;
}

static void hook_SetAct(void *field, int *chara, int a, int b, int c, int d, int e, int f, int g, int h)
{
    g_origSetAct(field, chara, a, b, c, d, e, f, g, h);
    if (!chara) return;
    if (chara == g_dashChara) g_dashOrig = 0;      /* 新しい1歩：前の1歩の戻し待ちは要らない */
    if (chara != g_ctrlChara || !((g_cur & K_BACK) || g_dashOn)) return;
    int type = RD(int, chara, 0x78);
    if (type < 0 || type > 8 || !((1 << type) & 0x194)) return;   /* 歩く動作だけ */
    int fr = RD(int, chara, 0x68);
    if (fr <= 1) return;
    *(volatile int *)((uintptr_t)chara + 0x68) = (fr + 1) / 2;
    g_dashChara = chara;
    g_dashOrig = fr;
}

/* 歩き終えたあとも、ゲームは毎フレーム「マス幅÷フレーム数」を足してからマスの中心へ戻す。
   フレーム数が2だとちょうど半マスになり、隣のマスへ吸い寄せられて止まらなくなる（乗り物で山や海を抜ける）。
   そこで、短くした1歩を歩き終えたら、フレーム数を元に戻しておく */
static void dash_restore(void)
{
    int *ch = g_dashChara;
    if (!g_dashOrig || !ch) return;
    if (RD(int, ch, 0x44) >= RD(int, ch, 0x68)) {
        *(volatile int *)((uintptr_t)ch + 0x68) = g_dashOrig;
        *(volatile int *)((uintptr_t)ch + 0x44) = g_dashOrig;
        g_dashOrig = 0;
    }
}

/* ---------- チャレンジダンジョンの入口モーグリを戻す ----------
   スマホ版ではモーグリのイベントが「絵のない仕掛け・起動条件なし」にされ、
   一部は壁の中へ移され、踊り子編・ナイト編・忍者編はスクリプトが空にされている。
   マップのイベントを読み込む直前のデータを書き換えて元に戻す。
   クリアフラグの確認は外し、その場所に来たときから入れるようにする */
#define OFF_LOADEV_FUNC 0x3bba78UL   /* FieldClass::LoadEventData(char*, int, char**, char***, int*, int) */
#define OFF_LOADEV_GOT  0x45ef00UL
typedef void (*LoadEvFn)(void *field, unsigned char *data, int off, void *hdrs, void *lines, void *counts, int n);
static LoadEvFn g_origLoadEv;
#include "moogle_tab.h"
static unsigned char g_evbuf[65536];

static int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static int be32(const unsigned char *p) { return (int)(((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3]); }
static void put32(unsigned char *p, int v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

static int dungeon_dest(int m)
{
    return m == 3000 || m == 1400 || m == 1600 || m == 2000 || m == 1100 || m == 1900;
}

/* スクリプトが空にされたモーグリ（イベント番号, x, y）→ 章 */
static int empty_moogle_chapter(const unsigned char *h)
{
    if (h[1] == 22 && h[2] == 58 && h[3] == 25) return 7;   /* 歓楽街ガーデンガーデン 酒場の前 */
    if (h[1] == 3  && h[2] == 6  && h[3] == 4)  return 8;   /* ブルトガングの町 北の塔 */
    if (h[1] == 4  && h[2] == 5  && h[3] == 3)  return 9;   /* マタギの村 宿屋 */
    return 0;
}

/* ---------- チャレンジダンジョンの報酬を取り切りやすくする ----------
   ボスの報酬はまだ持っていないレアが残っている間は必ずレア枠に入るようにし、
   部屋の宝箱の装備も取るまでは必ず出るようにする。全部そろったあとは元の抽選のまま。
   暁の章は抽選した時点で入手済みにしていたので、ボスの宝箱を開けたときに付けるよう移す */
#include "cd_tab.h"
static const unsigned char LOT_IF_HEAD[18] = { 0x3d, 0x00, 0x05, 0x0f, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x65,
                                               0x04, 0x0a, 0x00, 0x00, 0x00, 0x00 };
static const unsigned char RND_HEAD[11] = { 0x03, 0x0f, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const unsigned char DAWN_FLAG[8] = { 0x04, 0x02, 0x00, 0x03, 0x00, 0x10, 0x00, 0x01 };

static int bytes_eq(const unsigned char *a, const unsigned char *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* 1行（先頭は長さ）が「map_local[idx] = random(0, 0, hi)」なら1 */
static int is_rnd(const unsigned char *ln, int idx, int hi)
{
    return ln[0] == 0x11 && bytes_eq(ln + 1, RND_HEAD, 6) && ln[1 + 6] == (u8)idx && bytes_eq(ln + 1 + 7, RND_HEAD + 7, 4)
        && ln[1 + 11] == 0 && ln[1 + 12] == 0 && be32(ln + 1 + 13) == hi;
}

static void put16(unsigned char *p, int v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }

/* 1つのイベントの行を必要なら書き換える。戻り値は書き換えた数 */
static int patch_lottery(unsigned char *h, int cnt)
{
    unsigned char *ln[200]; int nl = cnt < 200 ? cnt : 200, done = 0, dawn = 0;
    unsigned char *q = h + 0x41;
    for (int k = 0; k < nl; k++) { ln[k] = q; q += 1 + q[0]; }
    for (int k = 0; k < nl; k++) {
        unsigned char *l = ln[k], *L = l + 1;
        /* 竜騎士・レンジャー・吟遊詩人・メモリスト・踊り子：IF random(1..100) <= 70/60 → 0 */
        if (k == 0 && l[0] == 0x1a && bytes_eq(L, LOT_IF_HEAD, 18) && (be32(L + 18) == 70 || be32(L + 18) == 60)
            && be16(L + 0x16) == 5 && nl > 1 && ln[1][0] == 0x11 && ln[1][1] == 0x03 && ln[1][2] == 0x0f && be32(ln[1] + 1 + 13) == 14) {
            put32(L + 18, 0); done++;
        }
        /* 暁の章：レアか通常か random(0..1) → 1 */
        if (is_rnd(l, 0, 2)) { put16(L + 11, 1); done++; dawn = 1; }
        /* ナイト編・忍者編：random(0..9) → 6..9 */
        if (is_rnd(l, 0, 10)) { put16(L + 11, 6); done++; }
        /* 部屋の宝箱：random(0..9) → 9（装備の枠。取ったあとは元の抽選へ） */
        if (is_rnd(l, 4, 10)) { put16(L + 11, 9); done++; }
        /* 暗黒騎士編：カルマの判定を常にS以上、Sの中の通常枠10%をなくす */
        if (k == 0 && l[0] == 0x1a && L[0] == 0x3d && L[3] == 3 && be16(L + 6) == 1 && be32(L + 8) == 36
            && be32(L + 18) == 900 && be16(L + 0x16) == 4 && nl > 1) {
            unsigned char *M = ln[1] + 1;
            if (ln[1][0] == 0x1a && M[0] == 0x3d && M[3] == 3 && be16(M + 6) == 1 && be32(M + 8) == 30
                && be32(M + 18) == 90 && be16(M + 0x16) == 2) {
                put32(L + 18, -1000000); put32(M + 18, 100); done += 2;
            }
        }
    }
    if (dawn) {
        for (int k = 0; k < nl; k++)
            if (ln[k][0] == 8 && bytes_eq(ln[k] + 1, DAWN_FLAG, 8)) { ln[k][8] = 0; done++; }   /* 抽選時の入手済みの印をやめる */
    }
    return done;
}

static void moogle_face(unsigned char *h)
{
    static const unsigned char face[12] = { 2, 1, 1, 1, 3, 2, 0, 0, 2, 0, 0, 0 };
    for (int k = 0; k < 12; k++) h[0x33 + k] = face[k];
}

/* v3.22: 竜騎士編（章2）のチャレンジダンジョンの仕掛け（B1F 0xfc、B2F 0xfa/0xfb/0x102）を作動したままにする。
   入口（1400）と出口（1403）のイベントがこの印を 0 に戻すので、その行を
   同じイベントにある「0x103 を 0 に戻す」行に置き換える（長さが同じなので他の行はずれない） */
static int keep_switches(unsigned char *h, int cnt)
{
    static const unsigned char r103[9] = { 8, 0x04, 0, 0, 1, 0x01, 0x03, 0, 0 };
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 2) return 0;
    unsigned char *q = h + 0x41;
    int has = 0;
    for (int k = 0; k < cnt; k++) { if (bytes_eq(q, r103, 9)) has = 1; q += 1 + q[0]; }
    if (!has) return 0;
    int done = 0;
    q = h + 0x41;
    for (int k = 0; k < cnt; k++) {
        if (q[0] == 8 && q[1] == 0x04 && q[2] == 0 && q[3] == 0 && q[4] == 1 && q[7] == 0 && q[8] == 0) {
            int f = q[5] << 8 | q[6];
            if (f == 0xfa || f == 0xfb || f == 0xfc || f == 0x102) {
                for (int i = 0; i < 9; i++) q[i] = r103[i];
                done++;
            }
        }
        q += 1 + q[0];
    }
    return done;
}

/* チャレンジダンジョンから町へ戻る移動（6b 02 09）の行き先の書き換え
   v3.29: 竜騎士編（章2）→飛竜のすみか 902 の (7,8) はクリア後に出る ev12 と重なる → (5,9)（1400 ev1・1403 ev9・ev11）
   v3.30: 吟遊詩人編（章4）→ルサルカ 200 の (28,5) は家の壁 → (38,18)（2000 ev2 魔法陣・2001 ev23 クリア）。
          v3.29 では LoadMap のフックで書き換えていたのを、イベントの書き換えに移した */
static const struct { int ch, dest, x, y, nx, ny; } k_retFix[] = {
    { 2, 902, 7, 8, 5, 9 },
    { 4, 200, 28, 5, 38, 18 },
};
static int return_fix(unsigned char *h, int cnt)
{
    void *game = game_inst();
    if (!game) return 0;
    int ch = RD(int, game, G_CHAPTER);
    unsigned char *q = h + 0x41; int done = 0;
    for (int k = 0; k < cnt; k++) {
        unsigned char *L = q + 1;
        if (q[0] >= 27 && L[0] == 0x6b && L[1] == 2 && L[2] == 9 && L[4] == 0 && be16(L + 15) == 2 && be16(L + 21) == 3) {
            for (unsigned i = 0; i < sizeof(k_retFix) / sizeof(k_retFix[0]); i++)
                if (ch == k_retFix[i].ch && be32(L + 5) == k_retFix[i].dest && be32(L + 17) == k_retFix[i].x && be32(L + 23) == k_retFix[i].y) {
                    put32(L + 17, k_retFix[i].nx); put32(L + 23, k_retFix[i].ny); done++;
                }
        }
        q += 1 + q[0];
    }
    return done;
}

/* v3.32: 吟遊詩人編（章4）のチャレンジダンジョンの海の地図（マップ2）で、乗り物の枠 ev1〜3 が「絵なしの人」になっていて、
   黒チョコボ（ev3）を動かす MoveCharaAction が読み込まれていない絵のアニメを読み 0+0x400 で落ちる。
   世界地図（章4 マップ1）の同じ枠の見出し（出る条件・絵 11/77/119・表示の設定）に合わせる。番号と位置はそのまま */
static const unsigned char k_vehHdr[3][0x3f] = {
    { 0x00, 0x01, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0f, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x01, 0x02, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
    { 0x00, 0x02, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x03, 0x03, 0x00, 0x4d, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x02, 0x0a, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
    { 0x00, 0x03, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x11, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x03, 0x03, 0x00, 0x77, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x02, 0x00, 0x02, 0x0a, 0x02, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00 },
};
/* v3.33: 位置指定のメッセージ枠（命令 0x01）が x0/5・y294・幅64・高さ26 で1行分しかなく、2行の表示の下が切れる。
   章4 マップ2 ev11「経度／緯度」（メッセージ0x27）は x=0 で左端も欠ける → x5・y272・高さ48（下端320は同じ）。
   章5（暗黒騎士編のチャレンジダンジョン）の「カルマ 点」（メッセージ0x2e、x5）も同じ大きさなので y272・高さ48 に */
static int msgbox_fix(unsigned char *h, int cnt)
{
    void *game = game_inst();
    if (!game) return 0;
    int ch = RD(int, game, G_CHAPTER);
    if (ch != 4 && ch != 5) return 0;
    static const unsigned char yhw[6] = { 0x01, 0x26, 0x00, 0x40, 0x00, 0x1a };
    unsigned char *q = h + 0x41; int done = 0;
    for (int k = 0; k < cnt; k++) {
        unsigned char *L = q + 1;
        /* L[10..11]=x, L[12..13]=y, L[14..15]=幅, L[16..17]=高さ（ビッグエンディアン） */
        if (q[0] >= 18 && L[0] == 0x01 && L[1] == 0 && bytes_eq(L + 12, yhw, 6) && L[10] == 0 &&
            ((ch == 4 && g_curMap == 2 && L[2] == 0x27 && L[11] == 0) || (ch == 5 && L[2] == 0x2e && L[11] == 5))) {
            L[11] = 5; L[12] = 0x01; L[13] = 0x10; L[16] = 0x00; L[17] = 0x30;
            done++;
        }
        /* v3.40t3: 章5 CD の戦闘のあとに一瞬出る「カルマ：<数>点」（メッセージ 0x1b3、x70・y115・幅100・高さ26）を、
           左下の点数表示（x5）と左端をそろえて x5 に */
        static const unsigned char k1b3[8] = { 0x00, 0x46, 0x00, 0x73, 0x00, 0x64, 0x00, 0x1a };
        if (ch == 5 && q[0] >= 18 && L[0] == 0x01 && L[1] == 0x01 && L[2] == 0xb3 && bytes_eq(L + 10, k1b3, 8)) {
            L[11] = 5;
            done++;
        }
        q += 1 + q[0];
    }
    return done;
}

/* v3.33: 吟遊詩人編（章4）・メモリスト編（章6）のチャレンジダンジョンの脱出用の魔法陣（絵125、35 01 01／01 02 00 00 02 02 00 00）は
   仕掛けの1マス上に半歩ずれて見える。本編で同じく「仕掛けの1マス上」に置かれた魔法陣（例 章10 1001 ev12）と同じ値にする */
static int circle_fix(unsigned char *h)
{
    static const unsigned char from28[3] = { 0x35, 0x01, 0x01 }, to28[3] = { 0x35, 0x04, 0x04 };
    static const unsigned char from33[8] = { 1, 2, 0, 0, 2, 2, 0, 0 }, to33[8] = { 0, 2, 0, 3, 2, 2, 0, 0 };
    void *game = game_inst();
    if (!game || be16(h + 0x2b) != 125) return 0;
    int ch = RD(int, game, G_CHAPTER);
    if ((ch != 4 && ch != 6) || g_curMap < 2000 || g_curMap >= 2100) return 0;
    if (!bytes_eq(h + 0x28, from28, 3) || !bytes_eq(h + 0x33, from33, 8)) return 0;
    for (int i = 0; i < 3; i++) h[0x28 + i] = to28[i];
    for (int i = 0; i < 8; i++) h[0x33 + i] = to33[i];
    return 1;
}

/* v3.53: 踊り子編CDの最後の地図（章7 map1105）の脱出の仕掛け ev0 (12,25)（絵125・アニメ 12 00 00、乗ると動く、絵が出ない）に、
   ほかのチャレダンと同じ魔法陣（35 04 04／00 02 00 03 02 02 00 00）を出す。仕掛けのマスにそのまま描くので位置のずれは起きない */
static int dancer_circle(unsigned char *h)
{
    static const unsigned char anim0[3] = { 0x12, 0, 0 }, zero8[8] = { 0 };
    static const unsigned char circ[8] = { 0, 2, 0, 3, 2, 2, 0, 0 };
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 7 || g_curMap != 1105) return 0;
    if (be16(h) != 0 || h[2] != 12 || h[3] != 25 || be16(h + 0x2b) != 125 || h[7] != 1) return 0;
    if (!bytes_eq(h + 0x28, anim0, 3) || !bytes_eq(h + 0x33, zero8, 8)) { LOGE("circle: map 1105 event 0 differs, not patched"); return -1; }
    h[0x28] = 0x35; h[0x29] = 4; h[0x2a] = 4;
    for (int i = 0; i < 8; i++) h[0x33 + i] = circ[i];
    h[7] = 0;      /* v3.54: h[7]≠0 はマスの仕掛け扱いでキャラが作られず絵が出ない（GetAppearEventDataOfPosition）。暁の章CD 3002 の脱出と同じ 0 に */
    return 1;
}

static int veh_fix(unsigned char *h)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 4 || g_curMap != 2) return 0;
    int id = h[1];
    if (id < 1 || id > 3 || h[2] != id + 1 || h[3] != 0 || be16(h + 0x2b) != 0) return 0;
    for (int i = 4; i < 0x3f; i++) h[i] = k_vehHdr[id - 1][i];
    return 1;
}

/* v3.38: 妖精の森（章4 map801・811）の商人の宝箱の中身を決める下請けイベント（801 ev34/ev36、811 ev2/ev3）
   - 100ギル：大(≤3)を使わず、中(≤18→≤100)の7品を 200ギル・コテージ・ミニマムダガー・エクスポーション・エーテル・万能薬・金の竪琴 に
   - 1000ギル：大(≤3→≤33、アクセサリー。6回目までは順番どおり)、中(≤18→≤100)を random(1..2) で 2000ギル・金の竪琴
   元の行を確かめてから、同じ長さのまま数字だけ書き換える */
static int forest_chk_if(const unsigned char *L, int v) { return L[0] == 0x3d && L[13] == 0x0a && be32(L + 18) == v && be16(L + 22) == 5; }
static int forest_chk_item(const unsigned char *L, int idx, int item) { return L[0] == 0x03 && L[1] == 0x07 && L[6] == idx && be16(L + 9) == item; }
static int forest_lottery(unsigned char *h, int cnt)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 4 || (g_curMap != 801 && g_curMap != 811)) return 0;
    if (cnt != 59 && cnt != 67) return 0;
    unsigned char *ln[67]; unsigned char *q = h + 0x41;
    for (int k = 0; k < cnt; k++) { ln[k] = q + 1; q += 1 + q[0]; }
    static const unsigned char rnd100[] = { 0x03, 0x0f, 0, 0, 0x02, 0, 0, 0, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x65 };
    if (!bytes_eq(ln[0], rnd100, sizeof(rnd100))) return 0;
    if (cnt == 59) {
        static const short oldI[6] = { 0x1a5, 0x1ab, 0x1b3, 0x1aa, 0x1b4, 0x009 }, newI[6] = { 0x1b4, 0x009, 0x1a6, 0x1a7, 0x1b2, 0x0b8 };
        if (!forest_chk_if(ln[1], 3) || !forest_chk_if(ln[3], 18)) return 0;
        for (int i = 0; i < 6; i++) if (!forest_chk_item(ln[40 + i], 4 + i, oldI[i])) return 0;
        put32(ln[1] + 18, 0); put32(ln[3] + 18, 100);
        for (int i = 0; i < 6; i++) put16(ln[40 + i] + 9, newI[i]);
        return 100;
    }
    /* cnt == 67：1000ギル */
    if (!forest_chk_if(ln[1], 3) || !forest_chk_if(ln[5], 18)) return 0;
    if (ln[37][0] != 0x03 || ln[37][1] != 0x0f || be32(ln[37] + 13) != 6) return 0;
    if (ln[40][0] != 0x03 || ln[40][1] != 0x0a || be32(ln[40] + 13) != 2000) return 0;
    if (!forest_chk_item(ln[41], 4, 0x1a6)) return 0;
    put32(ln[1] + 18, 33); put32(ln[5] + 18, 100); put32(ln[37] + 13, 3); put16(ln[41] + 9, 0x0b8);
    return 1000;
}

/* v3.46: 1マス下の光のないセーブポイント（絵85・アニメ1・描く順番3）を本編と同じにする。
   本編は「セーブの場所（アニメ1・描く順番0）」＋「1マス下の光（アニメ2・向き1・判定なし・台本なし）」の2つ。
   チャレダン17か所と妖精の森・ルフェイン遺跡1F/B9F・ブルトガング山2枚は前者だけで、描く順番3（field_constant 0x52+3 = +1）のためキャラの上に描かれる。
   LoadMap は ReadBuffPackAndHeaderMap で毎回マップをコピーしてから LoadMapInfo(コピー+4) を呼び、その後でイベント数（コピー[イベント位置+4]）を読む。
   LoadMapInfo の時点でイベント数を1つ増やし、LoadEventData で最後に光のイベントを書き足す */
static unsigned char *g_spBlob;
static int g_spN;
static unsigned char g_spHdr[0x3f];
static int sp_is_light(const unsigned char *h) { return be16(h + 0x2b) == 85 && h[0x29] == 2 && h[0x2a] == 2; }
static void savept_prepare(unsigned char *blob)
{
    g_spBlob = 0; g_spN = 0;
    int evoff = be32(blob);
    if (evoff < 0x20 || evoff > 0x200000) return;
    int n = blob[evoff + 4];
    if (n < 1 || n > 250) return;
    unsigned char *p = blob + evoff + 5, *hs[256], *tgt = 0;
    int maxid = 0;
    for (int i = 0; i < n; i++) {
        hs[i] = p;
        int cnt = be16(p + 0x3f);
        if (cnt > 2000) return;
        unsigned char *q = p + 0x41;
        for (int k = 0; k < cnt; k++) q += 1 + q[0];
        if (be16(p) > maxid) maxid = be16(p);
        p = q;
    }
    for (int i = 0; i < n && !tgt; i++) {
        unsigned char *h = hs[i];
        if (be16(h + 0x2b) != 85 || h[0x28] != 0x0d || h[0x29] != 1 || h[0x2a] != 1 || h[0x33] != 3 || h[0x36] != 0) continue;
        int has = 0;
        for (int j = 0; j < n; j++) if (sp_is_light(hs[j]) && hs[j][2] == h[2] && hs[j][3] == h[3] + 1) has = 1;
        if (!has) tgt = h;
    }
    if (!tgt || maxid >= 0xff || tgt[3] >= 0xff) return;
    if ((p - (blob + evoff + 5)) + 0x41 + 4096 > (long)sizeof(g_evbuf)) return;
    for (int k = 0; k < 0x3f; k++) g_spHdr[k] = tgt[k];
    g_spHdr[0] = 0; g_spHdr[1] = (u8)(maxid + 1);
    g_spHdr[3] = (u8)(tgt[3] + 1);
    g_spHdr[6] = 0; g_spHdr[8] = 0;          /* 本編の光と同じ：判定なし・起動なし */
    g_spHdr[0x29] = 2; g_spHdr[0x2a] = 2;     /* アニメ2（薄い光） */
    g_spHdr[0x33] = 0; g_spHdr[0x36] = 1;     /* 描く順番0・向き1 */
    tgt[0x33] = 0;                            /* 元のセーブの場所も描く順番0（キャラの後ろ） */
    blob[evoff + 4] = (u8)(n + 1);
    g_spBlob = blob; g_spN = n + 1;
}

/* v3.46: 暗黒騎士編CD（章5 map2000 ev6、3つの宝箱の中身を入場時に抽選する下請け、90行）。
   13行目「乱数(1..100) <= 8 ならレア」を <= 100 にし、眠りの指輪・星のペンダント・銀縁メガネが残っている間は必ずレア。
   3つとも取った（または同じ挑戦で先の箱に入った）ときは、元の作りどおり 66行目から中の組へ回る */
static int dk_lottery(unsigned char *h, int cnt)
{
    if (cnt != 90 || h[1] != 6 || be16(h + 0x2b) != 7) return 0;
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 5 || g_curMap != 2000) return 0;
    unsigned char *q = h + 0x41, *L13 = 0, *L67 = 0;
    for (int k = 0; k < cnt; k++) { if (k == 13) L13 = q + 1; if (k == 67) L67 = q + 1; q += 1 + q[0]; }
    static const unsigned char want13[26] = { 0x3d,0,1,3,0,0,0,2,0,0,0,0,4,0x0a,0,0,0,0,0,0,0,8,0,5,0,0x0f };
    if (!L13 || L13[-1] != 26) return -1;
    for (int k = 0; k < 26; k++) if (L13[k] != want13[k]) return k == 21 && L13[k] == 100 ? 0 : -1;
    if (!(L67 && L67[0] == 0x03 && L67[1] == 0x0f && L67[16] == 4)) return -1;   /* レアの組の乱数(1..3) */
    L13[21] = 100;
    return 1;
}

/* v3.47: 暗黒の洞窟1F（章5 map401 ev24）・グルグ火山の麓（819 ev3）・灼熱の洞窟B1F（900 ev12）の鉱石の地点の抽選。
   入口で地点ごとに var1[0x14..] = 乱数(1..100) を振り、各マップで 30 以下の地点だけを出し、同じ数で中身を決める（31以上は中身の分岐がない）。
   9行目の上限 0x65 を 0x1f（1..30）にして毎回すべての地点を出す。中身の分かれ目には触らないので、出た地点の中身の確率は元のまま */
static int ore_gen_fix(unsigned char *h, int cnt)
{
    if (cnt != 15) return 0;
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 5) return 0;
    if (!((g_curMap == 401 && h[1] == 24) || (g_curMap == 819 && h[1] == 3) || (g_curMap == 900 && h[1] == 12))) return 0;
    static const unsigned char want9[17] = { 0x03,0x0f,0x02,0,0x01,0,0,0,0,0,0,0,0x01,0,0,0,0x65 };
    static const unsigned char want7[8] = { 0x04,0,0,0x01,0,0xe4,0,0x01 };
    unsigned char *q = h + 0x41, *L7 = 0, *L9 = 0;
    for (int k = 0; k < cnt; k++) { if (k == 7) L7 = q; if (k == 9) L9 = q; q += 1 + q[0]; }
    if (!L7 || !L9 || L7[0] != 8 || L9[0] != 17) return -1;
    for (int k = 0; k < 8; k++) if (L7[1 + k] != want7[k]) return -1;
    for (int k = 0; k < 17; k++) if (L9[1 + k] != want9[k]) return (k == 16 && L9[1 + k] == 0x1f) ? 0 : -1;
    L9[1 + 16] = 0x1f;
    return 1;
}

/* 1.1.1: モグのおまもり。エンカウントの前に共通イベント 0x0119 が var31 = 0..7 について GetReferenceChara(キャラ, 4, 4)
   （アクセサリー）== 393 を見るが、0〜7 は主人公8人だけで、ゲストとスティクス（21）は見ていなかった。
   2つの輪（4行目・12行目「var31 < 7 なら続ける」の 7）を 23 に（GetReferenceChara は 24 未満を受ける） */
static void charm_fix(unsigned char *h, int cnt)
{
    if (h[0] != 1 || h[1] != 0x19 || cnt != 17) return;
    static const unsigned char want[26] = { 0x3d,0,1,3,0,0,0,2,0,0,0,0x1f,4,0x0a,0,0,0,0,0,0,0,7,0,3,0,0 };
    unsigned char *q = h + 0x41;
    unsigned char *L[17];
    for (int k = 0; k < cnt; k++) { L[k] = q + 1; q += 1 + q[0]; }
    static int told;
    for (int j = 0; j < 2; j++) {
        unsigned char *x = L[j ? 12 : 4];
        if (x[-1] != 26) goto bad;
        for (int k = 0; k < 24; k++) if (x[k] != want[k] && !(k == 21 && x[k] == 23)) goto bad;
        if (x[25] != (j ? 0x0f : 0x07)) goto bad;
    }
    L[4][21] = 23; L[12][21] = 23;
    if (!told) { told = 1; LOGI("charm: common event 0x119 checks characters 0-23"); }
    return;
bad:
    if (!told) { told = 1; LOGE("charm: common event 0x119 differs, kept"); }
}

/* 1.1.7: ナイト編CDの屋外7マップ（章8 1900〜1913）は、入ったときのイベント（ev0）の命令 0x27「27 03 80 80 80 00 00 01 01 番号 00 00」で
   その番号以降のキャラを半分の明るさで描く（残骸を暗く見せる指定。本編にはない）。残す岩の山が暗く見えるので、色を ff ff ff にする */
/* 1.1.8: 忍者編CD（章9 1100〜1115）も同じ指定があるが、鎖（絵100）を暗くする行は本編（729/779）にもあるので残し、
   岩から始まる行だけを ff ff ff にする（マップと番号の組） */
static const short nj_tint[7][2] = { { 1100, 6 }, { 1103, 6 }, { 1107, 3 }, { 1109, 8 }, { 1111, 3 }, { 1112, 3 }, { 1113, 3 } };
static void knight_tint(unsigned char *h, int cnt)
{
    if (h[0] != 0 || h[1] != 0) return;
    int kn = g_curMap >= 1900 && g_curMap <= 1915, nj = g_curMap >= 1100 && g_curMap <= 1115;
    if (!kn && !nj) return;
    void *game = game_inst();
    if (!game) return;
    int ch = RD(int, game, G_CHAPTER);
    if (!((kn && ch == 8) || (nj && ch == 9))) return;
    unsigned char *q = h + 0x41;
    for (int k = 0; k < cnt; k++, q += 1 + q[0]) {
        unsigned char *L = q + 1;
        if (q[0] == 12 && L[0] == 0x27 && L[1] == 3 && L[2] == 0x80 && L[3] == 0x80 && L[4] == 0x80 &&
            L[5] == 0 && L[6] == 0 && L[7] == 1 && L[8] == 1 && L[10] == 0 && L[11] == 0) {
            if (nj) {
                int ok = 0;
                for (int j = 0; j < 7; j++) if (nj_tint[j][0] == g_curMap && nj_tint[j][1] == L[9]) ok = 1;
                if (!ok) continue;
            }
            L[2] = L[3] = L[4] = 0xff;
            LOGI("map: %s challenge %d tint from event %d removed", nj ? "ninja" : "knight", g_curMap, L[9]);
        }
    }
}

static void hook_LoadEv(void *field, unsigned char *data, int off, void *hdrs, void *lines, void *counts, int n)
{
    g_dashChara = 0; g_dashOrig = 0;               /* マップが変わったらダッシュの戻し待ちは捨てる */
    unsigned char *p = data + off;
    int addsp = (g_spBlob && data == g_spBlob && n == g_spN);
    int norig = addsp ? n - 1 : n;
    /* 差し替えるイベント：ヘッダー、新しい行、行数、元の行の終わり */
    unsigned char *rh[6]; const unsigned char *rs[6]; int rlen[6], rnl[6]; unsigned char *rend[6]; int nrep = 0;
    for (int i = 0; i < norig; i++) {
        unsigned char *h = p;
        int cnt = be16(h + 0x3f);
        unsigned char *q = h + 0x41, *first = cnt ? q : 0;
        int dest = -1;
        for (int k = 0; k < cnt; k++) {
            if (q[0] == 0x39 && q[1] == 0x6b && q[2] == 2 && q[3] == 9 && q[5] == 0)
                dest = be32(q + 6);
            q += 1 + q[0];
        }
        veh_fix(h);
        circle_fix(h);
        dancer_circle(h);
        if (cnt) msgbox_fix(h, cnt);
        charm_fix(h, cnt);
        if (cnt) knight_tint(h, cnt);
        if (be16(h + 0x2b) == 12 && h[7] == 1 && h[8] == 0) {
            if (first && dungeon_dest(dest) && first[1] == 0x3d && first[4] == 1 && be16(first + 7) == 5) {
                h[6] = 1; h[7] = 0; h[8] = 1;                 /* 人として出し、話しかけで起動 */
                moogle_face(h);
                if (dest == 3000 && h[2] == 8  && h[3] == 0) { h[2] = 6;  h[3] = 5; }    /* リーン城 */
                if (dest == 1400 && h[2] == 14 && h[3] == 0) { h[2] = 5;  h[3] = 7; }    /* 飛竜のすみか（v3.27: バルバラの2マス左） */
                if (dest == 1600 && h[2] == 41 && h[3] == 0) { h[2] = 10; h[3] = 10; }   /* 森の小屋 */
                if (dest == 2000 && h[2] == 34 && h[3] == 2) { h[2] = 38; h[3] = 16; }   /* 港町ルサルカ（v3.28: タルと重なるので池の下、花の間へ） */
                /* クリアフラグの確認を外す。右辺の種類2（フラグの状態）は値を「0以外なら1」に変換するので、
                   v3.27 で種類9（ただの数）に変え、「印 == 2」（成り立たない）にする */
                first[1 + 13] = 9;
                put32(first + 1 + 0x12, 2);
            } else if (cnt == 0 && nrep < 6) {
                int ch = empty_moogle_chapter(h);
                if (ch) {
                    h[6] = 1; h[7] = 0; h[8] = 1;
                    moogle_face(h);
                    rh[nrep] = h; rend[nrep] = h + 0x41;      /* 元は0行 */
                    if (ch == 7)      { rs[nrep] = moogle_script_7; rlen[nrep] = sizeof(moogle_script_7); rnl[nrep] = MOOGLE_LINES_7; }
                    else if (ch == 8) { rs[nrep] = moogle_script_8; rlen[nrep] = sizeof(moogle_script_8); rnl[nrep] = MOOGLE_LINES_8; }
                    else              { rs[nrep] = moogle_script_9; rlen[nrep] = sizeof(moogle_script_9); rnl[nrep] = MOOGLE_LINES_9; }
                    nrep++;
                }
            }
        }
        /* 1.1.8: 忍者編CD（章9 1100〜1115）も同じ。左端の行き止まりへの通路をふさぐ岩とその奥の岩16個は残す */
        if (cnt == 0 && be16(h + 0x2b) == 133 && h[6] == 1 && h[7] == 0 && h[8] == 1 && g_curMap >= 1100 && g_curMap <= 1115) {
            static const short keep[16][3] = {
                { 1107, 8, 13 }, { 1107, 1, 11 }, { 1107, 0, 12 }, { 1109, 6, 11 }, { 1109, 2, 10 }, { 1109, 0, 12 },
                { 1111, 7, 12 }, { 1111, 8, 13 }, { 1111, 5, 11 }, { 1111, 1, 10 }, { 1111, 0, 11 },
                { 1112, 8, 11 }, { 1112, 4, 10 }, { 1112, 2, 12 }, { 1113, 7, 25 }, { 1113, 8, 24 } };
            int kp = 0;
            for (int k = 0; k < 16; k++) if (keep[k][0] == g_curMap && keep[k][1] == h[2] && keep[k][2] == h[3]) kp = 1;
            void *game = game_inst();
            if (game && RD(int, game, G_CHAPTER) == 9) {
                if (!kp) { h[0x2b] = 0; h[0x2c] = 0; h[6] = 0; h[8] = 0; }
                else { h[0x28] = 7; h[0x29] = 1; h[0x2a] = 1; h[0x36] = 0; }
            }
        }
        /* 1.1.4: ナイト編CD（章8 1900〜1915）の岩の山（絵133、0行、通れない）を消す。絵をなくし、通れるようにする
           1.1.5: 右端の入れない門・戸口への通路をふさぐ14個（忍者編CDと対の作り）は残す */
        if (cnt == 0 && be16(h + 0x2b) == 133 && h[6] == 1 && h[7] == 0 && h[8] == 1 && g_curMap >= 1900 && g_curMap <= 1915) {
            static const short keep[14][3] = {
                { 1907, 26, 33 }, { 1907, 28, 31 }, { 1907, 32, 32 }, { 1909, 5, 70 }, { 1909, 6, 69 }, { 1909, 13, 71 },
                { 1912, 5, 12 }, { 1912, 6, 11 }, { 1912, 13, 13 },
                { 1913, 20, 12 }, { 1913, 21, 11 }, { 1913, 25, 12 }, { 1913, 27, 13 }, { 1913, 28, 14 } };
            int kp = 0;
            for (int k = 0; k < 14; k++) if (keep[k][0] == g_curMap && keep[k][1] == h[2] && keep[k][2] == h[3]) kp = 1;
            void *game = game_inst();
            if (game && RD(int, game, G_CHAPTER) == 8) {
                if (!kp) { h[0x2b] = 0; h[0x2c] = 0; h[6] = 0; h[8] = 0; }
                /* 1.1.6: 残す岩は本編の岩と同じ1枚絵の設定に（CD だけ 02 02 02・向き1 で、絵が細かく切られて小石に見えた） */
                else { h[0x28] = 7; h[0x29] = 1; h[0x2a] = 1; h[0x36] = 0; }
            }
        }
        /* 見えない魔法陣を見えるようにする */
        if (be16(h + 0x2b) == 125 && h[7] == 0 && h[0x29] == 0 && h[0x2a] == 0) {
            static const unsigned char circ[8] = { 0, 2, 0, 3, 2, 2, 0, 0 };   /* 本編の魔法陣と同じ向き・動き */
            h[0x28] = 0x35; h[0x29] = 4; h[0x2a] = 4;
            for (int k = 0; k < 8; k++) h[0x33 + k] = circ[k];
        }
        if (cnt) {
            return_fix(h, cnt);
            keep_switches(h, cnt);
        }
        if (cnt) {
            if (ore_gen_fix(h, cnt) < 0) LOGE("ore: map %d event %d differs, not patched", g_curMap, h[1]);
            local_event_patch(h, cnt);
        }
        if (cnt) {
            if (dk_lottery(h, cnt) < 0) LOGE("dkchest: map 2000 event 6 differs, not patched");
        }
        if (cnt) {
            forest_lottery(h, cnt);
            patch_lottery(h, cnt);
        }
        /* 暁の章のボスの宝箱 */
        if (cnt == DAWN_CHEST_ORIG_LINES && h[1] == 2 && be16(h + 0x2b) == 7 && nrep < 6
            && bytes_eq(first, dawn_chest_first, sizeof(dawn_chest_first))) {
            rh[nrep] = h; rend[nrep] = q; rs[nrep] = dawn_chest_script; rlen[nrep] = sizeof(dawn_chest_script);
            rnl[nrep] = DAWN_CHEST_LINES; nrep++;
        }
        p = q;
    }
    if (nrep || addsp) {
        unsigned char *end = p, *src = data + off;
        long need = (end - src) + (addsp ? 0x41 : 0);
        for (int j = 0; j < nrep; j++) need += rlen[j];
        if (need <= (long)sizeof(g_evbuf)) {
            unsigned char *o = g_evbuf;
            for (int j = 0; j < nrep; j++) {
                unsigned char *h = rh[j];
                for (unsigned char *s = src; s < h; s++) *o++ = *s;
                for (int k = 0; k < 0x3f; k++) o[k] = h[k];
                o[0x3f] = (u8)(rnl[j] >> 8); o[0x40] = (u8)rnl[j];
                o += 0x41;
                for (int k = 0; k < rlen[j]; k++) *o++ = rs[j][k];
                src = rend[j];
            }
            for (unsigned char *s = src; s < end; s++) *o++ = *s;
            if (addsp) {
                for (int k = 0; k < 0x3f; k++) o[k] = g_spHdr[k];
                o[0x3f] = 0; o[0x40] = 0;
                o += 0x41;
            }
            g_spBlob = 0;
            g_origLoadEv(field, g_evbuf, 0, hdrs, lines, counts, n);
            return;
        }
        LOGE("event data too large (%ld)", need);
        if (addsp) { LOGE("savept: light event dropped"); n--; }
    }
    g_spBlob = 0;
    g_origLoadEv(field, data, off, hdrs, lines, counts, n);
}

/* ---------- 地形の読み込み ----------
   竜騎士編チャレンジダンジョンB1Fの「崖の後ろを歩く」上の層は、スマホ版でチップの絵が消され、
   左端の 0xb0 番だけ別の絵の切れ端が残って黒い四角に見える。この 0xb0 番を消す */
#define OFF_LOADMI_FUNC 0x3bb03cUL   /* FieldClass::LoadMapInfo(char*, int) */
#define OFF_LOADMI_GOT  0x45f100UL
typedef void (*LoadMIFn)(void *field, unsigned char *data, int off);
static LoadMIFn g_origLoadMI;


/* ---------- v3.14: 暁の章のチャレンジダンジョンのタロス ----------
   タロス(No.92)は終章と同じデータ(Lv64/HP41000/攻撃力210/倍率15/素早さ58)。
   章0(暁の章)で使うのはチャレンジダンジョンのボス(編成189)だけなので、
   章0の戦闘でだけ敵データ(GetEnemyData の 0x50 バイト)を書き換え、ほかの章では元に戻す。
   報酬(EXP/AP)も戦闘後にこの表から読むので、戦闘中は書き換えたままにする。 */
#define OFF_SETBEP_FUNC 0x39b1acUL   /* BattleClass::SetBtlEnemyParam(BTLACT&, int, int, int, int) */
#define OFF_SETBEP_GOT  0x45e2c0UL
#define OFF_GETENEMY    0x3e7938UL   /* GameClass::GetEnemyData(int) */
#define TALOS_ID 92
typedef void (*SetBEPFn)(void *bc, void *act, int id, int a, int b, int c);
typedef unsigned char *(*GetEnemyFn)(void *game, int id);
static SetBEPFn g_origSetBEP;
static unsigned char g_talosOrig[0x50];
static int g_talosSaved, g_talosState = -1;
static void talos_apply(int dawn)
{
    void *game = game_inst();
    if (!game) return;
    GetEnemyFn ge = (GetEnemyFn)(g_base + OFF_GETENEMY);
    unsigned char *e = ge(game, TALOS_ID);
    if (!e) return;
    if (!g_talosSaved) {
        if (e[0x8] != 64) { LOGE("talos: unexpected level %d, skip", e[0x8]); return; }
        for (int i = 0; i < 0x50; i++) g_talosOrig[i] = e[i];
        g_talosSaved = 1;
    }
    if (dawn == g_talosState) return;
    for (int i = 0; i < 0x50; i++) e[i] = g_talosOrig[i];
    if (dawn) {
        e[0x08] = 20;                         /* Lv */
        *(int *)(e + 0x0c) = 4000;            /* HP */
        *(int *)(e + 0x10) = 2000;            /* EXP */
        e[0x18] = 10;                         /* AP */
        e[0x19] = 70;                         /* 攻撃力 */
        e[0x1a] = 2;                          /* 攻撃の倍率 */
        e[0x1c] = 15;                         /* 防御力 */
        e[0x22] = 16; e[0x23] = 16;           /* 素早さ 下限・上限 */
    }
    g_talosState = dawn;
}
/* v3.25: ストーンゴーレム(No.107、Lv29)の防御力 29 → 20（全章）。敵データの表をそのまま書き換える */
#define STONEGOLEM_ID 107
static void stonegolem_apply(void)
{
    void *game = game_inst();
    if (!game) return;
    unsigned char *e = ((GetEnemyFn)(g_base + OFF_GETENEMY))(game, STONEGOLEM_ID);
    if (!e || e[0x1c] == 20) return;
    if (e[0x08] != 29 || e[0x1c] != 29) { LOGE("stone golem: unexpected Lv %d def %d, skip", e[0x08], e[0x1c]); return; }
    e[0x1c] = 20;
}
/* v3.53: アースイーター(No.372)は終章と同じデータ(Lv64/HP38560/攻撃190/倍率16/素早さ60/魔力90)。
   踊り子編で使うのはチャレンジダンジョンの最後のボス(編成158)だけなので、タロスと同じく章7の戦闘でだけ書き換え、ほかの章では元に戻す。
   Lv50 HP25000 攻撃120 倍率14 素早さ30-30 魔力50（v3.55・v3.56 で攻撃・倍率、v3.57 で魔力を上げた）。ほかの欄（防御・魔防・命中・EXP・AP など）は元のまま */
#define EARTHEATER_ID 372
static unsigned char g_eeOrig[0x50];
static int g_eeSaved, g_eeState = -1;
static void eartheater_apply(int dancer)
{
    void *game = game_inst();
    if (!game) return;
    unsigned char *e = ((GetEnemyFn)(g_base + OFF_GETENEMY))(game, EARTHEATER_ID);
    if (!e) return;
    if (!g_eeSaved) {
        if (e[0x8] != 64) { LOGE("earth eater: unexpected level %d, skip", e[0x8]); return; }
        for (int i = 0; i < 0x50; i++) g_eeOrig[i] = e[i];
        g_eeSaved = 1;
    }
    if (dancer == g_eeState) return;
    for (int i = 0; i < 0x50; i++) e[i] = g_eeOrig[i];
    if (dancer) {
        e[0x08] = 50;                         /* Lv */
        *(int *)(e + 0x0c) = 25000;           /* HP */
        e[0x19] = 120;                        /* 攻撃力（v3.55: 100→110、v3.56: →120） */
        e[0x1a] = 14;                         /* 攻撃の倍率（v3.55: 10→12、v3.56: →14） */
        e[0x22] = 30; e[0x23] = 30;           /* 素早さ 下限・上限 */
        e[0x24] = 50;                         /* 魔力（v3.57: 40→50） */
    }
    g_eeState = dancer;
}
/* 1.1.7: エルダードラゴン(No.421、Lv65 HP52600 攻撃196 倍率16 素早さ58 魔力110)。ナイト編はチャレンジダンジョンのボス（編成160）、
   終章は編成141。アースイーターと同じく章8の戦闘でだけ書き換え、ほかの章では元に戻す。
   Lv60 HP32000 攻撃164 倍率10 素早さ40-40 魔力86（ユーザー指定）。ほかの欄は元のまま */
#define ELDERDRAGON_ID 421
static unsigned char g_edOrig[0x50];
static int g_edSaved, g_edState = -1;
static void elderdragon_apply(int knight)
{
    void *game = game_inst();
    if (!game) return;
    unsigned char *e = ((GetEnemyFn)(g_base + OFF_GETENEMY))(game, ELDERDRAGON_ID);
    if (!e) return;
    if (!g_edSaved) {
        if (e[0x8] != 65 || *(int *)(e + 0x0c) != 52600) { LOGE("elder dragon: unexpected Lv %d HP %d, skip", e[0x8], *(int *)(e + 0x0c)); return; }
        for (int i = 0; i < 0x50; i++) g_edOrig[i] = e[i];
        g_edSaved = 1;
    }
    if (knight == g_edState) return;
    for (int i = 0; i < 0x50; i++) e[i] = g_edOrig[i];
    if (knight) {
        e[0x08] = 60;                         /* Lv */
        *(int *)(e + 0x0c) = 32000;           /* HP */
        e[0x19] = 164;                        /* 攻撃力 */
        e[0x1a] = 10;                         /* 攻撃の倍率 */
        e[0x22] = 40; e[0x23] = 40;           /* 素早さ 下限・上限 */
        e[0x24] = 86;                         /* 魔力 */
    }
    g_edState = knight;
    LOGI("elder dragon: %s", knight ? "knight chapter values" : "original values");
}
static void hook_SetBEP(void *bc, void *act, int id, int a, int b, int c)
{
    if (id == ELDERDRAGON_ID) {
        void *game = game_inst();
        elderdragon_apply(game && RD(int, game, G_CHAPTER) == 8);
    }
    if (id == EARTHEATER_ID) {
        void *game = game_inst();
        eartheater_apply(game && RD(int, game, G_CHAPTER) == 7);
    }
    if (id == STONEGOLEM_ID) stonegolem_apply();
    if (id == TALOS_ID) {
        void *game = game_inst();
        talos_apply(game && RD(int, game, G_CHAPTER) == 0);
    }
    g_origSetBEP(bc, act, id, a, b, c);
    slow_magic_apply();
    /* v3.36: スロウの耐性貫通（案A）。敵の状態異常の効かない印（BTLACT+0x98、敵データ+0x34）の bit2＝スロウを消す */
    if (act) {
        unsigned *imm = (unsigned *)((unsigned char *)act + 0x98);
        if (*imm & 4) {
            *imm &= ~4u;
        }
    }
}

/* v3.14: 暁の章のチャレンジダンジョン(ch0 マップ3000〜3002)は地名が「名称未設定」で表示もオフ。
   B2F(3001)は入り口の上の1マス(5,32)が黒いマス(0x2e)になっている。
   マップの見分け: 章0・地名0・40x40・先頭 d[4..4096) の FNV-1a */
static unsigned fnv1a(const unsigned char *p, int n)
{
    unsigned h = 0x811c9dc5u;
    for (int i = 0; i < n; i++) { h ^= p[i]; h *= 0x01000193u; }
    return h;
}
static void cd0_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 0) return;
    if (d[2] || d[3] || be16(d + 4) != 40 || be16(d + 6) != 40) return;
    unsigned h = fnv1a(d + 4, 4092);
    int name = 0, mid = 0;
    if (h == 0xfab13d2du) { name = 69; mid = 3000; }       /* チャレンジダンジョン　Ｂ１Ｆ */
    else if (h == 0x0968b9cdu) { name = 70; mid = 3001; }  /* Ｂ２Ｆ */
    else if (h == 0xdafdc549u) { name = 38; mid = 3002; }  /* Ｂ３Ｆ */
    if (!name) return;
    d[1] = 1; d[2] = (unsigned char)(name >> 8); d[3] = (unsigned char)name;
    if (mid == 3001 && d[2620] == 0x2e && d[2621] == 0) d[2620] = 0x77;   /* 下の層の (5,32) を左上と同じ岩壁に */
}

/* v3.24: レンジャー編のチャレンジダンジョン(ch3 1600・1603)は、下の層に茂みの組の空き番号
   (0x35 0x36 0x43 0x52：透明)が置かれていて黒く抜ける。上下左右に茂みが続くかを見て、
   茂みの組（上 0x30/0x32/0x34、中 0x40/0x41/0x44、下 0x50/0x53/0x54）から合う縁を選ぶ */
static int rg_bush(int v) { return (v >= 0x30 && v <= 0x36) || (v >= 0x40 && v <= 0x44) || (v >= 0x50 && v <= 0x54); }
static int rg_blank(int v) { return v == 0x35 || v == 0x36 || v == 0x43 || v == 0x52; }
static void rg_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 3) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (!((W == 59 || W == 61) && H == 65 && nl >= 1 && d[0x16])) return;
    unsigned h = fnv1a(d + 4, 4092);
    int mid = h == 0x638b9f38u ? 1600 : h == 0x3e93510eu ? 1603 : 0;
    if (!mid) return;
    unsigned char *q = d + 0x16 + nl * 7;   /* 下の層の番号（2バイト、下位が先） */
    #define RGV(x, y) ((x) >= 0 && (x) < W && (y) >= 0 && (y) < H ? (q[((y) * W + (x)) * 2] | q[((y) * W + (x)) * 2 + 1] << 8) : 0)
    static const unsigned char tab[3][3] = { { 0x30, 0x32, 0x34 }, { 0x40, 0x41, 0x44 }, { 0x50, 0x53, 0x54 } };
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            if (!rg_blank(RGV(x, y))) continue;
            int u = rg_bush(RGV(x, y - 1)), dn = rg_bush(RGV(x, y + 1));
            int l = rg_bush(RGV(x - 1, y)), r = rg_bush(RGV(x + 1, y));
            int row = !u ? 0 : !dn ? 2 : 1, col = !l ? 0 : !r ? 2 : 1;
            q[(y * W + x) * 2] = tab[row][col]; q[(y * W + x) * 2 + 1] = 0;
        }
    #undef RGV
}

/* v3.26: レンジャー編のチャレンジダンジョン3マップ目(ch3 1602・1605・1606、乱数で1枚)の隠し通路を見えるように。
   隠し通路＝下の層が草地(0x24)で上の層に木(0x9d 0x9e 0xad 0xae)を重ねたマス。上の層を濃い木
   (0xbd 0xbe)に差し替える。capk.dat の属性は元の木と同じなので、通れるかどうかは変わらない */
/* v3.32: 読み込み中のマップ番号を覚える（LoadEventData の中で使う） */
#define OFF_LOADMAP_FUNC 0x3b1bd4UL   /* FieldClass::LoadMap(int mapID, int layer, int x, int y, int dir, bool resume) */
#define OFF_LOADMAP_GOT  0x45f0f0UL
typedef void (*LoadMapFn)(void *field, int map, int layer, int x, int y, int dir, int resume);
static LoadMapFn g_origLoadMap;
static void hook_LoadMap(void *field, int map, int layer, int x, int y, int dir, int resume)
{
    g_curMap = map;
    g_origLoadMap(field, map, layer, x, y, dir, resume);
    slow_magic_apply();
}

static void rg_hidden(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 3) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (W != 61 || H != 65 || nl < 2) return;
    unsigned h = fnv1a(d + 4, 4092);
    int mid = h == 0x1b45cf82u ? 1602 : h == 0xc8082f38u ? 1605 : h == 0xbacfb7f5u ? 1606 : 0;
    if (!mid) return;
    unsigned char *ly = d + 0x16, *p = ly + nl * 7, *ids[2] = { 0, 0 };
    for (int i = 0; i < 2; i++) {
        unsigned char *L = ly + i * 7;
        if (!L[0]) return;
        ids[i] = p; p += W * H * 2;
        if (L[2]) p += W * H;
        if (L[1]) p += W * H;
    }
    for (int i = 0; i < W * H; i++) {
        unsigned char *a = ids[0] + i * 2, *b = ids[1] + i * 2;
        if (a[0] != 0x24 || a[1] != 0 || b[1] != 0) continue;
        unsigned char v = b[0], n = 0;
        if (v == 0x9d || v == 0xad) n = 0xbd;
        else if (v == 0x9e || v == 0xae) n = 0xbe;
        if (n) b[0] = n;
    }
}

/* v3.34: 吟遊詩人編のチャレンジダンジョン（章4）の地図の直し
   - 2011/2012（草原）：茂みの空き番号 0x35/0x43（絵なし、黒く見える）を上下左右の茂みから端・真ん中のタイルに
   - 2011 (14,14)：草地の中に花壇の枠のかけら 0x16 → 草地 0x24
   - 2020（洞窟）(12,11) 0x6f → 岩の頂上 0x12、(12,14) 0x2e → 岩壁 0x77（本編の入口の上と同じ並び）
   - 2（海）：背景のアニメ（えぐれた所の紫の雲）を本編と同じ 01 03 00 に（v3.35）
   どれも元の値を確かめてから書き換える */
static void bard_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 4) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (nl < 1 || nl > 8 || !d[0x16]) return;
    unsigned char *q = d + 0x16 + nl * 7;
    static const struct { short map, w, h, x, y, from, to; } fx[] = {
        { 2011, 15, 21,  2, 16, 0x35, 0x41 }, { 2011, 15, 21,  5, 19, 0x43, 0x54 }, { 2011, 15, 21, 14, 14, 0x16, 0x24 },
        { 2012, 16, 21,  2,  4, 0x43, 0x54 }, { 2012, 16, 21,  9, 20, 0x43, 0x54 },
        { 2020, 33, 25, 12, 11, 0x6f, 0x12 }, { 2020, 33, 25, 12, 14, 0x2e, 0x77 },
    };
    for (unsigned i = 0; i < sizeof(fx) / sizeof(fx[0]); i++) {
        if (fx[i].map != g_curMap || fx[i].w != W || fx[i].h != H) continue;
        unsigned char *c = q + (fx[i].y * W + fx[i].x) * 2;
        if ((c[0] | c[1] << 8) != fx[i].from) continue;
        c[0] = (unsigned char)fx[i].to; c[1] = 0;
    }
    if (g_curMap == 2 && W == 100 && H == 100) {
        unsigned char *p = q;
        for (int i = 0; i < nl; i++) {
            unsigned char *L = d + 0x16 + i * 7;
            if (!L[0]) continue;
            p += W * H * 2;
            if (L[2]) p += W * H;
            if (L[1]) p += W * H;
        }
        /* v3.35: えぐれた領域（0x10）のある本編の地図は全部、遠景のあとの「背景のアニメ」が 01 03 00（紫の雲）。
           この地図だけ 00 00 00 で、空の絵がそのまま見えていた。遠景は残し、背景のアニメを本編と同じにする */
        if (p[10] == 1 && p[13] == 0 && p[14] == 0 && p[15] == 0) {
            p[13] = 1; p[14] = 3; p[15] = 0;
        }
    }
}

/* v3.40t: 暗黒騎士編のチャレンジダンジョン（章5）の地図の直し
   - 2000（洞窟）(5,1) 0x2e → 0x22（中央の欠け）
   - 2001 (14,14) 0x1c → 0x01（グラムの右の消し残し）
   元の値と地図の大きさを確かめてから書き換える */
static void dk_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 5) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (nl < 1 || nl > 8 || !d[0x16]) return;
    unsigned char *q = d + 0x16 + nl * 7;
    static const struct { short map, w, h, x, y, from, to; } fx[] = {
        { 2000, 11, 12,  5,  1, 0x2e, 0x22 }, { 2001, 15, 20, 14, 14, 0x1c, 0x01 },
    };
    for (unsigned i = 0; i < sizeof(fx) / sizeof(fx[0]); i++) {
        if (fx[i].map != g_curMap || fx[i].w != W || fx[i].h != H) continue;
        unsigned char *c = q + (fx[i].y * W + fx[i].x) * 2;
        if ((c[0] | c[1] << 8) != fx[i].from) continue;
        c[0] = (unsigned char)fx[i].to; c[1] = 0;
    }
}

/* v3.48: メモリスト編のチャレンジダンジョン（章6）の地図の欠け（絵のない番号が黒く抜ける）
   - 2006/2010（砂漠）：崖の下の 0x71/0x75 → 砂 0x01（本編の同じ崖の下は 0x01、通れるかの属性も同じ）
   - 2003/2009（森）：茂みの空き番号 0x35/0x36/0x43/0x52 → レンジャー編と同じく上下左右の茂みで縁を選ぶ
   - 2008（洞窟）：0x2e → 岩壁 0x77（吟遊詩人編と同じ、属性同じ）
   （v3.49：2004（町）の (26..28,16) は隠し通路だったので書き換えをやめた）
   地図番号・大きさ・元の値を確かめてから書き換える */
static void mem_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 6) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (nl < 1 || nl > 8 || !d[0x16]) return;
    int m = g_curMap, kind = 0;
    if ((m == 2006 && W == 40 && H == 40) || (m == 2010 && W == 44 && H == 43)) kind = 1;
    else if ((m == 2003 && W == 40 && H == 40) || (m == 2009 && W == 50 && H == 50)) kind = 2;
    else if (m == 2008 && W == 40 && H == 40) kind = 3;
    if (!kind) return;
    unsigned char *q = d + 0x16 + nl * 7;
    #define MMV(x, y) ((x) >= 0 && (x) < W && (y) >= 0 && (y) < H ? (q[((y) * W + (x)) * 2] | q[((y) * W + (x)) * 2 + 1] << 8) : 0)
    #define MMSET(x, y, v) do { q[((y) * W + (x)) * 2] = (unsigned char)(v); q[((y) * W + (x)) * 2 + 1] = 0; } while (0)
    static const unsigned char tab[3][3] = { { 0x30, 0x32, 0x34 }, { 0x40, 0x41, 0x44 }, { 0x50, 0x53, 0x54 } };
    int cnt = 0;
    {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int v = MMV(x, y);
                if (kind == 1 && (v == 0x71 || v == 0x75)) { MMSET(x, y, 0x01); cnt++; }
                else if (kind == 3 && v == 0x2e) { MMSET(x, y, 0x77); cnt++; }
                else if (kind == 2 && rg_blank(v)) {
                    int u = rg_bush(MMV(x, y - 1)), dn = rg_bush(MMV(x, y + 1));
                    int l = rg_bush(MMV(x - 1, y)), r = rg_bush(MMV(x + 1, y));
                    MMSET(x, y, tab[!u ? 0 : !dn ? 2 : 1][!l ? 0 : !r ? 2 : 1]); cnt++;
                }
            }
    }
    #undef MMV
    #undef MMSET
    LOGI("map: memorist challenge %d gaps filled (%d)", m, cnt);
}
/* v3.53: 踊り子編のチャレンジダンジョン（章7）の地図
   - 1100〜1104（砂漠）：崖の下の絵のない 0x75 → 砂 0x01、0x71 → 真上が 0x51 なら 0x61、ほかは 0x01
     （本編の同じタイルセット mc50 で、0x99/0x9f の下は 0x01、0x51 の下は 0x61。通れるかの属性は4つとも同じ）
   - 1105：脱出の仕掛け (12,25) の床に描かれた四角い模様 0x18c → 周りと同じ床 0x102（魔法陣の絵は hook_LoadEv で出す）
   地図番号・大きさ・元の値を確かめてから書き換える */
static void dancer_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 7) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (nl < 1 || nl > 8 || !d[0x16]) return;
    int m = g_curMap;
    static const short sz[6][3] = { { 1100, 20, 34 }, { 1101, 57, 68 }, { 1102, 63, 79 }, { 1103, 43, 91 }, { 1104, 42, 93 }, { 1105, 21, 38 } };
    int ok = 0;
    for (int i = 0; i < 6; i++) if (sz[i][0] == m && sz[i][1] == W && sz[i][2] == H) ok = 1;
    if (!ok) return;
    unsigned char *q = d + 0x16 + nl * 7;
    #define DMV(x, y) ((x) >= 0 && (x) < W && (y) >= 0 && (y) < H ? (q[((y) * W + (x)) * 2] | q[((y) * W + (x)) * 2 + 1] << 8) : 0)
    #define DMSET(x, y, v) do { q[((y) * W + (x)) * 2] = (unsigned char)((v) & 0xff); q[((y) * W + (x)) * 2 + 1] = (unsigned char)((v) >> 8); } while (0)
    int cnt = 0;
    if (m == 1105) {
        if (DMV(12, 25) == 0x18c) { DMSET(12, 25, 0x102); cnt++; }
        else LOGE("map: dancer challenge 1105 (12,25) is %x, not patched", DMV(12, 25));
    } else {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                int v = DMV(x, y);
                if (v == 0x75) { DMSET(x, y, 0x01); cnt++; }
                else if (v == 0x71) { DMSET(x, y, DMV(x, y - 1) == 0x51 ? 0x61 : 0x01); cnt++; }
            }
    }
    #undef DMV
    #undef DMSET
    LOGI("map: dancer challenge %d fixed (%d)", m, cnt);
}

/* 1.1.4: ナイト編のチャレンジダンジョン（章8）の地図
   - 1900/1903/1907：外壁の窓の位置に置かれた絵のない 0xb2 → 1913 と同じ窓 0x38
   - 1900/1913/1914：門の内側（1.1.5）。中の段の瓦礫 0x2c と絵のない 0x2d → 無地の暗がり 0x2b、下の段の 0x3c（灰色）・0x3d（ぼかしなし）→ 丸石 0x3b
   - 1909：戸口の下（層0が空）→ 床 0x1c
   - 1900/1909/1913：床の真ん中の縁の切れ端（0x12/0x26/0x27）→ 床 0x22
   - 屋外の7マップ（1.1.5）：背景のアニメを次元城（章12 1630）と同じ 01 04 00 に（地の色 #210031 はこの絵の地の色）
   地図番号・大きさ・元の値を確かめてから書き換える */
static const struct { short map; unsigned char layer, x, y; unsigned short old, nw; } kn_fix[] = {
    { 1900, 1, 8, 7, 0x12, 0x22 },    { 1900, 1, 9, 7, 0x12, 0x22 },    { 1900, 1, 10, 7, 0x12, 0x22 },
    { 1900, 1, 7, 8, 0x26, 0x22 },    { 1900, 1, 11, 8, 0x27, 0x22 },    { 1900, 1, 5, 10, 0x12, 0x22 },
    { 1900, 1, 6, 10, 0x26, 0x22 },    { 1900, 1, 12, 10, 0x27, 0x22 },    { 1900, 1, 13, 10, 0x12, 0x22 },
    { 1900, 1, 3, 11, 0x12, 0x22 },    { 1900, 1, 4, 11, 0x26, 0x22 },    { 1900, 1, 14, 11, 0x27, 0x22 },
    { 1900, 1, 15, 11, 0x12, 0x22 },    { 1900, 1, 8, 15, 0xb2, 0x38 },    { 1900, 1, 10, 15, 0xb2, 0x38 },
    { 1900, 1, 10, 18, 0x2d, 0x2b },    { 1900, 1, 9, 18, 0x2c, 0x2b },    { 1900, 1, 9, 19, 0x3c, 0x3b },    { 1900, 1, 10, 19, 0x3d, 0x3b },
       { 1903, 1, 8, 5, 0xb2, 0x38 },    { 1903, 1, 10, 5, 0xb2, 0x38 },
    { 1903, 1, 4, 6, 0xb2, 0x38 },    { 1903, 1, 14, 6, 0xb2, 0x38 },    { 1903, 1, 8, 7, 0xb2, 0x38 },
    { 1903, 1, 10, 7, 0xb2, 0x38 },    { 1903, 1, 1, 28, 0xb2, 0x38 },    { 1903, 1, 17, 28, 0xb2, 0x38 },
    { 1903, 1, 4, 31, 0xb2, 0x38 },    { 1903, 1, 14, 31, 0xb2, 0x38 },    { 1903, 1, 7, 32, 0xb2, 0x38 },
    { 1903, 1, 11, 32, 0xb2, 0x38 },    { 1907, 1, 18, 3, 0xb2, 0x38 },    { 1907, 1, 20, 3, 0xb2, 0x38 },
    { 1907, 1, 13, 4, 0xb2, 0x38 },    { 1907, 1, 15, 4, 0xb2, 0x38 },    { 1907, 1, 23, 4, 0xb2, 0x38 },
    { 1907, 1, 25, 4, 0xb2, 0x38 },    { 1907, 1, 18, 5, 0xb2, 0x38 },    { 1907, 1, 20, 5, 0xb2, 0x38 },
    { 1907, 1, 18, 7, 0xb2, 0x38 },    { 1907, 1, 20, 7, 0xb2, 0x38 },    { 1907, 1, 13, 8, 0xb2, 0x38 },
    { 1907, 1, 15, 8, 0xb2, 0x38 },    { 1907, 1, 23, 8, 0xb2, 0x38 },    { 1907, 1, 25, 8, 0xb2, 0x38 },
    { 1909, 1, 8, 45, 0x12, 0x22 },    { 1909, 1, 9, 45, 0x12, 0x22 },    { 1909, 1, 10, 45, 0x12, 0x22 },
    { 1909, 0, 9, 51, 0x0, 0x1c },    { 1909, 1, 19, 69, 0x12, 0x22 },    { 1909, 0, 19, 76, 0x0, 0x1c },
    { 1913, 1, 8, 5, 0x12, 0x22 },    { 1913, 1, 9, 5, 0x12, 0x22 },    { 1913, 1, 10, 5, 0x12, 0x22 },
    { 1913, 1, 28, 5, 0x12, 0x22 },    { 1913, 1, 29, 5, 0x12, 0x22 },    { 1913, 1, 30, 5, 0x12, 0x22 },
    { 1913, 1, 20, 9, 0x2d, 0x2b },    { 1913, 1, 19, 9, 0x2c, 0x2b },    { 1913, 1, 19, 10, 0x3c, 0x3b },    { 1913, 1, 20, 10, 0x3d, 0x3b },
    { 1914, 0, 6, 3, 0x12d, 0x12b },    { 1914, 0, 5, 3, 0x12c, 0x12b },    { 1914, 0, 5, 4, 0x13c, 0x13b },    { 1914, 0, 6, 4, 0x13d, 0x13b },
};
/* 1.1.8: 忍者編CD（章9 1100〜1113）。ナイト編と同じ直し（窓 0xb2→0x38、門の内側、縁の切れ端→0x22、屋外の背景）。表は元の値を確かめる */
static const struct { short map; unsigned char layer, x, y; unsigned short old, nw; } nj_fix[] = {
    { 1100, 1, 3, 0, 0xb2, 0x38 },
    { 1100, 1, 15, 0, 0xb2, 0x38 },
    { 1100, 1, 8, 1, 0xb2, 0x38 },
    { 1100, 1, 10, 1, 0xb2, 0x38 },
    { 1100, 1, 3, 3, 0xb2, 0x38 },
    { 1100, 1, 8, 3, 0xb2, 0x38 },
    { 1100, 1, 10, 3, 0xb2, 0x38 },
    { 1100, 1, 15, 3, 0xb2, 0x38 },
    { 1100, 1, 8, 5, 0xb2, 0x38 },
    { 1100, 1, 10, 5, 0xb2, 0x38 },
    { 1100, 1, 3, 6, 0xb2, 0x38 },
    { 1100, 1, 15, 6, 0xb2, 0x38 },
    { 1100, 1, 9, 9, 0x2c, 0x2b },
    { 1100, 1, 10, 9, 0x2d, 0x2b },
    { 1100, 1, 9, 10, 0x3c, 0x3b },
    { 1100, 1, 10, 10, 0x3d, 0x3b },
    { 1100, 1, 2, 20, 0x12, 0x22 },
    { 1100, 1, 4, 20, 0x12, 0x22 },
    { 1100, 1, 16, 20, 0x12, 0x22 },
    { 1100, 1, 3, 26, 0xb2, 0x38 },
    { 1100, 1, 15, 26, 0xb2, 0x38 },
    { 1100, 1, 3, 29, 0xb2, 0x38 },
    { 1100, 1, 15, 29, 0xb2, 0x38 },
    { 1100, 1, 3, 32, 0xb2, 0x38 },
    { 1100, 1, 15, 32, 0xb2, 0x38 },
    { 1100, 1, 3, 35, 0xb2, 0x38 },
    { 1100, 1, 15, 35, 0xb2, 0x38 },
    { 1100, 1, 7, 36, 0xb2, 0x38 },
    { 1100, 1, 8, 36, 0xb2, 0x38 },
    { 1100, 1, 10, 36, 0xb2, 0x38 },
    { 1100, 1, 11, 36, 0xb2, 0x38 },
    { 1100, 1, 3, 38, 0xb2, 0x38 },
    { 1100, 1, 15, 38, 0xb2, 0x38 },
    { 1100, 1, 7, 39, 0xb2, 0x38 },
    { 1100, 1, 8, 39, 0xb2, 0x38 },
    { 1100, 1, 10, 39, 0xb2, 0x38 },
    { 1100, 1, 11, 39, 0xb2, 0x38 },
    { 1100, 1, 3, 41, 0xb2, 0x38 },
    { 1100, 1, 15, 41, 0xb2, 0x38 },
    { 1103, 1, 10, 0, 0xb2, 0x38 },
    { 1103, 1, 14, 0, 0xb2, 0x38 },
    { 1103, 1, 2, 1, 0xb2, 0x38 },
    { 1103, 1, 5, 1, 0xb2, 0x38 },
    { 1103, 1, 19, 1, 0xb2, 0x38 },
    { 1103, 1, 22, 1, 0xb2, 0x38 },
    { 1103, 1, 10, 2, 0xb2, 0x38 },
    { 1103, 1, 14, 2, 0xb2, 0x38 },
    { 1103, 1, 2, 6, 0xb2, 0x38 },
    { 1103, 1, 3, 6, 0xb2, 0x38 },
    { 1103, 1, 21, 6, 0xb2, 0x38 },
    { 1103, 1, 22, 6, 0xb2, 0x38 },
    { 1103, 1, 6, 7, 0xb2, 0x38 },
    { 1103, 1, 8, 7, 0xb2, 0x38 },
    { 1103, 1, 16, 7, 0xb2, 0x38 },
    { 1103, 1, 18, 7, 0xb2, 0x38 },
    { 1103, 1, 11, 8, 0xb2, 0x38 },
    { 1103, 1, 13, 8, 0xb2, 0x38 },
    { 1103, 1, 8, 20, 0x26, 0x22 },
    { 1103, 1, 10, 20, 0xb2, 0x38 },
    { 1103, 1, 14, 20, 0xb2, 0x38 },
    { 1103, 1, 18, 20, 0x26, 0x22 },
    { 1103, 1, 11, 30, 0xb2, 0x38 },
    { 1103, 1, 13, 30, 0xb2, 0x38 },
    { 1103, 1, 8, 31, 0xb2, 0x38 },
    { 1103, 1, 16, 31, 0xb2, 0x38 },
    { 1103, 1, 11, 33, 0xb2, 0x38 },
    { 1103, 1, 13, 33, 0xb2, 0x38 },
    { 1103, 1, 8, 34, 0xb2, 0x38 },
    { 1103, 1, 16, 34, 0xb2, 0x38 },
    { 1107, 1, 17, 10, 0x12, 0x22 },
    { 1107, 1, 14, 11, 0x12, 0x22 },
    { 1107, 1, 15, 11, 0x12, 0x22 },
    { 1109, 1, 2, 45, 0x12, 0x22 },
    { 1109, 1, 3, 45, 0x12, 0x22 },
    { 1109, 1, 4, 45, 0x12, 0x22 },
    { 1111, 1, 8, 4, 0x12, 0x22 },
    { 1111, 1, 9, 4, 0x12, 0x22 },
    { 1111, 1, 10, 4, 0x12, 0x22 },
    { 1111, 1, 0, 8, 0x2d, 0x2b },
    { 1111, 1, 0, 9, 0x3d, 0x3b },
    { 1113, 1, 1, 31, 0x12, 0x22 },
    { 1113, 1, 2, 31, 0x12, 0x22 },
    { 1113, 1, 3, 31, 0x12, 0x22 },
    { 1113, 1, 21, 31, 0x12, 0x22 },
    { 1113, 1, 22, 31, 0x12, 0x22 },
    { 1113, 1, 23, 31, 0x12, 0x22 },
};
static void knight_mapfix(unsigned char *d)
{
    void *game = game_inst();
    if (!game) return;
    int ch = RD(int, game, G_CHAPTER);
    int kn = ch == 8 && g_curMap >= 1900 && g_curMap <= 1915, nj = ch == 9 && g_curMap >= 1100 && g_curMap <= 1115;
    if (!kn && !nj) return;
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    if (nl < 2 || nl > 8) return;
    static const short sz[15][3] = { { 1900, 19, 33 }, { 1903, 19, 37 }, { 1907, 35, 45 }, { 1909, 20, 85 }, { 1911, 22, 26 }, { 1912, 14, 23 }, { 1913, 31, 26 }, { 1914, 11, 16 },
        { 1100, 19, 42 }, { 1103, 25, 36 }, { 1107, 34, 33 }, { 1109, 13, 79 }, { 1111, 19, 24 }, { 1112, 15, 39 }, { 1113, 29, 37 } };
    int m = g_curMap, ok = 0;
    for (int i = 0; i < 15; i++) if (sz[i][0] == m && sz[i][1] == W && sz[i][2] == H) ok = 1;
    if (!ok) return;
    unsigned char *ly = d + 0x16, *p = ly + nl * 7, *ids[8];
    for (int i = 0; i < nl; i++) {
        unsigned char *L = ly + i * 7;
        if (!L[0]) { ids[i] = 0; continue; }
        ids[i] = p; p += W * H * 2;
        if (L[2]) p += W * H;
        if (L[1]) p += W * H;
    }
    int cnt = 0, bad = 0;
    unsigned nfix = kn ? sizeof(kn_fix) / sizeof(kn_fix[0]) : sizeof(nj_fix) / sizeof(nj_fix[0]);
    for (unsigned i = 0; i < nfix; i++) {
        short fm = kn ? kn_fix[i].map : nj_fix[i].map;
        unsigned char fl = kn ? kn_fix[i].layer : nj_fix[i].layer, fx = kn ? kn_fix[i].x : nj_fix[i].x, fy = kn ? kn_fix[i].y : nj_fix[i].y;
        unsigned short fo = kn ? kn_fix[i].old : nj_fix[i].old, fn = kn ? kn_fix[i].nw : nj_fix[i].nw;
        if (fm != m) continue;
        unsigned char *q = ids[fl];
        if (!q) { bad++; continue; }
        q += (fy * W + fx) * 2;
        int v = q[0] | q[1] << 8;
        if (v == fn) continue;
        if (v != fo) { bad++; continue; }
        q[0] = (unsigned char)fn; q[1] = (unsigned char)(fn >> 8); cnt++;
    }
    if (bad) LOGE("map: %s challenge %d: %d tiles differ, kept", kn ? "knight" : "ninja", m, bad);
    /* 背景：チップの組 30・1（屋外）で、遠景なし・背景のアニメなしのときだけ */
    int bg = 0;
    if (p[6] == 30 && p[8] == 1 && p[7] == 0 && p[9] == 0
        && p[10] == 0 && p[13] == 0 && p[14] == 0 && p[15] == 0) {
        p[13] = 1; p[14] = 4; p[15] = 0; bg = 1;
    }
    LOGI("map: %s challenge %d fixed (%d, bg %d)", kn ? "knight" : "ninja", m, cnt, bg);
}

/* v3.34: 吟遊詩人編CDの海（章4 map2）の幽霊船（ev22、層1）。層1は中身のない層なので全部通れる扱いになり島に上がる。
   CheckMovePass(field, chara, layer, x, y, dx, dy)：0=通れる 1=地形 2=キャラ。幽霊船だけ、行き先の層0が
   海（capk の属性 (a>>16)&0xff==0x3c、組0のチップ）でなければ 1 にする */
#define OFF_CMP_FUNC 0x3b6a14UL
#define OFF_CMP_GOT  0x45f488UL
typedef int (*CmpFn)(void *field, int *ch, int layer, int x, int y, int dx, int dy);
static CmpFn g_origCMP;
static int ghost_sea(void *field, int x, int y)
{
    static const unsigned char sea[] = { 0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x8c,0x8d,0x8e,
        0xb0,0xb1,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xbb,0xbc,0xbd,0xbe,
        0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,0xc8,0xd0,0xd1,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xdb,
        0xe0,0xe1,0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xeb,0xec,0xed,0xee,0xf0,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8 };
    unsigned char *m = (unsigned char *)field + 0xdc18;
    int W = *(int *)m, H = *(int *)(m + 4);
    if (x < 0 || y < 0 || x >= W || y >= H) return 0;
    unsigned char *L0 = *(unsigned char **)(m + 0x28);
    if (!L0 || !L0[0]) return 1;
    unsigned short *ids = *(unsigned short **)(L0 + 0x10);
    if (!ids) return 1;
    unsigned v = ids[y * W + x];
    if (v >> 8) return 0;
    for (unsigned i = 0; i < sizeof(sea); i++) if (sea[i] == v) return 1;
    return 0;
}
static int hook_CMP(void *field, int *ch, int layer, int x, int y, int dx, int dy)
{
    int r = g_origCMP(field, ch, layer, x, y, dx, dy);
    if (r == 0 && layer == 1 && g_curMap == 2 && ch && ch[0] == 22) {
        void *game = game_inst();
        if (game && RD(int, game, G_CHAPTER) == 4 && !ghost_sea(field, x + dx, y + dy) && ghost_sea(field, x, y)) {
            r = 1;
        }
    }
    return r;
}

/* v3.35: CD の脱出用の魔法陣（絵125）は乗る仕掛けの1マス上に置かれ、境目に描かれて半歩上に見える。
   InitEventDataOfChara のあと、その4か所だけ描く位置（+0x58 画素y）を16（半マス）下げる。マスの位置は変えない */
#define OFF_IEDC_FUNC 0x3bc1d0UL
#define OFF_IEDC_GOT  0x45f680UL
typedef void (*IedcFn)(void *field, unsigned char *h);
static IedcFn g_origIEDC;
static void hook_IEDC(void *field, unsigned char *h)
{
    g_origIEDC(field, h);
    if (!h || be16(h + 0x2b) != 125) return;
    void *game = game_inst();
    if (!game) return;
    int ch = RD(int, game, G_CHAPTER);
    static const struct { short ch, map, ev, x, y, dy; } k[] = {
        { 4, 2000, 7, 8, 3, 16 }, { 6, 2000, 4, 4, 6, 16 }, { 6, 2014, 4, 7, 6, 16 }, { 6, 2020, 4, 8, 16, 16 },
        /* v3.40t: 暗黒騎士編CD（章5）と踊り子編CD（章7） */
        { 5, 2000, 4, 5, 7, 16 }, { 5, 2010, 3, 8, 14, 16 }, { 5, 2010, 5, 4, 14, 16 }, { 5, 2020, 3, 8, 14, 16 },
        { 7, 1100, 2, 9, 9, 16 },
        /* 1.1.4: ナイト編CD（章8）。乗る仕掛けより1マス半上に見えていた（1.1.5 で 32→48） */
        { 8, 1900, 2, 9, 5, 48 }, { 8, 1914, 4, 8, 6, 48 },
        /* 1.1.8: 忍者編CD（章9）。絵の位置は乗る仕掛けより3マス下・3マス上。仕掛けのマスに合わせる */
        { 9, 1100, 2, 9, 33, -112 }, { 9, 1115, 5, 14, 16, 80 },
    };
    int id = be16(h);
    for (unsigned i = 0; i < sizeof(k) / sizeof(k[0]); i++) {
        if (k[i].ch != ch || k[i].map != g_curMap || k[i].ev != id || k[i].x != h[2] || k[i].y != h[3]) continue;
        unsigned char *c = (unsigned char *)field + 0xc68;
        for (int j = 0; j < 64; j++, c += 0xc8) {
            if (*(int *)c != id) continue;
            int *py = (int *)(c + 0x58);
            if (*py == (h[3] + h[0x3a]) * 32 + 16) { *py += k[i].dy; }
            break;
        }
        break;
    }
}

/* v3.45: 暗黒騎士編CD（章5 マップ2000〜2099）の左上の枠を出さない。
   入ったときのイベントの「25 07 2b …」と「カルマ：〇〇点」直前の「25 07 2c …」は、命令0x25 → FieldClass::AddEffect(int×10)（0x3c84dc）で
   組7・43番/44番の効果（btlefc_sp.dat 区画7、左上の角に付く情報の枠。中に何も表示しない）を作る。この2つだけ「作れなかった」（NULL）を返す。
   命令0x25 は戻り値が NULL なら次の行へ進む。2つとも「終わるまで待つ」の印がなく、効果を消す命令0x26 も CD にないので、ほかの処理は変わらない */
#define OFF_ADDEFC_FUNC 0x3c84dcUL
#define OFF_ADDEFC_GOT  0x45fa58UL
typedef void *(*AddEfcFn)(void *field, int grp, int id, int fl, int a4, int a5, int x, int y, int tx, int ty, int obs);
static AddEfcFn g_origAddEfc;
static void *hook_AddEfc(void *field, int grp, int id, int fl, int a4, int a5, int x, int y, int tx, int ty, int obs)
{
    void *game = game_inst();
    if (grp == 7 && (id == 43 || id == 44) && game && RD(int, game, G_CHAPTER) == 5 && g_curMap >= 2000 && g_curMap <= 2099) {
        static int told[2];
        if (!told[id - 43]) { told[id - 43] = 1; }
        return 0;
    }
    return g_origAddEfc(field, grp, id, fl, a4, a5, x, y, tx, ty, obs);
}

/* v3.51: 踊り子編（章7）のスロット（マップ9901）でリールが回っている間は、フィールドの1フレーム分の処理
   FieldClass::SceneMain（0x3b01f8、GOT 0x45efb0）を止まるまで続けて回す（演出を一瞬にする）。
   回っている間＝マップ専用の印（field+0xe488 のビット、命令 FLAG の組2）の 0 が立ち 7 がまだ。
   当たりは回す前に ev1 で決まり、リールは目標の位置まで動いて止まるだけなので結果は変わらない。
   止まったあとの結果表示・「続けますか？」・ファンファーレはそのままの速さ。1フレームでは2000回まで */
#define OFF_SCENEMAIN_FUNC 0x3b01f8UL
#define OFF_SCENEMAIN_GOT  0x45efb0UL
typedef void (*SceneMainFn)(void *field);
static SceneMainFn g_origSceneMain;
static int slot_spinning(void *field)
{
    void *game = game_inst();
    if (!game || RD(int, game, G_CHAPTER) != 7 || g_curMap != 9901) return 0;
    unsigned f = RD(unsigned, field, 0xe488);
    return (f & 1u) && !(f & (1u << 7));
}
static void hook_SceneMain(void *field)
{
    g_origSceneMain(field);
    if (!slot_spinning(field)) return;
    int n = 0;
    while (n < 2000 && slot_spinning(field)) { g_origSceneMain(field); n++; }
    LOGI("slot: spin fast-forwarded %d updates%s", n, n >= 2000 ? " (limit)" : "");
}

/* v3.37: ボスの印を持つ敵がいるのに、編成データ（form.bin）先頭の「AUTO を切る」bool が 0 の編成。
   LoadFormation(0x3aba3c) は bool が真なら BattleClass+0x6e35（AUTO、BattleClass は GameClass+0x44d80）を 0 にする。
   同じことをこの3つでも行う：章0 189 タロス、章4 185 キャプテンゾンビ、章6 260 メタモルファ */
#define OFF_LFORM_FUNC 0x3aba3cUL
#define OFF_LFORM_GOT  0x45ec50UL
typedef void (*LFormFn)(void *bc, int form);
static LFormFn g_origLForm;
static void hook_LForm(void *bc, int form)
{
    g_origLForm(bc, form);
    void *game = game_inst();
    if (!game || !bc) return;
    int ch = RD(int, game, G_CHAPTER);
    if ((ch == 0 && form == 189) || (ch == 4 && form == 185) || (ch == 6 && form == 260)) {
        unsigned char *a = (unsigned char *)bc + 0x6e35;
        if (*a) { *a = 0; }
    }
}

/* v3.37: 白魔法スロウ（魔法4、GameClass+0x343c0 の表、1件0x58）
   - 対象の印 +0x44：0x85（敵/味方の単体）→ 0x8f（ファイアと同じ、単体/全体を選べる）。敵の BSC_SetAction は 2/8 だけを全体扱いにするので敵の使い方は変わらない
   - 効果時間 +0x32：6 → 15（ヘイストと同じ）
   - 説明 +0x08：「単体の行動を遅くする」→「単体/全体の行動を遅くする」（元の文字コードに合わせる）
   欄の位置は既知の値を持つ魔法の組で探す（説明では +0x44・+0x32 と見ていたが確かめる） */
static const char k_slowOldSJ[] = "\x92\x50\x91\xcc\x82\xcc\x8d\x73\x93\xae\x82\xf0\x92\x78\x82\xad\x82\xb7\x82\xe9";
static const char k_slowNewSJ[] = "\x92\x50\x91\xcc/\x91\x53\x91\xcc\x82\xcc\x8d\x73\x93\xae\x82\xf0\x92\x78\x82\xad\x82\xb7\x82\xe9";
static const char k_slowOldU8[] = "単体の行動を遅くする";
static const char k_slowNewU8[] = "単体/全体の行動を遅くする";
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
/* 魔法表の中の欄の位置は、決まった値を持つ魔法の組で探す（ただ1か所に決まったときだけ使う） */
static int mgc_find(unsigned char *tab, const short (*kv)[2], int n)
{
    int found = -1;
    for (int k = 0x10; k < 0x58; k++) {
        int ok = 1;
        for (int i = 0; i < n && ok; i++) if (tab[kv[i][0] * 0x58 + k] != (unsigned char)kv[i][1]) ok = 0;
        if (ok) { if (found >= 0) return -2; found = k; }
    }
    return found;
}
static void slow_magic_apply(void)
{
    void *game = game_inst();
    if (!game) return;
    unsigned char *tab = *(unsigned char **)((unsigned char *)game + 0x343c0);
    if (!tab) return;
    /* 対象の印：ケアル0x0f ファイア0x8f ライブラ0x85 テレポ0x02 ブリンク0x01 クエイク0x88（スロウは0x85か直した0x8f） */
    static const short kT[][2] = { { 1, 0x0f }, { 25, 0x8f }, { 2, 0x85 }, { 12, 0x02 }, { 14, 0x01 }, { 43, 0x88 } };
    /* 効果時間：ヘイスト15 プロテス15 ストップ5 リジェネ32 ブリンク2 バイオ16 */
    static const short kD[][2] = { { 18, 15 }, { 8, 15 }, { 38, 5 }, { 17, 32 }, { 14, 2 }, { 36, 16 } };
    static int offT = -1, offD = -1, told;
    if (offT < 0) offT = mgc_find(tab, kT, 6);
    if (offD < 0) offD = mgc_find(tab, kD, 6);
    if (offT < 0 || offD < 0) {
        if (!told) { told = 1; LOGE("slow: magic fields not found (target %d time %d), skip", offT, offD); }
        offT = offT == -2 ? -2 : -1; offD = offD == -2 ? -2 : -1;
        return;
    }
    unsigned char *slow = tab + 4 * 0x58;
    if (slow[offT] == 0x85) slow[offT] = 0x8f;
    if (slow[offD] == 6) slow[offD] = 15;
    const char *d = *(const char **)(slow + 8);
    if (d && str_eq(d, k_slowOldSJ)) *(const char **)(slow + 8) = k_slowNewSJ;
    else if (d && str_eq(d, k_slowOldU8)) *(const char **)(slow + 8) = k_slowNewU8;
}

/* v3.39: 海底神殿1F（章4 map1015 ev12/ev21）で呪いが解けたエドアルド（キャラ19）を「レベル40」にする命令（op 0x7c → SetMemberLevel）。
   呪われたエドアルド（キャラ11）のほうが高いときは 11 のレベルにし、経験値（MEMBER_STATUS+0x38）も 11 にそろえる。
   19 のレベルを決める命令は全章でここだけ（終章の再加入はそのまま使う） */
#define OFF_SETML_FUNC 0x3d5c08UL
#define OFF_SETML_GOT  0x45fc58UL
#define OFF_GETLEVEL   0x3e3b70UL   /* GameClass::GetLevel(MEMBER_STATUS&)：経験値からレベル */
typedef void (*SetMLFn)(void *field, int member, int level, int flag);
static SetMLFn g_origSetML;
static void hook_SetML(void *field, int member, int level, int flag)
{
    void *game = game_inst();
    int copyExp = -1;
    if (game && member == 19 && level == 40 && RD(int, game, G_CHAPTER) == 4) {
        u8 *m11 = member_at(game, 11);
        int lv11 = ((int (*)(void *, void *))(g_base + OFF_GETLEVEL))(game, m11);
        if (lv11 > 40 && lv11 <= 99) {
            level = lv11; copyExp = RD(int, m11, 0x38);
        }
    }
    g_origSetML(field, member, level, flag);
    if (copyExp >= 0) {
        u8 *m19 = member_at(game, 19);
        if (RD(int, m19, 0x38) < copyExp) { *(volatile int *)(m19 + 0x38) = copyExp; }
    }
}


/* 1.1.7: 画面の右端（GetScrollPosX 0x3c8af0）。ゲームは 640 幅で 0〜(幅−画面)×32 に収めるが、ワイド表示は左右に g_wideE ずつ広く描く。
   g_edgeR のときだけ、右端がワイドの余白も含めて地図の右端を越えないようにする。層ごとの流れる速さ（層+8、16が等速）で割り戻して比べる */
#define OFF_GSPX_FUNC 0x3c8af0UL
#define OFF_GSPX_GOT  0x45fa68UL
typedef int (*GspxFn)(void *field, int layer);
static GspxFn g_origGSPX;
static int g_edgeR;
static int g_edgePx;   /* 止める線（地図の左からの画素）。0 なら地図の右端。地図が画面より狭くても止めるのはこれを指定した地図だけ */
static int hook_GSPX(void *field, int layer)
{
    int r = g_origGSPX(field, layer);
    if (!g_edgeR || !field) return r;
    unsigned char *m = (unsigned char *)field + 0xdc18;
    int W = *(int *)m;
    unsigned char *ls = *(unsigned char **)(m + 0x28);
    if (!ls || layer < 0 || layer > 7) return r;
    int rate = *(int *)(ls + layer * 40 + 8);
    if (rate <= 0) return r;
    float sw = *(float *)((unsigned char *)field + 0xc);
    int e = (int)(g_wideE + 0.999f);
    /* 1.1.7-pre7: field+0xc（682）はワイドの余白込みの幅。g_edgePx の地図は余白を引かずに右端ぴったりで止める
       （ほかの地図は pre4 のまま余白分手前で止める。ユーザー確認済みの見た目を変えない） */
    int maxpos = g_edgePx > 0 ? g_edgePx - (int)sw : W * 32 - (int)sw - e;
    if (maxpos < 0 && g_edgePx <= 0) return r;   /* 地図が画面より狭いときは、止める線を指定した地図（g_edgePx）だけ止める */
    int pos = r * 16 / rate;
    if (pos > maxpos) r = maxpos * rate / 16;
    return r;
}


static void hook_LoadMI(void *field, unsigned char *data, int off)
{
    unsigned char *d = data + off;
    if (off == 0) savept_prepare(data - 4);
    int W = be16(d + 4), H = be16(d + 6), nl = d[0x15];
    /* v3.33: 無効な層（記述の1バイト目が0）に配列の印（2・3バイト目）が残っていると、LoadMapInfo は配列を用意しないのに
       GetLayerOfPosition(0x3b922c) が印を見て未確保の属性配列(+0x20)の y*W+x を読み、0番地付近で落ちる。
       全章で当たるのは章4のマップ2（吟遊詩人編チャレンジダンジョンの海）の層1だけ（オフラインで確認） */
    if (nl <= 8) {
        for (int i = 0; i < nl; i++) {
            unsigned char *L = d + 0x16 + i * 7;
            if (!L[0] && (L[1] || L[2])) {
                L[1] = 0; L[2] = 0;
            }
        }
    }
    if (W > 0 && H > 0 && W <= 512 && H <= 512 && nl <= 8) {
        unsigned char *ly = d + 0x16, *p = ly + nl * 7;
        unsigned char *ids[8]; int nid = 0;
        for (int i = 0; i < nl; i++) {
            unsigned char *L = ly + i * 7;
            if (!L[0]) { ids[i] = 0; continue; }
            ids[i] = p; p += W * H * 2;
            if (L[2]) p += W * H;
            if (L[1]) p += W * H;
            nid++;
        }
        signed char c0a = (signed char)p[6]; unsigned char c0b = p[7];
        if (c0a == 7 && c0b == 0 && nl >= 2 && ids[1]) {
            unsigned char *q = ids[1]; int nb1 = 0;
            for (int i = 0; i < W * H; i++) if (q[i * 2] == 0xb1 && q[i * 2 + 1] == 0) nb1++;
            if (nb1 >= 40) {
                for (int i = 0; i < W * H; i++)
                    if (q[i * 2] == 0xb0 && q[i * 2 + 1] == 0) q[i * 2] = 0;
            }
        }
    }
    cd0_mapfix(d);
    rg_mapfix(d);
    rg_hidden(d);
    bard_mapfix(d);
    dk_mapfix(d);
    mem_mapfix(d);
    dancer_mapfix(d);
    knight_mapfix(d);
    g_origLoadMI(field, data, off);
    /* v3.50: メモリスト編CDの砂漠 2006（40x40）・2010（44x43）だけ、画面をマップの端で止める。
       field+0xdc32（地図情報の3つ目の bool、LoadMapInfo が毎回入れる）が1なら GetScrollPosX/Y・GetScrollPosition が
       0〜（幅−画面）に収める。0 だと主人公を中心に追い、マップの外（模様のない砂）が映って左端の崖が切れて見えた */
    {
        void *game = game_inst();
        if (off == 0 && game && RD(int, game, G_CHAPTER) == 6 &&
            ((g_curMap == 2006 && W == 40 && H == 40) || (g_curMap == 2010 && W == 44 && H == 43)))
            *(volatile unsigned char *)((uintptr_t)field + 0xdc32) = 1;
        /* 1.1.7: ナイト編CD 1907（35x45）・1911（22x26）・1912（14x23）・1913（31x26）だけ、画面の右端をマップの右端で止める（hook_GSPX） */
        if (off == 0) g_edgeR = (game && RD(int, game, G_CHAPTER) == 8 &&
                                 ((g_curMap == 1907 && W == 35 && H == 45) || (g_curMap == 1911 && W == 22 && H == 26) ||
                                  (g_curMap == 1913 && W == 31 && H == 26) || (g_curMap == 1912 && W == 14 && H == 23)));
        /* 1.1.7: 1912（幅14、画面より狭い）は、右端（岩 (13,13) の右側）が画面の右端を越えないように止める */
        if (off == 0) g_edgePx = (g_edgeR && g_curMap == 1912) ? 14 * 32 : 0;
    }
}

/* ---------- v3.40t 試作: スティクスを仲間に（キャラ21） ----------
   ・ジョブ33「魔女」（ゲスト専用。ジョブ30 の写しに魔人(18) の能力値、コマンド 青魔法(アビリティ86) と 白黒魔ＬＶ７）
   ・青魔法（コマンド24、分類17）で並ぶ技は、空き番号に作る味方用の写し:
       73 ウォータ 74 ウォタガ（単体/全体） 76 ウンディーネの叫び 77 ローレライの悲涙 78 ゆうわく（命中100・バーサク）
       79 ディスチャージ（敵全体） 80 セイレーンの呪歌（台本80） 81 テンプテーション（台本81）
     セイレーンの台本が使う 204 トード 205 ミニマム 206 サイレス（命中100、分類19＝一覧に出ない）
   ・終章で8人そろってから（ゲスト強化と同じ条件）キャラ21 の枠が空なら作る。編成の R2/L2 でだけ入れられる
   どれも元の値を確かめてから書き、ゲームが表を読み直したら次の見回りでもう一度書く */
#define STYX_ID     21
#define STYX_JOB    33
#define STYX_MODEL  120
#define MG_SIZE     0x58
#define G_MAGIC_TBL 0x343c0
#define G_MAGIC_N   0x343e0
#define AB_SIZE     0x30
typedef void *(*MallocFn)(int);
typedef void (*FreeFn)(void *);
typedef void (*SetLVFn)(void *game, int member, int lv);
static MallocFn p_Malloc;
static FreeFn p_Free;
static SetLVFn p_SetLV;
static int g_sjis = -1;               /* 文字コード: 1=SJIS 0=UTF-8 */

static const char k_majoSJ[] = "\x96\x82\x8f\x97", k_majoU8[] = "魔女";
static const char k_styxSJ[] = "\x83\x58\x83\x65\x83\x42\x83\x4e\x83\x58", k_styxU8[] = "スティクス";
static const char k_bmSJ[] = "\x90\xc2\x96\x82\x96\x40", k_bmU8[] = "青魔法";
static const char k_bmDescSJ[] = "\x8a\x6f\x82\xa6\x82\xbd\x93\x47\x82\xcc\x8b\x5a\x82\xf0\x8e\x67\x82\xa4", k_bmDescU8[] = "覚えた敵の技を使う";
static const char k_empty[] = "";

static int str_eq(const char *a, const char *b);
static void styx_encoding(void *game)
{
    if (g_sjis >= 0) return;
    u8 *jd = (u8 *)RDP(game, G_JOBDATA);
    if (!jd || RD(int, game, G_JOBCOUNT) < 19) return;
    const char *n = *(const char **)(jd + 18 * JD_SIZE);
    if (!n) return;
    if (str_eq(n, "\x96\x82\x90\x6c")) g_sjis = 1;
    else if (str_eq(n, "魔人")) g_sjis = 0;
    else { static int told; if (!told) { told = 1; LOGE("styx: job 18 name unknown (%02x %02x %02x %02x)", (u8)n[0], (u8)n[1], (u8)n[2], (u8)n[3]); } return; }
    LOGI("styx: text is %s", g_sjis ? "sjis" : "utf8");
}

/* 技の写し。from を to に丸ごと写してから欄を直す */
/* v3.40t5: 対象は 0x8f などを入れるので short（v3.40t4 までは signed char で負になり、書かれていなかった）。
   待機時間 +0x1c（黒魔法の MP が近いものにそろえる）、説明 +0x08 を足す */
static const char k_dWaterSJ[] = "\x92\x50\x91\xcc\x2f\x91\x53\x91\xcc\x82\xc9\x90\x85\x91\xae\x90\xab\x82\xcc\x83\x5f\x83\x81\x81\x5b\x83\x57";
static const char k_dWaterU8[] = "単体/全体に水属性のダメージ";
static const char k_dSlipSJ[] = "\x93\x47\x91\x53\x91\xcc\x82\xc9\x90\x85\x91\xae\x90\xab\x82\xcc\x83\x5f\x83\x81\x81\x5b\x83\x57\x82\xf0\x97\x5e\x82\xa6\x81\x75\x83\x58\x83\x8a\x83\x62\x83\x76\x81\x76\x8f\xf3\x91\xd4\x82\xc9\x82\xb7\x82\xe9";
static const char k_dSlipU8[] = "敵全体に水属性のダメージを与え「スリップ」状態にする";
static const char k_dBerSJ[] = "\x92\x50\x91\xcc\x82\xf0\x81\x75\x83\x6f\x81\x5b\x83\x54\x83\x4e\x81\x76\x8f\xf3\x91\xd4\x82\xc9\x82\xb7\x82\xe9";
static const char k_dBerU8[] = "単体を「バーサク」状態にする";
static const char k_dStunSJ[] = "\x93\x47\x91\x53\x91\xcc\x82\xf0\x81\x75\x83\x58\x83\x5e\x83\x93\x81\x76\x8f\xf3\x91\xd4\x82\xc9\x82\xb7\x82\xe9";
static const char k_dStunU8[] = "敵全体を「スタン」状態にする";
static const char k_dSirenSJ[] = "\x93\x47\x91\x53\x91\xcc\x82\xf0\x81\x75\x83\x4a\x83\x47\x83\x8b\x81\x76\x81\x75\x82\xb1\x82\xd1\x82\xc6\x81\x76\x81\x75\x92\xbe\x96\xd9\x81\x76\x82\xcc\x82\xa2\x82\xb8\x82\xea\x82\xa9\x82\xc9\x82\xb7\x82\xe9";
static const char k_dSirenU8[] = "敵全体を「カエル」「こびと」「沈黙」のいずれかにする";
static const char k_dConfSJ[] = "\x93\x47\x91\x53\x91\xcc\x82\xf0\x81\x75\x8d\xac\x97\x90\x81\x76\x8f\xf3\x91\xd4\x82\xc9\x82\xb7\x82\xe9";
static const char k_dConfU8[] = "敵全体を「混乱」状態にする";
typedef struct { short to, from, mp, hit, target, status, script, cat, wait; const char *dsj, *du8; } StyxMagic;
static const StyxMagic k_styxMagic[] = {
    {  73, 432, 12,  -1, 0x8f, -1,     -1, -1, 24, k_dWaterSJ, k_dWaterU8 },   /* ウォータ：単体/全体 */
    {  74, 433, 32,  -1, 0x8f, -1,     -1, -1, 48, k_dWaterSJ, k_dWaterU8 },   /* ウォタガ：単体/全体 */
    {  76, 431, 24,  -1, 0x88, -1,     -1, -1, 36, k_dSlipSJ,  k_dSlipU8 },    /* ウンディーネの叫び：敵全体 */
    {  77, 435, 45,  -1, 0x88, -1,     -1, -1, 50, k_dSlipSJ,  k_dSlipU8 },    /* ローレライの悲涙：敵全体 */
    {  78, 393, 10, 100, 0x85, 0x0200, -1, -1, 24, k_dBerSJ,   k_dBerU8 },     /* ゆうわく：命中100 バーサク */
    {  79, 434, 28,  -1, 0x88, -1,     -1, -1, 36, k_dStunSJ,  k_dStunU8 },    /* ディスチャージ：敵全体だけ */
    {  80, 469, 30,  -1, 0x88, -1,     80, -1, 36, k_dSirenSJ, k_dSirenU8 },   /* セイレーンの呪歌：台本80 */
    {  81, 437, 30,  -1, 0x88, -1,     81, -1, 36, k_dConfSJ,  k_dConfU8 },    /* テンプテーション：台本81 */
    { 204,  30,  0, 100,   -1, -1,     -1, 19, -1, 0, 0 },   /* トード（呪歌用） */
    { 205,   6,  0, 100,   -1, -1,     -1, 19, -1, 0, 0 },   /* ミニマム（呪歌用） */
    { 206,   5,  0, 100,   -1, -1,     -1, 19, -1, 0, 0 },   /* サイレス（呪歌用） */
};
static int styx_magic(void *game)
{
    u8 *tab = (u8 *)RDP(game, G_MAGIC_TBL);
    int n = RD(int, game, G_MAGIC_N);
    if (!tab || n < 470) return 0;
    /* 欄の位置を既知の値で確かめる：ケアル MP4 命中100、ゆうわく 命中0x55 状態0x0400、
       セイレーン 台本469、ウォータ 分類17 対象0x0f、ファイア 対象0x8f */
    static int checked;
    if (!checked) {
        int ok = tab[1 * MG_SIZE + 0x19] == 4 && tab[1 * MG_SIZE + 0x1a] == 100 &&
                 tab[393 * MG_SIZE + 0x1a] == 0x55 && RD(unsigned short, tab, 393 * MG_SIZE + 0x40) == 0x0400 &&
                 RD(unsigned short, tab, 469 * MG_SIZE + 0x36) == 469 && tab[432 * MG_SIZE + 0x10] == 17 &&
                 tab[432 * MG_SIZE + 0x44] == 0x0f && tab[25 * MG_SIZE + 0x44] == 0x8f;
        if (!ok) {
            LOGE("styx: magic fields differ (%d %d %d %x %d %d %x %x), skipped", tab[1 * MG_SIZE + 0x19], tab[1 * MG_SIZE + 0x1a],
                 tab[393 * MG_SIZE + 0x1a], RD(unsigned short, tab, 393 * MG_SIZE + 0x40), RD(unsigned short, tab, 469 * MG_SIZE + 0x36),
                 tab[432 * MG_SIZE + 0x10], tab[432 * MG_SIZE + 0x44], tab[25 * MG_SIZE + 0x44]);
            checked = -1;
            return 0;
        }
        checked = 1;
    }
    if (checked < 0) return 0;
    int ch = 0;
    for (unsigned i = 0; i < sizeof(k_styxMagic) / sizeof(k_styxMagic[0]); i++) {
        const StyxMagic *m = &k_styxMagic[i];
        u8 *d = tab + m->to * MG_SIZE;
        const u8 *s = tab + m->from * MG_SIZE;
        if (*(const char *const *)(d) == *(const char *const *)(s) && d[0x19] == (u8)m->mp) {   /* 済み：欄が変わっていないか確かめる */
            int bad = (m->hit >= 0 && d[0x1a] != (u8)m->hit) || (m->target >= 0 && d[0x44] != (u8)m->target) ||
                      (m->status >= 0 && RD(unsigned short, d, 0x40) != (unsigned short)m->status) ||
                      (m->script >= 0 && RD(unsigned short, d, 0x36) != (unsigned short)m->script) ||
                      (m->cat >= 0 ? d[0x10] != (u8)m->cat : d[0x18] != 1) ||
                      (m->wait >= 0 && RD(unsigned short, d, 0x1c) != (unsigned short)m->wait) ||
                      (m->du8 && *(const char *const *)(d + 8) != (g_sjis ? m->dsj : m->du8)) ||
                      (RD(short, d, 0x4a) == 0 && RD(short, d, 0x48) > 0);
            if (!bad) continue;
            LOGE("styx: magic %d changed (cat %d flag %d hit %d target %x status %x script %d wait %d), rewritten", m->to, d[0x10], d[0x18],
                 d[0x1a], d[0x44], RD(unsigned short, d, 0x40), RD(unsigned short, d, 0x36), RD(unsigned short, d, 0x1c));
        } else {
        /* 空き番号（名前が空）のときだけ使う */
        const char *nm = *(const char *const *)d;
        if (nm && nm[0]) {
            static unsigned warned;
            if (!(warned & (1u << i))) { warned |= 1u << i; LOGE("styx: magic %d not empty, skipped", m->to); }
            continue;
        }
        }
        for (int k = 0; k < MG_SIZE; k++) d[k] = s[k];
        if (m->cat >= 0) d[0x10] = (u8)m->cat; else d[0x18] = 1;   /* 一覧に出す写しは味方の魔法の印 */
        d[0x19] = (u8)m->mp;
        if (m->hit >= 0) d[0x1a] = (u8)m->hit;
        if (m->target >= 0) d[0x44] = (u8)m->target;
        if (m->status >= 0) *(volatile unsigned short *)(d + 0x40) = (unsigned short)m->status;
        if (m->script >= 0) *(volatile unsigned short *)(d + 0x36) = (unsigned short)m->script;
        if (m->wait >= 0) *(volatile unsigned short *)(d + 0x1c) = (unsigned short)m->wait;
        if (m->du8) *(const char **)(d + 8) = g_sjis ? m->dsj : m->du8;
        /* v3.40t6: 全体のエフェクトは狙う側で別（+0x48/+0x4f＝味方側、+0x4a/+0x50＝敵側、PlayTargetMagicEffect）。
           敵の技は味方側しか持たないので、敵側が空なら味方側を写す（ウンディーネ・ローレライ・ディスチャージ） */
        if (RD(short, d, 0x4a) == 0 && RD(short, d, 0x48) > 0) {
            *(volatile short *)(d + 0x4a) = RD(short, d, 0x48);
            d[0x50] = d[0x4f];
            LOGI("styx: magic %d all-enemy effect %d set", m->to, RD(short, d, 0x48));
        }
        ch++;
    }
    if (ch) LOGI("styx: %d magic copies written", ch);
    return 1;
}

/* アビリティ86（DEBUG用）を「青魔法」（歌う63 の写し、コマンド24）に */
static int styx_ability(void *game)
{
    u8 *t = (u8 *)RDP(game, G_PASSIVE_TBL);
    if (!t || g_sjis < 0) return 0;
    static int off = -1;
    if (off == -1) {
        static const short kv[][2] = { { 19, 6 }, { 29, 7 }, { 63, 16 }, { 80, 17 }, { 4, 11 }, { 62, 22 } };
        int found = -1;
        for (int k = 0x10; k < AB_SIZE; k++) {
            int ok = 1;
            for (int i = 0; i < 6 && ok; i++) if (t[kv[i][0] * AB_SIZE + k] != (u8)kv[i][1]) ok = 0;
            if (ok) { if (found >= 0) { found = -2; break; } found = k; }
        }
        off = found >= 0 ? found : -2;
        LOGI("styx: ability command field %s%x", off < 0 ? "not found " : "+", off < 0 ? 0 : off);
    }
    if (off < 0) return 0;
    u8 *d = t + 86 * AB_SIZE;
    const char *nm = g_sjis ? k_bmSJ : k_bmU8;
    if (*(const char **)d == nm && d[off] == 24) return 1;
    const char *old = *(const char **)d;
    if (!old || (!str_eq(old, "DEBUG\x97\x70") && !str_eq(old, "DEBUG用") && old != nm)) {
        static int told;
        if (!told) { told = 1; LOGE("styx: ability 86 is not DEBUG (%02x %02x %02x %02x %02x %02x %02x), skipped",
                                    old ? (u8)old[0] : 0, old ? (u8)old[1] : 0, old ? (u8)old[2] : 0, old ? (u8)old[3] : 0,
                                    old ? (u8)old[4] : 0, old ? (u8)old[5] : 0, old ? (u8)old[6] : 0); }
        return 0;
    }
    for (int k = 0; k < AB_SIZE; k++) d[k] = t[63 * AB_SIZE + k];
    d[off] = 24;
    *(const char **)d = nm;
    *(const char **)(d + 8) = g_sjis ? k_bmDescSJ : k_bmDescU8;
    LOGI("styx: ability 86 -> blue magic (command 24 at +%x)", off);
    return 1;
}

/* v3.40t5: 魔法・技の画面のコマンドは CreateMemberMenuCommand が、コマンド +0x2c の bit0 が立ったものだけ並べる
   （白魔法・黒魔法 0x03、歌うなど 0x0b、青魔法 0x00）。青魔法を歌うと同じ 0x0b に（v3.40t4 の +0x1e の変更は外した） */
#define G_CMD_TBL 0x343e8
/* 1.0-pre8: 魔法・技の画面で、青魔法（コマンド24）を選んだときの説明（コマンド表 +0x8。MenuClass::DrawMagic が描く）。
   元は空なので、白魔法・黒魔法と同じ形の文を入れる */
static const char k_bmHelpSJ[] = "\x8f\x4b\x93\xbe\x82\xb5\x82\xc4\x82\xa2\x82\xe9\x90\xc2\x96\x82\x96\x40\x82\xf0\x8e\x67\x97\x70\x2f\x8a\x6d\x94\x46\x82\xb5\x82\xdc\x82\xb7",
                  k_bmHelpU8[] = "習得している青魔法を使用/確認します";
static int styx_command(void *game)
{
    u8 *t = (u8 *)RDP(game, G_CMD_TBL);
    if (!t) return 0;
    if (g_sjis >= 0) {
        const char **dp = (const char **)(t + 24 * 0x30 + 8);
        const char *want = g_sjis ? k_bmHelpSJ : k_bmHelpU8;
        if (*dp != want) {
            static int toldH;
            if (!*dp || !**dp) {
                *dp = want;
                if (!toldH) { toldH = 1; LOGI("styx: command 24 help set"); }
            } else if (!toldH) { toldH = 1; LOGE("styx: command 24 help is not empty, kept"); }
        }
    }
    /* 1.0-pre20: 魔法を選んでから順番を待つ間のポーズ（コマンド +0x16 − 1、BattleClass::SetCommand が BTLACT +0x638 に入れる）。
       青魔法は歌うの写しで詠唱しないので、黒魔法（コマンド7）と同じ詠唱にする */
    if (t[24 * 0x30 + 0x16] != t[7 * 0x30 + 0x16]) {
        LOGI("styx: command 24 wait pose %d -> %d (black magic)", t[24 * 0x30 + 0x16], t[7 * 0x30 + 0x16]);
        t[24 * 0x30 + 0x16] = t[7 * 0x30 + 0x16];
    }
    volatile u8 *f = t + 24 * 0x30 + 0x2c;
    if (*f == 0x0b) return 1;
    static int told;
    if (*f != 0x00 || t[6 * 0x30 + 0x2c] != 0x03 || t[16 * 0x30 + 0x2c] != 0x0b || RD(unsigned, t, 24 * 0x30 + 0x20) != 0x20000) {
        if (!told) { told = 1; LOGE("styx: command fields differ (%x %x %x %x), skipped", *f, t[6 * 0x30 + 0x2c], t[16 * 0x30 + 0x2c],
                                    RD(unsigned, t, 24 * 0x30 + 0x20)); }
        return 0;
    }
    *f = 0x0b;
    LOGI("styx: command 24 menu flags 0x00 -> 0x0b");
    return 1;
}

/* ジョブ表を34件に広げ、33 を「魔女」にする */
static const u8 k_stat18[7] = { 0x44, 0x93, 0x15, 0x26, 0x22, 0x4b, 0x1f };   /* 魔人の能力値（+0x1d〜） */
static const u8 k_stat30[7] = { 0x58, 0xb7, 0x18, 0x2a, 0x27, 0x5e, 0x27 };   /* ジョブ30（？？？？） */
/* _MALLOC は「そのときのメモリの組」に入り、場面の切り替えでまとめて返されるので使わない。
   広げた表は fflmod の中に置き、元の表はそのまま残す（返さない） */
static u8 g_jobBuf[(STYX_JOB + 1) * JD_SIZE] __attribute__((aligned(16)));
static int styx_job(void *game)
{
    if (g_sjis < 0) return 0;
    int n = RD(int, game, G_JOBCOUNT);
    u8 *jd = (u8 *)RDP(game, G_JOBDATA);
    if (!jd) return 0;
    const char *nm = g_sjis ? k_majoSJ : k_majoU8;
    if (n == STYX_JOB + 1 && jd == g_jobBuf && *(const char **)(jd + STYX_JOB * JD_SIZE) == nm) return 1;
    if (n != STYX_JOB || jd == g_jobBuf) {
        static int told;
        if (!told) { told = 1; LOGE("styx: job count %d, skipped", n); }
        return 0;
    }
    u8 *nj = g_jobBuf;
    for (int i = 0; i < STYX_JOB * JD_SIZE; i++) nj[i] = jd[i];
    u8 *j = nj + STYX_JOB * JD_SIZE;
    for (int i = 0; i < JD_SIZE; i++) j[i] = jd[30 * JD_SIZE + i];
    int st18 = 1, st30 = 1;
    for (int i = 0; i < 7; i++) { if (jd[18 * JD_SIZE + 0x1d + i] != k_stat18[i]) st18 = 0; if (j[0x1d + i] != k_stat30[i]) st30 = 0; }
    if (st18 && st30) for (int i = 0; i < 7; i++) j[0x1d + i] = k_stat18[i];
    *(const char **)j = nm;
    *(const char **)(j + 8) = k_empty;
    j[0x11] = 0;                                   /* 絵はキャラの +0xb6/+0xb8 で決める */
    static const u8 innate[6] = { 86, 55 };        /* 青魔法 白黒魔ＬＶ７ */
    for (int i = 0; i < 6; i++) j[JD_INNATE + i] = innate[i];
    *(u8 **)((uintptr_t)game + G_JOBDATA) = nj;
    *(volatile int *)((uintptr_t)game + G_JOBCOUNT) = STYX_JOB + 1;
    LOGI("styx: job 33 added (stats %s, side %d, equip %08x)", st18 && st30 ? "of job 18" : "of job 30 (fields differ)", j[0x10], RD(unsigned, j, 0x18));
    return 1;
}

/* v3.41: セーブに入らない欄（+0x31〜0x33、+0x40〜0x4d、+0xac〜0xbf。ジョブの欄 +0xbc を除く）をキャラ18 にそろえ、
   絵の欄（+0xb0 +0xb6 +0xb8）はスティクスの値にする。キャラ18 の陣営が 0（まだ何も入っていない）なら何もしない。
   変えたら 1 */
static int styx_fix_unsaved(void *game, u8 *m)
{
    u8 *src = member_at(game, 18);
    if (src[0x32] == 0) return 0;
    int ch = 0;
    for (int o = 0x31; o < 0x34; o++) if (m[o] != src[o]) { m[o] = src[o]; ch = 1; }
    for (int o = 0x40; o < 0x4e; o++) if (m[o] != src[o]) { m[o] = src[o]; ch = 1; }
    for (int o = 0xac; o < 0xc0; o++) {
        if (o == MS_CANJOB || o == 0xb0 || (o >= 0xb6 && o <= 0xb8)) continue;
        if (m[o] != src[o]) { m[o] = src[o]; ch = 1; }
    }
    if (RD(short, m, 0xb6) != STYX_MODEL || m[0xb8] != 0 || m[0xb0] != 0) {
        *(volatile short *)(m + 0xb6) = STYX_MODEL; m[0xb8] = 0; m[0xb0] = 0; ch = 1;
    }
    return ch;
}

/* キャラ21 を作る（枠が空のときだけ） */
static int styx_ready(void *game)
{
    u8 *m = member_at(game, STYX_ID);
    return (signed char)m[0x30] == STYX_ID && RD(int, m, MS_JOB) == STYX_JOB;
}
static void styx_member(void *game)
{
    u8 *m = member_at(game, STYX_ID);
    if (styx_ready(game)) {
        /* v3.40t6: セーブに入らない欄（SetMemberData が書かない +0x31〜0x33 陣営など、+0x40〜0x4d、+0xac〜0xbf）は
           読み直すと 0 になる。最初の版ではキャラ18 の写しだったのでフィールドで歩けていた。0 のときは18 から写し直す */
        {
            static int dumped2;
            u8 *src = member_at(game, 18);
            if (!dumped2) {
                dumped2 = 1;
                char a[3 * 40 + 1], b[3 * 40 + 1];
                static const char hx[] = "0123456789abcdef";
                for (int i = 0; i < 40; i++) {
                    int o = i < 20 ? 0x30 + i : 0xac + (i - 20);
                    a[i * 3] = hx[m[o] >> 4]; a[i * 3 + 1] = hx[m[o] & 15]; a[i * 3 + 2] = ' ';
                    b[i * 3] = hx[src[o] >> 4]; b[i * 3 + 1] = hx[src[o] & 15]; b[i * 3 + 2] = ' ';
                }
                a[120] = b[120] = 0;
                LOGI("styx: m21 30-43/ac-bf: %s", a);
                LOGI("styx: m18 30-43/ac-bf: %s", b);
            }
            if (styx_fix_unsaved(game, m)) {
                refresh_member(game, m);
                LOGI("styx: unsaved fields copied from member 18 (side %d/%d, 40 %d, bb %d)", m[0x31], m[0x32], m[0x40], m[0xbb]);
            }
        }
        /* 絵の番号はセーブから戻らない（読み直すと 0 になる）ので毎回入れ直す */
        if (RD(short, m, 0xb6) != STYX_MODEL || m[0xb8] != 0 || m[0xb0] != 0) {
            LOGI("styx: model %d/%d/%d -> %d/0/0", RD(short, m, 0xb6), m[0xb8], m[0xb0], STYX_MODEL);
            *(volatile short *)(m + 0xb6) = STYX_MODEL; m[0xb8] = 0; m[0xb0] = 0;
        }
        /* 技の印は毎回そろえる（セーブを読んだあとなど） */
        int ch = 0;
        static const short mg[] = { 73, 74, 76, 77, 78, 79, 80, 81 };
        for (unsigned i = 0; i < sizeof(mg) / sizeof(mg[0]); i++) ch += bit_set(m + MS_MAGIC, mg[i], 1);
        for (int i = 1; i <= 21; i++) ch += bit_set(m + MS_MAGIC, i, 1);
        for (int i = 25; i <= 45; i++) ch += bit_set(m + MS_MAGIC, i, 1);
        if (ch) { refresh_member(game, m); LOGI("styx: member magic marks set (%d)", ch); }
        static int dumped;
        if (!dumped) {
            dumped = 1;
            char buf[200]; int k = 0;
            for (int i = 46; i < 256 && k < 190; i++)
                if (bit_get(m + MS_MAGIC, i)) {
                    int v = i, d = v >= 100 ? 3 : v >= 10 ? 2 : 1;
                    for (int j = d - 1; j >= 0; j--) { buf[k + j] = (char)('0' + v % 10); v /= 10; }
                    k += d; buf[k++] = ' ';
                }
            buf[k] = 0;
            LOGI("styx: magic marks above 45: %s(job %d, cmd list %d %d %d)", buf, RD(int, m, MS_JOB),
                 m[0x2b4], m[0x2b5], m[0x2b6]);
        }
        return;
    }
    static int told;
    /* 使っていない枠は、名前が空・ジョブ0 で、番号欄（+0x30）には枠の番号 21 が入っている（終章のセーブで確認） */
    int empty = (signed char)m[0x30] == STYX_ID && RD(int, m, MS_JOB) == 0;
    for (int i = 0; i < 0x30 && empty; i++) if (m[i]) empty = 0;
    if (!told) {
        for (int k = 20; k <= 23; k++) {
            u8 *e = member_at(game, k);
            LOGI("styx: slot %d: id %d job %d name %02x%02x lv %d side %d/%d", k, (signed char)e[0x30], RD(int, e, MS_JOB), e[0], e[1],
                 RD(int, e, 0x114), e[0x31], e[0x32]);
        }
    }
    if (!empty) {
        if (told < 2) { told = 2; LOGE("styx: member 21 slot in use (id %d job %d), skipped", (signed char)m[0x30], RD(int, m, MS_JOB)); }
        return;
    }
    told = 1;
    if (!p_SetLV) return;
    u8 *src = member_at(game, 18);
    for (int i = 0; i < 0x4c4; i++) m[i] = src[i];
    const char *nm = g_sjis ? k_styxSJ : k_styxU8;
    for (int i = 0; i < 0x30; i++) m[i] = 0;
    for (int i = 0; nm[i] && i < 0x2f; i++) m[i] = (u8)nm[i];
    m[0x30] = STYX_ID;
    *(volatile int *)(m + MS_JOB) = STYX_JOB;
    for (int o = 0x4e; o <= 0x56; o += 2) *(volatile short *)(m + o) = 0;     /* 装備なし */
    for (int i = 0; i < 32; i++) m[MS_MAGIC + i] = 0;
    for (int i = 0; i < 16; i++) m[MS_PASSIVE + i] = 0;
    static const short mg[] = { 73, 74, 76, 77, 78, 79, 80, 81 };
    for (unsigned i = 0; i < sizeof(mg) / sizeof(mg[0]); i++) bit_set(m + MS_MAGIC, mg[i], 1);
    for (int i = 1; i <= 21; i++) bit_set(m + MS_MAGIC, i, 1);
    for (int i = 25; i <= 45; i++) bit_set(m + MS_MAGIC, i, 1);
    m[0xb0] = 0;
    *(volatile short *)(m + 0xb6) = STYX_MODEL;
    m[0xb8] = 0;
    *(volatile int *)(m + MS_STATUS) = 0;
    p_SetLV(game, STYX_ID, 60);
    refresh_member(game, m);
    *(volatile int *)(m + MS_HPMP) = RD(int, m, MS_MAXHPMP);
    *(volatile int *)(m + MS_HPMP + 4) = RD(int, m, MS_MAXHPMP + 4);
    LOGI("styx: member 21 created (lv 60, hp %d mp %d, side %d/%d, row %d, b9 %d ba %d bb %d)", RD(int, m, MS_HPMP), RD(int, m, MS_HPMP + 4),
         m[0x31], m[0x32], RD(int, m, MS_ROW), m[0xb9], m[0xba], m[0xbb]);
}

/* 絵の番号120（スティクス）を読む ReadPlayerJobModel（メニューの絵）。キャラ21 のときだけ */
#define OFF_RPJM_FUNC 0x3f3724UL
#define OFF_RPJM_GOT  0x460318UL
typedef long (*RpjmFn)(void *game, u8 *ms, void *anm, int a, int b, int c, int d, int e, int f);
static RpjmFn g_origRpjm;
static long hook_Rpjm(void *game, u8 *ms, void *anm, int a, int b, int c, int d, int e, int f)
{
    static int n;
    int log = ms && (signed char)ms[0x30] == STYX_ID && n < 30;
    if (log) n++;
    /* v3.40t9 調査用：キャラの表の外（セーブの一覧などの一時的な写し）から読むときは全部出す（40行まで） */
    if (ms && game) {
        static int nt;
        u8 *m0 = member_at(game, 0), *m23 = member_at(game, 23);
        if ((ms < m0 || ms > m23) && nt < 40) {
            nt++;
        }
    }
    /* v3.40t7: 中断データ・セーブの一覧はセーブの中身から直接絵を読む。絵の番号（+0xb6）と陣営はセーブに入らないので、
       キャラ21・魔女のときは読む直前に入れる（陣営はキャラ18 から。18 も 0 なら入れない） */
    if (ms && (signed char)ms[0x30] == STYX_ID && RD(int, ms, MS_JOB) == STYX_JOB) {
        int fixed = 0;
        if (RD(short, ms, 0xb6) != STYX_MODEL || ms[0xb8] != 0 || ms[0xb0] != 0) {
            *(volatile short *)(ms + 0xb6) = STYX_MODEL; ms[0xb8] = 0; ms[0xb0] = 0; fixed |= 1;
        }
        u8 *src = game ? member_at(game, 18) : 0;
        if (ms[0x32] == 0 && src && src != ms && src[0x32] != 0) {
            ms[0x31] = src[0x31]; ms[0x32] = src[0x32]; ms[0x33] = src[0x33]; fixed |= 2;
        }
        static int nf;
        if (fixed && nf++ < 10) LOGI("styx: ReadPlayerJobModel input fixed (%s%s) from %x", fixed & 1 ? "model" : "", fixed & 2 ? " side" : "",
                                     (unsigned)((uintptr_t)__builtin_return_address(0) - g_base));
    }
    long r = g_origRpjm(game, ms, anm, a, b, c, d, e, f);
    if (log) LOGI("styx: ReadPlayerJobModel from %x  %d %d %d %d %d %d  job %d model %d/%d -> %ld",
                  (unsigned)((uintptr_t)__builtin_return_address(0) - g_base), a, b, c & 1, d & 1, e, f & 1, RD(int, ms, MS_JOB),
                  RD(short, ms, 0xb6), ms[0xb8], r);
    return r;
}

/* v3.40t8: スティクスが使ったウンディーネの叫び（効果203）・ローレライの悲涙（199）・ディスチャージ（127）を左右反転して描く。
   効果は敵が味方へ向けて使う向きの絵しかない。描く行列（MtxGraphics3D の Translate/Scale/Flip）は反転に対応しているので、
   MtxEfcData::Draw の外側で、効果の原点を通る縦の線について左右反転をかける。
   位置の基準にキャラを使う要素（ディスチャージは使ったキャラ）は、SetViewParam の間だけ、基準のキャラの x を
   画面の幅 W から引いた値にした写しへ差し替える（反転したあと元の位置に戻る）。
   使ったキャラ（MtxEfcCtrl+0x130）が画面の右半分（味方の側）にいるときだけ反転する。敵が使うときは今まで通り */
#define G_EFCPACK      0x4dea8
#define OFF_EFCDRAW_FUNC 0x36c5a4UL
#define OFF_EFCDRAW_GOT  0x45d578UL
#define OFF_EFCVIEW_FUNC 0x36bc78UL
#define OFF_EFCVIEW_GOT  0x45d568UL
typedef void (*GfxFn)(void *g);
typedef void (*GTransFn)(void *g, float x, float y, float z);
typedef void (*GScaleFn)(void *g, float x, float y, float z, float w);
typedef void (*EfcDrawFn)(void *data, void *g, void *ref, void *kfp, float *vec, void *mat, int a, int b, int flag, int c);
typedef void (*EfcViewFn)(void *data, void *kfp, int a, u8 *ctrl, void *ev, int flag);
static GfxFn p_GPush, p_GPop;
static GTransFn p_GTrans;
static GScaleFn p_GScale;
static EfcDrawFn g_origEfcDraw;
static EfcViewFn g_origEfcView;
#define MIR_N 12
#define MIR_OBJ 0x200
static struct { u8 *ctrl; u8 base[MIR_OBJ], targ[MIR_OBJ]; } g_mir[MIR_N] __attribute__((aligned(16)));
static int g_mirNext, g_efcDepth;
static int mirror_effect(const u8 *data)
{
    void *game = game_inst();
    if (!game || !data) return 0;
    u8 *pk = (u8 *)RDP(game, G_EFCPACK);
    if (!pk || RD(int, pk, 0) < 5) return 0;
    u8 *sec = pk + RD(int, pk, 4 + 4 * 4);
    int n = RD(int, sec, 0);
    static const short ids[] = { 203, 199, 127 };
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++)
        if (ids[i] <= n && data == sec + RD(int, sec, 4 + (ids[i] - 1) * 4)) return ids[i];
    return 0;
}
static int mirror_slot(const void *obj)
{
    for (int i = 0; i < MIR_N; i++) if (obj == g_mir[i].base || obj == g_mir[i].targ) return i;
    return -1;
}
static void hook_EfcView(void *data, void *kfp, int a, u8 *ctrl, void *ev, int flag)
{
    int id = ctrl ? mirror_effect(RDP(data, 8)) : 0;
    u8 *bo = id ? (u8 *)RDP(ctrl, 0x130) : 0;
    int W = ctrl ? RD(int, ctrl, 0x110) : 0;
    if (!bo || W <= 0 || RD(float, bo, 0x1c) <= W * 0.5f) { g_origEfcView(data, kfp, a, ctrl, ev, flag); return; }
    int k = -1;
    for (int i = 0; i < MIR_N; i++) if (g_mir[i].ctrl == ctrl) k = i;
    if (k < 0) {
        k = g_mirNext; g_mirNext = (g_mirNext + 1) % MIR_N; g_mir[k].ctrl = ctrl;
    }
    u8 *to = (u8 *)RDP(ctrl, 0x138);
    for (int i = 0; i < MIR_OBJ; i++) g_mir[k].base[i] = bo[i];
    *(float *)(g_mir[k].base + 0x1c) = (float)W - RD(float, bo, 0x1c);
    if (to) {
        for (int i = 0; i < MIR_OBJ; i++) g_mir[k].targ[i] = to[i];
        *(float *)(g_mir[k].targ + 0x1c) = (float)W - RD(float, to, 0x1c);
    }
    float bx = RD(float, ctrl, 0x140), tx = RD(float, ctrl, 0x14c);
    *(u8 **)(ctrl + 0x130) = g_mir[k].base;
    if (to) *(u8 **)(ctrl + 0x138) = g_mir[k].targ;
    *(float *)(ctrl + 0x140) = (float)W - bx;
    *(float *)(ctrl + 0x14c) = (float)W - tx;
    g_origEfcView(data, kfp, a, ctrl, ev, flag);
    *(u8 **)(ctrl + 0x130) = bo;
    if (to) *(u8 **)(ctrl + 0x138) = to;
    *(float *)(ctrl + 0x140) = bx;
    *(float *)(ctrl + 0x14c) = tx;
}
static void hook_EfcDraw(void *data, void *g, void *ref, void *kfp, float *vec, void *mat, int a, int b, int flag, int c)
{
    /* SetViewParam で写しに差し替えた印：MtxEfcData+0x90（使ったキャラ）が写しを指している */
    if (g_efcDepth || !p_GPush || !p_GPop || !p_GTrans || !p_GScale || mirror_slot(RDP(data, 0x90)) < 0) {
        g_efcDepth++;
        g_origEfcDraw(data, g, ref, kfp, vec, mat, a, b, flag, c);
        g_efcDepth--;
        return;
    }
    float tx = vec ? vec[0] : 0.0f;
    if (!(flag & 1)) tx += (float)(RD(int, data, 0xb8) / 2);
    p_GPush(g);
    p_GTrans(g, tx, 0.0f, 0.0f);
    p_GScale(g, -1.0f, 1.0f, 1.0f, 1.0f);
    p_GTrans(g, -tx, 0.0f, 0.0f);
    g_efcDepth++;
    g_origEfcDraw(data, g, ref, kfp, vec, mat, a, b, flag, c);
    g_efcDepth--;
    p_GPop(g);
}

/* v3.41: 絵のファイルを開く PaletteCtrlClass::ReadResourceImageForICP。fldchr120_0_1.dat（もとから無い）を開こうとしたら
   セーラの fldchr16_0_1.dat を読ませ、LoadICPDataBuffer でスティクスの色と絵に差し替える。
   セーブ・中断データの一覧（LoadSaveSceneSlotImage）は LoadCharaImage を通らず、ここを直接呼ぶ */
#define OFF_RRIFI_FUNC 0x413c44UL
#define OFF_RRIFI_GOT  0x45e2d0UL
typedef long (*RrifiFn)(void *pal, const char *name, int slot);
static RrifiFn g_origRrifi;
static int ends_with(const char *s, const char *t)
{
    int a = 0, b = 0;
    while (s[a]) a++;
    while (t[b]) b++;
    if (a < b) return 0;
    for (int i = 0; i < b; i++) if (s[a - b + i] != t[i]) return 0;
    return 1;
}
static long hook_Rrifi(void *pal, const char *name, int slot)
{
    if (name && ends_with(name, "fldchr120_0_1.dat")) {
        char nm[96]; int k = 0;
        int a = 0; while (name[a]) a++;
        int pre = a - 17;                       /* "fldchr120_0_1.dat" の前（あれば道筋） */
        if (pre < 0 || pre > 60) pre = 0;
        for (int i = 0; i < pre; i++) nm[k++] = name[i];
        static const char rep16[] = "fldchr16_0_1.dat";
        for (int i = 0; rep16[i]; i++) nm[k++] = rep16[i];
        nm[k] = 0;
        g_styxSub = 1;
        long r = g_origRrifi(pal, nm, slot);
        int left = g_styxSub;
        g_styxSub = 0;
        static int nl;
        if (nl++ < 20) LOGI("styx: %s -> %s (slot %d, %s)", name, nm, slot, left ? "not replaced" : "replaced");
        return r;
    }
    return g_origRrifi(pal, name, slot);
}

static void alarm_item(void *game);
static void alarm_frame(void);
static void styx_tick(void)
{
    void *game = game_inst();
    if (!game || !p_InitSlot || !p_SetJobStatus) return;
    styx_encoding(game);
    if (g_sjis < 0) return;
    /* v3.40t10: セーブ・中断データの一覧（ReadSaveSceneSlotImage）は、絵の番号などをセーブではなく
       ゲームのキャラの表（+0xb6 +0xb8 +0x31 +0xbb）から取る。タイトルではキャラ21 の枠が空なので、先に入れておく */
    {
        u8 *m = member_at(game, STYX_ID);
        static int nl;
        if (!styx_ready(game) && styx_fix_unsaved(game, m) && nl++ < 6)
            LOGI("styx: slot 21 unsaved fields preset (chapter %d, side %d/%d)", RD(int, game, G_CHAPTER), m[0x31], m[0x32]);
    }
    int a = styx_magic(game), b = styx_ability(game), c = styx_job(game);
    styx_command(game);
    static int last = -1;
    if (last != (a | b << 1 | c << 2)) { last = a | b << 1 | c << 2; LOGI("styx: magic %d ability %d job %d", a, b, c); }
    if (a && b && c && g_guestOn) styx_member(game);
}

/* v3.40t: 戦闘の台本 bsc.dat を読んだ直後、スティクス用の台本 80/81 を後ろに足し、目次の 80/81 をそこへ向ける。
   BufferControl::Load は数え数が0のときだけ CreateDataBuffer で読む。足した分は _MALLOC で作り直し、元は _FREE で返す
   （Release は bytearray::free で _FREE する） */
#include "bsc_tab.h"
#define OFF_BUFLOAD_FUNC 0x3ad2c8UL
#define OFF_BUFLOAD_GOT  0x45e1a8UL
#define G_BSCBUF         0x4df38
typedef int (*BufLoadFn)(void *bc);
static BufLoadFn g_origBufLoad;
#define OFF_BUFREL_FUNC 0x3ad364UL
#define OFF_BUFREL_GOT  0x45e240UL
typedef void (*BufRelFn)(void *bc);
static BufRelFn g_origBufRel;
static u8 g_bscBuf[BSC_SIZE + sizeof(bsc_80) + sizeof(bsc_81)] __attribute__((aligned(16)));
static u8 *g_bscOrig;
static int g_bscOrigLen;
static int mem_eq(const u8 *a, const u8 *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static void be32w(u8 *p, unsigned v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static int hook_BufLoad(void *bc)
{
    int first = RD(int, bc, 8) <= 0;
    int r = g_origBufLoad(bc);
    void *game = game_inst();
    if (!r || !first || !game || bc != (void *)((uintptr_t)game + G_BSCBUF) || !g_origBufRel) return r;
    u8 *d = (u8 *)RDP(bc, 0x10);
    int len = RD(int, bc, 0x18);
    if (!d || len != BSC_SIZE) { LOGE("bsc: size %d, not patched", len); return r; }
    u8 *ix = d + BSC_S1;
    if (be32(ix + 4 * 469) != BSC_OFF469 || be32(ix + 4 * 437) != BSC_OFF437 || be32(ix + 4 * 80) != BSC_EMPTY || be32(ix + 4 * 81) != BSC_EMPTY ||
        !mem_eq(ix + BSC_OFF469, bsc_469_orig, sizeof(bsc_469_orig)) || !mem_eq(ix + BSC_OFF437, bsc_437_orig, sizeof(bsc_437_orig))) {
        LOGE("bsc: scripts differ, not patched");
        return r;
    }
    int nl = len + (int)sizeof(bsc_80) + (int)sizeof(bsc_81);
    u8 *nd = g_bscBuf;
    for (int i = 0; i < len; i++) nd[i] = d[i];
    for (unsigned i = 0; i < sizeof(bsc_80); i++) nd[len + i] = bsc_80[i];
    for (unsigned i = 0; i < sizeof(bsc_81); i++) nd[len + sizeof(bsc_80) + i] = bsc_81[i];
    be32w(nd + BSC_S1 + 4 * 80, (unsigned)(len - BSC_S1));
    be32w(nd + BSC_S1 + 4 * 81, (unsigned)(len + sizeof(bsc_80) - BSC_S1));
    g_bscOrig = d; g_bscOrigLen = len;
    *(u8 **)((uintptr_t)bc + 0x10) = nd;
    *(volatile int *)((uintptr_t)bc + 0x18) = nl;
    LOGI("bsc: scripts 80/81 added (%d -> %d)", len, nl);
    return r;
}
/* 数え数が0になって返すときは、ゲームが読んだ元の置き場に戻してから返させる */
static void hook_BufRel(void *bc)
{
    if (g_bscOrig && RD(int, bc, 8) <= 1 && RDP(bc, 0x10) == (void *)g_bscBuf) {
        *(u8 **)((uintptr_t)bc + 0x10) = g_bscOrig;
        *(volatile int *)((uintptr_t)bc + 0x18) = g_bscOrigLen;
        g_bscOrig = 0;
        LOGI("bsc: original buffer restored for release");
    }
    g_origBufRel(bc);
}

/* ---------- キー入力 ---------- */

static int cur_bits(void) { return g_keyBits | g_axisBits | g_cur; }

static void memories_cost_tick(void *game);
/* 毎フレーム1回：押した瞬間とリピートを計算し、カーソルを動かしてから元の処理へ */
static long hook_AppRun(void *self, void *g)
{
    int cur = g_keyBits | g_axisBits | __atomic_exchange_n(&g_latch, 0, __ATOMIC_SEQ_CST);
    g_frame++;
    g_cur = cur;
    g_trig = cur & ~g_prev;
    g_rep = g_trig;
    for (int i = 0; i < 32; i++) {
        if (cur & (1 << i)) {
            int n = ++g_hold[i];
            if (n > REPEAT_DELAY && ((n - REPEAT_DELAY) % REPEAT_INTERVAL) == 0)
                g_rep |= (1 << i);
        } else {
            g_hold[i] = 0;
        }
    }
    g_prev = cur;
    g_pageTrig = __atomic_exchange_n(&g_pageLatch, 0, __ATOMIC_SEQ_CST);
    /* A＝乗る／降りる、Y＝地図（そのボタンが出ているときだけ） */
    g_blockA = g_blockMenu = 0;
    if (g_btnPress >= 0 && --g_btnWait <= 0) { LOGE("script button %d: not delivered", g_btnPress); g_btnPress = -1; }
    if (g_installed && !g_cursorMode) {
        if ((g_trig & K_A) && script_button_ok(BTN_RIDE)) { g_btnPress = BTN_RIDE; g_btnWait = 3; g_blockA = 1; }
        else if ((g_trig & K_MENU) && script_button_ok(BTN_MAP)) { g_btnPress = BTN_MAP; g_btnWait = 3; g_blockMenu = 1; }
    }

    g_benchTrig = __atomic_exchange_n(&g_benchLatch, 0, __ATOMIC_SEQ_CST);
    cursor_tick();
    dash_restore();                                /* イベントなどほかの処理が動かす前にも戻しておく */
    if (g_installed && (g_frame % 30) == 0) guest_tick();
    if (g_installed && (g_frame % 30) == 15) styx_tick();
    if (g_installed && (g_frame % 30) == 20) { void *gm = game_inst(); if (gm) alarm_item(gm); }
    if (g_installed) alarm_frame();
    if (g_installed && (g_frame % 30) == 20) memories_cost_tick(game_inst());
    if (g_installed) reward_rate_tick();
    if (g_installed) menu_step_tick(cur);
    long r = g_origAppRun(self, g);
    /* 部品の描画に乗れなかったカーソル（メンバー選択など）は、フレームの最後に上から描く */
    if (g_cursorMode && g_curC.item && g_drawnFrame != g_frame && p_FillRect && g)
        draw_frame(g, (int)g_curC.x, (int)g_curC.y, (int)g_curC.w, (int)g_curC.h);
    return r;
}

/* 台本がキーを読む所（FieldClass::GetReferenceEtc）。B（戻る）を押しっぱなしで見せると、
   乗り物の「押している方向と一致するか」の判定が外れ、山や海を抜けてしまう */
#define REF_ETC_START 0x3cf0d8UL
#define REF_ETC_SIZE  0x34cUL
static int script_key_read(uintptr_t ra, int mask)
{
    return mask == K_BACK && ra - g_base - REF_ETC_START < REF_ETC_SIZE;
}

static int hook_KeyState(void *self, int mask)
{
    uintptr_t ra;
    __asm__ volatile("mov %0, x30" : "=r"(ra));
    if (script_key_read(ra, mask)) return g_origState(self, mask);
    note_poll(mask, ra);
    return g_origState(self, mask) || (visible_bits(cur_bits()) & mask) != 0;
}

#define RA_FIELD_BACK 0x3b5058UL
#define RA_FIELD_A    0x3b4f88UL   /* FieldClass::MovePlayer が A（話しかける）を調べる所 */
#define RA_FIELD_SOFT 0x3b4fb4UL   /* FieldClass::MovePlayer がソフトキー（Y）を調べる所 */

/* 押せる状態の台本ボタンがあるか */
static int script_button_ok(int id)
{
    if (!g_field) return 0;
    uintptr_t S = (uintptr_t)g_field + FB_BASE;
    if (!RDP(S, 0)) return 0;
    for (int k = 0; k < 8; k++) {
        void *it = RDP(S, 0x58 + 8 * k);
        if (it && RD(int, S, 0x98 + 4 * k) == id && RD(u8, it, 0xc) && RD(u8, it, 0xd)) return 1;
    }
    return 0;
}   /* FieldClass::MovePlayer が「戻る」でメニューを開くか調べる所 */
static int hook_KeyTrigger(void *self, int mask)
{
    uintptr_t ra;
    __asm__ volatile("mov %0, x30" : "=r"(ra));
    if (ra - g_base == RA_FIELD_A && g_blockA) return 0;          /* A は台本ボタンへ回した */
    if (ra - g_base == RA_FIELD_SOFT && g_blockMenu) return 0;    /* Y は台本ボタンへ回した */
    if (ra - g_base == RA_FIELD_BACK) {
        /* フィールドのメニューは X（キーボードは M）で開く。B はキャンセル専用にする */
        if (g_origTrigger(self, mask)) return 1;           /* Android の戻る操作はそのまま */
        return (g_trig & K_XMENU) != 0;
    }
    if (ra - g_base == RA_BTL_PREV) {
        /* 戦闘の L：同時押し（ESCAPE）を待ってから効かせる。R はここで L の処理を借りる */
        if (g_rForce) { g_rUsed = 1; return 1; }
        if (g_btlBtn) {
            if (g_origTrigger(self, mask)) return 1;
            if (g_fireL > 0) { g_fireL = 0; return 1; }
            return 0;
        }
    }
    note_poll(mask, ra);
    if (g_origTrigger(self, mask))
        return 1;
    return (remap_for_caller(visible_bits(g_trig), ra) & mask) != 0;
}

static int hook_KeyRepeat(void *self, int mask)
{
    uintptr_t ra;
    __asm__ volatile("mov %0, x30" : "=r"(ra));
    if (script_key_read(ra, mask)) return g_origRepeat(self, mask);
    note_poll(mask, ra);
    if (g_origRepeat(self, mask))
        return 1;
    return (remap_for_caller(visible_bits(g_rep), ra) & mask) != 0;
}

/* ---------- v4.00: すっぴん の引き継ぎ（FF5 方式） ----------
   すっぴん（ジョブ1）の能力値を出すときだけ、マスター済み（JLV20、ジョブデータ +0x91 bit0）のジョブの
   倍率（+0x1d..+0x23: HP MP 力 速さ 体力 知性 精神）の最大値 ×1.2（JLV20 の上乗せ込み）に差し替える。
   すっぴん は JLV を数えないジョブなので、上乗せは差し替えた値にあらかじめ入れておく。
   差し替えは SetJobStatus の間だけで、終わったらすぐ戻す（中で SetJobStatus を呼び直しても戻し合う）。
   パッシブは、覚えるジョブをマスターしたものを SetMemberPassiveAbility の最後に足す。
   アビリティ選択では MakeList_AbilitySetCheck の印（灰色・選べない）を付ける。画面の固有欄には出さない */
#define OFF_SJS_FUNC   0x3e0f90UL     /* GameClass::SetJobStatus(MEMBER_STATUS&, bool) */
#define OFF_SJS_GOT    0x45ea80UL
#define OFF_SMPA_FUNC  0x3e5fdcUL     /* GameClass::SetMemberPassiveAbility(MEMBER_STATUS&) */
#define OFF_SMPA_GOT   0x4600f8UL
#define OFF_MLASC_FUNC 0x3e2294UL     /* GameClass::MakeList_AbilitySetCheck(MEMBER_STATUS&) */
#define OFF_MLASC_GOT  0x460068UL
#define OFF_SMD_FUNC   0x3a1314UL     /* BattleClass::SetMagicDamage(BTLACT&, BTLACT&) */
#define OFF_SMD_GOT    0x45eb88UL
#define JD_PCT         0x1d            /* ジョブデータ: 倍率7つ */
#define JD_FLAGS       0x91            /* bit0: マスターの判定に入る正規ジョブ */
#define MS_JLV         0xc4            /* MEMBER_STATUS: ジョブごとの JLV */
#define SUPPIN         1
#define G_SELLIST_N    0x21528         /* アビリティ選択の一覧の数 */
#define G_SELLIST      0x2152c         /* 一覧（int） */
#define G_SETCHECK     0x1856c         /* 印（bit = id-1） */

typedef void (*SMPAFn)(void *game, u8 *m);
typedef void (*MLASCFn)(void *game, u8 *m);
typedef void (*AddPassiveFn)(void *game, u8 *m, int id);
typedef void (*SMDFn)(void *bc, u8 *atk, u8 *tgt);
typedef u8 *(*GetMgcFn)(void *game, int id);
static SetJobStatusFn g_origSJS;
static SMPAFn g_origSMPA;
static MLASCFn g_origMLASC;
static SMDFn g_origSMD;
static AddPassiveFn p_AddPassive;
static GetMgcFn p_GetMgc;

/* 引き継ぐパッシブと、それを覚えるジョブ */
static const struct { u8 job, ab; } k_inherit[] = {
    { 2, 5 },    /* 戦士: 反撃 */
    { 4, 13 },   /* シーフ: トレジャーハント */
    { 4, 15 },   /* シーフ: 警戒 */
    { 15, 67 },  /* ナイト: かばう */
    { 15, 68 },  /* ナイト: ＭＰ変換 */
    { 10, 73 },  /* レンジャー: てあて */
    { 12, 77 },  /* 暗黒騎士: 後列斬り */
    { 12, 79 },  /* 暗黒騎士: ＨＰ吸収 */
};

static int job_mastered(void *game, const u8 *m, int job)
{
    int n = RD(int, game, G_JOBCOUNT);
    u8 *jd = (u8 *)RDP(game, G_JOBDATA);
    if (!jd || job < 0 || job >= n || job >= 64) return 0;
    if (!(jd[job * JD_SIZE + JD_FLAGS] & 1)) return 0;
    return (signed char)m[MS_JLV + job] >= 20;
}

static u8 *g_sjsMember;               /* v4.01: SetJobStatus の最中のメンバー（中から呼ぶ SetMemberEquipStatus が見る） */
static void sjs_body(void *game, u8 *m, int b);
static void hook_SJS(void *game, u8 *m, int b)
{
    u8 *prev = g_sjsMember;
    g_sjsMember = m;
    sjs_body(game, m, b);
    g_sjsMember = prev;
}

static void sjs_body(void *game, u8 *m, int b)
{
    u8 *jd = game ? (u8 *)RDP(game, G_JOBDATA) : 0;
    int n = game ? RD(int, game, G_JOBCOUNT) : 0;
    if (!m || !jd || m[MS_JOB] != SUPPIN || n <= SUPPIN || n > 64) { g_origSJS(game, m, b); return; }
    u8 *own = jd + SUPPIN * JD_SIZE + JD_PCT;
    u8 saved[7], best[7];
    int any = 0;
    for (int k = 0; k < 7; k++) { saved[k] = own[k]; best[k] = own[k]; }
    for (int j = 0; j < n; j++) {
        if (j == SUPPIN || !job_mastered(game, m, j)) continue;
        any = 1;
        const u8 *p = jd + j * JD_SIZE + JD_PCT;
        for (int k = 0; k < 7; k++) {
            int v = p[k] * 120 / 100;
            if (v > 255) v = 255;
            if (v > best[k]) best[k] = (u8)v;
        }
    }
    if (!any) { g_origSJS(game, m, b); return; }
    static u8 lastBest[7];
    static int lastSet;
    int diff = !lastSet;
    for (int k = 0; k < 7; k++) if (lastBest[k] != best[k]) diff = 1;
    if (diff) {
        lastSet = 1;
        for (int k = 0; k < 7; k++) lastBest[k] = best[k];
        LOGI("suppin: inherit HP%d MP%d STR%d SPD%d VIT%d INT%d SPR%d",
             best[0], best[1], best[2], best[3], best[4], best[5], best[6]);
    }
    for (int k = 0; k < 7; k++) own[k] = best[k];
    g_origSJS(game, m, b);
    for (int k = 0; k < 7; k++) own[k] = saved[k];
}

static void hook_SMPA(void *game, u8 *m)
{
    g_origSMPA(game, m);
    if (!m || !game || !p_AddPassive || m[MS_JOB] != SUPPIN) return;
    unsigned got = 0;
    for (unsigned i = 0; i < sizeof(k_inherit) / sizeof(k_inherit[0]); i++) {
        if (!job_mastered(game, m, k_inherit[i].job)) continue;
        p_AddPassive(game, m, k_inherit[i].ab);
        got |= 1u << i;
    }
    static unsigned lastGot = ~0u;
    if (got != lastGot) { lastGot = got; LOGI("suppin: passive mask %02x", got); }
}

/* 1.0-pre15: 魔法・技の画面の右の列（白魔法・黒魔法…、GameClass::CreateMemberMenuCommand）。一覧の最後に、コマンドの側の印（+0x2a）と
   キャラの側（+0x32）を照らし合わせる。魔法剣のコマンド（18）は側の印 3、ゲストのエルゴは側 0 なので外れる
   （戦闘のコマンド欄は照らし合わせないので出る）。エルゴのときだけ、作る間だけ側を 3 にしてすぐ戻す */
#define OFF_CMMC_FUNC 0x3e51acUL
#define OFF_CMMC_GOT  0x460108UL
#define MS_SIDE       0x32
typedef void (*CmmcFn)(void *game, u8 *m);
static CmmcFn g_origCmmc;
static void hook_Cmmc(void *game, u8 *m)
{
    if (!game || !m || m != member_at(game, 8) || m[MS_SIDE] != 0) { g_origCmmc(game, m); return; }
    m[MS_SIDE] = 3;
    g_origCmmc(game, m);
    m[MS_SIDE] = 0;
}

static void hook_MLASC(void *game, u8 *m)
{
    g_origMLASC(game, m);
    if (!m || !game || m[MS_JOB] != SUPPIN) return;
    int cnt = RD(int, game, G_SELLIST_N);
    for (int i = 0; i < cnt && i < 128; i++) {
        int id = RD(int, game, G_SELLIST + i * 4);
        if (id < 1) continue;
        for (unsigned k = 0; k < sizeof(k_inherit) / sizeof(k_inherit[0]); k++) {
            if (k_inherit[k].ab != id || !job_mastered(game, m, k_inherit[k].job)) continue;
            volatile unsigned *w = (volatile unsigned *)((uintptr_t)game + G_SETCHECK + ((id - 1) >> 5) * 4);
            *w |= 1u << ((id - 1) & 31);
        }
    }
}

/* 1.0-pre22: スティクスの青魔法は、元の敵の技の分類 17 なので、BattleClass::ChrActionMagic が
   放つときのエフェクト（PlayMagicStartEffect、分類 1〜16 だけ。手を上げる動きもこれ）を出さない。
   ChrActionMagic の後で、スティクスの青魔法で、この回に MP が減った（＝放った瞬間）なら、
   黒魔法（ファイア、魔法25 の分類）と同じエフェクトを、ゲームと同じ条件（BTLACT +0x62c・+0x628）で出す */
#define OFF_CAM_FUNC  0x3a62dcUL
#define OFF_CAM_GOT   0x45e868UL
#define OFF_PMSE_FUNC 0x3a4304UL   /* BattleClass::PlayMagicStartEffect(BTLACT&, int) */
typedef void (*CamFn)(void *b, u8 *act);
typedef void (*PmseFn)(void *b, u8 *act, int kind);
static CamFn g_origCam;
static int styx_blue_id(int id) { return id == 73 || id == 74 || (id >= 76 && id <= 81); }
static void hook_Cam(void *b, u8 *act)
{
    int mp0 = act ? RD(int, act, 0x38) : 0;
    g_origCam(b, act);
    void *game = game_inst();
    if (!act || !game || RD(int, act, 0x38) >= mp0 || !styx_blue_id(RD(int, act, 0x46c))) return;
    if ((u8 *)RDP(act, 8) != member_at(game, STYX_ID) || !act[0x62c] || !act[0x628]) return;
    u8 *tab = (u8 *)RDP(game, G_MAGIC_TBL);
    if (!tab) return;
    int kind = tab[25 * MG_SIZE + 0x10] - 1;
    if (kind < 0 || kind > 15) return;
    ((PmseFn)(g_base + OFF_PMSE_FUNC))(b, act, kind);
    RD(int, b, 0x6dfc + 0xff8) |= 8;
    static int nlog;
    if (nlog < 10) { nlog++; LOGI("styx: blue magic %d start effect %d (mp %d -> %d)", RD(int, act, 0x46c), kind, mp0, RD(int, act, 0x38)); }
}

/* 1.1: アラーム（FF4 のもの）。空きのアイテム 489 を道具にし、道具屋（品ぞろえにポーションがある店）で 100 ギルで売る。
   フィールドで使うと警報（SE 74）を鳴らし、狩りの対象の編成（alarm_tab.h）を呼ぶ：今いる区画の1段目（稼ぎ・しっぽ・
   ドロップ限定装備）→ 2段目（牙だけ）→ そのマップで一番重みの低い編成。同じ段では区画の狭い方、同じ広さなら重みの低い方、
   それも同じならランダム。
   戦闘はふつうのエンカウントと同じく共通イベント 0x0106 の抽選（36行目から）に任せ、ScriptEncount で編成だけ差し替える。
   エンカウントのないマップでは「何も起こらなかった」を出して減らさない。
   1.1-pre2: 道具を使う処理は ItemFunc の中にもあり（UseItem を通らない）、どちらも ITEM_DATA +0x1a が0でなければ
   FieldClass::CallItemEvent(+0x1a − 1) を呼ぶ。アラームは +0x1a に魔法（1バイト）とも道具とも重ならない値を入れ、
   CallItemEvent で受ける。呼んだあと +0x20 bit5 なら bit4 で1個減らしてメニューを閉じるので、
   使えないときは bit5 をその場だけ外す（メニューに残り、減らない。次のフレームで戻す）
   1.1-pre3: 使えない場所では、セーブポイント以外のテントと同じく暗く表示して選べなくする（DrawMenuItem・IsItemSelect）。
   戦闘は、共通イベント 0x0106 を「選んだ行の番号を作業値0に入れて57行目から」動かす（57〜58行で先制・BGM などを決め、
   59行で編成を取り、64行で戦闘の共通イベント 0x0101 を呼ぶ）。積んだあと MoveEvent(番号, true) で動かす
   （CallItemEvent と同じ）。1.1-pre4: PushEventInfo の引数は（番号, 状態, 条件, 番号, 行）。行は最後
   あわせて、終章のファブリカの武器屋（マテルの村の品をまとめた店）に、入れ忘れの夢の竪琴を足す */
#include "alarm_tab.h"
#define ALARM_ID       489
#define TENT_ID        435
#define G_ITEMDATA     0x25380        /* GameClass: ITEM_DATA[640]（0x60 ずつ。+0 名前 +8 説明 +0x10 種類 +0x14 値段 +0x1a イベント +0x1c 戦闘 +0x20 印） */
#define ITEM_SZ        0x60
#define G_BAG          0x25340        /* GameClass: 持ち物の一覧（int*、MenuClass +0x28 が位置） */
#define G_SOUND        0xb0           /* GameClass: SoundCtrlClass */
#define G_SHOP_N       0x19ec8        /* GameClass: 店の品数（ScriptShop が書く） */
#define G_SHOP_ITEMS   0x19ed8        /* GameClass: 店の品（int[64]） */
#define F_ENC_N        0xac0          /* FieldClass: エンカウントの表の数 */
#define F_ENC_TAB      0xac8          /* FieldClass: 表（int×6：編成, 重み, x1, y1, x2, y2） */
#define F_ENC_RATE     0xad0          /* FieldClass: エンカウント率（0 ならエンカウントしない） */
#define F_WORK         0xe9ac         /* FieldClass: 台本の作業値 */
#define SE_ALARM       74             /* 空中戦艦の火薬庫などの警報 */
#define EV_ENCOUNT     0x0106         /* 共通イベント：ランダムエンカウント */
#define EV_ENC_LINE    57             /* その中の、抽選のあと（作業値0＝選んだ行）から戦闘までの部分 */
#define ALARM_EVENT       0x400       /* ITEM_DATA +0x1a（CallItemEvent には −1 で来る） */
#define OFF_CIE_FUNC      0x3d1d94UL  /* FieldClass::CallItemEvent(int) */
#define OFF_CIE_GOT       0x45fbe0UL
#define OFF_SHOP_FUNC     0x3bcc70UL  /* FieldClass::ScriptShop(char*) */
#define OFF_SHOP_GOT      0x45f6b8UL
#define OFF_SENC_FUNC     0x3bcd6cUL  /* FieldClass::ScriptEncount(char*) */
#define OFF_SENC_GOT      0x45f6c0UL
#define OFF_EVID2IDX      0x3cf778UL  /* FieldClass::EventIDToIndex(int) */
#define OFF_EVBOOT        0x3c10d4UL  /* FieldClass::GetEventBootCondition(int) */
#define OFF_PUSHEV        0x3d197cUL  /* FieldClass::PushEventInfo(int, int, int, int, int) */
#define OFF_REFPARTY      0x3ce5c8UL  /* FieldClass::GetReferenceParty(int, int, int) */
#define OFF_RESERVESE     0x41a888UL  /* SoundCtrlClass::ReserveSE(int) */
#define OFF_ADDITEM       0x3e3188UL  /* GameClass::AddItem(int, int) */
#define OFF_MOVEEV        0x3d23d8UL  /* FieldClass::MoveEvent(int, bool) */
#define OFF_GETWAIT       0x3c206cUL  /* FieldClass::GetWaitInfo(int) */
#define OFF_SETWAIT       0x3c20fcUL  /* FieldClass::SetWait(int*, int, int) */
#define ALARM_WAIT        84          /* 警報が鳴り終わるまで（ヘリオガバルスでは SE 74 に合わせて 14 フレームの点滅を6回） */
#define OFF_ISSEL_FUNC    0x403c5cUL  /* MenuClass::IsItemSelect(int) */
#define OFF_ISSEL_GOT     0x4604d8UL
#define OFF_DMI_FUNC      0x410a58UL  /* MenuClass::DrawMenuItem(ITEM_DATA*, int, int, int, int) */
#define OFF_DMI_GOT       0x460658UL
typedef void (*CieFn)(void *field, int ev);
typedef void (*ScriptFn)(void *field, u8 *sc);
typedef int (*FieldIntFn)(void *field, int a);
typedef void (*PushEvFn)(void *field, int a, int b, int c, int d, int e);
typedef int (*RefPartyFn)(void *field, int a, int b, int c);
typedef void (*SeFn)(void *snd, int id);
typedef void (*AddItemFn)(void *game, int id, int n);
typedef void (*MoveEvFn)(void *field, int idx, int now);
typedef int *(*GetWaitFn)(void *field, int idx);
typedef void (*SetWaitFn)(void *field, int *w, int type, int frames);
typedef int (*IsSelFn)(void *menu, int pos);
typedef void (*DmiFn)(void *menu, u8 *item, int a, int b, int c, int d);
static CieFn g_origCie;
static int g_alarmRestore;            /* 使えなかったときに外した bit5 を次のフレームで戻す */
static ScriptFn g_origShop, g_origSEnc;
static int g_alarmForm = -1;          /* 次の ScriptEncount で差し替える編成（-1 はなし） */
static unsigned g_alarmFrame;
static const char k_alarmNameSJ[] = "\x83\x41\x83\x89\x81\x5b\x83\x80", k_alarmNameU8[] = "アラーム";
static const char k_alarmHelpSJ[] = "\x83\x82\x83\x93\x83\x58\x83\x5e\x81\x5b\x82\xf0\x8c\xc4\x82\xd1\x8a\xf1\x82\xb9\x82\xe9",
                  k_alarmHelpU8[] = "モンスターを呼び寄せる";
static IsSelFn g_origIsSel;
static DmiFn g_origDmi;

/* アイテム 489（名前のない空き、種類 0x19）をテントの写しから作る。名前・説明・値段 100・イベントなし・どこでも選べる印 */
static void alarm_item(void *game)
{
    if (g_sjis < 0) return;
    u8 *a = (u8 *)game + G_ITEMDATA + ALARM_ID * ITEM_SZ, *t = (u8 *)game + G_ITEMDATA + TENT_ID * ITEM_SZ;
    const char *nm = g_sjis ? k_alarmNameSJ : k_alarmNameU8;
    if (*(const char **)a == nm) {
        if (RD(unsigned short, a, 0x1a) != ALARM_EVENT) RD(unsigned short, a, 0x1a) = ALARM_EVENT;
        return;
    }
    const char *old = *(const char **)a;
    if (a[0x10] != 0x19 || (old && *old) || RD(unsigned short, t, 0x1a) != 1) {
        static int told;
        if (!told) { told = 1; LOGE("alarm: item %d is not free (type %x, name %d) or tent differs (event %d), skipped", ALARM_ID, a[0x10],
                                    old && *old, RD(unsigned short, t, 0x1a)); }
        return;
    }
    for (int k = 0; k < ITEM_SZ; k++) a[k] = t[k];
    *(const char **)a = nm;
    *(const char **)(a + 8) = g_sjis ? k_alarmHelpSJ : k_alarmHelpU8;
    RD(unsigned, a, 0x14) = 100;
    RD(unsigned short, a, 0x1a) = ALARM_EVENT;
    RD(unsigned short, a, 0x20) = (RD(unsigned short, t, 0x20) & ~8) | 1;   /* テントのセーブポイント限定（bit3）を外し、いつでも選べる（bit0） */
    LOGI("alarm: item %d ready (flags %x)", ALARM_ID, RD(unsigned short, a, 0x20));
}

static int alarm_tier(int ch, int form)
{
    unsigned key = (unsigned)((ch << 16) | (form & 0xffff)) << 2;
    int lo = 0, hi = (int)(sizeof(k_alarmTab) / sizeof(k_alarmTab[0])) - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        unsigned v = k_alarmTab[m] & ~3u;
        if (v == key) return (int)(k_alarmTab[m] & 3);
        if (v < key) lo = m + 1; else hi = m - 1;
    }
    return 0;
}

static unsigned alarm_rand(void)
{
    static unsigned x = 0x9e3779b9u;
    x ^= g_frame * 2654435761u; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

/* 表の行を選ぶ（戻り値は行の番号、*form に編成）。使えない場所では -1。lg が0ならログを出さない */
static int alarm_scan(void *game, void *field, int lg, int *form)
{
    int n = RD(int, field, F_ENC_N), rate = RD(int, field, F_ENC_RATE);
    int *e = (int *)RDP(field, F_ENC_TAB);
    if (n <= 0 || !e || rate <= 0) { if (lg) LOGI("alarm: no encounter here (n %d rate %d)", n, rate); return -1; }
    int x = ((RefPartyFn)(g_base + OFF_REFPARTY))(field, 0, 1, 1), y = ((RefPartyFn)(g_base + OFF_REFPARTY))(field, 0, 1, 2);
    int ch = RD(int, game, G_CHAPTER);
    int here = 0;
    int bestT = 9, bestA = 1 << 30, bestW = 1 << 30, bestI = -1, ties = 0;     /* 今いる区画の対象 */
    int lowW = 1 << 30, lowI = -1, lowTies = 0;                /* マップ全体で一番重みの低い行 */
    for (int i = 0; i < n && i < 64; i++) {
        int *r = e + i * 6;
        int f = r[0], w = r[1];
        if (f <= 0 || w <= 0) continue;
        int in = x >= r[2] && x <= r[4] && y >= r[3] && y <= r[5];
        if (in) here = 1;
        if (!lg) { if (here) break; continue; }
        int t = alarm_tier(ch, f);
        /* 1.1-pre6: 同じ段では区画の狭い方（その場所でしか出ない敵）を先に、同じ広さなら重みの低い方 */
        int ar = (r[4] - r[2] + 1) * (r[5] - r[3] + 1);
        if (in && t && (t < bestT || (t == bestT && (ar < bestA || (ar == bestA && w < bestW))))) { bestT = t; bestA = ar; bestW = w; bestI = i; ties = 1; }
        else if (in && t && t == bestT && ar == bestA && w == bestW && alarm_rand() % (unsigned)++ties == 0) bestI = i;
        if (w < lowW) { lowW = w; lowI = i; lowTies = 1; }
        else if (w == lowW && alarm_rand() % (unsigned)++lowTies == 0) lowI = i;
    }
    if (!here) { if (lg) LOGI("alarm: no entry covers %d,%d", x, y); return -1; }
    if (!lg) return 0;
    int i = bestI >= 0 ? bestI : lowI;
    *form = e[i * 6];
    LOGI("alarm: chapter %d map %d at %d,%d -> row %d form %d (%s)", ch, g_curMap, x, y, i, *form,
         bestI >= 0 ? (bestT == 1 ? "tier 1" : "tier 2") : "lowest weight");
    return i;
}

static int alarm_usable(void *game)
{
    int f;
    return alarm_scan(game, (u8 *)game + G_FIELD, 0, &f) >= 0;
}

/* 使えない場所では、セーブポイント以外のテントと同じく選べない（押すとブザー） */
static int hook_IsSel(void *menu, int pos)
{
    void *game = game_inst();
    int *bag = game ? (int *)RDP(game, G_BAG) : 0;
    if (bag && pos >= 0 && pos < 640 && bag[pos] == ALARM_ID && !alarm_usable(game)) return 0;
    return g_origIsSel(menu, pos);
}

/* 使えない場所では暗く描く（+0x20 bit3＝セーブポイントだけ、を描く間だけ立てる） */
static void hook_Dmi(void *menu, u8 *item, int a, int b, int c, int d)
{
    void *game = game_inst();
    if (!game || item != (u8 *)game + G_ITEMDATA + ALARM_ID * ITEM_SZ || alarm_usable(game)) { g_origDmi(menu, item, a, b, c, d); return; }
    unsigned short fl = RD(unsigned short, item, 0x20);
    RD(unsigned short, item, 0x20) = fl | 8;
    g_origDmi(menu, item, a, b, c, d);
    RD(unsigned short, item, 0x20) = fl;
}

static void hook_Cie(void *field, int ev)
{
    if (ev != ALARM_EVENT - 1) { g_origCie(field, ev); return; }
    void *game = game_inst();
    if (!game) return;
    u8 *a = (u8 *)game + G_ITEMDATA + ALARM_ID * ITEM_SZ;
    int f = -1;
    int row = alarm_scan(game, field, 1, &f);
    int idx = row >= 0 ? ((FieldIntFn)(g_base + OFF_EVID2IDX))(field, EV_ENCOUNT) : -1;
    if (row >= 0 && idx < 0) LOGE("alarm: common event %x not found", EV_ENCOUNT);
    if (idx < 0) {   /* 選べないようにしてあるので、ふつうは来ない */
        RD(unsigned short, a, 0x20) &= ~0x20;   /* メニューに残し、減らさない */
        g_alarmRestore = 1;
        return;
    }
    int boot = ((FieldIntFn)(g_base + OFF_EVBOOT))(field, idx);
    ((SeFn)(g_base + OFF_RESERVESE))((u8 *)game + G_SOUND, SE_ALARM);
    RD(int, field, F_WORK) = row;
    g_alarmForm = f; g_alarmFrame = g_frame;
    /* PushEventInfo(番号, 状態, 条件, 番号, 行)：状態0＝これから動かす、最後が始める行（+0x18）。
       pre2/pre3 は状態の欄に行を入れていて、MoveEvent が知らない状態のまま回り続けて止まっていた */
    /* 1.1-pre5: 警報が鳴り終わるまで待ってから戦闘へ。台本の待ち（命令 0x32）と同じく SetWait(待ち, 1, フレーム数) を入れ、
       状態3（待っている）で積む。待っている間は、イベント中なので動けない */
    int *wt = ((GetWaitFn)(g_base + OFF_GETWAIT))(field, idx);
    if (wt) ((SetWaitFn)(g_base + OFF_SETWAIT))(field, wt, 1, ALARM_WAIT);
    ((PushEvFn)(g_base + OFF_PUSHEV))(field, idx, wt ? 3 : 0, boot == 5, idx, EV_ENC_LINE);
    ((MoveEvFn)(g_base + OFF_MOVEEV))(field, idx, 1);
    LOGI("alarm: event %x (index %d, boot %d) started at line %d, row %d", EV_ENCOUNT, idx, boot, EV_ENC_LINE, row);
}

/* 毎フレーム：使えなかったときに外した印を戻す */
static void alarm_frame(void)
{
    if (!g_alarmRestore) return;
    void *game = game_inst();
    if (game) RD(unsigned short, game, G_ITEMDATA + ALARM_ID * ITEM_SZ + 0x20) |= 0x20;
    g_alarmRestore = 0;
}

static void hook_SEnc(void *field, u8 *sc)
{
    int *w = (int *)((u8 *)field + F_WORK);
    if (g_alarmForm >= 0) {
        if (g_frame - g_alarmFrame < 600 && sc[1]) {
            LOGI("alarm: formation %d -> %d", w[0], g_alarmForm);
            w[0] = g_alarmForm;
        } else LOGE("alarm: pending formation %d dropped (frames %u, direct %d)", g_alarmForm, g_frame - g_alarmFrame, !sc[1]);
        g_alarmForm = -1;
    }
    g_origSEnc(field, sc);
}

/* 店を開いたあと：道具屋（種類0＝道具を1つでも売る店。終盤はポーションを置かない店がある）ならアラームをテントの前に、終章のファブリカの武器屋なら夢の竪琴をメルヘンハープの前に */
static void hook_Shop(void *field, u8 *sc)
{
    g_origShop(field, sc);
    void *game = game_inst();
    if (!game || sc[1] != 0) return;
    int n = RD(int, game, G_SHOP_N);
    int *it = (int *)((u8 *)game + G_SHOP_ITEMS);
    if (n <= 0 || n >= 64) return;
    int tool = 0, harp = -1, dream = -1, alarm = -1, rod = 0, claw = 0;
    for (int i = 0; i < n; i++) {
        unsigned id = (unsigned)it[i];
        if (id && id < 640 && id != ALARM_ID && RD(u8, game, G_ITEMDATA + id * ITEM_SZ + 0x10) == 0) tool = 1;
        if (it[i] == 178) harp = i;
        if (it[i] == 176) dream = i;
        if (it[i] == ALARM_ID) alarm = i;
        if (it[i] == 92) rod = 1;
        if (it[i] == 141) claw = 1;
    }
    if (tool && alarm < 0 && RD(int, game, G_ITEMDATA + ALARM_ID * ITEM_SZ + 0x14) == 100) {
        int at = n;   /* テントの前（なければ末尾） */
        for (int i = 0; i < n; i++) if (it[i] == TENT_ID) { at = i; break; }
        for (int i = n; i > at; i--) it[i] = it[i - 1];
        it[at] = ALARM_ID; RD(int, game, G_SHOP_N) = ++n;
        if (harp >= at) harp++;
        static int nl;
        if (nl++ < 10) LOGI("alarm: added to shop (map %d, %d items)", g_curMap, n);
    }
    if (RD(int, game, G_CHAPTER) == 11 && n == 17 && harp >= 0 && dream < 0 && rod && claw) {
        for (int i = n; i > harp; i--) it[i] = it[i - 1];
        it[harp] = 176; RD(int, game, G_SHOP_N) = n + 1;
        LOGI("shop: dream harp added (map %d)", g_curMap);
    }
}

/* v4.00: 敵の物理技（ダメージ式が攻撃力の種類8、MGC_DATA +0x28 == 8）が HP に当たったら、たたかう と同じく
   「物理で殴られた」印（BTLACT +0x1a4 bit0。味方の反撃と敵のカウンターが見る）を立て、
   混乱・睡眠（+0xb4 bit10/11）なら解除の印（+0xd8 |= 0xc00）を書く（SetNormalPhysicAttack と同じ）。
   攻撃したのが敵（BTLACT +0x14 == 1）のときだけ。味方の種類8（剣の舞など）は変えない */
static void hook_SMD(void *bc, u8 *atk, u8 *tgt)
{
    int pre = tgt ? RD(int, tgt, 0xc8) : 0;
    g_origSMD(bc, atk, tgt);
    if (!atk || !tgt || !p_GetMgc || RD(int, atk, 0x14) != 1) return;
    void *game = game_inst();
    if (!game) return;
    u8 *mgc = p_GetMgc(game, RD(int, atk, 0x46c));
    if (!mgc || RD(unsigned short, mgc, 0x28) != 8) return;
    if (!(RD(u8, tgt, 0xbc) & 0x10) || RD(int, tgt, 0xc8) == pre) return;
    RD(int, tgt, 0x1a4) |= 1;
    int wake = (RD(u8, tgt, 0xb5) & 0xc) != 0;
    if (wake) RD(int, tgt, 0xd8) |= 0xc00;
    static int nlog;
    if (nlog < 40) { nlog++; LOGI("phys skill: magic %d hit (wake %d)", RD(int, atk, 0x46c), wake); }
}

/* ---------- v4.01: メモリーズ（すっぴん は引き継ぎとメモリーの大きい方、2枠） ----------
   SetMemberEquipStatus は +0x12c bit3（メモリーズ）が立っていると、力〜精神（+0x1dc..+0x1ec）を
   GameClass+0x4ddd0+0xe4..+0xf4 のメモリーの値（255 まで）で置き換える。
   すっぴん のときだけ、その5つを一時的に「メモリーの値と引き継いだ能力値の大きい方」にしてから呼び、すぐ戻す。
   引き継いだ能力値は SetJobStatus が +0x1dc.. に書いた値。SetJobStatus の中から呼ばれたときに覚えておき、
   ほかから呼ばれたとき（装備の変更など。+0x1dc.. は前のメモリーの置き換えで上書きされている）はそれを使う */
#define OFF_SMES_FUNC  0x3e4040UL     /* GameClass::SetMemberEquipStatus(MEMBER_STATUS&) */
#define OFF_SMES_GOT   0x45e9d0UL
#define G_MEMPARAM     (0x4ddd0 + 0xe4)  /* GameClass: メモリーの値 力 速さ 体力 知性 精神（int） */
#define MS_BASESTAT    0x1dc           /* MEMBER_STATUS: 力 速さ 体力 知性 精神（int、装備の前） */
#define AB_MEMORIES    65
#define AB_MEMORANDUM  64
#define AB_COST        0x10
typedef void (*SMESFn)(void *game, u8 *m);
static SMESFn g_origSMES;
static struct { u8 *m; int v[5]; } g_inhCache[24];
static int g_inhNext;

static void hook_SMES(void *game, u8 *m)
{
    if (!game || !m || m[MS_JOB] != SUPPIN) { g_origSMES(game, m); return; }
    int v[5], k, slot = -1;
    for (k = 0; k < 24; k++) if (g_inhCache[k].m == m) { slot = k; break; }
    if (m == g_sjsMember) {          /* SetJobStatus の続き：+0x1dc.. は引き継いだ値 */
        if (slot < 0) { slot = g_inhNext; g_inhNext = (g_inhNext + 1) % 24; g_inhCache[slot].m = m; }
        for (k = 0; k < 5; k++) g_inhCache[slot].v[k] = v[k] = RD(int, m, MS_BASESTAT + 4 * k);
    } else if (slot >= 0) {
        for (k = 0; k < 5; k++) v[k] = g_inhCache[slot].v[k];
    } else {
        static int told;
        if (!told) { told = 1; LOGI("memories: no inherited values yet, using current"); }
        for (k = 0; k < 5; k++) v[k] = RD(int, m, MS_BASESTAT + 4 * k);
    }
    volatile int *gm = (volatile int *)((uintptr_t)game + G_MEMPARAM);
    int saved[5], raised = 0;
    for (k = 0; k < 5; k++) {
        saved[k] = gm[k];
        if (v[k] > saved[k]) { gm[k] = v[k]; raised |= 1 << k; }
    }
    g_origSMES(game, m);
    for (k = 0; k < 5; k++) gm[k] = saved[k];
    if (RD(int, m, 0x12c) & 8) {
        static int lastLog[10] = { -1 };
        int now[10];
        for (k = 0; k < 5; k++) { now[k] = saved[k]; now[5 + k] = v[k]; }
        int diff = 0;
        for (k = 0; k < 10; k++) if (now[k] != lastLog[k]) diff = 1;
        if (diff) {
            for (k = 0; k < 10; k++) lastLog[k] = now[k];
            LOGI("memories: suppin mem %d/%d/%d/%d/%d inherit %d/%d/%d/%d/%d raised %02x",
                 saved[0], saved[1], saved[2], saved[3], saved[4], v[0], v[1], v[2], v[3], v[4], raised);
        }
    }
}

/* メモリーズの必要枠 3 → 2（アビリティ表 +0x10）。名前と、メモランダム（同じ3枠）を確かめてから */
static const char k_memSJ[] = "\x83\x81\x83\x82\x83\x8a\x81\x5b\x83\x59", k_memU8[] = "メモリーズ";
static void memories_cost_tick(void *game)
{
    static int done;
    if (done || !game) return;
    styx_encoding(game);
    if (g_sjis < 0) return;
    u8 *t = (u8 *)RDP(game, G_PASSIVE_TBL);
    if (!t) return;
    u8 *d = t + AB_MEMORIES * AB_SIZE;
    const char *nm = *(const char **)d;
    if (!nm) return;
    if (!str_eq(nm, g_sjis ? k_memSJ : k_memU8)) {
        done = 1;
        LOGE("memories: ability %d is not メモリーズ (%02x %02x %02x %02x), cost unchanged", AB_MEMORIES,
             (u8)nm[0], (u8)nm[1], (u8)nm[2], (u8)nm[3]);
        return;
    }
    if (d[AB_COST] == 2) { done = 1; return; }
    if (d[AB_COST] == 3 && t[AB_MEMORANDUM * AB_SIZE + AB_COST] == 3) {
        d[AB_COST] = 2;
        done = 1;
        LOGI("memories: cost 3 -> 2");
        return;
    }
    done = 1;
    LOGE("memories: cost field %d / memorandum %d, unchanged", d[AB_COST], t[AB_MEMORANDUM * AB_SIZE + AB_COST]);
}

/* ---------- v4.01: ダッシュの ON/OFF ボタン（フィールド） ----------
   台本ボタンと同じ仕組み（AddScriptButton）で、番号 DASH_BTN_ID のボタンを地図ボタンの左に置く。
   SyncScriptButton が毎フレーム「操作できるか」で見せる／隠すのに加え、歩いている間は隠す。
   押されたら（部品 +0x40==1、+0x44==0x10000|番号）台本へ渡さずに ON/OFF を切り替え、files/fflmod_dash.txt に残す。
   ON の間は B を押していなくても走る（hook_SetAct） */
#define OFF_SYNCSB_FUNC 0x3d63e0UL    /* FieldClass::SyncScriptButton() */
#define OFF_SYNCSB_GOT  0x45f2d0UL
#define DASH_BTN_ID     125
#define DASH_X 228
#define DASH_Y 6
#define DASH_W 96
#define DASH_H 40
#define DASH_SHIFT_OFF  0.0f          /* 文字の位置の補正。v4.01 の引き伸ばしの画面では 5（OFF）・4（ON）左にずれていたが、
                                         1.0-pre7（縦横同じ倍率）ではずれないので 0（R7 の 1.0-pre6 で 4 右に寄っていた） */
#define DASH_SHIFT_ON   0.0f
#define DASH_STILL      6             /* 止まってから出すまでのフレーム */
#define DASH_FILE "/storage/emulated/0/Android/data/com.square_enix.android_googleplay.ffl_gp/files/fflmod_dash.txt"
typedef void (*SyncSBFn)(void *field);
typedef void (*AddSBFn)(void *field, int id, const char *text, int x, int y, int w, int h, int a, int b);
static SyncSBFn g_origSyncSB;
static AddSBFn p_AddSB;
static int g_dashLoaded, g_dashLabel = -1, g_dashStill;
static const char k_dashOnSJ[] = "\x83\x5f\x83\x62\x83\x56\x83\x85 ON", k_dashOffSJ[] = "\x83\x5f\x83\x62\x83\x56\x83\x85 OFF";
static const char k_dashOnU8[] = "ダッシュ ON", k_dashOffU8[] = "ダッシュ OFF";

static void dash_load(void)
{
    g_dashLoaded = 1;
    int fd = open(DASH_FILE, 0);
    if (fd < 0) return;
    char c = 0;
    if (read(fd, &c, 1) == 1) g_dashOn = c == '1';
    close(fd);
    LOGI("dash: loaded %s", g_dashOn ? "on" : "off");
}

static void dash_save(void)
{
    int fd = open(DASH_FILE, 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0660);
    if (fd < 0) { LOGE("dash: cannot save"); return; }
    if (write(fd, g_dashOn ? "1\n" : "0\n", 2) != 2) LOGE("dash: write failed");
    close(fd);
}

/* 押されたか（MenuWidget::Update の直後に呼ぶ）。押されていたら切り替えて 1 */
static int dash_button_pressed(void *w)
{
    if (!g_field || w != RDP(g_field, FB_BASE)) return 0;
    if (RD(int, w, 0x40) != 1 || RD(int, w, 0x44) != (0x10000 | DASH_BTN_ID)) return 0;
    *(volatile int *)((uintptr_t)w + 0x40) = 0;
    *(volatile int *)((uintptr_t)w + 0x44) = 0;
    g_dashOn = !g_dashOn;
    dash_save();
    LOGI("dash: %s", g_dashOn ? "on" : "off");
    return 1;
}

static u8 *dash_item(void *field)
{
    uintptr_t S = (uintptr_t)field + FB_BASE;
    for (int k = 0; k < 8; k++)
        if (RD(int, S, 0x98 + 4 * k) == DASH_BTN_ID) return (u8 *)RDP(S, 0x58 + 8 * k);
    return 0;
}

static void hook_SyncSB(void *field)
{
    g_origSyncSB(field);
    if (!field || !p_AddSB || !g_installed) return;
    uintptr_t S = (uintptr_t)field + FB_BASE;
    if (!RDP(S, 0)) return;
    if (!g_dashLoaded) dash_load();
    if (g_sjis < 0) { void *game = game_inst(); if (game) styx_encoding(game); if (g_sjis < 0) return; }
    u8 *it = dash_item(field);
    if (!it || g_dashLabel != g_dashOn) {
        const char *txt = g_sjis ? (g_dashOn ? k_dashOnSJ : k_dashOffSJ) : (g_dashOn ? k_dashOnU8 : k_dashOffU8);
        /* 最後の引数 1：SyncScriptButton がメニューボタン（field+0xf3d8+0x810）の見え方・透明度・文字色を写す */
        p_AddSB(field, DASH_BTN_ID, txt, DASH_X, DASH_Y, DASH_W, DASH_H, 0, 1);
        it = dash_item(field);
        if (!it) {
            static int told;
            if (!told) { told = 1; LOGE("dash: no free script button slot"); }
            return;
        }
        if (g_dashLabel < 0) LOGI("dash: button added");
        /* 中央寄せ（+0x80 bit1）は文字の幅を測って置くが、半角の英字を広く測るので左へずれる。
           ずれた分（OFF と ON で違う）を文字の位置 +0x84 に足す */
        *(volatile float *)(it + 0x84) = g_dashOn ? DASH_SHIFT_ON : DASH_SHIFT_OFF;
        g_dashLabel = g_dashOn;
        /* 作った直後は SyncScriptButton がまだ見ていないので、次のフレームまで隠す */
        it[0xc] = it[0xd] = 0;
        return;
    }
    /* 歩いている間は隠す（方向を押している、または歩く動作の途中） */
    int moving = (g_cur & K_DIRS) != 0;
    int *ch = g_plChara;
    if (ch) {
        int type = RD(int, ch, 0x78);
        if (type >= 0 && type <= 8 && ((1 << type) & 0x194) && RD(int, ch, 0x44) < RD(int, ch, 0x68)) moving = 1;
    }
    if (moving) g_dashStill = 0;
    else if (g_dashStill < 1000) g_dashStill++;
    if (g_dashStill < DASH_STILL) it[0xc] = it[0xd] = 0;
}

/* ---------- v4.02: 編成の一覧を仲間全員に（20人並び） ----------
   編成画面（メニュー7）の一覧 MenuClass+0xc10 は JobsMenuItemListControl で、InitMenuItemCtrl(0) が
   パーティの8枠ぶんの JobsMenuMemberInfoItem(メンバー, 絵, 0, y, w, h) を作る。
   その後ろに、パーティにいない仲間（メイン8人とゲスト。k_benchOrder の順）の行を足す。
   行の絵は絵の置き場 8〜23（MenuClass +0x748 の絵、+0x808 の番号、テクスチャは置き場+11）に読む。
   一覧の中身の高さ（+0x4c）は行数×行の高さにする（元は最初の空き枠までで、空き枠の後ろが消えていた）。
   タップは JobsMenuItemListControl::TouchUp が SetResultParam(1, 0, 行) で部品 +0x40/+0x48 に入れ、
   スワイプは (1, 0, 一覧+0xd0) と +0x4c=行。8行目より下（控え）はゲームに見せずにここで扱う：
     ・1人目に控えを選ぶ → 選んだ状態（+0x40=1、+0x28=行）。2人目にパーティの枠 → その枠に入れる
     ・1人目にパーティの枠、2人目に控え → 同じく入れ替え。控え同士は選び直し、同じ行は取り消し
     ・控えの行のスワイプ（前列／後列）は無視
   入れ替えは FieldClass::SetPartyMember（v2.20 と同じ）。外れた人は作り直した一覧の控えに並ぶ。
   控えの行は光と闇の8人がそろってゲストが強化されたあと（バルバラがドラゴンソウルを覚えている）だけ。
   編成画面を出たら（InitMenuMode が7以外）元の8行に作り直す */
#define OFF_IMIC_FUNC  0x3f7c84UL     /* MenuClass::InitMenuItemCtrl(int) */
#define OFF_IMIC_GOT   0x460380UL
#define OFF_IMM_FUNC   0x3f6fa8UL     /* MenuClass::InitMenuMode(int, bool, bool) */
#define OFF_IMM_GOT    0x460378UL
#define MENU_FORMATION 7
#define FORM_IMG_BASE  8              /* 控えの絵の置き場の先頭 */
#define FORM_BENCH_MAX 16             /* 置き場 8〜23 */
typedef void (*IMICFn)(void *mc, int kind);
typedef void (*IMMFn)(void *mc, int mode, int a, int b);
typedef void *(*NewFn)(unsigned long n);
typedef void (*DelArrFn)(void *p);
typedef void (*MemberItemCtorFn)(void *obj, u8 *m, void *anm, int x, int y, int w, int h);
typedef void (*SetAnimeIDFn)(void *anm, int id);
typedef void *(*WGetItemFn)(void *w, int i);
typedef void (*SelParamFn)(void *mc, int a);
static IMICFn g_origIMIC;
static IMMFn g_origIMM;
static NewFn p_Znwm, p_Znam;
static DelArrFn p_ZdaPv;
static MemberItemCtorFn p_MemberItemCtor;
static SetAnimeIDFn p_SetAnimeID;
static WGetItemFn p_WGetItem;
static SelParamFn p_SelParam;
static int g_formRows = 8;            /* 今の一覧の行数 */
static signed char g_formBench[FORM_BENCH_MAX];   /* 控えの行のキャラ番号 */
static int g_formBenchN;

static int form_bench_ok(void *game)
{
    return game && ((RD(u8, game, 0x1a180 + 9 * 0x4c4 + 0x5c + 108 / 8) >> (108 % 8)) & 1);
}

static int form_parts_ok(void)
{
    return p_Znwm && p_Znam && p_ZdaPv && p_MemberItemCtor && p_SetAnimeID && p_WGetItem &&
           p_SetPartyMember && p_GetMemberID && p_GetMemberIDList && p_ReadImgAll && p_InitItemCtrl &&
           p_ResetWidget && g_origRpjm && vt_itemlist;
}

/* 一覧の部品（JobsMenuItemListControl）。違えば 0 */
static u8 *form_list(void *mc)
{
    void *w = RDP(mc, MC_FORMWIDGET);
    if (!w || !p_WGetItem) return 0;
    u8 *L = (u8 *)p_WGetItem(w, 0);
    if (!L || (uintptr_t)RDP(L, 0) != vt_itemlist + 0x10) return 0;
    return L;
}

/* v4.04: 控えの並び順。並べ替えたり入れ替えたりしたら files/fflmod_bench.txt に書く */
#define BENCH_FILE "/storage/emulated/0/Android/data/com.square_enix.android_googleplay.ffl_gp/files/fflmod_bench.txt"
static signed char g_benchCustom[24];
static int g_benchCustomN = -1;           /* -1=まだ読んでいない */

static void bench_load(void)
{
    g_benchCustomN = 0;
    int fd = open(BENCH_FILE, 0);
    if (fd < 0) return;
    char buf[128];
    long r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r <= 0) return;
    buf[r] = 0;
    int v = -1;
    for (char *q = buf; ; q++) {
        if (*q >= '0' && *q <= '9') { v = (v < 0 ? 0 : v * 10) + (*q - '0'); if (v > 99) v = 99; continue; }
        if (v >= 0 && v < 24 && g_benchCustomN < 24) g_benchCustom[g_benchCustomN++] = (signed char)v;
        v = -1;
        if (!*q) break;
    }
    LOGI("form: bench order loaded (%d)", g_benchCustomN);
}

static void bench_save(void)
{
    char buf[128];
    int p = 0;
    for (int i = 0; i < g_benchCustomN && p < 120; i++) {
        int v = g_benchCustom[i];
        if (v >= 10) buf[p++] = (char)('0' + v / 10);
        buf[p++] = (char)('0' + v % 10);
        buf[p++] = ' ';
    }
    buf[p++] = '\n';
    int fd = open(BENCH_FILE, 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0660);
    if (fd < 0) { LOGE("form: cannot save bench order"); return; }
    if (write(fd, buf, p) != p) LOGE("form: bench order write failed");
    close(fd);
}

static int bench_ok(void *game, int id)
{
    if (id < 0 || id >= 24 || bench_in_party(game, id, -1)) return 0;
    if (id == 21 && !styx_ready(game)) return 0;
    for (unsigned i = 0; i < sizeof(k_benchOrder); i++) if (k_benchOrder[i] == id) return 1;
    return 0;
}

/* g_formBench を作り、その順を g_benchCustom に残す（変わったら保存） */
static void bench_build(void *game)
{
    if (g_benchCustomN < 0) bench_load();
    g_formBenchN = 0;
    for (int i = 0; i < g_benchCustomN && g_formBenchN < FORM_BENCH_MAX; i++) {
        int id = g_benchCustom[i], dup = 0;
        for (int k = 0; k < g_formBenchN; k++) if (g_formBench[k] == id) dup = 1;
        if (!dup && bench_ok(game, id)) g_formBench[g_formBenchN++] = (signed char)id;
    }
    for (unsigned i = 0; i < sizeof(k_benchOrder) && g_formBenchN < FORM_BENCH_MAX; i++) {
        int id = k_benchOrder[i], dup = 0;
        for (int k = 0; k < g_formBenchN; k++) if (g_formBench[k] == id) dup = 1;
        if (!dup && bench_ok(game, id)) g_formBench[g_formBenchN++] = (signed char)id;
    }
    int same = g_formBenchN == g_benchCustomN;
    for (int i = 0; i < g_formBenchN && same; i++) if (g_formBench[i] != g_benchCustom[i]) same = 0;
    if (!same) {
        for (int i = 0; i < g_formBenchN; i++) g_benchCustom[i] = g_formBench[i];
        g_benchCustomN = g_formBenchN;
        bench_save();
    }
}

static void form_add_bench(void *mc, void *game)
{
    g_formBenchN = 0;
    u8 *L = form_list(mc);
    if (!L) { LOGE("form: list not found"); return; }
    int n0 = RD(int, L, 0xac);
    void **old = (void **)RDP(L, 0xb0);
    if (n0 != 8 || !old || !old[0]) { LOGE("form: list has %d rows, not extended", n0); return; }
    float x = RD(float, old[0], 0x28), w = RD(float, old[0], 0x30), h = RD(float, old[0], 0x34);
    if (h < 1.0f) return;
    /* 控え：パーティにいない人。v4.04: 並べ替えた順（files/fflmod_bench.txt）を先に、残りを決まった順で */
    bench_build(game);
    int N = 8 + g_formBenchN;
    void **arr = (void **)p_Znam((unsigned long)N * 8);
    if (!arr) return;
    for (int i = 0; i < 8; i++) arr[i] = old[i];
    int flag = RD(int, mc, MC_FORMFLAG) == 1;
    for (int k = 0; k < g_formBenchN; k++) {
        u8 *m = (u8 *)((uintptr_t)game + G_MEMBERS + (uintptr_t)g_formBench[k] * 0x4c4);
        int slot = FORM_IMG_BASE + k;
        void *anm = RDP(mc, 0x748 + slot * 8);
        if (anm) {
            int hd = (int)hook_Rpjm(game, m, anm, slot + 11, RD(int, mc, 0x808 + slot * 4), 1, flag, m[0x120] & 0x80, 1);
            p_SetAnimeID(anm, 0);
            *(volatile int *)((uintptr_t)mc + 0x808 + slot * 4) = hd;
        }
        void *it = p_Znwm(0xc0);
        if (!it) { arr[8 + k] = 0; continue; }
        p_MemberItemCtor(it, m, anm, (int)x, (int)((8 + k) * h), (int)w, (int)h);
        ((u8 *)it)[0xbb] = 0;   /* v4.03: 6行目より下と同じ暗い色（InitMenuItemCtrlSelectParam は8行目までしか消さない） */
        arr[8 + k] = it;
    }
    *(volatile int *)(L + 0xac) = N;
    *(void *volatile *)(L + 0xb0) = arr;
    p_ZdaPv(old);
    *(volatile float *)(L + 0x4c) = (float)N * h;   /* 中身の高さ：全部の行 */
    g_formRows = N;
    static int lastN = -1;
    if (N != lastN) { lastN = N; LOGI("form: %d rows (bench %d)", N, g_formBenchN); }
}

static void hook_IMIC(void *mc, int kind)
{
    g_origIMIC(mc, kind);
    if (kind != 0) return;
    g_formRows = 8;
    g_formBenchN = 0;
    if (RD(int, mc, MC_MENU) != MENU_FORMATION || !form_parts_ok()) return;
    void *game = game_inst();
    if (!form_bench_ok(game)) return;
    form_add_bench(mc, game);
}

/* スクロールを y に（範囲の中に収めて） */
static void form_set_scroll(u8 *L, float y)
{
    if (!p_ScrollMin || !p_ILSetScroll || !p_ILSync) return;
    Vec2f mn = p_ScrollMin(L);
    if (y < mn.y) y = mn.y;
    if (y > 0.0f) y = 0.0f;
    Vec2f v = { RD(float, L, 0x50), y };
    p_ILSetScroll(L, &v);
    p_ILSync(L);
}

static void form_truncate(void *mc)
{
    g_formRows = 8;
    g_formBenchN = 0;
    u8 *L = form_list(mc);
    if (!L) return;
    int n = RD(int, L, 0xac);
    void **arr = (void **)RDP(L, 0xb0);
    if (n <= 8 || !arr || !arr[0]) return;
    for (int i = 8; i < n; i++) {
        void *o = arr[i];
        arr[i] = 0;
        if (o) { void (**vt)(void *) = *(void (***)(void *))o; vt[1](o); }   /* 消す（Create と同じく vtable +8） */
    }
    *(volatile int *)(L + 0xac) = 8;
    *(volatile float *)(L + 0x4c) = 8.0f * RD(float, arr[0], 0x34);
    form_set_scroll(L, RD(float, L, 0x54));
    LOGI("form: back to 8 rows");
}

static void hook_IMM(void *mc, int mode, int a, int b)
{
    g_origIMM(mc, mode, a, b);
    if (!g_origIMIC || !form_parts_ok()) return;
    if (mode == MENU_FORMATION) {
        if (!form_bench_ok(game_inst())) return;
        hook_IMIC(mc, 0);                      /* 入ったとき：控えの行を足した一覧に作り直す（p_InitItemCtrl は差し込みを通らない） */
        if (g_origRW) hook_RW(mc, 1, 1); else p_ResetWidget(mc, 1, 1);
    } else if (g_formRows > 8) {
        form_truncate(mc);                     /* 出たとき：足した行だけ消して8行に戻す（作り直すとメインメニューの配置になる） */
    }
}

/* 一覧を作り直し、前のスクロールに戻して、row の行が見えるようにして汎用カーソルを移す（v4.03） */
static void form_rebuild(void *mc, void *game, int row)
{
    *(volatile int *)((uintptr_t)mc + MC_FORMSEL) = 0;
    *(volatile int *)((uintptr_t)mc + MC_FORMPICK) = -1;
    if (row < 8) *(volatile int *)((uintptr_t)mc + MC_FORMCUR) = row;
    u8 *L0 = form_list(mc);
    float sy = L0 ? RD(float, L0, 0x54) : 0.0f;
    p_ReadImgAll(mc, p_GetMemberIDList(game), 8, RD(int, mc, MC_FORMFLAG) == 1);
    hook_IMIC(mc, 0);
    if (g_origRW) hook_RW(mc, 1, 1); else p_ResetWidget(mc, 1, 1);
    if (p_SelParam) p_SelParam(mc, 0);
    u8 *L = form_list(mc);
    void **rows = L ? (void **)RDP(L, 0xb0) : 0;
    if (L && rows && rows[0] && row >= 0 && row < RD(int, L, 0xac)) {
        float h = RD(float, rows[0], 0x34), vh = RD(float, L, 0x34);
        float top = row * h;
        if (top + sy < 0.0f) sy = -top;
        if (top + h + sy > vh) sy = vh - top - h;
        form_set_scroll(L, sy);
        g_pendItem = L; g_pendSub = row; g_pendWait = 10;
    }
}

/* 入れ替え：パーティの枠 slot に控えの k 番目を入れる。外れた人はその控えの場所に入る（v4.04）。カーソルは curRow へ */
static void form_swap(void *mc, void *game, int slot, int k, int curRow)
{
    if (slot < 0 || slot > 7 || k < 0 || k >= g_formBenchN) return;
    int id = g_formBench[k];
    void *field = (void *)((uintptr_t)game + G_FIELD);
    int before = p_GetMemberID(game, slot);
    p_SetPartyMember(field, slot, -1, 0);
    u8 *m = (u8 *)((uintptr_t)game + G_MEMBERS + (uintptr_t)id * 0x4c4);
    p_SetPartyMember(field, slot, id, RD(int, m, MS_ROW));
    LOGI("form: slot %d %d -> %d", slot, before, p_GetMemberID(game, slot));
    /* 控えの並び：入った人の場所を外れた人に（空き枠からなら詰める） */
    for (int i = 0; i < g_benchCustomN; i++) {
        if (g_benchCustom[i] != id) continue;
        if (before >= 0) g_benchCustom[i] = (signed char)before;
        else { for (int j = i; j + 1 < g_benchCustomN; j++) g_benchCustom[j] = g_benchCustom[j + 1]; g_benchCustomN--; }
        break;
    }
    bench_save();
    form_rebuild(mc, game, curRow);
}

/* v4.04: 控え同士の並べ替え（行 r1 と r2、どちらも 8 以上） */
static void form_bench_swap(void *mc, void *game, int r1, int r2)
{
    int a = r1 - 8, b = r2 - 8;
    if (a < 0 || b < 0 || a >= g_formBenchN || b >= g_formBenchN || a == b) return;
    int ia = -1, ib = -1;
    for (int i = 0; i < g_benchCustomN; i++) {
        if (g_benchCustom[i] == g_formBench[a]) ia = i;
        if (g_benchCustom[i] == g_formBench[b]) ib = i;
    }
    if (ia < 0 || ib < 0) return;
    signed char t = g_benchCustom[ia]; g_benchCustom[ia] = g_benchCustom[ib]; g_benchCustom[ib] = t;
    LOGI("form: bench %d <-> %d", g_benchCustom[ib], g_benchCustom[ia]);
    bench_save();
    form_rebuild(mc, game, r2);
}

static int form_tap(void *mc)
{
    if (g_formRows <= 8 || RD(int, mc, MC_MENU) != MENU_FORMATION) return 0;
    void *w = RDP(mc, MC_FORMWIDGET);
    u8 *L = form_list(mc);
    if (!w || !L || RD(int, w, 0x40) != 1) return 0;
    int a = RD(int, w, 0x48);
    int swipe = a == RD(int, L, 0xd0);
    int row = swipe ? RD(int, w, 0x4c) : a;
    int mode = RD(int, mc, MC_FORMSEL), pick = RD(int, mc, MC_FORMPICK);
    void *game = game_inst();
    if (!game) return 0;
    if (swipe) {
        if (row < 8) return 0;
        *(volatile int *)((uintptr_t)w + 0x40) = 0;     /* 控えの前列／後列は変えない */
        return 1;
    }
    if (row < 0 || row >= g_formRows) return 0;
    if (row < 8 && !(mode == 1 && pick >= 8)) return 0;   /* パーティの中だけ：ゲームに任せる */
    *(volatile int *)((uintptr_t)w + 0x40) = 0;
    if (p_ReserveSE) p_ReserveSE((void *)((uintptr_t)game + 0xb0), 1);
    if (row < 8) { form_swap(mc, game, row, pick - 8, row); return 1; }        /* 控え → パーティの枠 */
    if (mode == 1 && pick >= 0 && pick < 8) { form_swap(mc, game, pick, row - 8, pick); return 1; }  /* 枠 → 控え */
    if (mode == 1 && pick >= 8 && pick != row) { form_bench_swap(mc, game, pick, row); return 1; }  /* v4.04: 控え同士は並べ替え */
    if (mode == 1 && pick == row) {                                     /* 同じ控え：取り消し */
        *(volatile int *)((uintptr_t)mc + MC_FORMSEL) = 0;
        *(volatile int *)((uintptr_t)mc + MC_FORMPICK) = -1;
    } else {                                                            /* 控えを選ぶ（選び直し） */
        *(volatile int *)((uintptr_t)mc + MC_FORMSEL) = 1;
        *(volatile int *)((uintptr_t)mc + MC_FORMPICK) = row;
    }
    if (g_origRW) hook_RW(mc, 1, 1); else p_ResetWidget(mc, 1, 1);
    return 1;
}

/* 控えの行の表示の印（ゲームは8行目までしか書かない）：+0xb8 = +0xf、+0xb9 = 選んでいる */
static void form_after(void *mc)
{
    if (g_formRows <= 8 || RD(int, mc, MC_MENU) != MENU_FORMATION) return;
    u8 *L = form_list(mc);
    if (!L) return;
    int n = RD(int, L, 0xac), mode = RD(int, mc, MC_FORMSEL), pick = RD(int, mc, MC_FORMPICK);
    u8 **rows = (u8 **)RDP(L, 0xb0);
    if (!rows) return;
    for (int i = 8; i < n; i++) {
        u8 *r = rows[i];
        if (!r) continue;
        r[0xb8] = r[0xf];
        r[0xb9] = (u8)(mode == 1 && pick == i);
        r[0xbb] = 0;
    }
}

/* ---------- 差し込み ---------- */

static int patch_slot(uintptr_t slot, uintptr_t value)
{
    uintptr_t page = slot & ~0xfffUL;
    if (mprotect((void *)page, 0x1000, PROT_READ | PROT_WRITE) != 0) {
        LOGE("mprotect RW failed");
        return 0;
    }
    *(volatile uintptr_t *)slot = value;
    mprotect((void *)page, 0x1000, PROT_READ);
    return 1;
}

/* セーブファイル（save.bin。通常セーブ枠と中断セーブが入っている）を、
   adb で取り出せる場所 /sdcard/Android/data/<パッケージ>/files/save_export.bin へ写す。
   起動時と、アプリが裏に回ったとき（onPause）に行う。ゲームのセーブ自体には触らない */
#define PKG "com.square_enix.android_googleplay.ffl_gp"
static u8 g_copyBuf[65536];
/* src を dst へ写す（一時ファイルに書いてから名前を変える）。写したバイト数、失敗なら -1 */
static long copy_file(const char *src, const char *dst, const char *tmp)
{
    int in = open(src, 0 /* O_RDONLY */);
    if (in < 0) return -1;
    int out = open(tmp, 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0660);
    if (out < 0) { close(in); return -1; }
    long total = 0, r;
    int ok = 1;
    while ((r = read(in, g_copyBuf, sizeof(g_copyBuf))) > 0) {
        if (write(out, g_copyBuf, (size_t)r) != r) { ok = 0; break; }
        total += r;
    }
    if (r < 0) ok = 0;
    close(in);
    close(out);
    if (!ok || rename(tmp, dst) != 0) return -1;
    return total;
}

static long file_size(const char *path)
{
    int fd = open(path, 0);
    if (fd < 0) return -1;
    long total = 0, r;
    while ((r = read(fd, g_copyBuf, sizeof(g_copyBuf))) > 0) total += r;
    close(fd);
    return r < 0 ? -1 : total;
}

/* /sdcard/Android/data/<パッケージ>/files/save_import.bin があれば save.bin として取り込む。
   取り込む前の save.bin は save_before_import.bin として残す。
   取り込んだら読み込み元を save_import.done に改名し、次の起動で二重に取り込まない。
   大きさが 260000 バイト（このゲームのセーブの大きさ）でなければ取り込まない */
#define EXT_DIR "/storage/emulated/0/Android/data/" PKG "/files/"
#define INT_DIR "/data/data/" PKG "/files/"
#define SAVE_SIZE 260000
static void import_save(void)
{
    const char *imp = EXT_DIR "save_import.bin";
    long sz = file_size(imp);
    if (sz < 0) return;
    if (sz != SAVE_SIZE) { LOGE("import: size %ld != %d, skipped", sz, SAVE_SIZE); return; }
    if (file_size(INT_DIR "save.bin") >= 0) {
        if (copy_file(INT_DIR "save.bin", INT_DIR "save_before_import.bin", INT_DIR "save_before_import.tmp") < 0) {
            LOGE("import: cannot back up current save.bin, skipped");
            return;
        }
    }
    /* 1.0-pre17: 入れてから一度も起動していないと files フォルダがまだない（ゲームが最初に使うときに作る）ので作っておく */
    mkdir(INT_DIR, 0771);
    long n = copy_file(imp, INT_DIR "save.bin", INT_DIR "save_import.tmp");
    if (n != SAVE_SIZE) { LOGE("import: copy failed (%ld)", n); return; }
    if (rename(imp, EXT_DIR "save_import.done") != 0) LOGE("import: cannot rename import file");
    LOGI("import: save_import.bin -> save.bin (%ld bytes), old save kept as save_before_import.bin", n);
}

static void export_save(void)
{
    static const char *srcs[] = { "/data/user/0/" PKG "/files/save.bin", "/data/data/" PKG "/files/save.bin" };
    int in = -1;
    for (unsigned i = 0; i < 2 && in < 0; i++) in = open(srcs[i], 0 /* O_RDONLY */);
    if (in < 0) { LOGE("export: save.bin not found"); return; }
    mkdir("/storage/emulated/0/Android/data/" PKG, 0771);
    mkdir("/storage/emulated/0/Android/data/" PKG "/files", 0771);
    const char *tmp = "/storage/emulated/0/Android/data/" PKG "/files/save_export.tmp";
    const char *dst = "/storage/emulated/0/Android/data/" PKG "/files/save_export.bin";
    int out = open(tmp, 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0660);
    if (out < 0) { LOGE("export: cannot create %s", tmp); close(in); return; }
    long total = 0, r;
    int ok = 1;
    while ((r = read(in, g_copyBuf, sizeof(g_copyBuf))) > 0) {
        if (write(out, g_copyBuf, (size_t)r) != r) { ok = 0; break; }
        total += r;
    }
    if (r < 0) ok = 0;
    close(in);
    close(out);
    if (ok && rename(tmp, dst) == 0) LOGI("export: save.bin %ld bytes -> %s", total, dst);
    else LOGE("export: failed");
}

/* v3.50: 召喚獣を袋（お金・アイテム・魔法の組 0〜2、GameClass+0x21730 が今の番号）どうしで共有する。
   覚えた魔法の印は GameClass+0x21494 + 袋*32 のビット（LearnMagicGlobal 0x3ea924）。召喚獣は魔法 49〜62
   （シルフ〜バハムート）＝6バイト目の bit1〜7 と7バイト目の bit0〜6。暁の章（章0）以外で、3つの袋の印を足し合わせて全部に書く。
   消す命令（ForgetMagicGlobal）はシナリオで召喚獣に使われていない。終章の MargePartyData も袋0〜2 を OR するので結果は同じ */
static void summon_sync(void)
{
    u8 *g = (u8 *)game_inst();
    if (!g || RD(int, g, G_CHAPTER) == 0) return;
    volatile u8 *t = (volatile u8 *)(g + 0x21494);
    u8 a = 0, b = 0;
    for (int k = 0; k < 3; k++) { a |= t[k * 32 + 6] & 0xfe; b |= t[k * 32 + 7] & 0x7f; }
    for (int k = 0; k < 3; k++) { t[k * 32 + 6] |= a; t[k * 32 + 7] |= b; }
}

/* 手元だけの追加（src/local.c、リポジトリには含めない）。無ければ何もしない */
#ifdef FFLMOD_LOCAL
#include "local.c"
#else
static void local_field_poll(void) {}
static void local_event_patch(unsigned char *h, int cnt) { (void)h; (void)cnt; }
#endif

void Java_com_square_1enix_android_1googleplay_ffl_1gp_FFLMod_install(void *env, void *cls)
{
    (void)env; (void)cls;
    if (g_installed)
        return;
    if (g_logFd < 0) {
        mkdir("/storage/emulated/0/Android/data/" PKG, 0771);
        mkdir("/storage/emulated/0/Android/data/" PKG "/files", 0771);
        g_logFd = open("/storage/emulated/0/Android/data/" PKG "/files/fflmod_log.txt", 0x241 /* O_WRONLY|O_CREAT|O_TRUNC */, 0660);
    }
    /* v4.05: padhook から fflmod へ名前を変えた。古い名前の設定ファイルがあれば引き継ぐ */
    {
        static const char *const ren[][2] = {
            { EXT_DIR "padhook_dash.txt",  EXT_DIR "fflmod_dash.txt" },
            { EXT_DIR "padhook_bench.txt", EXT_DIR "fflmod_bench.txt" },
        };
        for (int k = 0; k < 2; k++) {
            int fd = open(ren[k][1], 0);
            if (fd >= 0) { close(fd); continue; }
            if (rename(ren[k][0], ren[k][1]) == 0) LOGI("renamed %s -> %s", ren[k][0], ren[k][1]);
        }
    }
    void *h = dlopen("libjniproxy.so", RTLD_NOW);
    if (!h) { LOGE("dlopen libjniproxy.so failed"); return; }
    void *fn = dlsym(h, SYM_KEYSTATE);
    if (!fn) { LOGE("dlsym KeyState failed"); return; }
    uintptr_t base = (uintptr_t)fn - OFF_KEYSTATE_FUNC;
    if (base & 0xfff) { LOGE("unexpected base %lx (version mismatch?)", base); return; }
    g_base = base;

    /* 呼び出すだけの関数は、名前でも引いて位置が合っているか確かめる */
    struct { const char *sym; uintptr_t off; void **dst; } fns[] = {
        { "_Z12GetTouchInfov",             OFF_GETTOUCHINFO, (void **)&p_GetTouchInfo },
        { "_ZN13MtxTouchPanel7SetDataEiffi", OFF_TP_SETDATA, (void **)&p_SetData },
        { "_ZN13MtxGraphics3D8FillRectEiiii", OFF_FILLRECT,  (void **)&p_FillRect },
    };
    {
        void *v1 = dlsym(h, "_ZTV19JobsMenuListControl");
        void *v2 = dlsym(h, "_ZTV23JobsMenuItemListControl");
        vt_list = (uintptr_t)v1;
        vt_itemlist = (uintptr_t)v2;
        vt_slidepad = (uintptr_t)dlsym(h, "_ZTV16JobsMenuSlidePad");
    }
    p_GetMember   = (GetMemberFn)dlsym(h, "_ZN9GameClass18GetPartyMemberDataEi");
    p_ResetWidget = (ResetWidgetFn)dlsym(h, "_ZN9MenuClass11ResetWidgetEbb");
    p_ReserveSE   = (ReserveSEFn)dlsym(h, "_ZN14SoundCtrlClass9ReserveSEEi");
    p_DrawString  = (DrawStringFn)dlsym(h, "_ZN9GameClass10DrawStringEPKciii");
    p_SetColor    = (SetColorFn)dlsym(h, "_ZN9GameClass8SetColorEi");
    p_SetPartyMember  = (SetPartyMemberFn)dlsym(h, "_ZN10FieldClass14SetPartyMemberEiii");
    p_GetMemberID     = (GetMemberIDFn)dlsym(h, "_ZN9GameClass16GetPartyMemberIDEi");
    p_GetMemberIDList = (GetMemberIDListFn)dlsym(h, "_ZN9GameClass20GetPartyMemberIDListEv");
    p_ReadImgAll      = (ReadImgAllFn)dlsym(h, "_ZN9MenuClass17ReadMenuImage_AllEPiib");
    p_InitItemCtrl    = (InitItemCtrlFn)dlsym(h, "_ZN9MenuClass16InitMenuItemCtrlEi");
    p_InitSlot      = (InitSlotFn)dlsym(h, "_ZN9GameClass21InitMemberAbilitySlotER13MEMBER_STATUSb");
    p_SetJobStatus  = (SetJobStatusFn)dlsym(h, "_ZN9GameClass12SetJobStatusER13MEMBER_STATUSb");
    p_AddPassive = (AddPassiveFn)dlsym(h, "_ZN9GameClass17AddPassiveAbilityER13MEMBER_STATUSi");
    p_GetMgc     = (GetMgcFn)dlsym(h, "_ZN9GameClass10GetMgcDataEi");
    p_IsLearnJob = (IsLearnJobFn)dlsym(h, "_ZN9GameClass10IsLearnJobEi");
    p_DrawIcon   = (DrawIconFn)dlsym(h, "_ZN9GameClass8DrawIconEP13MtxGraphics3Diffiiii");
    p_Znwm = (NewFn)dlsym(h, "_Znwm");
    p_Znam = (NewFn)dlsym(h, "_Znam");
    p_ZdaPv = (DelArrFn)dlsym(h, "_ZdaPv");
    p_MemberItemCtor = (MemberItemCtorFn)dlsym(h, "_ZN22JobsMenuMemberInfoItemC1EP13MEMBER_STATUSP10MtxAnmCtrliiii");
    p_SetAnimeID = (SetAnimeIDFn)dlsym(h, "_ZN10MtxAnmCtrl10SetAnimeIDEi");
    p_WGetItem = (WGetItemFn)dlsym(h, "_ZN10MenuWidget7GetItemEi");
    p_SelParam = (SelParamFn)dlsym(h, "_ZN9MenuClass27InitMenuItemCtrlSelectParamEi");
    if (!(p_Znwm && p_Znam && p_ZdaPv && p_MemberItemCtor && p_SetAnimeID && p_WGetItem))
        LOGE("form: missing parts %d%d%d%d%d%d", !!p_Znwm, !!p_Znam, !!p_ZdaPv, !!p_MemberItemCtor, !!p_SetAnimeID, !!p_WGetItem);
    p_AddSB      = (AddSBFn)dlsym(h, "_ZN10FieldClass15AddScriptButtonEiPKciiiibb");
    p_Malloc = (MallocFn)dlsym(h, "_Z7_MALLOCi");
    p_Free   = (FreeFn)dlsym(h, "_Z5_FREEPv");
    p_SetLV  = (SetLVFn)dlsym(h, "_ZN9GameClass5SetLVEii");
    LOGI("styx: malloc %d free %d setlv %d", p_Malloc != 0, p_Free != 0, p_SetLV != 0);
    p_ScrollMin   = (ScrollMinFn)dlsym(h, "_ZN8MenuItem13CalcScrollMinEv");
    p_ILSetScroll = (ILSetScrollFn)dlsym(h, "_ZN23JobsMenuItemListControl17SetScrollPositionER7vector2");
    p_ILSync      = (ILSyncFn)dlsym(h, "_ZN23JobsMenuItemListControl10SyncScrollEv");
    p_GPush  = (GfxFn)dlsym(h, "_ZN13MtxGraphics3D10PushMatrixEv");
    p_GPop   = (GfxFn)dlsym(h, "_ZN13MtxGraphics3D9PopMatrixEv");
    p_GTrans = (GTransFn)dlsym(h, "_ZN13MtxGraphics3D9TranslateEfff");
    p_GScale = (GScaleFn)dlsym(h, "_ZN13MtxGraphics3D5ScaleEffff");
    p_SetViewItem = (SetViewItemFn)dlsym(h, "_ZN19JobsMenuListControl19SetViewEnableItemIDEi");
    p_IsCtrlChg   = (IsCtrlChgFn)dlsym(h, "_ZN10FieldClass22IsControlMemberChangedEi");
    for (unsigned i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
        void *p = dlsym(h, fns[i].sym);
        if (p == (void *)(base + fns[i].off)) *fns[i].dst = p;
        else LOGE("%s: %p != expected %lx (cursor disabled)", fns[i].sym, p, base + fns[i].off);
    }

    struct { uintptr_t got, func; uintptr_t hook; void **orig; const char *name; int required; } t[] = {
        { OFF_APPRUN_GOT,   OFF_APPRUN_FUNC,   (uintptr_t)hook_AppRun,     (void **)&g_origAppRun,  "App::run", 1 },
        { OFF_KEYSTATE_GOT, OFF_KEYSTATE_FUNC, (uintptr_t)hook_KeyState,   (void **)&g_origState,   "KeyState", 1 },
        { OFF_TRIGGER_GOT,  OFF_TRIGGER_FUNC,  (uintptr_t)hook_KeyTrigger, (void **)&g_origTrigger, "KeyTrigger", 1 },
        { OFF_REPEAT_GOT,   OFF_REPEAT_FUNC,   (uintptr_t)hook_KeyRepeat,  (void **)&g_origRepeat,  "KeyRepeat", 1 },
        { OFF_WUPDATE_GOT,  OFF_WUPDATE_FUNC,  (uintptr_t)hook_WUpdate,    (void **)&g_origWUpdate, "MenuWidget::Update", 0 },
        { OFF_WDRAW_GOT,    OFF_WDRAW_FUNC,    (uintptr_t)hook_WDraw,      (void **)&g_origWDraw,   "MenuWidget::Draw", 0 },
        { OFF_MOVEW_GOT,    OFF_MOVEW_FUNC,    (uintptr_t)hook_MoveW,      (void **)&g_origMoveW,   "MenuClass::MoveWidget", 0 },
        { OFF_DRAWW_GOT,    OFF_DRAWW_FUNC,    (uintptr_t)hook_DrawW,      (void **)&g_origDrawW,   "MenuClass::DrawWidget", 0 },
        { OFF_FOGEN_GOT,    OFF_FOGEN_FUNC,    (uintptr_t)hook_FogEn,      (void **)&g_origFogEn,   "SetFogColor", 0 },
        { OFF_DRAWPRIM_GOT, OFF_DRAWPRIM_FUNC, (uintptr_t)hook_DrawPrim,   (void **)&g_origDrawPrim, "DrawPrimitive", 0 },
        { OFF_VTX_GOT,      OFF_VTX_FUNC,      (uintptr_t)hook_Vtx,        (void **)&g_origVtx,     "Vertex3f", 0 },
        { OFF_CJL_GOT,      OFF_CJL_FUNC,      (uintptr_t)hook_CJL,        (void **)&g_origCJL,     "CreateJobList", 0 },
        { OFF_JOBF_GOT,     OFF_JOBF_FUNC,     (uintptr_t)hook_JobF,       (void **)&g_origJobF,    "JobFunc", 0 },
        { OFF_DJOB_GOT,     OFF_DJOB_FUNC,     (uintptr_t)hook_DJob,       (void **)&g_origDJob,    "DrawJob", 0 },
        { OFF_LCI_GOT,      OFF_LCI_FUNC,      (uintptr_t)hook_LCI,        (void **)&g_origLCI,     "LoadCharaImage", 0 },
        { OFF_LICP_GOT,     OFF_LICP_FUNC,     (uintptr_t)hook_LICP,       (void **)&g_origLICP,    "LoadICPDataBuffer", 0 },
        { OFF_RW_GOT,       OFF_RW_FUNC,       (uintptr_t)hook_RW,         (void **)&g_origRW,      "ResetWidget", 0 },
        { OFF_FORM_GOT,     OFF_FORM_FUNC,     (uintptr_t)hook_Form,       (void **)&g_origForm,    "FormationFunc", 0 },
        { OFF_ADDJP_GOT,    OFF_ADDJP_FUNC,    (uintptr_t)hook_AddJp,      (void **)&g_origAddJp,   "AddJp", 0 },
        { OFF_SETCTRL_GOT,  OFF_SETCTRL_FUNC,  (uintptr_t)hook_SetCtrl,    (void **)&g_origSetCtrl, "SetControlMember", 0 },
        { OFF_MOVEPL_GOT,   OFF_MOVEPL_FUNC,   (uintptr_t)hook_MovePl,     (void **)&g_origMovePl,  "MovePlayer", 0 },
        { OFF_BTLMENU_GOT,  OFF_BTLMENU_FUNC,  (uintptr_t)hook_BtlMenu,    (void **)&g_origBtlMenu, "BattleMenuExec", 0 },
        { OFF_VIEWPORT_GOT, OFF_VIEWPORT_FUNC, (uintptr_t)hook_ViewPort,   (void **)&g_origViewPort, "SetViewPort", 0 },
        { OFF_MOVECTRL_GOT, OFF_MOVECTRL_FUNC, (uintptr_t)hook_MoveCtrl,   (void **)&g_origMoveCtrl, "MoveCharaControl", 0 },
        { OFF_SETACT_GOT,   OFF_SETACT_FUNC,   (uintptr_t)hook_SetAct,     (void **)&g_origSetAct,  "SetCharaAction", 0 },
        { OFF_LOADEV_GOT,   OFF_LOADEV_FUNC,   (uintptr_t)hook_LoadEv,     (void **)&g_origLoadEv,  "LoadEventData", 0 },
        { OFF_LOADMI_GOT,   OFF_LOADMI_FUNC,   (uintptr_t)hook_LoadMI,     (void **)&g_origLoadMI,  "LoadMapInfo", 0 },
        { OFF_LOADMAP_GOT,  OFF_LOADMAP_FUNC,  (uintptr_t)hook_LoadMap,    (void **)&g_origLoadMap, "LoadMap", 0 },
        { OFF_CMP_GOT,      OFF_CMP_FUNC,      (uintptr_t)hook_CMP,        (void **)&g_origCMP,     "CheckMovePass", 0 },
        { OFF_IEDC_GOT,     OFF_IEDC_FUNC,     (uintptr_t)hook_IEDC,       (void **)&g_origIEDC,    "InitEventDataOfChara", 0 },
        { OFF_GSPX_GOT,     OFF_GSPX_FUNC,     (uintptr_t)hook_GSPX,       (void **)&g_origGSPX,    "GetScrollPosX", 0 },
        { OFF_LFORM_GOT,    OFF_LFORM_FUNC,    (uintptr_t)hook_LForm,      (void **)&g_origLForm,   "LoadFormation", 0 },
        { OFF_SETML_GOT,    OFF_SETML_FUNC,    (uintptr_t)hook_SetML,      (void **)&g_origSetML,   "SetMemberLevel", 0 },
        { OFF_LEVELUP_GOT,  OFF_LEVELUP_FUNC,  (uintptr_t)hook_LevelUp,    (void **)&g_origLevelUp, "LevelUp", 0 },
        { OFF_ENDBTL_GOT,   OFF_ENDBTL_FUNC,   (uintptr_t)hook_EndBtl,     (void **)&g_origEndBtl,  "EndBattle", 0 },
        { OFF_BTLWIN_GOT,   OFF_BTLWIN_FUNC,   (uintptr_t)hook_BtlWin,     (void **)&g_origBtlWin,  "BattleScaduleWin", 0 },
        { OFF_SETBEP_GOT,   OFF_SETBEP_FUNC,   (uintptr_t)hook_SetBEP,     (void **)&g_origSetBEP,  "SetBtlEnemyParam", 0 },
        { OFF_ADDDROP_GOT,  OFF_ADDDROP_FUNC,  (uintptr_t)hook_AddDrop,    (void **)&g_origAddDrop, "AddEnemyDropData", 0 },
        { OFF_EFCVIEW_GOT,  OFF_EFCVIEW_FUNC,  (uintptr_t)hook_EfcView,    (void **)&g_origEfcView, "MtxEfcData::SetViewParam", 0 },
        { OFF_EFCDRAW_GOT,  OFF_EFCDRAW_FUNC,  (uintptr_t)hook_EfcDraw,    (void **)&g_origEfcDraw, "MtxEfcData::Draw", 0 },
        { OFF_RRIFI_GOT,    OFF_RRIFI_FUNC,    (uintptr_t)hook_Rrifi,      (void **)&g_origRrifi,   "ReadResourceImageForICP", 0 },
        { OFF_ADDEFC_GOT,   OFF_ADDEFC_FUNC,   (uintptr_t)hook_AddEfc,     (void **)&g_origAddEfc,  "AddEffect10", 0 },
        { OFF_SCENEMAIN_GOT, OFF_SCENEMAIN_FUNC, (uintptr_t)hook_SceneMain, (void **)&g_origSceneMain, "SceneMain", 0 },
        { OFF_RPJM_GOT,     OFF_RPJM_FUNC,     (uintptr_t)hook_Rpjm,       (void **)&g_origRpjm,    "ReadPlayerJobModel", 0 },
        { OFF_BUFREL_GOT,   OFF_BUFREL_FUNC,   (uintptr_t)hook_BufRel,     (void **)&g_origBufRel,  "BufferControl::Release", 0 },
        { OFF_BUFLOAD_GOT,  OFF_BUFLOAD_FUNC,  (uintptr_t)hook_BufLoad,    (void **)&g_origBufLoad, "BufferControl::Load", 0 },
        { OFF_SJS_GOT,      OFF_SJS_FUNC,      (uintptr_t)hook_SJS,        (void **)&g_origSJS,     "SetJobStatus", 0 },
        { OFF_SMPA_GOT,     OFF_SMPA_FUNC,     (uintptr_t)hook_SMPA,       (void **)&g_origSMPA,    "SetMemberPassiveAbility", 0 },
        { OFF_MLASC_GOT,    OFF_MLASC_FUNC,    (uintptr_t)hook_MLASC,      (void **)&g_origMLASC,   "MakeList_AbilitySetCheck", 0 },
        { OFF_SMD_GOT,      OFF_SMD_FUNC,      (uintptr_t)hook_SMD,        (void **)&g_origSMD,     "SetMagicDamage", 0 },
        { OFF_SMES_GOT,     OFF_SMES_FUNC,     (uintptr_t)hook_SMES,       (void **)&g_origSMES,    "SetMemberEquipStatus", 0 },
        { OFF_SYNCSB_GOT,   OFF_SYNCSB_FUNC,   (uintptr_t)hook_SyncSB,     (void **)&g_origSyncSB,  "SyncScriptButton", 0 },
        { OFF_IMIC_GOT,     OFF_IMIC_FUNC,     (uintptr_t)hook_IMIC,       (void **)&g_origIMIC,    "InitMenuItemCtrl", 0 },
        { OFF_IMM_GOT,      OFF_IMM_FUNC,      (uintptr_t)hook_IMM,        (void **)&g_origIMM,     "InitMenuMode", 0 },
        { OFF_FIRSTSEL_GOT, OFF_FIRSTSEL_FUNC, (uintptr_t)hook_FirstSel,   (void **)&g_origFirstSel, "SetFirstSelectID", 0 },
        { OFF_CMMC_GOT,     OFF_CMMC_FUNC,     (uintptr_t)hook_Cmmc,       (void **)&g_origCmmc,    "CreateMemberMenuCommand", 0 },
        { OFF_CAM_GOT,      OFF_CAM_FUNC,      (uintptr_t)hook_Cam,        (void **)&g_origCam,     "ChrActionMagic", 0 },
        { OFF_CIE_GOT,      OFF_CIE_FUNC,      (uintptr_t)hook_Cie,        (void **)&g_origCie,     "CallItemEvent", 0 },
        { OFF_ISSEL_GOT,    OFF_ISSEL_FUNC,    (uintptr_t)hook_IsSel,      (void **)&g_origIsSel,   "IsItemSelect", 0 },
        { OFF_DMI_GOT,      OFF_DMI_FUNC,      (uintptr_t)hook_Dmi,        (void **)&g_origDmi,     "DrawMenuItem", 0 },
        { OFF_SHOP_GOT,     OFF_SHOP_FUNC,     (uintptr_t)hook_Shop,       (void **)&g_origShop,    "ScriptShop", 0 },
        { OFF_SENC_GOT,     OFF_SENC_FUNC,     (uintptr_t)hook_SEnc,       (void **)&g_origSEnc,    "ScriptEncount", 0 },
        /* 1.0-pre3: 本当のワイド表示（末尾の9つ。SetViewPort［22］と合わせて全部そろったときだけ入れる） */
        { OFF_MSVP_GOT,     OFF_MSVP_FUNC,     (uintptr_t)hook_MSVP,       (void **)&g_origMSVP,    "MainScene::SetViewPort", 0 },
        { OFF_S2DM_GOT,     OFF_S2DM_FUNC,     (uintptr_t)hook_S2DM,       (void **)&g_origS2DM,    "Set2DModeMatrix", 0 },
        { OFF_DIEX_GOT,     OFF_DIEX_FUNC,     (uintptr_t)hook_DIEx,       (void **)&g_origDIEx,    "DrawImageEx", 0 },
        { OFF_FILL4_GOT,    OFF_FILL4_FUNC,    (uintptr_t)hook_Fill4,      (void **)&g_origFill4,   "FillRect4", 0 },
        { OFF_FILL8_GOT,    OFF_FILL8_FUNC,    (uintptr_t)hook_Fill8,      (void **)&g_origFill8,   "FillRect8", 0 },
        { OFF_DRAWFAR_GOT,  OFF_DRAWFAR_FUNC,  (uintptr_t)hook_DrawFar,    (void **)&g_origDrawFar, "DrawFar", 0 },
        { OFF_MINIMAP_GOT,  OFF_MINIMAP_FUNC,  (uintptr_t)hook_MiniMap,    (void **)&g_origMiniMap, "DrawMiniMap", 0 },
        { OFF_ISPOINTIN_GOT, OFF_ISPOINTIN_FUNC, (uintptr_t)hook_IsPointIn, (void **)&g_origIsPointIn, "vector2::IsPointIn", 0 },
        { OFF_SCRFILTER_GOT, OFF_SCRFILTER_FUNC, (uintptr_t)hook_ScrFilter, (void **)&g_origScrFilter, "DrawScreenFilter", 0 },
    };
    const int n = sizeof(t) / sizeof(t[0]);
    int ok[sizeof(t) / sizeof(t[0])];
    for (int i = 0; i < n; i++) {
        uintptr_t v = *(volatile uintptr_t *)(base + t[i].got);
        ok[i] = (v == base + t[i].func);
        if (!ok[i]) {
            LOGE("%s: GOT holds %lx, expected %lx", t[i].name, v, base + t[i].func);
            if (t[i].required) return;
        }
    }
    /* カーソル関係は Update と Draw の両方がそろったときだけ入れる */
    int cursorOk = ok[4] && ok[5] && p_GetTouchInfo && p_SetData && p_FillRect;
    int wideOk = ok[22] && p_GetTouchInfo;
    for (int i = n - WIDE_HOOKS; i < n; i++) wideOk = wideOk && ok[i];
    if (!wideOk) LOGE("wide: hooks missing, screen left as the game draws it");
    for (int i = 0; i < n; i++) {
        if (i >= 4 && i <= 7 && !cursorOk) continue;
        if (i >= 6 && i <= 7 && !(ok[6] && ok[7])) continue;
        if (i >= 8 && !ok[i]) continue;   /* MenuClass 側は両方そろったときだけ */
        if (i >= 11 && i <= 13 && !(ok[11] && ok[12] && ok[13] && p_GetMember)) continue;  /* ジョブのページ分け */
        if (i >= 14 && i <= 15 && !(ok[14] && ok[15])) continue;                           /* 色替え */
        if (i == 16 && !(ok[11] && ok[12] && ok[13] && p_GetMember)) continue;            /* 2ページ目のタッチ位置 */
        if (i >= 19 && i <= 20 && !(ok[19] && ok[20] && p_GetMemberID && p_IsCtrlChg)) continue;  /* 歩くキャラの切り替え */
        if ((i == 23 || i == 24) && !(ok[23] && ok[24])) continue;   /* ダッシュは2つそろったときだけ */
        if ((i == 22 || i >= n - WIDE_HOOKS) && !wideOk) continue;   /* ワイド表示は全部そろったときだけ */
        if (i == 17 && !(p_SetPartyMember && p_GetMemberID && p_GetMemberIDList && p_ReadImgAll && p_InitItemCtrl && p_ResetWidget)) continue;  /* ゲスト入れ替え */
        /* v3.05: 城の四角と線の対策（フォグのアルファ切り替えと継ぎ目埋め、v2.14b〜v2.15）は外し、ゲームの元の描画に戻す */
        if (!k_gfxFix && (t[i].hook == (uintptr_t)hook_FogEn || t[i].hook == (uintptr_t)hook_DrawPrim ||
                          t[i].hook == (uintptr_t)hook_Vtx)) {
            continue;
        }
        *t[i].orig = (void *)(base + t[i].func);
        if (!patch_slot(base + t[i].got, t[i].hook)) {
            LOGE("patch %s failed", t[i].name);
            if (t[i].required) return;
            continue;
        }
    }
    /* v4.00: このモッド自身が呼ぶ SetJobStatus も すっぴん の引き継ぎを通す */
    if (g_origSJS && *(volatile uintptr_t *)(base + OFF_SJS_GOT) == (uintptr_t)hook_SJS)
        p_SetJobStatus = hook_SJS;
    if (!wideOk) patch_field_width();
    else { wide_escape_patch(); wide_panel_patch(); }   /* 1.0-pre3: ワイド表示ではフィールドを広く描く（遠景は hook_DrawFar で伸ばす） */
    menu_step_init();
    g_installed = 1;
    LOGI("installed v" FFLMOD_VERSION ": base=%lx cursor=%d", base, cursorOk);
    import_save();
    export_save();
}

static int key_to_bit(int kc)
{
    switch (kc) {
    case KC_DPAD_UP: return K_UP;
    case KC_DPAD_DOWN: return K_DOWN;
    case KC_DPAD_LEFT: return K_LEFT;
    case KC_DPAD_RIGHT: return K_RIGHT;
    case KC_BUTTON_A: case KC_DPAD_CENTER: case KC_ENTER: case KC_SPACE: return K_A;
    case KC_BUTTON_B: case KC_ESCAPE: return K_BACK;
    case KC_BUTTON_Y: case KC_DEL: return K_MENU;
    case KC_BUTTON_START: case KC_P: return K_BPAUSE;
    case KC_BUTTON_SELECT: case KC_Z: return K_BAUTO;
    case KC_BUTTON_L1: case KC_Q: case KC_PAGE_UP: return K_PREV;
    case KC_BUTTON_R1: case KC_E: case KC_PAGE_DOWN: return K_NEXT;
    case KC_BUTTON_X: case KC_M: return K_XMENU;
    }
    return 0;
}

/* 戻り値: 0=処理しない（ゲーム／OSへ渡す） 1=処理済み */
int Java_com_square_1enix_android_1googleplay_ffl_1gp_FFLMod_nativeKey(void *env, void *cls, int keycode, int action)
{
    (void)env; (void)cls;
    int bit = key_to_bit(keycode);
    if (bit) {
        if (action == 0)      { g_keyBits |= bit; __atomic_or_fetch(&g_latch, bit, __ATOMIC_SEQ_CST); }
        else if (action == 1) g_keyBits &= ~bit;
        return 1;
    }
    /* 編成画面の前／次。日本語配列のキーボードでは、[ のキーが「]」(72)、] のキーが「\」(73) として届く。
       英語配列の [ (71) も「前」にする */
    int bench = 0, page = 0;
    switch (keycode) {
    case KC_TAB:           page = 1; break;
    case KC_BUTTON_L2:     page = 1; bench = -1; break;
    case KC_BUTTON_R2:     page = 1; bench = 1; break;
    case KC_LEFT_BRACKET:  bench = -1; break;
    case KC_RIGHT_BRACKET: bench = -1; break;
    case KC_BACKSLASH:     bench = 1; break;
    }
    if (page || bench) {
        static unsigned char held[256];   /* 押しっぱなしの自動リピートでは動かさない */
        unsigned k = (unsigned)keycode & 0xff;
        if (action == 0 && !held[k]) {
            held[k] = 1;
            if (page) __atomic_store_n(&g_pageLatch, 1, __ATOMIC_SEQ_CST);        /* ジョブ画面のページ切り替え */
            if (bench) __atomic_store_n(&g_benchLatch, bench, __ATOMIC_SEQ_CST);  /* 編成画面: 前／次のキャラ */
        } else if (action == 1) held[k] = 0;
        return 1;
    }
    switch (keycode) {
    case KC_BUTTON_THUMBL: case KC_BUTTON_THUMBR:
        return 1; /* 未割り当て。OS側の誤動作を防ぐため握りつぶす */
    }
    return 0;
}

void Java_com_square_1enix_android_1googleplay_ffl_1gp_FFLMod_nativeAxis(void *env, void *cls, float x, float y, float hx, float hy)
{
    (void)env; (void)cls;
    const float t = 0.5f;
    int b = 0;
    if (x < -t || hx < -t) b |= K_LEFT;
    if (x >  t || hx >  t) b |= K_RIGHT;
    if (y < -t || hy < -t) b |= K_UP;
    if (y >  t || hy >  t) b |= K_DOWN;
    g_axisBits = b;
}

void Java_com_square_1enix_android_1googleplay_ffl_1gp_FFLMod_nativeReset(void *env, void *cls)
{
    (void)env; (void)cls;
    g_keyBits = 0;
    g_axisBits = 0;
    export_save();
}
