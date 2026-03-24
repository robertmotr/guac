/*
 * test.c: HWBP hook demo using guac.h
 *
 * Hooks MessageBoxA so that every time it's called, the detour fires instead.
 * Build (MSVC):
 *   cl /DGUAC_DEBUG test.c /link user32.lib
 *
 * Build (MinGW):
 *   gcc -DGUAC_DEBUG test.c -o test.exe -luser32
 */

#define GUAC_IMPLEMENTATION
#include "guac.h"

/* original MessageBoxA typedef so we can call the real one after unhooking */
typedef int (WINAPI *fn_MessageBoxA)(HWND, LPCSTR, LPCSTR, UINT);

static int hook_count = 0;

/*
 * Our detour: same signature as MessageBoxA.
 * Instead of showing the original message, we show our own.
 */
__declspec(noinline) 
int WINAPI hooked_MessageBoxA(HWND hWnd, LPCSTR lpText, LPCSTR lpCaption, UINT uType)
{
    hook_count++;

    char buf[256];
    snprintf(buf, sizeof(buf), "HOOKED! (call #%d)\n\nOriginal text: \"%s\"", hook_count, lpText);

    /*
     * We can't call the real MessageBoxA here, our HWBP is still set on it,
     * so it would just trigger the hook again (infinite recursion).
     * Instead, use MessageBoxW which is a different function and not hooked.
     */
    wchar_t wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, buf, -1, wbuf, 256);
    return MessageBoxW(hWnd, wbuf, L"guac HWBP hook", uType);
}

int main(void)
{
    // get the address of MessageBoxA from user32.dll
    HMODULE user32 = GetModuleHandleA("user32.dll");
    if (!user32) {
        printf("Failed to get user32.dll handle\n");
        return 1;
    }

    void* msgbox_addr = (void*)GetProcAddress(user32, "MessageBoxA");
    printf("MessageBoxA address: %p\n", msgbox_addr);

    // show a normal messagebox before hooking 
    printf("\n[1] Calling MessageBoxA before hook...\n");
    MessageBoxA(NULL, "This is the ORIGINAL MessageBoxA.\nClick OK to continue.", "Before Hook", MB_OK | MB_ICONINFORMATION);

    // HWBP hook install
    printf("\n[2] Installing HWBP hook on MessageBoxA...\n");
    guac_handle_t handle = GUAC_INVALID_HANDLE;
    guac_status_t status = guac_hook(&handle, msgbox_addr, (void*)hooked_MessageBoxA, NULL);

    if (status != GUAC_ERROR_NONE) {
        printf("guac_hook failed: %s\n", guac_status_string(status));
        return 1;
    }
    printf("Hook installed (handle=%d)\n", handle);

    /* loop: every MessageBoxA call will be redirected to our detour */
    printf("\n[3] Entering hook loop — close each messagebox to see the next one.\n");
    printf("    Press Cancel (or close) on the hooked messagebox to exit the loop.\n\n");

    int iteration = 0;
    while (1) {
        iteration++;
        char text[128];
        snprintf(text, sizeof(text), "Loop iteration %d. This should be intercepted!", iteration);

        int result = MessageBoxA(NULL, text, "Calling MessageBoxA", MB_OKCANCEL | MB_ICONQUESTION);

        printf("  MessageBoxA returned: %d (hook_count=%d)\n", result, hook_count);

        if (result != IDOK)
            break;
    }

    /* unhook */
    printf("\n[4] Removing hook...\n");
    status = guac_unhook(&handle);
    if (status != GUAC_ERROR_NONE) {
        printf("guac_unhook failed: %s\n", guac_status_string(status));
        return 1;
    }
    printf("Hook removed (handle=%d)\n", handle);

    /* show a normal messagebox after unhooking to prove it's restored */
    printf("\n[5] Calling MessageBoxA AFTER unhook...\n");
    MessageBoxA(NULL, "This is the ORIGINAL MessageBoxA again.\nHook has been removed!", "After Unhook", MB_OK | MB_ICONINFORMATION);

    printf("\nDone. Total hooked calls: %d\n", hook_count);
    return 0;
}
