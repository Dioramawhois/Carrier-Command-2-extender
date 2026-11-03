/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TEST ONLY. Staged as steam_api.dll with generated forwarders to an unchanged
 * private copy of the installed Steam DLL. Redirect only the copied game's
 * APPDATA query before its entry point. Never linked into the production SDK. */
#include <windows.h>
#include <shlobj.h>
#include <wchar.h>
#include <string.h>
#include <stdint.h>
static wchar_t profile[MAX_PATH];
static char profile_ansi[MAX_PATH];
typedef HRESULT (WINAPI *FolderFn)(HWND,int,HANDLE,DWORD,LPSTR);
static FolderFn original_folder;
static void log_redirect(const char *text) {
    wchar_t path[MAX_PATH];_snwprintf(path,MAX_PATH,L"%ls\\redirect.log",profile);
    HANDLE file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ,NULL,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    if(file!=INVALID_HANDLE_VALUE){DWORD wrote;WriteFile(file,text,(DWORD)strlen(text),&wrote,NULL);CloseHandle(file);}
}
static HRESULT WINAPI private_folder(HWND window,int folder,HANDLE token,DWORD flags,LPSTR out) {
    if((folder&0xff)==CSIDL_APPDATA) {
        if(!out)return E_INVALIDARG;
        lstrcpynA(out,profile_ansi,MAX_PATH);log_redirect("APPDATA query redirected\r\n");return S_OK;
    }
    return original_folder(window,folder,token,flags,out);
}
static int redirect_import(void) {
    BYTE *image=(BYTE*)GetModuleHandleW(NULL);
    IMAGE_DOS_HEADER *dos=(IMAGE_DOS_HEADER*)image;
    if(!image||dos->e_magic!=IMAGE_DOS_SIGNATURE)return 0;
    IMAGE_NT_HEADERS32 *nt=(IMAGE_NT_HEADERS32*)(image+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return 0;
    DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if(!rva)return 0;
    for(IMAGE_IMPORT_DESCRIPTOR *import=(IMAGE_IMPORT_DESCRIPTOR*)(image+rva);import->Name;++import) {
        if(_stricmp((char*)image+import->Name,"SHELL32.dll"))continue;
        if(!import->OriginalFirstThunk)return 0;
        IMAGE_THUNK_DATA32 *name=(IMAGE_THUNK_DATA32*)(image+import->OriginalFirstThunk);
        IMAGE_THUNK_DATA32 *target=(IMAGE_THUNK_DATA32*)(image+import->FirstThunk);
        for(;name->u1.AddressOfData;++name,++target) {
            if(IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal))continue;
            IMAGE_IMPORT_BY_NAME *function=(IMAGE_IMPORT_BY_NAME*)(image+name->u1.AddressOfData);
            if(strcmp((char*)function->Name,"SHGetFolderPathA"))continue;
            original_folder=(FolderFn)(uintptr_t)target->u1.Function;
            DWORD old=0,ignored=0;
            if(!original_folder||!VirtualProtect(&target->u1.Function,sizeof(DWORD),PAGE_READWRITE,&old))return 0;
            InterlockedExchange((volatile LONG*)&target->u1.Function,(LONG)(uintptr_t)private_folder);
            if(!VirtualProtect(&target->u1.Function,sizeof(DWORD),old,&ignored))return 0;
            return 1;
        }
    }
    return 0;
}
BOOL WINAPI DllMain(HINSTANCE module,DWORD reason,LPVOID reserved) {
    (void)module;(void)reserved;
    if(reason!=DLL_PROCESS_ATTACH)return TRUE;
    wchar_t game[MAX_PATH],expected[MAX_PATH],marker[MAX_PATH];
    DWORD n=GetModuleFileNameW(NULL,game,MAX_PATH);
    if(!n||n>=MAX_PATH)return FALSE;
    wchar_t *slash=wcsrchr(game,L'\\');if(!slash)return FALSE;*slash=0;
    slash=wcsrchr(game,L'\\');if(!slash||_wcsicmp(slash+1,L"game"))return FALSE;*slash=0;
    if(wcslen(game)>MAX_PATH-32)return FALSE;
    _snwprintf(expected,MAX_PATH,L"%ls\\profile",game);
    _snwprintf(marker,MAX_PATH,L"%ls\\isolation.json",game);
    if(GetFileAttributesW(marker)==INVALID_FILE_ATTRIBUTES)return FALSE;
    n=GetEnvironmentVariableW(L"CC2X_TEST_PROFILE_ROOT",profile,MAX_PATH);
    if(!n||n>=MAX_PATH||_wcsicmp(profile,expected)||GetFileAttributesW(profile)==INVALID_FILE_ATTRIBUTES)return FALSE;
    if(!WideCharToMultiByte(CP_ACP,WC_NO_BEST_FIT_CHARS,profile,-1,profile_ansi,MAX_PATH,NULL,NULL))return FALSE;
    if(!SetEnvironmentVariableW(L"APPDATA",profile)||!redirect_import())return FALSE;
    log_redirect("IAT APPDATA redirect installed before game entry\r\n");
    return TRUE;
}
