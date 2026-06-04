/*
 * d3d8_widescreen.c  —  PSO BB widescreen 3D projection fix
 *
 * Open-source redistributable chain:
 *
 *   psobb.exe
 *     -> D3D8.dll        (this — projection fix only)
 *     -> d3d8_impl.dll   (crosire/d3d8to9, MIT — D3D8 -> D3D9)
 *     -> D3D9.dll        (DXVK, zlib — D3D9 -> Vulkan)
 *     -> GPU
 *
 * Setup:
 *   1. Download crosire/d3d8to9 release, rename d3d8.dll  -> d3d8_impl.dll
 *   2. Download DXVK release,            rename d3d9.dll  -> D3D9.dll
 *   3. Place this compiled D3D8.dll in game directory
 *
 * What we fix:
 *   The game always submits a 4:3 perspective matrix regardless of the actual
 *   backbuffer aspect ratio.  We intercept SetTransform(D3DTS_PROJECTION) for
 *   perspective matrices and scale m[0][0] by:
 *
 *       scale_x = (4/3) / (backbuffer_w / backbuffer_h)
 *                = (4 * bh) / (3 * bw)
 *
 *   For any 16:9 resolution this is always exactly 0.75.
 *
 * What we do NOT touch:
 *   2D / XYZRHW elements.  The HUD is authored for widescreen by the client
 *   and must not be rescaled.  Orthographic projection matrices (m[3][3]==1)
 *   are passed through unmodified.
 *
 * Build:
 *   i686-w64-mingw32-gcc -shared -O2 -o D3D8.dll d3d8_widescreen.c d3d8.def \
 *       -lkernel32 -Wl,--enable-stdcall-fixup
 */

#include <windows.h>

/* ── minimal D3D8 types ─────────────────────────────────────────────────── */

typedef struct { float m[4][4]; } D3DMATRIX;
#define D3DTS_PROJECTION 3

/* First two DWORDs of D3DPRESENT_PARAMETERS */
#define PP_WIDTH_OFF  0
#define PP_HEIGHT_OFF 4

/* ── state ──────────────────────────────────────────────────────────────── */

static HMODULE g_impl    = NULL;
static float   g_scale_x = 0.75f;
static int     g_hooked  = 0;    /* vtable patched flag — only patch once */

static char g_log[MAX_PATH];
static int  g_log_ok = 0;

/* ── function pointer types ─────────────────────────────────────────────── */

typedef void*   (WINAPI    *pfn_D3DCreate8)  (UINT);
typedef HRESULT (__stdcall *pfn_CreateDevice)(void*,UINT,int,HWND,DWORD,void*,void**);
typedef HRESULT (__stdcall *pfn_Reset)       (void*,void*);
typedef HRESULT (__stdcall *pfn_SetTransform)(void*,DWORD,const D3DMATRIX*);

static pfn_D3DCreate8   real_D3DCreate8   = NULL;
static pfn_CreateDevice real_CreateDevice = NULL;
static pfn_Reset        real_Reset        = NULL;
static pfn_SetTransform real_SetTransform = NULL;

/* ── logging ────────────────────────────────────────────────────────────── */

static void log_line(const char* s) {
    if (!g_log_ok) return;
    HANDLE f = CreateFileA(g_log, FILE_APPEND_DATA,
                           FILE_SHARE_READ|FILE_SHARE_WRITE,
                           NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, s, lstrlenA(s), &w, NULL);
    WriteFile(f, "\r\n", 2, &w, NULL);
    CloseHandle(f);
}

static void log_fmt(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    wvsprintfA(buf, fmt, ap);
    va_end(ap);
    log_line(buf);
}

/* ── vtable slot patcher ────────────────────────────────────────────────── */

static void* vtpatch(void* obj, int slot, void* hook) {
    void** vt = *(void***)obj;
    void*  old = vt[slot];
    DWORD prot;
    if (VirtualProtect(&vt[slot], sizeof(void*), PAGE_READWRITE, &prot)) {
        vt[slot] = hook;
        VirtualProtect(&vt[slot], sizeof(void*), prot, &prot);
    }
    return old;
}

/* ── scale computation ──────────────────────────────────────────────────── */

static void compute_scale(UINT bw, UINT bh) {
    if (!bw || !bh) return;
    g_scale_x = (4.0f * (float)bh) / (3.0f * (float)bw);
    /* wvsprintfA doesn't support %f — log numerator/denominator instead */
    UINT num = 4 * bh;
    UINT den = 3 * bw;
    log_fmt("backbuffer %ux%u  scale_x=%u/%u", bw, bh, num, den);
}

/* ── hook: SetTransform (slot 37) ───────────────────────────────────────── */

static HRESULT __stdcall hook_SetTransform(void* self, DWORD state,
                                           const D3DMATRIX* mtx) {
    if (state == D3DTS_PROJECTION && mtx && mtx->m[3][3] == 0.0f) {
        D3DMATRIX fixed = *mtx;
        fixed.m[0][0] *= g_scale_x;
        return real_SetTransform(self, state, &fixed);
    }
    return real_SetTransform(self, state, mtx);
}

/* ── hook: Reset (slot 14) ──────────────────────────────────────────────── */

static HRESULT __stdcall hook_Reset(void* self, void* pp) {
    UINT bw = *(UINT*)((BYTE*)pp + PP_WIDTH_OFF);
    UINT bh = *(UINT*)((BYTE*)pp + PP_HEIGHT_OFF);
    if (bw && bh) {
        log_line("Reset: recomputing scale");
        compute_scale(bw, bh);
    }
    return real_Reset(self, pp);
}

/* ── hook: CreateDevice (slot 15 on IDirect3D8) ─────────────────────────── */

static HRESULT __stdcall hook_CreateDevice(void* self, UINT adapter, int type,
                                           HWND hwnd, DWORD flags,
                                           void* pp, void** out) {
    UINT bw = *(UINT*)((BYTE*)pp + PP_WIDTH_OFF);
    UINT bh = *(UINT*)((BYTE*)pp + PP_HEIGHT_OFF);
    compute_scale(bw, bh);

    HRESULT hr = real_CreateDevice(self, adapter, type, hwnd, flags, pp, out);
    if (FAILED(hr) || !out || !*out) {
        log_fmt("CreateDevice failed hr=0x%08X", (unsigned)hr);
        return hr;
    }

    void* dev = *out;

    /* Fallback: if backbuffer dims were 0, query from the live surface */
    if (!bw || !bh) {
        typedef HRESULT (__stdcall *pfn_GetBB)(void*,UINT,int,void**);
        typedef HRESULT (__stdcall *pfn_Desc) (void*,void*);
        typedef ULONG   (__stdcall *pfn_Rel)  (void*);
        void** dv = *(void***)dev;
        void* surf = NULL;
        if (SUCCEEDED(((pfn_GetBB)dv[16])(dev, 0, 0, &surf)) && surf) {
            DWORD desc[8] = {0};
            if (SUCCEEDED(((pfn_Desc)(*(void***)surf)[7])(surf, desc)))
                compute_scale(desc[5], desc[6]); /* Width@20, Height@24 */
            ((pfn_Rel)(*(void***)surf)[2])(surf);
        }
    }

    /* Only patch the vtable once — d3d8to9 may call CreateDevice twice and
     * both devices share the same vtable class.  Patching a second time would
     * store the already-hooked pointer as real_*, causing infinite recursion. */
    if (!g_hooked) {
        real_Reset        = vtpatch(dev, 14, hook_Reset);
        real_SetTransform = vtpatch(dev, 37, hook_SetTransform);
        g_hooked = 1;
        log_line("hooks: Reset(14) SetTransform(37)");
    } else {
        log_line("vtable already patched — skipping");
    }
    return hr;
}

/* ── Direct3DCreate8 (our sole export) ──────────────────────────────────── */

__declspec(dllexport) void* WINAPI Direct3DCreate8(UINT sdk_ver) {
    if (!g_impl) {
        g_impl = LoadLibraryA("d3d8_impl.dll");
        if (!g_impl) {
            log_line("FATAL: d3d8_impl.dll not found");
            return NULL;
        }
        real_D3DCreate8 = (pfn_D3DCreate8)GetProcAddress(g_impl, "Direct3DCreate8");
        if (!real_D3DCreate8) {
            log_line("FATAL: Direct3DCreate8 missing from d3d8_impl.dll");
            return NULL;
        }
        log_line("d3d8_impl.dll loaded");
    }

    void* d3d8 = real_D3DCreate8(sdk_ver);
    if (!d3d8) { log_line("real Direct3DCreate8 returned NULL"); return NULL; }

    real_CreateDevice = vtpatch(d3d8, 15, hook_CreateDevice);
    log_line("CreateDevice hooked");
    return d3d8;
}

/* ── DllMain ─────────────────────────────────────────────────────────────── */

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID reserved) {
    (void)reserved;
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    DisableThreadLibraryCalls(hinst);

    char dir[MAX_PATH];
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    for (int i = lstrlenA(dir)-1; i >= 0; i--)
        if (dir[i]=='\\' || dir[i]=='/') { dir[i+1]='\0'; break; }

    lstrcpyA(g_log, dir);
    lstrcatA(g_log, "widescreen_fix.log");

    HANDLE f = CreateFileA(g_log, GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    g_log_ok = 1;
    log_line("=== widescreen_fix.dll loaded ===");
    return TRUE;
}
