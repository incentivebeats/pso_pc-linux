#include <windows.h>

/*
 * psopeeps.c  -  pso-peeps-r5
 *
 * PSO PC V2 proxy DLL.
 * Loaded by D3D8.dll (d3d8_loader) as psopeeps.dll.
 *
 * Config: pso.cfg in game directory.
 *
 * Build:
 *   i686-w64-mingw32-gcc -shared -O2 -o psopeeps.dll psopeeps.c d3d8.def
 *     -lkernel32 -Wl,--enable-stdcall-fixup
 */

typedef struct {
    UINT  BackBufferWidth, BackBufferHeight;
    DWORD BackBufferFormat, BackBufferCount, MultiSampleType, SwapEffect;
    HWND  hDeviceWindow;
    BOOL  Windowed, EnableAutoDepthStencil;
    DWORD AutoDepthStencilFormat, Flags;
    UINT  FullScreen_RefreshRateInHz, FullScreen_PresentationInterval;
} D3DPRESENT_PARAMETERS;

typedef void*   (WINAPI *Direct3DCreate8_t)(UINT);
typedef HRESULT (WINAPI *CreateDevice_t)(void*, UINT, DWORD, HWND, DWORD,
                                          D3DPRESENT_PARAMETERS*, void**);
typedef HRESULT (WINAPI *Present_t)(void*, const RECT*, const RECT*, HWND, const void*);

static HMODULE           real_d3d8;
static Direct3DCreate8_t real_Direct3DCreate8;
static CreateDevice_t    real_CreateDevice;
static Present_t         real_Present;
static int               hooked = 0;
static int               g_frame = 0;

/* =========================================================
 * Logging
 * ========================================================= */
#define LOG_BUF_CAP 65536
static char g_log_buf[LOG_BUF_CAP];
static int  g_log_pos = 0;

static void log_flush(void) {
    if (g_log_pos <= 0) return;
    HANDLE h = CreateFileA("pso-peeps.log",
        FILE_APPEND_DATA, FILE_SHARE_READ|FILE_SHARE_WRITE,
        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, g_log_buf, (DWORD)g_log_pos, &w, NULL);
        CloseHandle(h);
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

static void log_uint(const char* pfx, UINT v) {
    char b[128]; wsprintfA(b, "%s%u", pfx, v); log_line(b);
}

static void log_timestamp(const char* tag) {
    SYSTEMTIME utc, loc;
    GetSystemTime(&utc); GetLocalTime(&loc);
    char b[192];
    wsprintfA(b, "[TIME] %-16s F%05u UTC=%02u:%02u:%02u.%03u LOCAL=%02u:%02u:%02u.%03u",
        tag, g_frame,
        utc.wHour, utc.wMinute, utc.wSecond, utc.wMilliseconds,
        loc.wHour, loc.wMinute, loc.wSecond, loc.wMilliseconds);
    log_line(b);
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

static HRESULT WINAPI hook_Present(void* self,
    const RECT* src, const RECT* dst, HWND hwnd, const void* dirty)
{
    if ((g_frame % 60) == 0) log_timestamp("tick");
    g_frame++;
    log_flush();
    return real_Present(self, src, dst, hwnd, dirty);
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
        int tw, th;

        GetModuleFileNameA(NULL, exe, MAX_PATH);
        lstrcpyA(cfg, exe);
        for (p = cfg + lstrlenA(cfg) - 1; p > cfg; p--)
            if (*p == '\\' || *p == '/') { *(p+1) = '\0'; break; }
        lstrcatA(cfg, "pso.cfg");

        tw = GetPrivateProfileIntA("Resolution", "Width",  640, cfg);
        th = GetPrivateProfileIntA("Resolution", "Height", 480, cfg);

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

        if (params->EnableAutoDepthStencil) {
            log_uint("  AutoDepthStencilFormat was ", params->AutoDepthStencilFormat);
            params->AutoDepthStencilFormat = 80; /* D3DFMT_D32 */
            log_line("  AutoDepthStencilFormat -> D3DFMT_D32 (80)");
        }
    }

    hr = real_CreateDevice(self, adapter, dtype, hwnd, flags, params, ppDev);
    log_line(hr == 0 ? "CreateDevice succeeded" : "CreateDevice failed");

    if (hr == 0 && ppDev && *ppDev) {
        void** vt = *(void***)(*ppDev);
        patch_slot(&vt[15], hook_Present, (void**)&real_Present);
        log_line("device vtable patched");
    }
    return hr;
}

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
        log_line("IDirect3D8::CreateDevice hooked");
    }
    return obj;
}

__declspec(dllexport) void WINAPI DebugSetMute(void)         {}
__declspec(dllexport) void WINAPI ValidatePixelShader(void)  {}
__declspec(dllexport) void WINAPI ValidateVertexShader(void) {}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)h; (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        HANDLE lh = CreateFileA("pso-peeps.log",
            GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (lh != INVALID_HANDLE_VALUE) CloseHandle(lh);
        log_line("pso-peeps proxy r5 loaded");
        log_timestamp("startup");
        log_flush();
    }
    return TRUE;
}
