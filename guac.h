/*
 * guac.h: header-only hooking library for Windows (x86/x64)
 * Author: Robert Motrogeanu
 * 
 * Usage:
 *   #include "guac.h"            // declarations only
 *
 *   // In one .c file:
 *   #define GUAC_IMPLEMENTATION
 *   #include "guac.h"
 *
 * License: MIT
 */

#ifndef GUAC_H
#define GUAC_H

#ifndef _WIN32
  #error "guac.h requires Windows"
#endif

#include <windows.h>
#include <tlhelp32.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef GUAC_API
  #define GUAC_API
#endif

#ifndef GUAC_INTERNAL
  #define GUAC_INTERNAL static
#endif 

#if defined(_M_X64) || defined(__x86_64__)
  #define GUAC__ARCH_X64 1
  #define GUAC__ARCH_X86 0
#elif defined(_M_IX86) || defined(__i386__)
  #define GUAC__ARCH_X64 0
  #define GUAC__ARCH_X86 1
#else
  #error "guac.h requires an x86 or x64 architecture"
#endif

/************************************************
*                                               *
*                  PUBLIC TYPES                 *
*                                               *
*************************************************/
typedef enum 
{
    GUAC_ERROR_NONE              = 0,
    GUAC_ERROR_INVALID_ARG       = 1,
    GUAC_ERROR_ALREADY_HOOKED    = 2,
    GUAC_ERROR_NOT_HOOKED        = 3,
    GUAC_ERROR_NOT_INITIALIZED   = 4,
    GUAC_ERROR_NO_SLOT_AVAILABLE = 5,
    GUAC_ERROR_THREAD_SUSPEND    = 6,
    GUAC_ERROR_CONTEXT_FAILED    = 7,
    GUAC_ERROR_UNSUPPORTED       = 8,
    GUAC__STATUS_FORCE_U32       = 0x7FFFFFFF,
} guac_status_t;

typedef enum 
{
    GUAC_METHOD_TRAMPOLINE       = 0, // TODO: implement
    GUAC_METHOD_HWBP             = 1, 
    GUAC_METHOD_SWBP             = 2, // TODO: implement
    GUAC_METHOD_PAGE             = 3, // TODO: implement
    GUAC_METHOD_INSTRUMENT       = 4, // TODO: implement
    GUAC_METHOD_CFG              = 5, // TODO: implement
} guac_method_t;

typedef struct
{
    guac_method_t                hook_method;
    bool                         suspend_current_thread;
    bool                         suspend_all_threads;
    bool                         flush_instruction_cache;
} guac_options_t;

typedef int guac_handle_t;
#ifndef GUAC_INVALID_HANDLE
    #define GUAC_INVALID_HANDLE ((guac_handle_t)-1)
#endif

/************************************************
*                                               *
*                  PUBLIC API                   *
*                                               *
*************************************************/
GUAC_API 
guac_status_t guac_hook(
    guac_handle_t   *handle,
    void            *address,
    void            *detour,
    guac_options_t  *options
);

GUAC_API 
guac_status_t guac_unhook(
    guac_handle_t   *handle
);

GUAC_API 
guac_options_t guac_default_options(void);

GUAC_API 
const char* guac_status_string(
    guac_status_t status
);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // GUAC_H

/************************************************
*                                               *
*           INTERNAL IMPLEMENTATION             *
*                                               *
*************************************************/
#ifdef GUAC_IMPLEMENTATION
#ifndef GUAC__IMPL_GUARD
#define GUAC__IMPL_GUARD

#ifdef __cplusplus
extern "C" {
#endif

// -- LOGGING -- 
typedef enum 
{
    GUAC_LOG_LEVEL_DEBUG = 0,
    GUAC_LOG_LEVEL_INFO  = 1,
    GUAC_LOG_LEVEL_WARN  = 2,
    GUAC_LOG_LEVEL_ERROR = 3,
} guac_log_level_t;

GUAC_INTERNAL const char*          _guac_log_level_str(guac_log_level_t level);
GUAC_INTERNAL const char*          _guac_log_level_color(guac_log_level_t level);
GUAC_INTERNAL void                 _guac_log(guac_log_level_t level, const char* file,
                                        int line, const char* func, const char* fmt, ...);

#define GUAC_LOG_INFO(fmt, ...)    _guac_log(GUAC_LOG_LEVEL_INFO,  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define GUAC_LOG_WARN(fmt, ...)    _guac_log(GUAC_LOG_LEVEL_WARN,  __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#define GUAC_LOG_ERROR(fmt, ...)   _guac_log(GUAC_LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#ifdef GUAC_DEBUG
  #define GUAC_LOG_DEBUG(fmt, ...) _guac_log(GUAC_LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#else
  #define GUAC_LOG_DEBUG(fmt, ...) ((void)0)
#endif

GUAC_INTERNAL
const char* _guac_log_level_str(guac_log_level_t level) 
{
    switch (level) {
        case GUAC_LOG_LEVEL_DEBUG: return "DEBUG";
        case GUAC_LOG_LEVEL_INFO:  return "INFO";
        case GUAC_LOG_LEVEL_WARN:  return "WARN";
        case GUAC_LOG_LEVEL_ERROR: return "ERROR";
        default:                   return "???";
    }
}

GUAC_INTERNAL
const char* _guac_log_level_color(guac_log_level_t level) 
{
    switch (level) {
        case GUAC_LOG_LEVEL_DEBUG: return "\033[33m";       // yellow
        case GUAC_LOG_LEVEL_INFO:  return "\033[0m";        // default
        case GUAC_LOG_LEVEL_WARN:  return "\033[91m";       // light red
        case GUAC_LOG_LEVEL_ERROR: return "\033[31m";       // red 
        default:                   return "\033[0m";
    }
}

GUAC_INTERNAL
void _guac_log(
    guac_log_level_t level, const char* file, int line,
    const char* func, const char* fmt, ...) 
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    const char* color = _guac_log_level_color(level);
    const char* reset = "\033[0m";

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "[GUAC] [%02d:%02d:%02d:%06d] %s[%s]%s (%s:%d) (%s) %s\n",
        st.wHour, st.wMinute, st.wSecond, st.wMilliseconds * 1000,
        color, _guac_log_level_str(level), reset,
        file, line, func,
        msg);

    fprintf(stderr, "%s", buf);
}
// -- LOGGING -- 


/************************************************
*                                               *
*               INTERNAL TYPES                  *
*                                               *
*************************************************/
#define GUAC__MAX_HWBP 4

typedef LONG NTSTATUS;
#define GUAC__NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)

typedef NTSTATUS (NTAPI *guac__fn_NtGetContextThread)(HANDLE, PCONTEXT);
typedef NTSTATUS (NTAPI *guac__fn_NtSetContextThread)(HANDLE, PCONTEXT);
typedef NTSTATUS (NTAPI *guac__fn_NtGetNextThread)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
typedef NTSTATUS (NTAPI *guac__fn_NtSuspendThread)(HANDLE, PULONG);
typedef NTSTATUS (NTAPI *guac__fn_NtResumeThread)(HANDLE, PULONG);
typedef NTSTATUS (NTAPI *guac__fn_NtClose)(HANDLE);
typedef PVOID    (NTAPI *guac__fn_RtlAddVEH)(ULONG, PVECTORED_EXCEPTION_HANDLER);
typedef ULONG    (NTAPI *guac__fn_RtlRemoveVEH)(PVOID);

typedef struct
{
    void*   address;
    void*   detour;
    int     dr_index;   // dr0-3
    bool    active;
} guac__hwbp_slot_t;

static struct
{
    /* resolved ntdll functions */
    guac__fn_NtGetContextThread  NtGetContextThread;
    guac__fn_NtSetContextThread  NtSetContextThread;
    guac__fn_NtGetNextThread     NtGetNextThread;
    guac__fn_NtSuspendThread     NtSuspendThread;
    guac__fn_NtResumeThread      NtResumeThread;
    guac__fn_NtClose             NtClose;
    guac__fn_RtlAddVEH           RtlAddVEH;
    guac__fn_RtlRemoveVEH        RtlRemoveVEH;

    /* hwbp hook slots (one per debug register) */
    guac__hwbp_slot_t            hwbp[GUAC__MAX_HWBP];
    PVOID                        veh_handle;

    /* synchronization */
    CRITICAL_SECTION             lock;
    bool                         initialized;

} g_guac = {0};

GUAC_INTERNAL void*         _guac_get_peb(void);
GUAC_INTERNAL void*         _guac_find_ntdll(void);
GUAC_INTERNAL void*         _guac_find_export(void* base, const char* name);
GUAC_INTERNAL guac_status_t _guac_ensure_init(void);
GUAC_INTERNAL LONG CALLBACK _guac_veh_handler(EXCEPTION_POINTERS* ep);
GUAC_INTERNAL void          _guac_ctx_set_dr(CONTEXT* ctx, int dr, void* address);
GUAC_INTERNAL void          _guac_ctx_clear_dr(CONTEXT* ctx, int dr);
GUAC_INTERNAL guac_status_t _guac_apply_dr_all_threads(int dr, void* address, bool enable);
GUAC_INTERNAL int           _guac_find_free_dr(void);
GUAC_INTERNAL guac_status_t _guac_hook_hwbp(
                                guac_handle_t *handle, void *address,
                                void *detour, guac_options_t  *options);
GUAC_INTERNAL guac_status_t _guac_unhook_hwbp(guac_handle_t handle);


GUAC_INTERNAL
void* _guac_get_peb(void)
{
#if GUAC__ARCH_X64
  #if defined(_MSC_VER)
    return (void*)__readgsqword(0x60);
  #else
    void* peb;
    __asm__ __volatile__("movq %%gs:0x60, %0" : "=r"(peb));
    return peb;
  #endif
#else
  #if defined(_MSC_VER)
    return (void*)(uintptr_t)__readfsdword(0x30);
  #else
    void* peb;
    __asm__ __volatile__("movl %%fs:0x30, %0" : "=r"(peb));
    return peb;
  #endif
#endif
}

GUAC_INTERNAL
void* _guac_find_ntdll(void)
{
    /*
     * Walk PEB -> Ldr -> InMemoryOrderModuleList.
     * Entry order: [0] = exe, [1] = ntdll.dll.
     * We use raw offsets to avoid pulling in winternl.h definitions.
     */
    unsigned char* peb = (unsigned char*)_guac_get_peb();
#if GUAC__ARCH_X64
    unsigned char* ldr  = *(unsigned char**)(peb + 0x18);
    LIST_ENTRY*    head = (LIST_ENTRY*)(ldr + 0x20);
#else
    unsigned char* ldr  = *(unsigned char**)(peb + 0x0C);
    LIST_ENTRY*    head = (LIST_ENTRY*)(ldr + 0x14);
#endif
    /* skip exe (first entry), ntdll is second */
    LIST_ENTRY* entry = head->Flink->Flink;

    /* DllBase offset from the InMemoryOrderLinks field */
#if GUAC__ARCH_X64
    return *(void**)((unsigned char*)entry + 0x20);
#else
    return *(void**)((unsigned char*)entry + 0x10);
#endif
}

GUAC_INTERNAL
void* _guac_find_export(void* base, const char* name)
{
    unsigned char* mod = (unsigned char*)base;
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)mod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(mod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;

    IMAGE_DATA_DIRECTORY* dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!dir->VirtualAddress) return NULL;

    IMAGE_EXPORT_DIRECTORY* exp = (IMAGE_EXPORT_DIRECTORY*)(mod + dir->VirtualAddress);
    DWORD* names     = (DWORD*)(mod + exp->AddressOfNames);
    WORD*  ordinals  = (WORD*)(mod + exp->AddressOfNameOrdinals);
    DWORD* functions = (DWORD*)(mod + exp->AddressOfFunctions);

    for (DWORD i = 0; i < exp->NumberOfNames; i++) {
        if (strcmp((const char*)(mod + names[i]), name) == 0)
            return mod + functions[ordinals[i]];
    }
    return NULL;
}

GUAC_INTERNAL
guac_status_t _guac_ensure_init(void)
{
    if (g_guac.initialized) return GUAC_ERROR_NONE;

    void* ntdll = _guac_find_ntdll();
    if (!ntdll) {
        GUAC_LOG_ERROR("Failed to locate ntdll.dll via PEB walk");
        return GUAC_ERROR_NOT_INITIALIZED;
    }

    GUAC_LOG_DEBUG("PEB: %p, Ldr found, ntdll base: %p", _guac_get_peb(), ntdll);

    #define GUAC__RESOLVE(field, type, name) do {                           \
        g_guac.field = (type)_guac_find_export(ntdll, name);                \
        if (g_guac.field)                                                   \
            GUAC_LOG_DEBUG("  resolved %-45s -> %p", name, (void*)g_guac.field); \
        else                                                                \
            GUAC_LOG_ERROR("  FAILED to resolve %s", name);                 \
    } while(0)

    GUAC__RESOLVE(NtGetContextThread, guac__fn_NtGetContextThread, "NtGetContextThread");
    GUAC__RESOLVE(NtSetContextThread, guac__fn_NtSetContextThread, "NtSetContextThread");
    GUAC__RESOLVE(NtGetNextThread,    guac__fn_NtGetNextThread,    "NtGetNextThread");
    GUAC__RESOLVE(NtSuspendThread,    guac__fn_NtSuspendThread,    "NtSuspendThread");
    GUAC__RESOLVE(NtResumeThread,     guac__fn_NtResumeThread,     "NtResumeThread");
    GUAC__RESOLVE(NtClose,            guac__fn_NtClose,            "NtClose");
    GUAC__RESOLVE(RtlAddVEH,          guac__fn_RtlAddVEH,          "RtlAddVectoredExceptionHandler");
    GUAC__RESOLVE(RtlRemoveVEH,       guac__fn_RtlRemoveVEH,       "RtlRemoveVectoredExceptionHandler");

    #undef GUAC__RESOLVE

    if (!g_guac.NtGetContextThread || !g_guac.NtSetContextThread ||
        !g_guac.NtGetNextThread    || !g_guac.NtSuspendThread    ||
        !g_guac.NtResumeThread     || !g_guac.NtClose            ||
        !g_guac.RtlAddVEH         || !g_guac.RtlRemoveVEH) {
        GUAC_LOG_ERROR("One or more ntdll exports could not be resolved — aborting init");
        return GUAC_ERROR_NOT_INITIALIZED;
    }

    InitializeCriticalSection(&g_guac.lock);
    g_guac.initialized = true;

    GUAC_LOG_INFO("guac initialized (ntdll: %p, %d NT functions resolved)", ntdll, 8);
    return GUAC_ERROR_NONE;
}

GUAC_INTERNAL
LONG CALLBACK _guac_veh_handler(EXCEPTION_POINTERS* ep)
{
    if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP)
        return EXCEPTION_CONTINUE_SEARCH;

    void* fault_addr = ep->ExceptionRecord->ExceptionAddress;

    GUAC_LOG_DEBUG("VEH: SINGLE_STEP at %p (DR6=0x%llx)",
                   fault_addr, (unsigned long long)ep->ContextRecord->Dr6);

    for (int i = 0; i < GUAC__MAX_HWBP; i++) {
        if (!g_guac.hwbp[i].active)  continue;
        if (g_guac.hwbp[i].address != fault_addr) continue;

        GUAC_LOG_DEBUG("VEH: matched slot DR%d (%p -> %p), redirecting", i, fault_addr, g_guac.hwbp[i].detour);

#if GUAC__ARCH_X64
        ep->ContextRecord->Rip = (DWORD64)g_guac.hwbp[i].detour;
#else
        ep->ContextRecord->Eip = (DWORD)g_guac.hwbp[i].detour;
#endif
        /* Resume Flag: skip the breakpoint for one instruction to avoid infinite loop */
        ep->ContextRecord->EFlags |= 0x10000;

        return EXCEPTION_CONTINUE_EXECUTION;
    }

    GUAC_LOG_DEBUG("VEH: SINGLE_STEP at %p did not match any active slot, passing", fault_addr);
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ========== Debug Register Helpers ========== */

/*
 * DR7 layout (per-slot):
 *   Local Enable  : bit (dr * 2)
 *   Condition/Size: 4 bits starting at bit (16 + dr * 4)
 *     condition 00 = execute, size 00 = 1 byte
 */

GUAC_INTERNAL
void _guac_ctx_set_dr(CONTEXT* ctx, int dr, void* address)
{
    DWORD_PTR* dr_regs[] = { &ctx->Dr0, &ctx->Dr1, &ctx->Dr2, &ctx->Dr3 };

    GUAC_LOG_DEBUG("  DR%d: setting address=%p (DR7 before: 0x%llx)", dr, address, (unsigned long long)ctx->Dr7);

    *dr_regs[dr] = (DWORD_PTR)address;

    ctx->Dr7 |= (1ull << (dr * 2));              /* local enable */
    ctx->Dr7 &= ~(0xFull << (16 + dr * 4));      /* condition=execute, size=1byte */

    GUAC_LOG_DEBUG("  DR%d: DR7 after: 0x%llx", dr, (unsigned long long)ctx->Dr7);
}

GUAC_INTERNAL
void _guac_ctx_clear_dr(CONTEXT* ctx, int dr)
{
    DWORD_PTR* dr_regs[] = { &ctx->Dr0, &ctx->Dr1, &ctx->Dr2, &ctx->Dr3 };

    GUAC_LOG_DEBUG("  DR%d: clearing (DR7 before: 0x%llx)", dr, (unsigned long long)ctx->Dr7);

    *dr_regs[dr] = 0;

    ctx->Dr7 &= ~(1ull << (dr * 2));             /* disable */
    ctx->Dr7 &= ~(0xFull << (16 + dr * 4));      /* clear condition/size */

    GUAC_LOG_DEBUG("  DR%d: DR7 after: 0x%llx", dr, (unsigned long long)ctx->Dr7);
}


GUAC_INTERNAL
guac_status_t _guac_apply_dr_all_threads(int dr, void* address, bool enable)
{
    HANDLE process     = GetCurrentProcess();
    DWORD  current_tid = GetCurrentThreadId();
    HANDLE prev        = NULL;
    HANDLE thread      = NULL;
    DWORD  access      = THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                         THREAD_SET_CONTEXT    | THREAD_QUERY_INFORMATION;

    int thread_count = 0;
    int success_count = 0;

    GUAC_LOG_DEBUG("Enumerating threads (current TID=%lu, %s DR%d for %p)",
                   (unsigned long)current_tid, enable ? "ENABLE" : "DISABLE", dr, address);

    while (GUAC__NT_SUCCESS(g_guac.NtGetNextThread(process, prev, access, 0, 0, &thread))) {
        if (prev) g_guac.NtClose(prev);
        prev = thread;

        DWORD tid = GetThreadId(thread);
        bool is_current = (tid == current_tid);
        thread_count++;

        GUAC_LOG_DEBUG("  thread %d: TID=%lu %s", thread_count, (unsigned long)tid,
                       is_current ? "(current)" : "");

        if (!is_current) {
            NTSTATUS sus = g_guac.NtSuspendThread(thread, NULL);
            if (!GUAC__NT_SUCCESS(sus)) {
                GUAC_LOG_WARN("  NtSuspendThread failed (0x%08lx) for TID %lu",
                              (unsigned long)sus, (unsigned long)tid);
                continue;
            }
        }

        CONTEXT ctx = {0};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

        NTSTATUS status = g_guac.NtGetContextThread(thread, &ctx);
        if (!GUAC__NT_SUCCESS(status)) {
            GUAC_LOG_WARN("  NtGetContextThread failed (0x%08lx) for TID %lu",
                          (unsigned long)status, (unsigned long)tid);
            if (!is_current) g_guac.NtResumeThread(thread, NULL);
            continue;
        }

        GUAC_LOG_DEBUG("  TID %lu context: DR0=%p DR1=%p DR2=%p DR3=%p DR7=0x%llx",
                       (unsigned long)tid,
                       (void*)(uintptr_t)ctx.Dr0, (void*)(uintptr_t)ctx.Dr1,
                       (void*)(uintptr_t)ctx.Dr2, (void*)(uintptr_t)ctx.Dr3,
                       (unsigned long long)ctx.Dr7);

        if (enable)
            _guac_ctx_set_dr(&ctx, dr, address);
        else
            _guac_ctx_clear_dr(&ctx, dr);

        status = g_guac.NtSetContextThread(thread, &ctx);
        if (!GUAC__NT_SUCCESS(status)) {
            GUAC_LOG_WARN("  NtSetContextThread failed (0x%08lx) for TID %lu",
                          (unsigned long)status, (unsigned long)tid);
        } else {
            success_count++;
        }

        if (!is_current)
            g_guac.NtResumeThread(thread, NULL);
    }

    if (prev) g_guac.NtClose(prev);

    GUAC_LOG_DEBUG("Thread enumeration done: %d threads found, %d updated successfully", thread_count, success_count);
    return GUAC_ERROR_NONE;
}

GUAC_INTERNAL
int _guac_find_free_dr(void)
{
    for (int i = 0; i < GUAC__MAX_HWBP; i++) {
        if (!g_guac.hwbp[i].active) return i;
    }
    return -1;
}

GUAC_INTERNAL
guac_status_t _guac_hook_hwbp(
    guac_handle_t   *handle,
    void            *address,
    void            *detour,
    guac_options_t  *options
)
{
    for (int i = 0; i < GUAC__MAX_HWBP; i++) {
        if (g_guac.hwbp[i].active && g_guac.hwbp[i].address == address) {
            GUAC_LOG_ERROR("Address %p is already hooked (DR%d)", address, i);
            return GUAC_ERROR_ALREADY_HOOKED;
        }
    }

    int dr = _guac_find_free_dr();
    if (dr < 0) {
        GUAC_LOG_ERROR("No free debug register (max %d)", GUAC__MAX_HWBP);
        return GUAC_ERROR_NO_SLOT_AVAILABLE;
    }

    GUAC_LOG_DEBUG("Allocating DR%d for %p -> %p", dr, address, detour);

    if (!g_guac.veh_handle) {
        g_guac.veh_handle = g_guac.RtlAddVEH(1, _guac_veh_handler);
        if (!g_guac.veh_handle) {
            GUAC_LOG_ERROR("RtlAddVectoredExceptionHandler failed");
            return GUAC_ERROR_UNSUPPORTED;
        }
        GUAC_LOG_DEBUG("VEH handler registered");
    }

    g_guac.hwbp[dr].address  = address;
    g_guac.hwbp[dr].detour   = detour;
    g_guac.hwbp[dr].dr_index = dr;
    g_guac.hwbp[dr].active   = true;

    guac_status_t rc = _guac_apply_dr_all_threads(dr, address, true);
    if (rc != GUAC_ERROR_NONE) {
        g_guac.hwbp[dr].active = false;
        return rc;
    }

    *handle = (guac_handle_t)dr;
    GUAC_LOG_INFO("Hooked %p -> %p via HWBP DR%d (handle=%d)", address, detour, dr, *handle);
    return GUAC_ERROR_NONE;
}

GUAC_INTERNAL
guac_status_t _guac_unhook_hwbp(guac_handle_t handle)
{
    if (handle < 0 || handle >= GUAC__MAX_HWBP) {
        GUAC_LOG_ERROR("Invalid HWBP handle: %d", handle);
        return GUAC_ERROR_INVALID_ARG;
    }

    guac__hwbp_slot_t* slot = &g_guac.hwbp[handle];
    if (!slot->active) {
        GUAC_LOG_ERROR("Handle %d is not active", handle);
        return GUAC_ERROR_NOT_HOOKED;
    }

    _guac_apply_dr_all_threads(slot->dr_index, NULL, false);

    GUAC_LOG_INFO("Unhooked %p from DR%d (handle=%d)", slot->address, slot->dr_index, handle);

    slot->address = NULL;
    slot->detour  = NULL;
    slot->active  = false;

    bool any_active = false;
    for (int i = 0; i < GUAC__MAX_HWBP; i++) {
        if (g_guac.hwbp[i].active) { any_active = true; break; }
    }
    if (!any_active && g_guac.veh_handle) {
        g_guac.RtlRemoveVEH(g_guac.veh_handle);
        g_guac.veh_handle = NULL;
        GUAC_LOG_DEBUG("VEH handler removed (no active hooks)");
    }

    return GUAC_ERROR_NONE;
}

GUAC_API 
guac_options_t guac_default_options(void)
{
    return (guac_options_t) {
        .hook_method             = GUAC_METHOD_HWBP,
        .suspend_current_thread  = true,
        .suspend_all_threads     = true,
        .flush_instruction_cache = true,
    };
}

GUAC_API 
guac_status_t guac_hook(
    guac_handle_t   *handle,
    void            *address,
    void            *detour,
    guac_options_t  *options
)
{
    if (!handle) {
        GUAC_LOG_ERROR("Must provide a non-NULL handle");
        return GUAC_ERROR_INVALID_ARG;
    }
    if (!address) {
        GUAC_LOG_ERROR("Must provide a non-NULL address");
        return GUAC_ERROR_INVALID_ARG;
    }
    if (!detour) {
        GUAC_LOG_ERROR("Must provide a non-NULL detour");
        return GUAC_ERROR_INVALID_ARG;
    }

    *handle = GUAC_INVALID_HANDLE;

    GUAC_LOG_DEBUG("guac_hook called: address=%p detour=%p options=%p", address, detour, (void*)options);

    guac_status_t status = _guac_ensure_init();
    if (status != GUAC_ERROR_NONE) return status;

    EnterCriticalSection(&g_guac.lock);

    guac_options_t opts = options ? *options : guac_default_options();

    GUAC_LOG_DEBUG("Using method=%d, suspend_all=%d, suspend_current=%d, flush_icache=%d",
                   opts.hook_method, opts.suspend_all_threads,
                   opts.suspend_current_thread, opts.flush_instruction_cache);

    switch (opts.hook_method) {
        case GUAC_METHOD_HWBP:
            status = _guac_hook_hwbp(handle, address, detour, &opts);
            break;
        default:
            GUAC_LOG_ERROR("Hook method %d not implemented", opts.hook_method);
            status = GUAC_ERROR_UNSUPPORTED;
            break;
    }

    LeaveCriticalSection(&g_guac.lock);
    return status;
}

GUAC_API 
guac_status_t guac_unhook(guac_handle_t *handle)
{
    if (!handle || *handle == GUAC_INVALID_HANDLE) {
        GUAC_LOG_ERROR("Must provide a valid handle");
        return GUAC_ERROR_INVALID_ARG;
    }

    GUAC_LOG_DEBUG("guac_unhook called: handle=%d", *handle);

    if (!g_guac.initialized) {
        GUAC_LOG_ERROR("Library not initialized");
        return GUAC_ERROR_NOT_INITIALIZED;
    }

    EnterCriticalSection(&g_guac.lock);

    guac_status_t status = _guac_unhook_hwbp(*handle);
    if (status == GUAC_ERROR_NONE)
        *handle = GUAC_INVALID_HANDLE;

    LeaveCriticalSection(&g_guac.lock);
    return status;
}

GUAC_API 
const char* guac_status_string(guac_status_t status)
{
    switch (status) {
        case GUAC_ERROR_NONE:              return "success";
        case GUAC_ERROR_INVALID_ARG:       return "invalid argument";
        case GUAC_ERROR_ALREADY_HOOKED:    return "already hooked";
        case GUAC_ERROR_NOT_HOOKED:        return "not hooked";
        case GUAC_ERROR_NOT_INITIALIZED:   return "not initialized";
        case GUAC_ERROR_NO_SLOT_AVAILABLE: return "no slot available";
        case GUAC_ERROR_THREAD_SUSPEND:    return "thread suspend failed";
        case GUAC_ERROR_CONTEXT_FAILED:    return "context operation failed";
        case GUAC_ERROR_UNSUPPORTED:       return "unsupported";
        default:                           return "unknown error";
    }
}

#ifdef __cplusplus
}
#endif

#endif // GUAC__IMPL_GUARD
#endif // GUAC_IMPLEMENTATION