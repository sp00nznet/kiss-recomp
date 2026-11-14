/*
 * KISS Psycho Circus: The Nightmare Child (1.13) - static recompilation host.
 *
 * A 32-bit host on pcrecomp's runtime/native32 (the native bridge, callbacks
 * and machine lock; see its header), with the loader from the UT99 recomp.
 * LithTech 2 is an engine exe (client.exe) and a set of DLLs, every one lifted:
 * the renderer (d3d.ren), the server (server.dll), the game code (cshell.dll,
 * cres.dll, object.lto, sres.dll) and Third Law's CD audio (cdaudio.dll).
 * Each is mapped at the base tools/rebase.py gave it, bound, and attached
 * (its DllMain run) the first time the engine loads it, the way Windows would.
 *
 * The engine copies the game DLLs to GetTempFileName files and LoadLibrary's
 * those, so a load is matched by name first, then by the PE timestamp in the
 * file it names.
 *
 * Linked at /BASE:0x60000000 (CMakeLists.txt) so 0x01CA0000 (client.exe),
 * 0x10000000..0x10718000 and 0x20000000..0x201EB000 are free.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "native32.h"
#include "recomp_trace.h"
#include "replay/replay.h"

/* ---- the guest modules: same bases as tools/rebase.py ------------------ */
typedef struct {
    const char* name;
    uint32_t    base;
    int         attached;
    uint32_t    stamp;      /* PE TimeDateStamp, read once mapped */
    uint32_t    span;       /* bytes mapped */
} module_t;

static module_t g_modules[] = {
    { "client.exe",  0x01CA0000u },
    { "server.dll",  0x10000000u },
    { "d3d.ren",     0x10100000u },
    { "cres.dll",    0x10300000u },
    { "object.lto",  0x10400000u },
    { "sres.dll",    0x10600000u },
    { "cdaudio.dll", 0x10700000u },
    { "cshell.dll",  0x20000000u },
};
#define NMODULES ((int)(sizeof g_modules / sizeof g_modules[0]))

static module_t* g_exe = &g_modules[0];
static char g_game_dir[MAX_PATH];
static char g_guest_cmdline[2048];
static DWORD g_watchdog_s;

#define ARG(n) MEM32(g_esp + 4 + 4 * (n))

static IMAGE_NT_HEADERS32* nt_of(uint32_t base) {
    uint8_t* b = (uint8_t*)(uintptr_t)base;
    return (IMAGE_NT_HEADERS32*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
}

static module_t* module_by_base(uint32_t h) {
    for (int i = 0; i < NMODULES; i++)
        if (g_modules[i].base == h) return &g_modules[i];
    return NULL;
}

static module_t* module_by_name(const char* path) {
    const char* p = path;
    for (const char* s = path; *s; s++)
        if (*s == '\\' || *s == '/' || *s == ':') p = s + 1;
    for (int i = 0; i < NMODULES; i++)
        if (!_stricmp(p, g_modules[i].name)) return &g_modules[i];
    return NULL;
}

/* A temp copy of a rez DLL: the file's PE timestamp names the module. A
 * different build of the same DLL (another rez won) is reported and runs
 * nothing: its code is not what was lifted. */
static module_t* module_by_file(const char* path) {
    uint8_t hdr[4096];
    DWORD got = 0;
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    ReadFile(h, hdr, sizeof hdr, &got, NULL);
    CloseHandle(h);
    if (got < 0x200 || hdr[0] != 'M' || hdr[1] != 'Z') return NULL;
    uint32_t lfa = *(uint32_t*)(hdr + 0x3C);
    if (lfa + 0x100 > got) return NULL;
    uint32_t stamp = *(uint32_t*)(hdr + lfa + 8);
    for (int i = 0; i < NMODULES; i++)
        if (g_modules[i].stamp == stamp) return &g_modules[i];
    fprintf(stderr, "[load] %s: a PE we did not lift (stamp 0x%08X)\n", path, stamp);
    return NULL;
}

static int attach(module_t* m) {
    if (m->attached) return 1;
    m->attached = 1;
    uint8_t* b = (uint8_t*)(uintptr_t)m->base;
    IMAGE_NT_HEADERS32* nt = nt_of(m->base);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + dd.VirtualAddress);
         dd.VirtualAddress && d->Name; d++) {
        module_t* dep = module_by_name((const char*)(b + d->Name));
        if (dep && !attach(dep)) return 0;
    }
    if (nt->FileHeader.Characteristics & IMAGE_FILE_DLL && nt->OptionalHeader.AddressOfEntryPoint) {
        uint32_t args[3] = { m->base, DLL_PROCESS_ATTACH, 0 };
        fprintf(stderr, "[load] %s: DllMain\n", m->name);
        native32_call_guest(m->base + nt->OptionalHeader.AddressOfEntryPoint, 3, args);
        if (!g_eax) { fprintf(stderr, "[load] %s: DllMain failed\n", m->name); return 0; }
    }
    return 1;
}

/* ---- the loader's shims (stdcall, arguments from g_esp) ----------------- */

/* Times written L<S> count from the first load of the level's game code
 * (object.lto), not from start-up: menus drift with machine load. */
static volatile DWORD g_level_t0;
#define LEVEL_TIME 0x80000000u
static DWORD due_at(DWORD t0, DWORD at) {
    if (!(at & LEVEL_TIME)) return t0 + at;
    while (!g_level_t0) Sleep(100);
    return g_level_t0 + (at & ~LEVEL_TIME);
}
static DWORD parse_time(const char* s) {
    return *s == 'L' ? LEVEL_TIME | (DWORD)(atof(s + 1) * 1000) : (DWORD)(atof(s) * 1000);
}

static void shim_LoadLibraryA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    module_t* m = module_by_name(name);
    if (!m) m = module_by_file(name);
    if (m) {
        fprintf(stderr, "[load] %s -> %s\n", name, m->name);
        if (!g_level_t0 && !_stricmp(m->name, "object.lto")) g_level_t0 = GetTickCount();
        g_eax = attach(m) ? m->base : 0;
    } else {
        g_eax = (uint32_t)(uintptr_t)LoadLibraryA(name);
        fprintf(stderr, "[load] %s native -> %08X (from sub_%08X)\n", name, g_eax, g_cur_func);
    }
    g_esp += 4 + 1 * 4;
}

static void shim_FreeLibrary(void) {
    g_eax = module_by_base(ARG(0)) ? TRUE : FreeLibrary((HMODULE)(uintptr_t)ARG(0));
    g_esp += 4 + 1 * 4;
}

static void shim_GetProcAddress(void) {
    uint32_t h = ARG(0);
    const char* name = (const char*)(uintptr_t)ARG(1);
    g_eax = module_by_base(h) ? native32_export(h, name)
                              : (uint32_t)(uintptr_t)GetProcAddress((HMODULE)(uintptr_t)h, name);
    if (module_by_base(h) && !g_eax && (uintptr_t)name > 0xFFFF)
        fprintf(stderr, "[load] GetProcAddress(%s, %s): no such export\n", module_by_base(h)->name, name);
    g_esp += 4 + 2 * 4;
}

static void shim_GetModuleHandleA(void) {
    const char* name = (const char*)(uintptr_t)ARG(0);
    module_t* m = name ? module_by_name(name) : g_exe;
    g_eax = m ? m->base : (uint32_t)(uintptr_t)GetModuleHandleA(name);
    g_esp += 4 + 1 * 4;
}

static void shim_GetModuleFileNameA(void) {
    char* out = (char*)(uintptr_t)ARG(1);
    uint32_t size = ARG(2);
    module_t* m = ARG(0) ? module_by_base(ARG(0)) : g_exe;
    if (m) {
        char path[MAX_PATH];
        _snprintf(path, sizeof path - 1, "%s\\%s", g_game_dir, m->name);
        path[MAX_PATH - 1] = 0;
        uint32_t len = (uint32_t)strlen(path);
        if (size) {
            uint32_t k = len < size ? len : size - 1;
            memcpy(out, path, k);
            out[k] = 0;
            len = k;
        }
        g_eax = len;
    } else {
        g_eax = GetModuleFileNameA((HMODULE)(uintptr_t)ARG(0), out, size);
    }
    g_esp += 4 + 3 * 4;
}

static void shim_GetCommandLineA(void) { g_eax = (uint32_t)(uintptr_t)g_guest_cmdline; g_esp += 4; }

static void shim_MessageBoxA(void) {
    fprintf(stderr, "[messagebox] %s: %s\n", (char*)(uintptr_t)ARG(2), (char*)(uintptr_t)ARG(1));
    g_eax = IDOK;
    g_esp += 4 + 4 * 4;
}

/* Miles' AIL_startup, called from the lifted engine, faults inside Windows
 * on a wild read two runs in three; called once from the host's own stack
 * before the guest starts it never has. So it runs here, early, and the
 * engine's call only reports that Miles is up. */
static int g_early_miles;        /* --early-miles */
static FARPROC g_ail_startup;
static void shim_AIL_startup(void) {
    if (!g_early_miles && g_ail_startup) ((void (WINAPI*)(void))g_ail_startup)();
    g_esp += 4;
}

/* --heap-check: every guest heap call validates the heap first, so a
 * corruption is caught at the first heap call after it, with the lifted
 * function that made that call and the ones before it. */
static int g_heap_check;
static const char* g_native[8];   /* --native MODULE: run its original code (bisecting) */
static int g_nnative, g_exe_native;
static uint32_t g_last_heap_func[4];
static void heap_check(const char* what) {
    HANDLE h = (HANDLE)(uintptr_t)ARG(0), all[64];
    DWORD n = g_heap_check ? GetProcessHeaps(64, all) : 0;
    for (DWORD k = 0; k < n && k < 64; k++)
        if (!HeapValidate(all[k], 0, NULL)) { h = all[k]; n = 0xFFFFFFFF; break; }
    if (n == 0xFFFFFFFF) {
        fprintf(stderr, "[heap] %s(%p) from sub_%08X: heap corrupt; earlier heap calls from sub_%08X sub_%08X sub_%08X\n",
                what, h, g_cur_func, g_last_heap_func[0], g_last_heap_func[1], g_last_heap_func[2]);
        native32_dump_icalls(16);
        TerminateProcess(GetCurrentProcess(), 5);
    }
    memmove(g_last_heap_func + 1, g_last_heap_func, 3 * sizeof g_last_heap_func[0]);
    g_last_heap_func[0] = g_cur_func;
}
static void shim_HeapAlloc(void) {
    heap_check("HeapAlloc");
    g_eax = (uint32_t)(uintptr_t)HeapAlloc((HANDLE)(uintptr_t)ARG(0), ARG(1), ARG(2));
    g_esp += 4 + 3 * 4;
}
static void shim_HeapFree(void) {
    heap_check("HeapFree");
    g_eax = HeapFree((HANDLE)(uintptr_t)ARG(0), ARG(1), (void*)(uintptr_t)ARG(2));
    g_esp += 4 + 3 * 4;
}
static void shim_HeapReAlloc(void) {
    heap_check("HeapReAlloc");
    g_eax = (uint32_t)(uintptr_t)HeapReAlloc((HANDLE)(uintptr_t)ARG(0), ARG(1), (void*)(uintptr_t)ARG(2), ARG(3));
    g_esp += 4 + 4 * 4;
}

/* Under --record/--replay/--keys f...: the guest's clock is virtual. Every
 * read of it moves it 1 ms, so the engine sees the same times after the same
 * work on any machine and at any load (after Exhume: record at the program's
 * own clock). Native DLLs (Smacker's movies) and Sleep keep the real one: a
 * virtual Sleep hung start-up at CreateWindowExA.
 * ponytail: a read counter, so threads that read the clock make it as
 * deterministic as their interleaving; per-thread clocks if that bites. */
static int g_vclock;
static volatile LONG64 g_vms = 1000;
static uint32_t vclock_read(LONG64 step) { return (uint32_t)InterlockedAdd64(&g_vms, step); }
static void shim_timeGetTime(void) {
    g_eax = g_vclock ? vclock_read(1) : timeGetTime();
    g_esp += 4;
}
static void shim_GetTickCount(void) {
    g_eax = g_vclock ? vclock_read(1) : GetTickCount();
    g_esp += 4;
}
static void shim_QueryPerformanceCounter(void) {
    LARGE_INTEGER* t = (LARGE_INTEGER*)(uintptr_t)ARG(0);
    if (g_vclock) t->QuadPart = (LONGLONG)vclock_read(1) * 1000;   /* 1 MHz */
    else QueryPerformanceCounter(t);
    g_eax = 1;
    g_esp += 4 + 4;
}
static void shim_QueryPerformanceFrequency(void) {
    LARGE_INTEGER* f = (LARGE_INTEGER*)(uintptr_t)ARG(0);
    if (g_vclock) f->QuadPart = 1000000;
    else QueryPerformanceFrequency(f);
    g_eax = 1;
    g_esp += 4 + 4;
}

static native32_shim_t g_shims[] = {
    { "timeGetTime", shim_timeGetTime },
    { "GetTickCount", shim_GetTickCount },
    { "QueryPerformanceCounter", shim_QueryPerformanceCounter },
    { "QueryPerformanceFrequency", shim_QueryPerformanceFrequency },
    { "HeapAlloc", shim_HeapAlloc },
    { "HeapFree", shim_HeapFree },
    { "HeapReAlloc", shim_HeapReAlloc },
    { "_AIL_startup@0", shim_AIL_startup },
    { "LoadLibraryA", shim_LoadLibraryA },
    { "FreeLibrary", shim_FreeLibrary },
    { "GetProcAddress", shim_GetProcAddress },
    { "GetModuleHandleA", shim_GetModuleHandleA },
    { "GetModuleFileNameA", shim_GetModuleFileNameA },
    { "GetCommandLineA", shim_GetCommandLineA },
    { "MessageBoxA", shim_MessageBoxA },
};

/* ---- diagnostics ---------------------------------------------------------- */


void recomp_not_lifted(uint32_t va) {
    fprintf(stderr, "\n[not-lifted] sub_%08X  (called from 0x%08X)\n", va, g_cur_func);
    recomp_dump_trace("not-lifted");
    native32_dump_icalls(8);
    fflush(stderr);
    TerminateProcess(GetCurrentProcess(), 2);
}

static const char* module_of(uint32_t va) {
    for (int i = 0; i < NMODULES; i++) {
        if (va >= g_modules[i].base && va - g_modules[i].base < g_modules[i].span)
            return g_modules[i].name;
    }
    return "";
}

static char g_crash_buf[4096];
static int g_crash_len;
static void crash_emit(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = _vsnprintf(g_crash_buf + g_crash_len, sizeof g_crash_buf - 1 - g_crash_len, fmt, ap);
    va_end(ap);
    if (n > 0) g_crash_len += n;
}

static LONG CALLBACK crash(EXCEPTION_POINTERS* ep) {
    static volatile LONG once;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) != 0xC0000000u) return EXCEPTION_CONTINUE_SEARCH;
    {   /* A fault inside a Windows DLL is its own business first: ddraw,
         * dinput and friends probe with __try. Only the host (lifted code)
         * and code outside any module (the guest, heap code) are reported. */
        HMODULE hm;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCSTR)r->ExceptionAddress, &hm) && hm != GetModuleHandleA(NULL) &&
            r->ExceptionCode != 0xC0000374u)
            return EXCEPTION_CONTINUE_SEARCH;
    }
    static volatile DWORD reporter;
    {   /* first, with next to no stack: the rest may not get that far */
        static char line[96];
        DWORD w2;
        int n2 = _snprintf(line, sizeof line - 1, "\n[fault] 0x%08lX at %p, thread %lu\n", r->ExceptionCode,
                           r->ExceptionAddress, GetCurrentThreadId());
        if (n2 > 0) WriteFile(GetStdHandle(STD_ERROR_HANDLE), line, (DWORD)n2, &w2, NULL);
    }
    if (InterlockedExchange(&once, 1)) {   /* the first report finishes and exits */
        if (reporter != GetCurrentThreadId()) Sleep(5000);
        TerminateProcess(GetCurrentProcess(), 3);
    }
    reporter = GetCurrentThreadId();
    crash_emit("\n=== fault 0x%08lX at 0x%p, thread %lu ===\n", r->ExceptionCode, r->ExceptionAddress,
               GetCurrentThreadId());
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2) {
        ULONG_PTR op = r->ExceptionInformation[0];
        uint32_t at = (uint32_t)r->ExceptionInformation[1];
        crash_emit("  %s of 0x%08X %s\n", op == 0 ? "read" : op == 1 ? "write" : "execute", at, module_of(at));
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery((void*)(uintptr_t)at, &mbi, sizeof mbi))
            crash_emit("  that page: base %p alloc %p size %lX state %lX protect %lX type %lX\n", mbi.BaseAddress,
                       mbi.AllocationBase, (unsigned long)mbi.RegionSize, mbi.State, mbi.Protect, mbi.Type);
    }
    crash_emit("  in lifted sub_%08X (%s), last native call %s\n", g_cur_func, module_of(g_cur_func),
               g_cur_import);
    crash_emit("  eax=%08X ecx=%08X edx=%08X ebx=%08X esp=%08X ebp=%08X esi=%08X edi=%08X\n",
               g_eax, g_ecx, g_edx, g_ebx, g_esp, g_ebp, g_esi, g_edi);
    {   /* the native stack: words that point into a loaded module */
        uint32_t* sp = (uint32_t*)(uintptr_t)ep->ContextRecord->Esp;
        crash_emit("native stack from esp=%08lX:\n", ep->ContextRecord->Esp);
        for (int k = 0, shown = 0; k < 512 && shown < 16; k++) {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(sp + k, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) break;
            HMODULE hm;
            char nm[MAX_PATH];
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)(uintptr_t)sp[k], &hm) && GetModuleFileNameA(hm, nm, sizeof nm)) {
                const char* b = strrchr(nm, '\\');
                crash_emit("  [esp+%03X] %08X %s+%X\n", k * 4, sp[k], b ? b + 1 : nm, sp[k] - (uint32_t)(uintptr_t)hm);
                shown++;
            }
        }
    }
    crash_emit("last indirect calls (newest first):\n");
    for (int i = 1; i <= 12 && i <= (int)g_icall_trace_idx; i++) {
        uint32_t k = (g_icall_trace_idx - i) & (ICALL_TRACE_SIZE - 1);
        const char* nm = native32_name(g_icall_trace[k]);
        crash_emit("  0x%08X  from 0x%08X  %s%s\n", g_icall_trace[k], g_icall_from[k], nm ? nm : "",
                   nm ? "" : module_of(g_icall_trace[k]));
    }
    DWORD w;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), g_crash_buf, (DWORD)g_crash_len, &w, NULL);
    recomp_trace_flush();
    TerminateProcess(GetCurrentProcess(), 3);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- --shot S FILE: the game window, from the screen, as a .bmp ----------- */
static DWORD g_shot_s[8];
static char g_shot_file[8][MAX_PATH];
static int g_shots;

static BOOL CALLBACK find_window(HWND w, LPARAM out) {
    DWORD pid;
    GetWindowThreadProcessId(w, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(w)) { *(HWND*)out = w; return FALSE; }
    return TRUE;
}

static void shot(const char* file) {
    HWND w = NULL;
    RECT rc;
    EnumWindows(find_window, (LPARAM)&w);
    if (!w || !GetClientRect(w, &rc)) { printf("[shot] no window\n"); return; }
    POINT o = { 0, 0 };
    ClientToScreen(w, &o);
    RECT on_screen = { o.x, o.y, o.x + rc.right, o.y + rc.bottom };
    int cw = rc.right, ch = rc.bottom;
    HDC scr = GetDC(NULL), mem = CreateCompatibleDC(scr);
    BITMAPINFO bi = { { sizeof(BITMAPINFOHEADER), cw, ch, 1, 24, BI_RGB } };
    void* bits;
    HBITMAP bmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    SelectObject(mem, bmp);
    /* From the screen, the window brought to the front first: DirectDraw's
     * primary-surface blits are on the screen, not in the window's DC. */
    SetForegroundWindow(w);
    BringWindowToTop(w);
    Sleep(300);
    BitBlt(mem, 0, 0, cw, ch, scr, o.x, o.y, SRCCOPY);
    GdiFlush();
    DWORD stride = (cw * 3 + 3) & ~3u, size = stride * ch;
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bi.bmiHeader + size, 0, 0, sizeof fh + sizeof bi.bmiHeader };
    FILE* f = fopen(file, "wb");
    if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(bits, size, 1, f);
        fclose(f);
        printf("[shot] %dx%d -> %s\n", cw, ch, file);
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(NULL, scr);
}

/* d3d.ren's back buffer (an IDirectDrawSurface4/7 it keeps at this VA),
 * locked and written as a .bmp: on the private desktop of --headless, and
 * often on a real one too, a GDI copy of the screen is black. The shot is
 * taken in d3d.ren's present, which the engine calls through the renderer's
 * function table (+0xA8), so the host can wrap it: right after it the back
 * buffer holds the whole frame. */
#define D3D_BACK_SURFACE 0x10266740u
#define D3D_PRESENT      0x1012267Eu
void sub_1012267E(void);
static volatile LONG g_shot_due = -1;

static volatile uint32_t g_frames;     /* d3d.ren presents: replay's frame */
static int shot_surface(const char* file, uint32_t slot) {
    void** surf = (void**)(uintptr_t)MEM32(slot);
    if (!surf) return 0;
    typedef HRESULT (WINAPI *lock_t)(void*, RECT*, void*, DWORD, HANDLE);
    typedef HRESULT (WINAPI *unlock_t)(void*, RECT*);
    void** vt = *(void***)surf;
    uint8_t d[0x7C] = { 0 };                    /* DDSURFACEDESC2 */
    *(DWORD*)d = sizeof d;
    if (((lock_t)vt[25])(surf, NULL, d, 1 /* DDLOCK_WAIT */, NULL)) return 0;
    int w = *(int*)(d + 0x0C), h = *(int*)(d + 0x08), pitch = *(int*)(d + 0x10);
    uint32_t bits = *(uint32_t*)(d + 0x54), rmask = *(uint32_t*)(d + 0x58);
    const uint8_t* src = *(const uint8_t**)(d + 0x24);
    DWORD stride = (w * 3 + 3) & ~3u;
    uint8_t* out = (uint8_t*)calloc(stride, h);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t* o = out + (h - 1 - y) * stride + x * 3;
            if (bits == 16) {
                uint16_t p = ((const uint16_t*)(src + y * pitch))[x];
                int r5 = rmask == 0x7C00 ? (p >> 10) & 31 : p >> 11;
                int g = rmask == 0x7C00 ? ((p >> 5) & 31) << 3 : ((p >> 5) & 63) << 2;
                o[0] = (uint8_t)((p & 31) << 3); o[1] = (uint8_t)g; o[2] = (uint8_t)(r5 << 3);
            } else {
                memcpy(o, src + y * pitch + x * 4, 3);
            }
        }
    ((unlock_t)vt[32])(surf, NULL);
    BITMAPINFOHEADER bh = { sizeof bh, w, h, 1, 24, BI_RGB };
    BITMAPFILEHEADER fh = { 0x4D42, sizeof fh + sizeof bh + stride * h, 0, 0, sizeof fh + sizeof bh };
    FILE* f = fopen(file, "wb");
    if (f) {
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&bh, sizeof bh, 1, f);
        fwrite(out, stride * h, 1, f);
        fclose(f);
        printf("[shot] back buffer %dx%d %ubpp frame %u hash %08X -> %s\n", w, h, bits, g_frames,
               replay_hash(out, (size_t)stride * h), file);
    }
    free(out);
    return 1;
}

/* --record FILE.mp4: every presented frame, piped raw to ffmpeg (on PATH).
 * The size is the first frame's; frames of another size (a mode change)
 * are dropped. ffmpeg stamps them by wall clock, so the video keeps the
 * game's real pace. */
static char g_record[MAX_PATH];
static FILE* g_rec;
static int g_rec_w, g_rec_h, g_rec_bits;
static void record_frame(void) {
    void** surf = (void**)(uintptr_t)MEM32(D3D_BACK_SURFACE);
    if (!surf || !g_record[0]) return;
    typedef HRESULT (WINAPI *lock_t)(void*, RECT*, void*, DWORD, HANDLE);
    typedef HRESULT (WINAPI *unlock_t)(void*, RECT*);
    void** vt = *(void***)surf;
    uint8_t d[0x7C] = { 0 };
    *(DWORD*)d = sizeof d;
    if (((lock_t)vt[25])(surf, NULL, d, 1, NULL)) return;
    int w = *(int*)(d + 0x0C), h = *(int*)(d + 0x08), pitch = *(int*)(d + 0x10);
    int bits = (int)*(uint32_t*)(d + 0x54), rmask = (int)*(uint32_t*)(d + 0x58);
    const uint8_t* src = *(const uint8_t**)(d + 0x24);
    if (!g_rec) {
        char cmd[MAX_PATH + 256];
        _snprintf(cmd, sizeof cmd - 1, "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pix_fmt %s -s %dx%d "
                  "-use_wallclock_as_timestamps 1 -i - -vf format=yuv420p -fps_mode vfr \"%s\"",
                  bits == 32 ? "bgr0" : rmask == 0x7C00 ? "rgb555le" : "rgb565le", w, h, g_record);
        cmd[sizeof cmd - 1] = 0;
        g_rec = _popen(cmd, "wb");
        g_rec_w = w; g_rec_h = h; g_rec_bits = bits;
        if (!g_rec) { fprintf(stderr, "[record] cannot start ffmpeg\n"); g_record[0] = 0; }
    }
    if (g_rec && w == g_rec_w && h == g_rec_h && bits == g_rec_bits)
        for (int y = 0; y < h; y++) fwrite(src + y * pitch, (size_t)(w * bits / 8), 1, g_rec);
    ((unlock_t)vt[32])(surf, NULL);
}
/* From another thread too (the watchdog): the frame in flight finishes first.
 * ponytail: a 200 ms grace, not a lock; a frame that takes longer is cut. */
static void record_close(void) {
    if (!g_rec) return;
    g_record[0] = 0;
    Sleep(200);
    _pclose(g_rec);
    g_rec = NULL;
}

static void replay_events(void);
static void hook_present(void) {
    sub_1012267E();
    g_frames++;
    record_frame();
    LONG i = g_shot_due;
    if (i >= 0 && shot_surface(g_shot_file[i], D3D_BACK_SURFACE)) {
        replay_log(g_frames, "shot %s", g_shot_file[i]);
        InterlockedExchange(&g_shot_due, -1);
    }
    replay_events();
}

recomp_func_t recomp_lookup_manual(uint32_t va) { return va == D3D_PRESENT ? hook_present : NULL; }

static DWORD WINAPI shot_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_shots; i++) {
        DWORD at = due_at(t0, g_shot_s[i]), now = GetTickCount();
        if (at > now) Sleep(at - now);
        InterlockedExchange(&g_shot_due, i);
        for (int k = 0; k < 100 && g_shot_due >= 0; k++) Sleep(20);
        if (InterlockedExchange(&g_shot_due, -1) >= 0) shot(g_shot_file[i]);   /* no frame in 2 s */
    }
    return 0;
}

/* One key edge into the game, both ways in: the game's actions read
 * DirectInput, its menus read WM_KEYDOWN, which a window that is not in front
 * never gets. --headless: native32's synthetic keyboard and posted messages
 * only (SendInput and the foreground belong to the user's desktop). */
static void send_key(WORD vk, int up) {
    HWND w = NULL;
    EnumWindows(find_window, (LPARAM)&w);
    int headless = GetEnvironmentVariableA("NATIVE32_HEADLESS", NULL, 0) != 0;
    INPUT in = { INPUT_KEYBOARD };
    in.ki.wVk = vk;
    in.ki.wScan = (WORD)MapVirtualKeyA(vk, 0);
    /* the arrows, Insert..PageDown: their scancodes are extended ones */
    DWORD ext = (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE ? KEYEVENTF_EXTENDEDKEY : 0;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | ext | (up ? KEYEVENTF_KEYUP : 0);
    LPARAM lp = 1 | ((LPARAM)in.ki.wScan << 16) | (ext ? 1 << 24 : 0) | (up ? 0xC0000000 : 0);
    replay_log(g_frames, "key %02x %s", vk, up ? "up" : "down");
    if (headless) native32_input_key(in.ki.wScan | (ext ? 0x80 : 0), !up);
    else SendInput(1, &in, sizeof(INPUT));
    if (w) PostMessageA(w, up ? WM_KEYUP : WM_KEYDOWN, vk, lp);
}

/* --replay FILE / --keys f<frame>:...: events due at this present. "key VK
 * down|up" (as recorded), "VK[*FRAMES]" (a press held FRAMES, default 8),
 * "shot FILE", "end". */
static WORD g_rp_held;
static uint32_t g_rp_release;
static void record_close(void);
static void replay_events(void) {
    if (g_rp_held && g_frames >= g_rp_release) { send_key(g_rp_held, 1); g_rp_held = 0; }
    for (const char* e; (e = replay_due(g_frames)) != NULL;) {
        char a[240] = "", b[16] = "", c[16] = "";
        sscanf(e, "%239s %15s %15s", a, b, c);
        if (!strcmp(a, "shot")) { shot_surface(replay_path(e + 5), D3D_BACK_SURFACE); continue; }
        if (!strcmp(a, "end")) { printf("[replay] frame %u: end\n", g_frames); record_close(); ExitProcess(0); }
        if (!strcmp(a, "key")) { send_key((WORD)strtoul(b, NULL, 16), *c == 'u'); continue; }
        char* hold = strchr(a, '*');
        uint32_t n = hold ? (*hold = 0, (uint32_t)atoi(hold + 1)) : 8;
        if (g_rp_held) send_key(g_rp_held, 1);
        g_rp_held = (WORD)strtoul(a, NULL, 0);
        g_rp_release = g_frames + n;
        if (g_rp_held) send_key(g_rp_held, 0);
    }
}

/* --key VK[:MS]@S: a key press sent to the game window at S seconds, held
 * MS milliseconds (default 250). In-game controls read DirectInput: under
 * --headless that is native32's synthetic keyboard, otherwise SendInput. */
static struct { WORD vk; DWORD at, hold; } g_keys[64];
static int g_nkeys;

static DWORD WINAPI key_thread(LPVOID unused) {
    (void)unused;
    DWORD t0 = GetTickCount();
    for (int i = 0; i < g_nkeys; i++) {
        DWORD at = due_at(t0, g_keys[i].at), now = GetTickCount();
        if (at > now) Sleep(at - now);
        HWND w = NULL;
        EnumWindows(find_window, (LPARAM)&w);
        int headless = GetEnvironmentVariableA("NATIVE32_HEADLESS", NULL, 0) != 0;
        if (w && !headless && GetForegroundWindow() != w) {
            /* Windows lets a background process take the foreground only
             * from inside the foreground thread's input queue. */
            DWORD fg = GetWindowThreadProcessId(GetForegroundWindow(), NULL), me = GetCurrentThreadId();
            AttachThreadInput(me, fg, TRUE);
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
            SetForegroundWindow(w);
            BringWindowToTop(w);
            AttachThreadInput(me, fg, FALSE);
            Sleep(200);
        }
        send_key(g_keys[i].vk, 0);
        Sleep(g_keys[i].hold);   /* held across a few frames: DirectInput is polled once a frame */
        send_key(g_keys[i].vk, 1);
        printf("[key] 0x%02X at %lu ms%s\n", g_keys[i].vk, GetTickCount() - t0,
               headless ? " (posted)" : GetForegroundWindow() == w ? "" : " (the window is not in front)");
    }
    return 0;
}

/* --trace-at S: the native-call trace for one second from S seconds, so a
 * steady state can be read without tracing the whole start-up. */
static DWORD g_trace_at;
static DWORD WINAPI trace_window(LPVOID unused) {
    (void)unused;
    Sleep(g_trace_at * 1000);
    native32_trace_native = 1;
    Sleep(1000);
    native32_trace_native = 0;
    return 0;
}

static DWORD WINAPI watchdog(LPVOID unused) {
    (void)unused;
    Sleep(g_watchdog_s * 1000);
    fprintf(stderr, "\n[watchdog] %lu s: in sub_%08X (%s), last native call %s, %u indirect calls\n",
            g_watchdog_s, g_cur_func, module_of(g_cur_func), g_cur_import, g_icall_count);
    native32_dump_icalls(8);
    recomp_trace_flush();
    fflush(stderr);
    replay_log(g_frames, "end");
    record_close();
    TerminateProcess(GetCurrentProcess(), 4);
    return 0;
}

/* The renderers draw 16-bit surfaces only ("Unknown DirectDraw surface
 * format" on a 32-bit desktop), and windowed their surfaces take the
 * desktop's format. Windows' own "reduced color mode" compatibility layer
 * gives this process a 16-bit DirectDraw, but it is read at process start:
 * so the host runs itself again with it set. --no-16bit skips that. */
static int relaunch_16bit(void) {
    char layer[256] = "";
    /* A marker of our own: Windows takes __COMPAT_LAYER out of the
     * environment it hands the child, so it cannot say "already done". */
    if (GetEnvironmentVariableA("KISS_RECOMP_CHILD", NULL, 0)) return -1;
    SetEnvironmentVariableA("KISS_RECOMP_CHILD", "1");
    GetEnvironmentVariableA("__COMPAT_LAYER", layer, sizeof layer);
    strcat_s(layer, sizeof layer, layer[0] ? " 16BITCOLOR" : "16BITCOLOR");
    SetEnvironmentVariableA("__COMPAT_LAYER", layer);
    STARTUPINFOA si = { sizeof si };
    PROCESS_INFORMATION pi;
    DWORD rc = 1;
    if (!CreateProcessA(NULL, GetCommandLineA(), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &rc);
    return (int)rc;
}

/* --di-probe: DirectInput's device enumeration from host C, no guest code. */
static int g_probe_n;
static BOOL WINAPI probe_cb(const void* ddi, void* ctx) { (void)ddi; (void)ctx; g_probe_n++; return TRUE; }
static int di_probe(void) {
    typedef HRESULT (WINAPI *create_t)(HINSTANCE, DWORD, void**, void*);
    create_t create = (create_t)GetProcAddress(LoadLibraryA("dinput.dll"), "DirectInputCreateA");
    void** di = NULL;
    HRESULT hr = create(GetModuleHandleA(NULL), 0x700, (void**)&di, NULL);
    printf("[probe] DirectInputCreateA 0x700: %08lX\n", hr);
    if (hr) return 1;
    typedef HRESULT (WINAPI *enum_t)(void*, DWORD, void*, void*, DWORD);
    hr = ((enum_t)(*(void***)di)[4])(di, 0, (void*)probe_cb, NULL, 1);
    printf("[probe] EnumDevices: %08lX, %d devices; heap %s\n", hr, g_probe_n,
           HeapValidate(GetProcessHeap(), 0, NULL) ? "ok" : "CORRUPT");
    return 0;
}

static int host_main(int argc, char** argv) {
    const char* game = "game";
    char rebased[MAX_PATH] = "work\\rebased", tail[1024] = "";
    int run = 0;
    const char *rp_record = NULL, *rp_replay = NULL, *rp_keys = NULL;
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    {
        int want16 = 1, rc;
        for (int i = 1; i < argc; i++) want16 &= !!strcmp(argv[i], "--no-16bit");
        for (int i = 1; i < argc; i++)
            if (!strcmp(argv[i], "--headless") && (rc = native32_headless()) >= 0) return rc;
        for (int i = 1; i < argc; i++) if (!strcmp(argv[i], "--run") && want16 && (rc = relaunch_16bit()) >= 0) return rc;
    }
    for (int i = 1; i < argc; i++) {
        int n = recomp_trace_arg(argc, argv, i);
        if (n) { i += n - 1; continue; }
        if (!strcmp(argv[i], "--run")) run = 1;
        else if (!strcmp(argv[i], "--no-16bit") || !strcmp(argv[i], "--headless")) ;
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--rebased") && i + 1 < argc) strcpy_s(rebased, sizeof rebased, argv[++i]);
        else if (!strcmp(argv[i], "--watchdog") && i + 1 < argc) g_watchdog_s = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--shot") && i + 2 < argc && g_shots < 8) {
            g_shot_s[g_shots] = parse_time(argv[++i]);
            GetFullPathNameA(argv[++i], MAX_PATH, g_shot_file[g_shots++], NULL);
        }
        else if (!strcmp(argv[i], "--key") && i + 1 < argc && g_nkeys < 64) {
            char* at = strchr(argv[++i], '@');
            if (!at) { fprintf(stderr, "--key wants VK@seconds\n"); return 1; }
            char* end;
            g_keys[g_nkeys].vk = (WORD)strtoul(argv[i], &end, 0);
            g_keys[g_nkeys].hold = *end == ':' ? strtoul(end + 1, NULL, 0) : 250;
            g_keys[g_nkeys++].at = parse_time(at + 1);
        }
        else if (!strcmp(argv[i], "--native-trace")) native32_trace_native = 1;
        else if (!strcmp(argv[i], "--heap-check")) g_heap_check = 1;
        else if (!strcmp(argv[i], "--early-miles")) g_early_miles = 1;
        else if (!strcmp(argv[i], "--record") && i + 1 < argc) {   /* .mp4: video; else the input log */
            size_t n = strlen(argv[++i]);
            if (n > 4 && !_stricmp(argv[i] + n - 4, ".mp4")) GetFullPathNameA(argv[i], MAX_PATH, g_record, NULL);
            else rp_record = argv[i];
        }
        else if (!strcmp(argv[i], "--replay") && i + 1 < argc) rp_replay = argv[++i];
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) rp_keys = argv[++i];
        else if (!strcmp(argv[i], "--di-probe")) { native32_init(); native32_allow_heap_code(); return di_probe(); }
        else if (!strcmp(argv[i], "--native") && i + 1 < argc && g_nnative < 8) g_native[g_nnative++] = argv[++i];
        else if (!strcmp(argv[i], "--trace-at") && i + 1 < argc) g_trace_at = strtoul(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "--callbacks")) native32_trace_callbacks = 1;
        else if (!strcmp(argv[i], "--")) {
            for (i++; i < argc; i++) {
                strcat_s(tail, sizeof tail, " ");
                strcat_s(tail, sizeof tail, argv[i]);
            }
        } else {
            printf("usage: kiss [--run] [--headless] [--record FILE.mp4|FILE.rec] [--replay FILE.rec]\n"
                   "            [--keys f<frame>:VK[*frames],...] [--game game] [--rebased work\\rebased]\n"
                   "            [--watchdog S] [--shot S FILE.bmp] [--key VK@S] [--no-16bit]\n"
                   "            [--native-trace] [--callbacks] [--heap-check] [--native MODULE]\n"
                   "            [--early-miles] [--di-probe]\n"
                   "            [-- engine arguments, e.g. +windowed 1 +RGBRast 1]\n");
            recomp_trace_help();
            return argv[i][1] == 'h' || argv[i][2] == 'h' ? 0 : 1;
        }
    }
    {
        /* client.exe's 0x01CA0000..0x01D55000 is where the process puts
         * heaps before main(): native32 starts the host again with it held. */
        char exe[MAX_PATH];
        _snprintf(exe, sizeof exe - 1, "%s\\client.exe", rebased);
        exe[MAX_PATH - 1] = 0;
        native32_hold_image(exe, g_exe->base);
    }
    GetFullPathNameA(game, MAX_PATH, g_game_dir, NULL);
    /* What psycho.exe (the launcher) passes, the patch rez last so it wins,
     * and the game folder as a resource tree of its own: the game DLLs are
     * files there, not entries in a rez, and the engine finds them only
     * through its trees ("Error copying file cshell.dll" without it). */
    _snprintf(g_guest_cmdline, sizeof g_guest_cmdline - 1,
              "\"%s\\client.exe\" -windowtitle \"The Nightmare Child\" -rez psycho.rez"
              " -rez KPCPatch.rez -rez . ++RenderDll d3d.ren%s", g_game_dir, tail);
    SetDllDirectoryA(g_game_dir);

    native32_init();
    {
        ULONG guarantee = 256 * 1024;
        SetThreadStackGuarantee(&guarantee);
    }
    {
        HMODULE mss = LoadLibraryA("mss32.dll");
        g_ail_startup = mss ? GetProcAddress(mss, "_AIL_startup@0") : NULL;
        if (g_early_miles && g_ail_startup) ((void (WINAPI*)(void))g_ail_startup)();
    }
    native32_allow_heap_code();   /* Direct3D's RGB rasteriser */
    AddVectoredExceptionHandler(0, crash);
    printf("KISS Psycho Circus recomp host\n  lifted functions in dispatch: %u\n", recomp_dispatch_count);

    for (int i = 0; i < NMODULES; i++) {
        char path[MAX_PATH];
        module_t* m = &g_modules[i];
        _snprintf(path, sizeof path - 1, "%s\\%s", rebased, m->name);
        uint32_t span = native32_map(path, m->base);
        if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", path, m->base); return 1; }
        m->stamp = nt_of(m->base)->FileHeader.TimeDateStamp;
        m->span = span;
        printf("  mapped %-12s 0x%08X-0x%08X\n", m->name, m->base, m->base + span);
    }
    for (int i = 0; i < g_nnative; i++) {
        module_t* m = module_by_name(g_native[i]);
        if (!m || !native32_set_native(m->base)) { fprintf(stderr, "--native %s: no such module\n", g_native[i]); return 1; }
        printf("  %s runs its original code\n", m->name);
        g_exe_native |= m == g_exe;
    }
    int missing = 0;
    for (int i = 0; i < NMODULES; i++)
        missing += native32_bind(g_modules[i].base, g_shims, (int)(sizeof g_shims / sizeof g_shims[0]));
    if (missing) fprintf(stderr, "warning: %d imports unresolved\n", missing);

    uint32_t entry = g_exe->base + nt_of(g_exe->base)->OptionalHeader.AddressOfEntryPoint;
    printf("  guest %s\n", g_guest_cmdline);
    if (!run) {
        printf("\n(dry run: mapped and bound; --run enters 0x%08X)\n", entry);
        return 0;
    }
    replay_init(rp_record ? rp_record : "", rp_replay ? rp_replay : "", rp_keys ? rp_keys : "");
    g_vclock = rp_record || rp_replay || rp_keys;
    if (!SetCurrentDirectoryA(g_game_dir)) { fprintf(stderr, "cannot enter %s\n", g_game_dir); return 1; }
    if (g_watchdog_s) CloseHandle(CreateThread(NULL, 0, watchdog, NULL, 0, NULL));
    if (g_shots) CloseHandle(CreateThread(NULL, 0, shot_thread, NULL, 0, NULL));
    if (g_trace_at) CloseHandle(CreateThread(NULL, 0, trace_window, NULL, 0, NULL));
    if (g_nkeys) CloseHandle(CreateThread(NULL, 0, key_thread, NULL, 0, NULL));
    if (!attach(g_exe)) return 1;
    printf("  entering 0x%08X\n\n", entry);
    if (g_exe_native) ((void (*)(void))(uintptr_t)entry)();   /* --native client.exe: its own code */
    else native32_call_guest(entry, 0, NULL);
    record_close();
    printf("\nentry returned eax=%08X\n", g_eax);
    return (int)g_eax;
}

/* Lifted code runs on the host's C stack and nests deeply, so it gets 16 MB on
 * a thread of its own; the process's first thread is linked with 256 KB
 * (CMakeLists.txt), so that stack fits below client.exe (native32_hold_image). */
static int g_argc;
static char** g_argv;
static DWORD WINAPI host_thread(LPVOID unused) {
    (void)unused;
    return (DWORD)host_main(g_argc, g_argv);
}

int main(int argc, char** argv) {
    DWORD code = 1;
    g_argc = argc;
    g_argv = argv;
    HANDLE t = CreateThread(NULL, 16u << 20, host_thread, NULL, STACK_SIZE_PARAM_IS_A_RESERVATION, NULL);
    if (!t) return 1;
    WaitForSingleObject(t, INFINITE);
    GetExitCodeThread(t, &code);
    return (int)code;
}
