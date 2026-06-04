#include <windows.h>

/*
 * psopeeps.c  -  pso-peeps-14x9-ui-culling-clean-r2
 *
 * PSO PC V2 proxy DLL focused on widescreen, HUD/menu correction,
 * black-bar clearing, culling/frustum fixes, and near-clip Z precision.
 *
 * Loaded by D3D8.dll (d3d8_loader) as psopeeps.dll.
 * D3D8.dll itself is stock -- all work lives here.
 *
 * Build:
 *   i686-w64-mingw32-gcc -shared -O2 -o psopeeps.dll psopeeps.c d3d8.def
 *     -lkernel32 -Wl,--enable-stdcall-fixup
 */
/* ---- D3D8 constants ---- */
#define D3DTS_PROJECTION    3
#define D3DFVF_XYZRHW       0x004
#define D3DLOCK_READONLY    0x0010
#define D3DLOCK_NOOVERWRITE 0x1000   /* D3D8 actual value, not 0x0800 */
#define D3DLOCK_DISCARD     0x2000

#define D3DPT_POINTLIST     1
#define D3DPT_LINELIST      2
#define D3DPT_LINESTRIP     3
#define D3DPT_TRIANGLELIST  4
#define D3DPT_TRIANGLESTRIP 5
#define D3DPT_TRIANGLEFAN   6

/* ---- types ---- */
typedef struct { float m[4][4]; } D3DMATRIX;

typedef struct {
    DWORD X, Y, Width, Height;
    float MinZ, MaxZ;
} D3DVIEWPORT8;


typedef struct {
    LONG x1, y1, x2, y2;
} D3DRECT8;

/* MENU_CLEAR_DIAG: pure observation, no behavior change */
static int   g_menu_clear_watch = 0;
static DWORD g_menu_clear_until_frame = 0;
static UINT  g_menu_clear_count = 0;

/* Last viewport as PSO set it (640x480 space)
 * Declared early because VB/UP hooks need to know whether the current
 * PSO viewport is fullscreen or a menu/preview subviewport.
 */
static D3DVIEWPORT8 g_pso_vp = {0, 0, 640, 480, 0.0f, 1.0f};

typedef struct {
    UINT  BackBufferWidth, BackBufferHeight;
    DWORD BackBufferFormat, BackBufferCount, MultiSampleType, SwapEffect;
    HWND  hDeviceWindow;
    BOOL  Windowed, EnableAutoDepthStencil;
    DWORD AutoDepthStencilFormat, Flags;
    UINT  FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

typedef void*   (WINAPI *Direct3DCreate8_t)       (UINT);
typedef HRESULT (WINAPI *Clear_t)(void*, DWORD, const void*, DWORD, DWORD, float, DWORD);
typedef HRESULT (WINAPI *Present_t)(void*, const RECT*, const RECT*, HWND, const void*);
typedef HRESULT (WINAPI *CreateDevice_t)           (void*, UINT, DWORD, HWND, DWORD,
                                                    D3DPRESENT_PARAMETERS*, void**);
typedef HRESULT (WINAPI *SetTransform_t)           (void*, DWORD, const D3DMATRIX*);
typedef HRESULT (WINAPI *SetViewport_t)            (void*, const D3DVIEWPORT8*);
typedef HRESULT (WINAPI *GetViewport_t)            (void*, D3DVIEWPORT8*);

typedef HRESULT (WINAPI *SetVertexShader_t)        (void*, DWORD);
typedef HRESULT (WINAPI *SetStreamSource_t)        (void*, UINT, void*, UINT);
typedef HRESULT (WINAPI *CreateVertexBuffer_t)     (void*, UINT, DWORD, DWORD, DWORD, void**);
typedef HRESULT (WINAPI *DrawPrimitive_t)          (void*, DWORD, UINT, UINT);
typedef HRESULT (WINAPI *SetRenderState_t)         (void*, DWORD, DWORD);
typedef HRESULT (WINAPI *DrawIndexedPrimitive_t)   (void*, DWORD, UINT, UINT, UINT, UINT);
typedef HRESULT (WINAPI *DrawPrimitiveUP_t)        (void*, DWORD, UINT, const void*, UINT);
typedef HRESULT (WINAPI *VBLock_t)                 (void*, UINT, UINT, BYTE**, DWORD);
typedef HRESULT (WINAPI *VBUnlock_t)               (void*);

/* ---- real pointers ---- */
static HMODULE           real_d3d8;
static Direct3DCreate8_t real_Direct3DCreate8;
static CreateDevice_t    real_CreateDevice;
static Clear_t           real_Clear;
static Present_t         real_Present;
static SetTransform_t    real_SetTransform;
static SetViewport_t     real_SetViewport;
static GetViewport_t     real_GetViewport;
static SetVertexShader_t real_SetVertexShader;
static SetStreamSource_t real_SetStreamSource;
static CreateVertexBuffer_t real_CreateVertexBuffer;
static DrawPrimitive_t   real_DrawPrimitive;
static DrawIndexedPrimitive_t real_DrawIndexedPrimitive;
static DrawPrimitiveUP_t real_DrawPrimitiveUP;
static SetRenderState_t  real_SetRenderState;
static VBLock_t          real_VBLock;
static VBUnlock_t        real_VBUnlock;
static int               hooked       = 0;
static int               g_vb_patched = 0;

/* Saved original projection for XYZRHW draw restore.
 * Near-clip rewrite corrupts pre-baked RHW values; we restore the
 * original projection before XYZRHW draws and re-apply after. */
static D3DMATRIX g_orig_proj;
static void*     g_proj_device   = NULL;
static int       g_orig_proj_set = 0;

/*
 * Full-screen XYZRHW threshold (PSO space, 0-640).
 * x_range > this -> 14:9 formula (fades, letterbox bars).
 * x_range <= this -> 4:3 centred formula (HUD elements).
 */
#define FULLWIDTH_THRESH 580.0f

/* ---- 2D correction state ---- */
static DWORD g_fvf        = 0;
static float g_xrhw_scale = 1.0f;
static float g_xrhw_cx    = 320.0f;

static UINT g_bb_w = 640;
static UINT g_bb_h = 480;

static UINT  g_bar_w  = 0;
static UINT  g_vp_w   = 640;
static int   g_vp_mode = 1;
static float g_vp_ar   = 14.0f / 9.0f;
static float g_k_proj = 0.75f;

/*
 * 4:3-centred HUD layout (native resolution mode).
 * g_hud_sy = bb_h / 480  (e.g. 4.5 at 4K)
 * g_hud_x0 = (bb_w - 640 * g_hud_sy) / 2  (e.g. 480 at 4K)
 * With sx == sy the HUD maintains 4:3 pixel proportions, matching
 * the 16:9 mode where sx == sy == 4.5 (0.75 compress x 6 stretch).
 */
static float g_hud_x0 = 0.0f;
static float g_hud_sy = 1.0f;

/* ---- frame counter (incremented per Present) ---- */
static int g_frame = 0;

/* =========================================================
 * Shadow VB table
 * ========================================================= */
#define MAX_SHADOW_VBS 128

typedef struct {
    void*  vb;
    BYTE*  shadow;
    UINT   shadow_cap;
    UINT   lock_off;
    UINT   lock_sz;
    BYTE*  real_ptr;
    UINT   stride;
    UINT   vb_size;
    DWORD  lock_count;      /* DIAG: total lock+unlock cycles on this VB */
    DWORD  vert_total;      /* DIAG: total vertices corrected on this VB */
    DWORD  lock_fvf;        /* g_fvf captured at most recent Lock call */
} VBShadow;

static VBShadow g_svb[MAX_SHADOW_VBS];
static int      g_svb_n = 0;

static VBShadow* svb_find(void* vb) {
    int i;
    for (i = 0; i < g_svb_n; i++)
        if (g_svb[i].vb == vb) return &g_svb[i];
    return NULL;
}

static VBShadow* svb_get(void* vb, UINT stride) {
    VBShadow* e = svb_find(vb);
    if (e) {
        if (e->stride == 0 && stride > 0) e->stride = stride;
        return e;
    }
    if (g_svb_n >= MAX_SHADOW_VBS) return NULL;
    e = &g_svb[g_svb_n++];
    e->vb         = vb;
    e->shadow     = NULL;
    e->shadow_cap = 0;
    e->lock_off   = 0;
    e->lock_sz    = 0;
    e->real_ptr   = NULL;
    e->stride     = stride;
    e->vb_size    = 0;
    e->lock_count      = 0;
    e->vert_total = 0;
    return e;
}

/* =========================================================
 * Scratch buffer
 * ========================================================= */
#define SCRATCH_SIZE 65536
static BYTE g_scratch[SCRATCH_SIZE];

static BYTE* get_buf(UINT bytes) {
    if (bytes <= SCRATCH_SIZE) return g_scratch;
    return (BYTE*)HeapAlloc(GetProcessHeap(), 0, bytes);
}
static void free_buf(BYTE* p, UINT bytes) {
    if (bytes > SCRATCH_SIZE) HeapFree(GetProcessHeap(), 0, p);
}

/* =========================================================
 * Logging -- buffered, flushed once per Present
 *
 * All log_line/log_f calls write into a 128 KB ring buffer.
 * log_flush() (called in hook_Present) does the single file
 * write per frame.  This keeps file I/O off every hot path.
 *
 * Exception: log_flush() itself is also called directly for
 * any startup/shutdown messages written before the first Present.
 * ========================================================= */
#define LOG_BUF_CAP 131072
static char g_log_buf[LOG_BUF_CAP];
static int  g_log_pos = 0;

static void log_flush(void) {
    if (g_log_pos <= 0) return;
    {
        HANDLE h = CreateFileA("pso-peeps-d3d8-wsh.log",
            FILE_APPEND_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE,
            NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(h, g_log_buf, (DWORD)g_log_pos, &w, NULL);
            CloseHandle(h);
        }
    }
    g_log_pos = 0;
}

static void log_line(const char* msg) {
    int len = lstrlenA(msg);
    if (g_log_pos + len + 2 >= LOG_BUF_CAP) log_flush();
    if (len < LOG_BUF_CAP - 2) {
        CopyMemory(g_log_buf + g_log_pos, msg, len);
        g_log_pos += len;
        g_log_buf[g_log_pos++] = '\r';
        g_log_buf[g_log_pos++] = '\n';
    }
}

/* log with frame prefix */
static void log_f(const char* msg) {
    char b[288];
    wsprintfA(b, "[F%05u] %s", g_frame, msg);
    log_line(b);
}

/* Wall-clock timestamps - both UTC and LOCAL so correlation with newserv
 * chat logs works regardless of timezone.  Bruce is EDT (UTC-4).
 * Format: [TIME] tag              F##### UTC=HH:MM:SS.mmm LOCAL=HH:MM:SS.mmm */
static void log_timestamp(const char* tag) {
    SYSTEMTIME utc, loc;
    GetSystemTime(&utc);
    GetLocalTime(&loc);
    char b[192];
    wsprintfA(b, "[TIME] %-16s F%05u UTC=%02u:%02u:%02u.%03u LOCAL=%02u:%02u:%02u.%03u",
        tag, g_frame,
        utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds,
        loc.wHour, loc.wMinute, loc.wSecond, loc.wMilliseconds);
    log_line(b);
}

static void log_uint(const char* pfx, UINT v) {
    char b[128]; wsprintfA(b, "%s%u", pfx, v); log_line(b);
}

/* =========================================================
 * Dedup tables for hot-path logging
 * ========================================================= */

/* Unique viewports seen */
typedef struct { DWORD X, Y, W, H; } VP4;
#define MAX_UNIQUE_VP 64
static VP4 g_vp_seen[MAX_UNIQUE_VP];
static int g_vp_seen_n = 0;

static int vp_is_new(DWORD x, DWORD y, DWORD w, DWORD h) {
    int i;
    for (i = 0; i < g_vp_seen_n; i++)
        if (g_vp_seen[i].X==x && g_vp_seen[i].Y==y &&
            g_vp_seen[i].W==w && g_vp_seen[i].H==h)
            return 0;
    if (g_vp_seen_n < MAX_UNIQUE_VP) {
        g_vp_seen[g_vp_seen_n].X = x;
        g_vp_seen[g_vp_seen_n].Y = y;
        g_vp_seen[g_vp_seen_n].W = w;
        g_vp_seen[g_vp_seen_n].H = h;
        g_vp_seen_n++;
        return 1;
    }
    return 0; /* table full - don't log duplicates */
}

/* Unique FVF values seen at SetStreamSource */
#define MAX_UNIQUE_FVF 32
static DWORD g_fvf_seen[MAX_UNIQUE_FVF];
static int   g_fvf_seen_n = 0;

static int fvf_is_new(DWORD fvf) {
    int i;
    for (i = 0; i < g_fvf_seen_n; i++)
        if (g_fvf_seen[i] == fvf) return 0;
    if (g_fvf_seen_n < MAX_UNIQUE_FVF) {
        g_fvf_seen[g_fvf_seen_n++] = fvf;
        return 1;
    }
    return 0;
}

/* Unique DrawPrimitiveUP call patterns */
typedef struct { DWORD fvf, pt; UINT pc, stride; } UP4;
#define MAX_UNIQUE_UP 64
static UP4 g_up_seen[MAX_UNIQUE_UP];
static int g_up_seen_n = 0;

static int up_is_new(DWORD fvf, DWORD pt, UINT pc, UINT stride) {
    int i;
    for (i = 0; i < g_up_seen_n; i++)
        if (g_up_seen[i].fvf==fvf && g_up_seen[i].pt==pt &&
            g_up_seen[i].pc==pc   && g_up_seen[i].stride==stride)
            return 0;
    if (g_up_seen_n < MAX_UNIQUE_UP) {
        g_up_seen[g_up_seen_n].fvf    = fvf;
        g_up_seen[g_up_seen_n].pt     = pt;
        g_up_seen[g_up_seen_n].pc     = pc;
        g_up_seen[g_up_seen_n].stride = stride;
        g_up_seen_n++;
        return 1;
    }
    return 0;
}

/* Unique projection m[0][0] values (stored as int * 10000) */
#define MAX_UNIQUE_PROJ 16
static int g_proj_seen[MAX_UNIQUE_PROJ];
static int g_proj_seen_n = 0;

static int proj_is_new(int v) {
    int i;
    for (i = 0; i < g_proj_seen_n; i++)
        if (g_proj_seen[i] == v) return 0;
    if (g_proj_seen_n < MAX_UNIQUE_PROJ) {
        g_proj_seen[g_proj_seen_n++] = v;
        return 1;
    }
    return 0;
}

/* =========================================================
 * Vtable patching
 * ========================================================= */
static void patch_slot(void** slot, void* rep, void** orig) {
    DWORD old = 0, tmp = 0;
    if (orig && !*orig) *orig = *slot;
    if (*slot == rep) return;
    VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old);
    *slot = rep;
    VirtualProtect(slot, sizeof(void*), old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void*));
}

/* =========================================================
 * Vertex count
 * ========================================================= */
static UINT vcount(DWORD pt, UINT pc) {
    switch (pt) {
        case D3DPT_POINTLIST:     return pc;
        case D3DPT_LINELIST:      return pc * 2;
        case D3DPT_LINESTRIP:     return pc + 1;
        case D3DPT_TRIANGLELIST:  return pc * 3;
        case D3DPT_TRIANGLESTRIP: return pc + 2;
        case D3DPT_TRIANGLEFAN:   return pc + 2;
        default:                  return pc * 3;
    }
}

/* =========================================================
 * X/Y correction
 * ========================================================= */
static void correct_xyzrhw(BYTE* data, UINT vc, UINT stride) {
    UINT i; BYTE* p = data;

    if (g_bb_w > 640) {
        /*
         * Native resolution mode -- two formulas:
         *
         * FULL-SCREEN (x_range > FULLWIDTH_THRESH):
         *   Maps PSO [0,640] -> [g_bar_w, g_bar_w+g_vp_w].
         *   Used for fades, letterbox bars -- must align with side bars.
         *
         * HUD ELEMENT (x_range <= FULLWIDTH_THRESH):
         *   Maps using sx == sy == g_hud_sy, centred at g_hud_x0.
         *   Preserves 4:3 pixel proportions (matches 16:9 mode behaviour).
         */
        float min_x =  1e9f, max_x = -1e9f;
        p = data;
        for (i = 0; i < vc; i++, p += stride) {
            float x = *(float*)p;
            if (x < min_x) min_x = x;
            if (x > max_x) max_x = x;
        }

        if ((max_x - min_x) > FULLWIDTH_THRESH) {
            /*
             * Full-screen element -> 14:9 scale with edge snapping.
             *
             * PSO was designed for CRT overscan: the top/bottom letterbox
             * bars start at PSO y~=8 (not y=0), assuming ~8 scan lines would
             * be hidden by overscan on real hardware. At 4K with no overscan,
             * that 8-unit gap becomes 36px of visible 3D scene above the bar.
             *
             * Edge snap: after scaling, any corrected coordinate within
             * EDGE_SNAP physical pixels of a screen boundary is extended to
             * that boundary. This eliminates the overscan gap AND the 2-3px
             * D3D8 half-pixel-offset gap at the sides without affecting any
             * non-edge content.
             *
             * EDGE_SNAP = 48px at 4K -> 48/4.5 ~= 10.7 PSO units of margin.
             * Safe because no legitimate full-screen interior content exists
             * within 10 PSO units of any screen edge.
             */
            float sx    = (float)g_vp_w / 640.0f;
            float sy    = (float)g_bb_h / 480.0f;
            float x_lo  = (float)g_bar_w;
            float x_hi  = (float)(g_bar_w + g_vp_w);
            float y_hi  = (float)g_bb_h;
#define EDGE_SNAP 48.0f
            p = data;
            for (i = 0; i < vc; i++, p += stride) {
                float* x = (float*)p;
                float* y = (float*)(p + 4);
                float cx = x_lo + *x * sx;
                float cy = *y * sy;
                /* snap X to bar edges */
                if (cx < x_lo + EDGE_SNAP) cx = x_lo;
                if (cx > x_hi - EDGE_SNAP) cx = x_hi;
                /* snap Y to screen top/bottom */
                if (cy < EDGE_SNAP) cy = 0.0f;
                if (cy > y_hi - EDGE_SNAP) cy = y_hi;
                *x = cx;
                *y = cy;
            }
#undef EDGE_SNAP
        } else {
            /* HUD element -> 4:3 centred scale */
            float sy = g_hud_sy;
            float x0 = g_hud_x0;
            p = data;
            for (i = 0; i < vc; i++, p += stride) {
                float* x = (float*)p;
                float* y = (float*)(p + 4);
                *x = x0 + *x * sy;
                *y = *y * sy;
            }
        }
    } else {
        /* 640x480 backbuffer mode: horizontal widescreen correction */
        for (i = 0; i < vc; i++, p += stride) {
            float* x = (float*)p;
            *x = g_xrhw_cx + (*x - g_xrhw_cx) * g_xrhw_scale;
        }
    }
}

/* =========================================================
 * VB hooks
 * ========================================================= */
static HRESULT WINAPI hook_CreateVertexBuffer(
    void* self, UINT length, DWORD usage, DWORD fvf, DWORD pool, void** ppVB)
{
    HRESULT hr = real_CreateVertexBuffer(self, length, usage, fvf, pool, ppVB);
    if (hr == 0 && ppVB && *ppVB) {
        VBShadow* e = svb_find(*ppVB);
        if (e) {
            e->vb_size = length;
            {
                char b[128];
                wsprintfA(b, "[VB] CreateVB size=%u fvf=0x%04X usage=0x%04X ptr=%p",
                    length, fvf, (UINT)usage, *ppVB);
                log_f(b);
            }
        }
    }
    return hr;
}

static HRESULT WINAPI hook_VBLock(
    void* self, UINT off, UINT sz, BYTE** ppData, DWORD flags)
{
    HRESULT hr = real_VBLock(self, off, sz, ppData, flags);
    if (hr == 0 && ppData && *ppData
        && !(flags & D3DLOCK_READONLY))
    {
        VBShadow* e = svb_find(self);
        if (e && e->stride >= 4) {
            UINT effective_sz = sz;
            if (effective_sz == 0 && e->vb_size > off)
                effective_sz = e->vb_size - off;
            if (effective_sz == 0) goto vblock_done;
            {
            UINT need = off + effective_sz;

            /* DIAG: log first 8 locks on this VB, then every 500th */
            if (e->lock_count < 8 || (e->lock_count % 500) == 0) {
                char b[192];
                wsprintfA(b,
                    "[VB] Lock #%u ptr=%p off=%u sz=%u(eff=%u) flags=0x%X stride=%u cap=%u",
                    e->lock_count, self, off, sz, effective_sz,
                    (UINT)flags, e->stride, e->shadow_cap);
                log_f(b);
                if (sz == 0) {
                    char b2[64];
                    wsprintfA(b2, "[VB]  ^ sz=0 expanded from vb_size=%u", e->vb_size);
                    log_f(b2);
                }
            }

            if (need > e->shadow_cap) {
                BYTE* ns = (BYTE*)HeapAlloc(GetProcessHeap(), 0, need);
                if (ns) {
                    if (e->shadow) {
                        CopyMemory(ns, e->shadow, e->shadow_cap);
                        HeapFree(GetProcessHeap(), 0, e->shadow);
                    }
                    e->shadow     = ns;
                    e->shadow_cap = need;
                }
            }
            if (e->shadow) {
                e->lock_off         = off;
                e->lock_sz          = effective_sz;
                e->real_ptr         = *ppData;
                e->lock_fvf         = g_fvf;
                *ppData     = e->shadow + off;
            }
            }
        }
    }
    vblock_done:
    return hr;
}

static HRESULT WINAPI hook_VBUnlock(void* self) {
    VBShadow* e = svb_find(self);
    if (e && e->real_ptr && e->lock_sz > 0 && e->stride >= 4) {
        BYTE* src = e->shadow + e->lock_off;
        UINT  vc  = e->lock_sz / e->stride;

        /* DIAG: log sample X values before and after correction */
        if (e->lock_count < 8 || (e->lock_count % 500) == 0) {
            float x0_before = (vc > 0) ? *(float*)(src) : 0.0f;
            float y0_before = (vc > 0) ? *(float*)(src+4) : 0.0f;
            float xN_before = (vc > 1) ? *(float*)(src + (vc-1)*e->stride) : x0_before;

            correct_xyzrhw(src, vc, e->stride);

            float x0_after  = (vc > 0) ? *(float*)(src) : 0.0f;
            float y0_after  = (vc > 0) ? *(float*)(src+4) : 0.0f;
            float xN_after  = (vc > 1) ? *(float*)(src + (vc-1)*e->stride) : x0_after;

            char b[256];
            wsprintfA(b,
                "[VB] Unlock #%u ptr=%p vc=%u | x0: %d->%d  xN: %d->%d  y0: %d->%d",
                e->lock_count, self, vc,
                (int)x0_before, (int)x0_after,
                (int)xN_before, (int)xN_after,
                (int)y0_before, (int)y0_after);
            log_f(b);
        } else {
            correct_xyzrhw(src, vc, e->stride);
        }

        CopyMemory(e->real_ptr, src, e->lock_sz);
        e->real_ptr = NULL;
        e->lock_sz  = 0;
        e->lock_count++;
        e->vert_total += vc;
    }
    return real_VBUnlock(self);
}

/* =========================================================
 * Device hooks
 * ========================================================= */
static HRESULT WINAPI hook_SetTransform(void* self, DWORD st, const D3DMATRIX* m) {
    D3DMATRIX w;
    if (!m) return real_SetTransform(self, st, m);

    if (st == D3DTS_PROJECTION) {
        int m00_i = (int)(m->m[0][0] * 10000.0f);
        if (proj_is_new(m00_i)) {
            char b[128];
            wsprintfA(b,
                "[TX] PROJECTION new m[0][0]=%d/10000 after_k_proj=%d/10000  k_proj=%d/10000",
                m00_i,
                (int)(m->m[0][0] * g_k_proj * 10000.0f),
                (int)(g_k_proj * 10000.0f));
            log_f(b);
          
            {
                float m22 = m->m[2][2];
                float m32 = m->m[3][2];
                if (m22 != 0.0f && m22 != 1.0f) {
                    float near_z = -m32 / m22;
                    float far_z  =  m22 * near_z / (m22 - 1.0f);
                    char nb[128];
                    wsprintfA(nb, "[TX] PROJ near=%d/1000 far=%d/10",
                        (int)(near_z * 1000.0f), (int)(far_z * 10.0f));
                    log_f(nb);
                }
            }
        }
        if (g_pso_vp.X == 0 && g_pso_vp.Y == 0 && g_pso_vp.Width == 640 && g_pso_vp.Height == 480) {
            g_orig_proj     = *m;
            g_proj_device   = self;
            g_orig_proj_set = 1;
            w = *m;
            w.m[0][0] *= g_k_proj;
            {
                float m22 = m->m[2][2];
                float m32 = m->m[3][2];
                if (m22 != 0.0f && m22 != 1.0f) {
                    float far_z    = m22 * (-m32 / m22) / (m22 - 1.0f);
                    float new_near = 2.0f;
                    w.m[2][2] = far_z / (far_z - new_near);
                    w.m[3][2] = -new_near * far_z / (far_z - new_near);
                }
            }
            return real_SetTransform(self, st, &w);
        }

        /*
         * Menu / preview / transition sub-viewports use their original
         * projection. Applying the gameplay 14:9 projection here can
         * break the Start menu 3D world / character preview.
         */
        return real_SetTransform(self, st, m);
    }

    return real_SetTransform(self, st, m);
}


static HRESULT WINAPI hook_GetViewport(void* self, D3DVIEWPORT8* vp) {
    if (vp && g_bb_w > 640) {
        *vp = g_pso_vp;
        return 0;
    }
    return real_GetViewport(self, vp);
}

static int viewport_looks_physical(const D3DVIEWPORT8* vp) {
    if (!vp) return 0;
    if (g_bb_w <= 640 || g_bb_h <= 480) return 0;
    if (vp->X == 0 && vp->Y == 0 &&
        vp->Width == g_bb_w && vp->Height == g_bb_h)
        return 1;
    if (vp->X > 640 || vp->Y > 480 ||
        vp->Width > 640 || vp->Height > 480)
        return 1;
    return 0;
}

static HRESULT WINAPI hook_SetViewport(void* self, const D3DVIEWPORT8* vp) {
    if (!vp) return real_SetViewport(self, vp);

    if (g_bb_w > 640 && g_bb_h > 480) {
        int is_phys = viewport_looks_physical(vp);

        /* DIAG: log every unique viewport call with its disposition */
        if (vp_is_new(vp->X, vp->Y, vp->Width, vp->Height)) {
            char b[256];
            const char* disp = is_phys ? "PHYSICAL-PASSTHRU" :
                               (vp->Width == 640 && vp->Height == 480 && vp->X == 0 && vp->Y == 0)
                               ? "FULLSCREEN-14:9" : "SUB-VIEWPORT";
            wsprintfA(b,
                "[VP] NEW vp={%u,%u,%u,%u} disp=%s  pso_vp={%u,%u,%u,%u}",
                vp->X, vp->Y, vp->Width, vp->Height, disp,
                g_pso_vp.X, g_pso_vp.Y, g_pso_vp.Width, g_pso_vp.Height);
            log_f(b);

            if (is_phys) {
                char b2[128];
                wsprintfA(b2,
                    "[VP]  ^ viewport_looks_physical fired! bb=%ux%u bar_w=%u vp_w=%u",
                    g_bb_w, g_bb_h, g_bar_w, g_vp_w);
                log_f(b2);
            }
        }

        if (is_phys)
            return real_SetViewport(self, vp);

        g_pso_vp = *vp;

        /* MENU_CLEAR_DIAG:
         * Watch Clear calls around menu/preview subviewport activity.
         * This does not change rendering. It only logs whether the 3D
         * menu/world preview is being cleared/wiped after a subviewport pass.
         */
        {
            int is_fullscreen_pso_vp =
                (vp->X == 0 && vp->Y == 0 && vp->Width == 640 && vp->Height == 480);

            if (!is_fullscreen_pso_vp) {
                g_menu_clear_watch = 1;
                g_menu_clear_until_frame = g_frame + 6;
                g_menu_clear_count = 0;

                {
                    char mb[192];
                    wsprintfA(mb,
                        "[MENU_CLEAR_DIAG] begin sub-vp frame=%u vp={%u,%u,%u,%u}",
                        g_frame, vp->X, vp->Y, vp->Width, vp->Height);
                    log_f(mb);
                }
            } else if (g_menu_clear_watch) {
                g_menu_clear_until_frame = g_frame + 6;

                {
                    char mb[192];
                    wsprintfA(mb,
                        "[MENU_CLEAR_DIAG] fullscreen after sub-vp frame=%u vp={%u,%u,%u,%u}",
                        g_frame, vp->X, vp->Y, vp->Width, vp->Height);
                    log_f(mb);
                }
            }
        }
        {
            D3DVIEWPORT8 vp2 = *vp;
            float sx = (float)g_vp_w / 640.0f;
            float sy = (float)g_bb_h / 480.0f;

            if (vp->X == 0 && vp->Y == 0 &&
                vp->Width == 640 && vp->Height == 480) {
                vp2.X      = g_bar_w;
                vp2.Y      = 0;
                vp2.Width  = g_vp_w;
                vp2.Height = g_bb_h;
            } else {
                /*
                 * Menu / preview / transition sub-viewports must preserve
                 * PSO's 4:3 pixel aspect.  Do NOT widen these with sx.
                 *
                 * Example start-menu viewport:
                 *   PSO 128x128 should remain square on screen.
                 *
                 * Fullscreen gameplay uses the 14:9 viewport above.
                 * Sub-viewports use HUD-style centered 4:3 scaling.
                 */
                vp2.X      = (DWORD)((float)g_hud_x0 + (float)vp->X * sy + 0.5f);
                vp2.Y      = (DWORD)((float)vp->Y * sy + 0.5f);
                vp2.Width  = (DWORD)((float)vp->Width  * sy + 0.5f);
                vp2.Height = (DWORD)((float)vp->Height * sy + 0.5f);
            }
            return real_SetViewport(self, &vp2);
        }
    }

    /* 640x480 backbuffer mode */
    if (g_xrhw_scale != 1.0f && vp->X != 0) {
        D3DVIEWPORT8 vp2 = *vp;
        vp2.X     = (DWORD)(g_xrhw_cx + ((float)vp->X - g_xrhw_cx) * g_xrhw_scale + 0.5f);
        vp2.Width = (DWORD)(vp->Width * g_xrhw_scale + 0.5f);
        return real_SetViewport(self, &vp2);
    }

    return real_SetViewport(self, vp);
}

static HRESULT WINAPI hook_SetVertexShader(void* self, DWORD h) {
    g_fvf = h;
    /* DIAG: log each unique FVF value */
    if (fvf_is_new(h)) {
        char b[80];
        wsprintfA(b, "[FVF] new FVF=0x%04X (XYZRHW=%s)",
            h, (h & D3DFVF_XYZRHW) ? "YES" : "no");
        log_f(b);
    }
    return real_SetVertexShader(self, h);
}

static HRESULT WINAPI hook_SetStreamSource(void* self, UINT stream, void* vb, UINT stride) {
    if (stream == 0) {
        if (vb && !g_vb_patched) {
            void** vvt = *(void***)vb;
            patch_slot(&vvt[11], (void*)hook_VBLock,   (void**)&real_VBLock);
            patch_slot(&vvt[12], (void*)hook_VBUnlock, (void**)&real_VBUnlock);
            g_vb_patched = 1;
            log_line("[VB] VB vtable patched");
        }

        if (vb && (g_fvf & D3DFVF_XYZRHW)) {
            VBShadow* e = svb_get(vb, stride);
            if (e && e->lock_count == 0) {
                /* DIAG: log first registration of this VB */
                char b[128];
                wsprintfA(b,
                    "[VB] Registered XYZRHW VB ptr=%p stride=%u vb_size=%u (svb_n=%d)",
                    vb, stride, e->vb_size, g_svb_n);
                log_f(b);
            }
        }
    }
    return real_SetStreamSource(self, stream, vb, stride);
}

static HRESULT WINAPI hook_DrawPrimitive(void* self, DWORD pt, UINT sv, UINT pc) {
    return real_DrawPrimitive(self, pt, sv, pc);
}

static HRESULT WINAPI hook_DrawIndexedPrimitive(
    void* self, DWORD pt, UINT min_idx, UINT nv, UINT si, UINT pc)
{
    return real_DrawIndexedPrimitive(self, pt, min_idx, nv, si, pc);
}

/* D3DRS constants needed for ATOC passthrough */
#define D3DRS_ZENABLE           7
#define D3DRS_ZWRITEENABLE     14
#define D3DRS_ALPHATESTENABLE  15
#define D3DRS_SRCBLEND         19
#define D3DRS_DESTBLEND        20
#define D3DRS_CULLMODE         22
#define D3DRS_ALPHABLENDENABLE 27
#define D3DRS_LIGHTING        137
#define D3DRS_POINTSIZE       154

#define ATOC_ENABLE   0x314D3241
#define ATOC_DISABLE  0x304D3241

static HRESULT WINAPI hook_SetRenderState(void* self, DWORD state, DWORD value) {
    if (state == D3DRS_ALPHATESTENABLE) {
        real_SetRenderState(self, D3DRS_POINTSIZE,
            value ? ATOC_ENABLE : ATOC_DISABLE);
    }
    return real_SetRenderState(self, state, value);
}


static HRESULT WINAPI hook_DrawPrimitiveUP(
    void* self, DWORD pt, UINT pc, const void* pD, UINT stride)
{
    int is_2d = (g_fvf & D3DFVF_XYZRHW) &&
                (g_xrhw_scale != 1.0f || g_bb_w > 640) &&
                pD && stride >= 4;

    /* DIAG: log each unique UP call signature */
    if (up_is_new(g_fvf, pt, pc, stride)) {
        char b[192];
        UINT vc = vcount(pt, pc);
        float x0 = pD ? *(float*)pD : 0.0f;
        float y0 = pD ? *((float*)pD + 1) : 0.0f;
        wsprintfA(b,
            "[UP] NEW DrawPrimUP fvf=0x%04X pt=%u pc=%u stride=%u vc=%u x0=%d y0=%d correcting=%s",
            g_fvf, pt, pc, stride, vc, (int)x0, (int)y0,
            is_2d ? "YES" : "no");
        log_f(b);
    }

    if (is_2d) {
        UINT  vc    = vcount(pt, pc);
        UINT  bytes = vc * stride;
        BYTE* buf   = get_buf(bytes);
        if (buf) {
            HRESULT hr;
            /* Restore original projection before XYZRHW draw --
             * PSO pre-bakes RHW against the unmodified projection;
             * our near-clip rewrite invalidates those values. */
            if (g_orig_proj_set)
                real_SetTransform(self, D3DTS_PROJECTION, &g_orig_proj);
            CopyMemory(buf, pD, bytes);
            correct_xyzrhw(buf, vc, stride);
            hr = real_DrawPrimitiveUP(self, pt, pc, buf, stride);
            free_buf(buf, bytes);
            /* Re-apply modified projection for subsequent 3D draws */
            if (g_orig_proj_set) {
                D3DMATRIX w2 = g_orig_proj;
                w2.m[0][0] *= g_k_proj;
                {
                    float m22 = g_orig_proj.m[2][2];
                    float m32 = g_orig_proj.m[3][2];
                    if (m22 != 0.0f && m22 != 1.0f) {
                        float far_z    = m22 * (-m32 / m22) / (m22 - 1.0f);
                        float new_near = 2.0f;
                        w2.m[2][2] = far_z / (far_z - new_near);
                        w2.m[3][2] = -new_near * far_z / (far_z - new_near);
                    }
                }
                real_SetTransform(self, D3DTS_PROJECTION, &w2);
            }
            return hr;
        }
    }
    return real_DrawPrimitiveUP(self, pt, pc, pD, stride);
}

/* ---- bar clearing ---- */
#define D3DCLEAR_TARGET 0x00000001
typedef struct { LONG x1, y1, x2, y2; } BarRect;

static void clear_black_bars_now(void* self) {
    D3DVIEWPORT8 saved, full;
    BarRect bars[2];
    HRESULT got_vp;

    if (!real_Clear || !real_SetViewport || g_bar_w == 0 || g_bb_w <= 640)
        return;

    bars[0].x1 = 0;              bars[0].y1 = 0;
    bars[0].x2 = (LONG)g_bar_w;  bars[0].y2 = (LONG)g_bb_h;
    bars[1].x1 = (LONG)(g_bar_w + g_vp_w);
    bars[1].y1 = 0;
    bars[1].x2 = (LONG)g_bb_w;   bars[1].y2 = (LONG)g_bb_h;

    got_vp = real_GetViewport ? real_GetViewport(self, &saved) : -1;

    full.X = 0; full.Y = 0;
    full.Width = g_bb_w; full.Height = g_bb_h;
    full.MinZ = 0.0f; full.MaxZ = 1.0f;
    real_SetViewport(self, &full);
    real_Clear(self, 2, bars, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0);

    if (got_vp == 0)
        real_SetViewport(self, &saved);
}

static HRESULT WINAPI hook_Present(void* self,
    const RECT* src, const RECT* dst, HWND hwnd, const void* dirty)
{
    /* Wall-clock timestamp every 60 frames (~1 sec) for newserv alignment */
    if ((g_frame % 60) == 0)
        log_timestamp("tick");

    /* DIAG: dump VB table summary every 300 frames */
    if (g_frame > 0 && (g_frame % 300) == 0) {
        char b[128];
        int i;
        wsprintfA(b, "[STAT] frame=%u svb_n=%d vp_seen=%d up_seen=%d fvf_seen=%d",
            g_frame, g_svb_n, g_vp_seen_n, g_up_seen_n, g_fvf_seen_n);
        log_line(b);
        for (i = 0; i < g_svb_n; i++) {
            wsprintfA(b, "[STAT]   svb[%d] ptr=%p stride=%u sz=%u locks=%u verts=%u",
                i, g_svb[i].vb, g_svb[i].stride,
                g_svb[i].vb_size, g_svb[i].lock_count, g_svb[i].vert_total);
            log_line(b);
        }
    }

    g_frame++;
    clear_black_bars_now(self);

    /* Flush log buffer -- single file write per frame */
    log_flush();

    return real_Present(self, src, dst, hwnd, dirty);
}

static HRESULT WINAPI hook_Clear(void* self, DWORD count, const void* pRects,
                                  DWORD flags, DWORD color, float z, DWORD stencil) {
    /* MENU_CLEAR_DIAG: pure observation, no behavior change */
    if (g_menu_clear_watch && g_frame <= g_menu_clear_until_frame) {
        D3DVIEWPORT8 real_vp;
        D3DVIEWPORT8* rvp = &real_vp;
        HRESULT gv_hr = -1;
        void* caller = __builtin_return_address(0);

        real_vp.X = real_vp.Y = real_vp.Width = real_vp.Height = 0;
        real_vp.MinZ = 0.0f;
        real_vp.MaxZ = 0.0f;

        if (real_GetViewport) {
            gv_hr = real_GetViewport(self, rvp);
        }

        {
            char mb[512];
            const D3DRECT8* rr = (const D3DRECT8*)pRects;

            if (count && rr) {
                wsprintfA(mb,
                    "[MENU_CLEAR_DIAG] Clear#%u frame=%u caller=%p count=%u rect0={%ld,%ld,%ld,%ld} flags=0x%X color=0x%08X z=%d/1000 stencil=%u pso_vp={%u,%u,%u,%u} real_vp_hr=0x%08X real_vp={%u,%u,%u,%u}",
                    g_menu_clear_count++, g_frame, caller, count,
                    rr[0].x1, rr[0].y1, rr[0].x2, rr[0].y2,
                    (UINT)flags, (UINT)color, (int)(z * 1000.0f), (UINT)stencil,
                    g_pso_vp.X, g_pso_vp.Y, g_pso_vp.Width, g_pso_vp.Height,
                    (UINT)gv_hr, real_vp.X, real_vp.Y, real_vp.Width, real_vp.Height);
            } else {
                wsprintfA(mb,
                    "[MENU_CLEAR_DIAG] Clear#%u frame=%u caller=%p count=%u rect0=<none> flags=0x%X color=0x%08X z=%d/1000 stencil=%u pso_vp={%u,%u,%u,%u} real_vp_hr=0x%08X real_vp={%u,%u,%u,%u}",
                    g_menu_clear_count++, g_frame, caller, count,
                    (UINT)flags, (UINT)color, (int)(z * 1000.0f), (UINT)stencil,
                    g_pso_vp.X, g_pso_vp.Y, g_pso_vp.Width, g_pso_vp.Height,
                    (UINT)gv_hr, real_vp.X, real_vp.Y, real_vp.Width, real_vp.Height);
            }

            log_f(mb);
        }
    } else if (g_menu_clear_watch && g_frame > g_menu_clear_until_frame) {
        log_f("[MENU_CLEAR_DIAG] end watch");
        g_menu_clear_watch = 0;
    }

    /* menu skip-target-clear diagnostic removed: full target clear is required. */
    HRESULT hr = real_Clear(self, count, pRects, flags, color, z, stencil);
    if (hr == 0 && g_bar_w > 0 && (flags & D3DCLEAR_TARGET)) {
        BarRect bars[2];
        bars[0].x1 = 0;               bars[0].y1 = 0;
        bars[0].x2 = (LONG)g_bar_w;   bars[0].y2 = (LONG)g_bb_h;
        bars[1].x1 = (LONG)(g_bar_w + g_vp_w);
        bars[1].y1 = 0;
        bars[1].x2 = (LONG)g_bb_w;   bars[1].y2 = (LONG)g_bb_h;
        real_Clear(self, 2, bars, D3DCLEAR_TARGET, 0xFF000000, z, stencil);
    }
    return hr;
}

/* =========================================================
 * CreateDevice hook
 * ========================================================= */

/*
 * PATCHF with explicit PASS/FAIL logging.
 * Logs address, expected value, actual value, and whether the patch fired.
 */
static void do_patchf(DWORD va, float expected, float val, const char* tag) {
    float* p   = (float*)va;
    float  act = 0.0f;
    float  d;
    char   b[128];
    DWORD  old, tmp;

    if (IsBadReadPtr(p, 4)) {
        wsprintfA(b, "[PATCH] BADPTR addr=0x%08X tag=%s", va, tag);
        log_line(b); return;
    }

    act = *p;
    d   = act - expected; if (d < 0) d = -d;

    if (d < 0.1f) {
        VirtualProtect(p, 4, PAGE_EXECUTE_READWRITE, &old);
        *p = val;
        VirtualProtect(p, 4, old, &tmp);
        wsprintfA(b, "[PATCH] PASS  addr=0x%08X %-20s expect=%d/1000 actual=%d/1000 -> %d/1000",
            va, tag,
            (int)(expected*1000.0f), (int)(act*1000.0f), (int)(val*1000.0f));
    } else {
        wsprintfA(b, "[PATCH] FAIL  addr=0x%08X %-20s expect=%d/1000 actual=%d/1000 (diff=%d/1000)",
            va, tag,
            (int)(expected*1000.0f), (int)(act*1000.0f), (int)(d*1000.0f));
    }
    log_line(b);
}

static HRESULT WINAPI hook_CreateDevice(
    void* self, UINT adapter, DWORD dtype, HWND hwnd, DWORD flags,
    D3DPRESENT_PARAMETERS* params, void** ppDev)
{
    HRESULT hr;
    log_line("hook_CreateDevice called");
    if (params) {
        log_uint("  BackBufferWidth  = ", params->BackBufferWidth);
        log_uint("  BackBufferHeight = ", params->BackBufferHeight);
    }

    if (params && params->BackBufferWidth == 640 && params->BackBufferHeight == 480) {
        char cfg[MAX_PATH] = {0};
        char exe[MAX_PATH] = {0};
        char* p;
        int   tw, th;

        GetModuleFileNameA(NULL, exe, MAX_PATH);
        lstrcpyA(cfg, exe);
        for (p = cfg + lstrlenA(cfg) - 1; p > cfg; p--)
            if (*p == '\\' || *p == '/') { *(p+1) = '\0'; break; }
        lstrcatA(cfg, "widescreen_res.cfg");

        tw = GetPrivateProfileIntA("Resolution", "Width",  640, cfg);
        th = GetPrivateProfileIntA("Resolution", "Height", 480, cfg);

        /* 14:9-clean: force 14:9 viewport mode; no legacy diagnostics. */
        g_vp_mode = 1;

        if (tw < 640)  tw = 640;
        if (tw > 3840) tw = 3840;
        if (th < 480)  th = 480;
        if (th > 2160) th = 2160;

        if (tw != 640 || th != 480) {
            char b[96];
            wsprintfA(b, "  overriding to %dx%d", tw, th);
            log_line(b);
            params->BackBufferWidth  = (UINT)tw;
            params->BackBufferHeight = (UINT)th;
        }
    }

    hr = real_CreateDevice(self, adapter, dtype, hwnd, flags, params, ppDev);
    log_line(hr == 0 ? "CreateDevice succeeded" : "CreateDevice failed");

    if (hr == 0 && ppDev && *ppDev) {
        void** vt = *(void***)(*ppDev);

        if (params && params->BackBufferWidth && params->BackBufferHeight) {
            float bb_ar = (float)params->BackBufferWidth /
                          (float)params->BackBufferHeight;
            g_bb_w       = params->BackBufferWidth;
            g_bb_h       = params->BackBufferHeight;
            g_xrhw_scale = bb_ar / (16.0f / 9.0f);
            g_xrhw_cx    = params->BackBufferWidth * 0.5f;

            if (g_bb_w > 640) {
                /* 14:9-clean: always use centered 14:9 viewport inside native backbuffer. */
                g_vp_mode = 1;
                g_vp_ar   = 14.0f / 9.0f;
                g_vp_w    = (UINT)((float)g_bb_h * g_vp_ar + 0.5f);
                g_bar_w  = (g_bb_w > g_vp_w) ? (g_bb_w - g_vp_w) / 2 : 0;
                g_k_proj = (4.0f / 3.0f) / g_vp_ar;
                g_hud_sy = (float)g_bb_h / 480.0f;
                g_hud_x0 = ((float)g_bb_w - 640.0f * g_hud_sy) * 0.5f;
            } else {
                g_vp_ar  = 16.0f / 9.0f;
                g_vp_w   = g_bb_w;
                g_bar_w  = 0;
                g_k_proj = (4.0f / 3.0f) / (16.0f / 9.0f);
                g_hud_sy = 1.0f;
                g_hud_x0 = 0.0f;
            }

            {
                char b[192];
                wsprintfA(b,
                    "[INIT] bb=%ux%u bar_w=%u vp_w=%u k_proj=%d/10000 hud_x0=%d hud_sy=%d/1000",
                    g_bb_w, g_bb_h, g_bar_w, g_vp_w,
                    (int)(g_k_proj * 10000.0f),
                    (int)g_hud_x0,
                    (int)(g_hud_sy * 1000.0f));
                log_line(b);
            }

            if (g_bb_w != 640 || g_bb_h != 480) {
                float  fw  = (float)g_bb_w, fh = (float)g_bb_h;
                float  fcx = fw * 0.5f,     fcy = fh * 0.5f;

                log_line("[PATCH] 14:9-clean mode active; applying pso.exe patches");
                log_line("[PATCH] --- applying pso.exe patches ---");
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x006517B4, 640.0f, fw,  "Width-A"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00651848, 640.0f, fw,  "Width-B"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00651864, 640.0f, fw,  "Width-C"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x006517B8, 480.0f, fh,  "Height-A"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00671DB8, 480.0f, fh,  "Height-B"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00671DC8, 480.0f, fh,  "Height-C"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x006724B4, 480.0f, fh,  "Height-D"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x006724CC, 480.0f, fh,  "Height-E"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x006724E4, 480.0f, fh,  "Height-F"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x0065020C, 320.0f, fcx, "CentreX-A"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00651AC8, 320.0f, fcx, "CentreX-B"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00651EC4, 320.0f, fcx, "CentreX-C"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00651ECC, 320.0f, fcx, "CentreX-D"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00650208, 240.0f, fcy, "CentreY-A"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00651ACC, 240.0f, fcy, "CentreY-B"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00671DC4, 240.0f, fcy, "CentreY-C"); */
        /* SKIP_SCREEN_CONSTANT_PATCH_TEST: do_patchf(0x00671DD4, 240.0f, fcy, "CentreY-D"); */
                /* Frustum -- DllMain already set to 1.4. Expected is 1.4. */
                log_line("[PATCH] --- done ---");
            }

            /* Frustum -- AR-dependent, applies regardless of internal bb size.
             * For dgvoodoo-stretched modes (bb stays 640x480), this is the only
             * way first-path culling matches g_k_proj and SecondCull. */
            {
                float frustum = (1.0f / g_k_proj) * 1.2f;
                do_patchf(0x0064FA70, 1.4f, 1.8f, "Frustum");
                do_patchf(0x0064FC84, 30.0f, 60.0f, "StaticObjectThreshold");
                /*
                 * Far object/effect distance candidate.
                 * These are immediate float arguments passed to 0x5597F0.
                 * 0x5597F0 calls 0x50FB00 for squared distance, then compares
                 * against the pushed threshold.
                 *
                 * Original value:
                 *   0x461C4000 = 10000.0 = 100^2
                 *
                 * Test value:
                 *   62500.0 = sqrt(62500.0) radius
                 */
                do_patchf(0x005BF5ED, 10000.0f, 62500.0f, "FarDistanceThreshold-A");
                do_patchf(0x005BF77D, 10000.0f, 62500.0f, "FarDistanceThreshold-B");
                do_patchf(0x005BF7F5, 10000.0f, 62500.0f, "FarDistanceThreshold-C");
            }

            /*
             * Second (per-mesh) culling path -- computed from viewport AR.
             * Scaled from calibrated 14:9 values (sin=0.358419, cos=0.933561)
             * using vp_ar / (14/9).  Precomputed for three standard ARs;
             * generic path handles any other AR with the same formula.
             *   14:9  sin=0.358419 cos=0.933561
             *   16:9  sin=0.401797 cos=0.915731
             *   16:10 sin=0.367300 cos=0.930110
             */
            {
                float sc_sin, sc_cos;
                float vp = 14.0f/9.0f;
                /* Check for standard ARs first (no sqrt needed) */
                if (vp >= 14.0f/9.0f - 0.01f && vp <= 14.0f/9.0f + 0.01f) {
                    sc_sin = 0.70f; sc_cos = 0.714f;  /* 14:9 widened cull test */
                } else if (vp >= 16.0f/9.0f - 0.01f && vp <= 16.0f/9.0f + 0.01f) {
                    sc_sin = 0.45f; sc_cos = 0.893f;  /* 16:9 */
                } else if (vp >= 16.0f/10.0f - 0.01f && vp <= 16.0f/10.0f + 0.01f) {
                    sc_sin = 0.367300f; sc_cos = 0.930110f;  /* 16:10 */
                } else {
                    /* Generic: scale tan from 14:9 anchor, approximate 1/sqrt */
                    float t = (0.358419f/0.933561f) * (vp / (14.0f/9.0f));
                    float q = 1.0f + t*t;
                    /* Fast inverse sqrt (Quake III), one Newton step */
                    float x = q * 0.5f;
                    int   i = 0x5F3759DF - (*(int*)&q >> 1);
                    float r = *(float*)&i;
                    r = r * (1.5f - x * r * r);
                    sc_cos = r;
                    sc_sin = t * r;
                }
                do_patchf(0x00441EDF, 0.3f,  sc_sin, "SecondCull-Sin");
                do_patchf(0x00441EE4, 0.96f, sc_cos, "SecondCull-Cos");

                /*
                 * Candidate effect/decal cull path.
                 * 0x0050E5CA builds a stack parameter block with a second
                 * 0.30 / 0.96 pair before calling 0x5036A0. This may affect
                 * alpha/effect objects such as laser bars, door symbols,
                 * Rico message lights, or similar overlay geometry.
                 *
                 * Immediate addresses:
                 *   0x0050E5F8 = 0.30f
                 *   0x0050E5CE = 0.96f
                 */
                do_patchf(0x0050E5F8, 0.3f,  sc_sin, "EffectCullCandidate-Sin");
                do_patchf(0x0050E5CE, 0.96f, sc_cos, "EffectCullCandidate-Cos");
            }
        }

        /*
         * Runtime cone culling override.
         *
         * Overrides runtime cone globals at 0x6F7B4C (cos) and 0x6F7B50 (sin),
         * and NOPs out the 8 instructions that overwrite them during gameplay.
         *
         * Cone globals use locally recomputed SecondCull values.  Do not
         * reuse the sc_sin/sc_cos variables from the SecondCull patch block above;
         * those variables are intentionally block-scoped and are not visible here.
         *
         * EXPERIMENTAL: addresses confirmed via static analysis, but the exact
         * culling test that reads these globals has not been runtime-verified.
         */
        {
            float sc_sin;
            float sc_cos;
            float vp = 14.0f / 9.0f;
            static const DWORD nop_addrs[] = {
                0x005046F2, 0x005046FB,
                0x00504767, 0x00504773,
                0x00503705, 0x0050370E,
                0x00503A2B, 0x00503A34,
            };
            DWORD old;
            int i;

            if (vp >= 14.0f / 9.0f - 0.01f && vp <= 14.0f / 9.0f + 0.01f) {
                sc_sin = 0.70f;
                sc_cos = 0.714f;
            } else if (vp >= 16.0f / 9.0f - 0.01f && vp <= 16.0f / 9.0f + 0.01f) {
                sc_sin = 0.45f;
                sc_cos = 0.893f;
            } else if (vp >= 16.0f / 10.0f - 0.01f && vp <= 16.0f / 10.0f + 0.01f) {
                sc_sin = 0.367300f;
                sc_cos = 0.930110f;
            } else {
                float t = (0.358419f / 0.933561f) * (vp / (14.0f / 9.0f));
                float q = 1.0f + t * t;
                float x = q * 0.5f;
                int bits = 0x5F3759DF - (*(int*)&q >> 1);
                float r = *(float*)&bits;

                r = r * (1.5f - x * r * r);
                sc_cos = r;
                sc_sin = t * r;
            }

            for (i = 0; i < 8; i++) {
                BYTE* p = (BYTE*)nop_addrs[i];

                VirtualProtect(p, 6, PAGE_EXECUTE_READWRITE, &old);
                memset(p, 0x90, 6);
                VirtualProtect(p, 6, old, &old);
            }

            {
                float* p_cos = (float*)0x006F7B4C;
                float* p_sin = (float*)0x006F7B50;

                VirtualProtect(p_cos, 8, PAGE_READWRITE, &old);
                *p_cos = sc_cos;

                *p_sin = sc_sin;
                VirtualProtect(p_cos, 8, old, &old);
            }

            log_line("[PATCH] runtime cone globals widened to sc_cos/sc_sin (8 NOPs + 2 writes)");
        }

        log_line("patching device vtable");
        patch_slot(&vt[23], hook_CreateVertexBuffer,   (void**)&real_CreateVertexBuffer);
        patch_slot(&vt[15], hook_Present,              (void**)&real_Present);
        patch_slot(&vt[36], hook_Clear,                (void**)&real_Clear);
        patch_slot(&vt[37], hook_SetTransform,         (void**)&real_SetTransform);
        patch_slot(&vt[40], hook_SetViewport,          (void**)&real_SetViewport);
        patch_slot(&vt[41], hook_GetViewport,          (void**)&real_GetViewport);
        patch_slot(&vt[50], hook_SetRenderState,       (void**)&real_SetRenderState);
        patch_slot(&vt[70], hook_DrawPrimitive,        (void**)&real_DrawPrimitive);
        patch_slot(&vt[71], hook_DrawIndexedPrimitive, (void**)&real_DrawIndexedPrimitive);
        patch_slot(&vt[72], hook_DrawPrimitiveUP,      (void**)&real_DrawPrimitiveUP);
        patch_slot(&vt[76], hook_SetVertexShader,      (void**)&real_SetVertexShader);
        patch_slot(&vt[83], hook_SetStreamSource,      (void**)&real_SetStreamSource);
        log_line("device vtable patched");
    }
    return hr;
}

/* =========================================================
 * Direct3DCreate8 export
 * ========================================================= */
__declspec(dllexport)
void* WINAPI Direct3DCreate8(UINT SDKVersion) {
    void* obj;
    log_line("Direct3DCreate8 called");
    if (!real_d3d8) {
        log_line("loading D3D8_dgvoodoo.dll");
        real_d3d8 = LoadLibraryA("D3D8_dgvoodoo.dll");
        if (!real_d3d8) { log_line("ERROR: load failed"); return NULL; }
        real_Direct3DCreate8 = (Direct3DCreate8_t)
            GetProcAddress(real_d3d8, "Direct3DCreate8");
        if (!real_Direct3DCreate8) { log_line("ERROR: GetProcAddress failed"); return NULL; }
        log_line("Direct3DCreate8 resolved");
    }
    obj = real_Direct3DCreate8(SDKVersion);
    if (!obj) { log_line("ERROR: real Direct3DCreate8 returned NULL"); return NULL; }
    log_line("real Direct3DCreate8 succeeded");
    if (!hooked) {
        void** vt = *(void***)obj;
        patch_slot(&vt[15], hook_CreateDevice, (void**)&real_CreateDevice);
        hooked = 1;
        log_line("IDirect3D8::CreateDevice hooked at slot 15");
    }
    return obj;
}

/* ---- required exports ---- */
__declspec(dllexport) void WINAPI DebugSetMute(void)         {}
__declspec(dllexport) void WINAPI ValidatePixelShader(void)  {}
__declspec(dllexport) void WINAPI ValidateVertexShader(void) {}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)h; (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        /* Wipe log file on each launch for clean sessions */
        HANDLE lh = CreateFileA("pso-peeps-d3d8-wsh.log",
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (lh != INVALID_HANDLE_VALUE) CloseHandle(lh);

        log_line("d3d8 widescreen proxy pso-peeps-14x9-ui-culling-clean-r2 loaded");
        log_timestamp("startup");
        log_flush();
        {
            char  path[MAX_PATH] = {0};
            char* fname          = path;
            char* p;
            float cur_val;
            GetModuleFileNameA(NULL, path, MAX_PATH);
            for (p = path; *p; p++)
                if (*p == '\\' || *p == '/') fname = p + 1;

            if (lstrcmpiA(fname, "pso.exe") == 0) {
                float* fp = (float*)0x0064FA70;
                DWORD  old;
                float  target = (7.0f / 6.0f) * 1.2f; /* 1.4 */

                /* Log what value is currently at the frustum address */
                cur_val = IsBadReadPtr(fp, 4) ? -1.0f : *fp;
                {
                    char b[128];
                    wsprintfA(b,
                        "[DLLMAIN] frustum addr=0x0064FA70 current=%d/1000 -> setting %d/1000",
                        (int)(cur_val * 1000.0f), (int)(target * 1000.0f));
                    log_line(b);
                }

                VirtualProtect(fp, sizeof(float), PAGE_EXECUTE_READWRITE, &old);
                *fp = target;
                VirtualProtect(fp, sizeof(float), old, &old);
                log_line("[DLLMAIN] 14:9-clean build; frustum pre-set for 14:9");
            } else {
                log_line("[DLLMAIN] skipping patches (not pso.exe)");
            }
        }
    }
    return TRUE;
}
