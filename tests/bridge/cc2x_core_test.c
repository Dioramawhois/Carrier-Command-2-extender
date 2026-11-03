/* Local fixtures for the actual CC2X transaction and activation code. */
#include "../../src/cc2x.c"
#include <assert.h>
static int failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)
int main(void) {
    InitializeCriticalSection(&g_log_cs);InitializeCriticalSection(&g_post_cs);
    uint8_t *image=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    g_base=(uintptr_t)image;memset(image,0x90,4096);
    uint8_t before[5]={0x90,0x90,0x90,0x90,0x90},after[5]={1,2,3,4,5};
    patch_begin("fixture");CHECK(patch_bytes(0x400010,5,before,after));CHECK(image[16]==0x90);CHECK(patch_commit());CHECK(!memcmp(image+16,after,5));
    patch_begin("overlap");CHECK(patch_bytes(0x400010,5,after,before));CHECK(!patch_commit());CHECK(!memcmp(image+16,after,5));
    patch_begin("changed_before_commit");CHECK(patch_bytes(0x400020,5,before,after));image[32]=0x91;CHECK(!patch_commit());CHECK(image[33]==0x90);
    patch_begin("guard");CHECK(!patch_bytes(0x400020,5,before,after));CHECK(!patch_commit());
    wchar_t temp[MAX_PATH],root[MAX_PATH],game[MAX_PATH*2],active[MAX_PATH*2],inactive[MAX_PATH*2],file[MAX_PATH*2];
    GetTempPathW(MAX_PATH,temp);GetTempFileNameW(temp,L"nbx",0,root);DeleteFileW(root);CreateDirectoryW(root,NULL);
    _snwprintf(game,MAX_PATH*2,L"%ls\\Carrier Command 2",root);CreateDirectoryW(game,NULL);
    _snwprintf(active,MAX_PATH*2,L"%ls\\A",root);CreateDirectoryW(active,NULL);
    _snwprintf(inactive,MAX_PATH*2,L"%ls\\B",root);CreateDirectoryW(inactive,NULL);
    _snwprintf(file,MAX_PATH*2,L"%ls\\mod.xml",active);FILE*f=_wfopen(file,L"wb");fputs("<mod/>",f);fclose(f);
    _snwprintf(file,MAX_PATH*2,L"%ls\\mod.xml",inactive);f=_wfopen(file,L"wb");fputs("<mod/>",f);fclose(f);
    wchar_t old[MAX_PATH];GetEnvironmentVariableW(L"APPDATA",old,MAX_PATH);SetEnvironmentVariableW(L"APPDATA",root);
    char a[MAX_PATH*3],b[MAX_PATH*3];WideCharToMultiByte(CP_UTF8,0,active,-1,a,sizeof(a),NULL,NULL);WideCharToMultiByte(CP_UTF8,0,inactive,-1,b,sizeof(b),NULL,NULL);
    _snwprintf(file,MAX_PATH*2,L"%ls\\mods.xml",game);f=_wfopen(file,L"wb");
    fprintf(f,"<data><active_mod_folders><path value=\"%s\"/><path value=\"%s\"/></active_mod_folders><inactive><path value=\"%s\"/></inactive></data>",a,a,b);fclose(f);
    load_mod_plugins();CHECK(active_mod_count()==1);wchar_t out[MAX_PATH*2];CHECK(active_mod_path(0,out,MAX_PATH*2));CHECK(!_wcsicmp(out,active));CHECK(!active_mod_path(1,out,MAX_PATH*2));
    f=_wfopen(file,L"wb");fputs("<data><active_mod_folders><path value=\"broken",f);fclose(f);load_mod_plugins();CHECK(active_mod_count()==-1);
    f=_wfopen(file,L"wb");fputs("<data><active_mod_folders/></data>",f);fclose(f);load_mod_plugins();CHECK(active_mod_count()==0);
    f=_wfopen(file,L"wb");fprintf(f,"<!-- <active_mod_folders><path value=\"%s\"/></active_mod_folders> --><data><active_mod_folders><path value=\"%s\"/></active_mod_folders></data>",b,a);fclose(f);load_mod_plugins();CHECK(active_mod_count()==1);
    f=_wfopen(file,L"wb");fputs("<active_mod_folders></active_mod_folders><active_mod_folders/>",f);fclose(f);load_mod_plugins();CHECK(active_mod_count()==-1);
    SetEnvironmentVariableW(L"APPDATA",old);
    DeleteFileW(file);_snwprintf(file,MAX_PATH*2,L"%ls\\mod.xml",active);DeleteFileW(file);_snwprintf(file,MAX_PATH*2,L"%ls\\mod.xml",inactive);DeleteFileW(file);
    RemoveDirectoryW(active);RemoveDirectoryW(inactive);RemoveDirectoryW(game);RemoveDirectoryW(root);VirtualFree(image,0,MEM_RELEASE);
    printf("%s CC2X raw transactions / overlaps / commit byte validation / bounded active section / duplicates / malformed XML\n",failures?"FAIL":"PASS");return failures?1:0;
}
