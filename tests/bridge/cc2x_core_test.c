/* Local fixtures for the actual CC2X transaction and activation code. */
#include "../../src/cc2x.c"
#include <assert.h>
static int failures;
#define CHECK(x) do {if(!(x)){printf("FAIL line %d: %s\n",__LINE__,#x);++failures;}}while(0)
static int dispatch_calls, tick_calls, replacement_calls, engine_ticks, fifo;
static void CC2X_THISCALL fake_tick(void *server) { (void)server; ++engine_ticks; }
static void count_tick(void *server, void *user) { (void)server; (void)user; ++tick_calls; }
static void repost(void *server, void *user) {
    (void)server; (void)user;
    ++dispatch_calls;
    /* Finite so the unfixed production loop fails without hanging the fixture. */
    if (dispatch_calls < 3) CHECK(server_post(repost, NULL));
}
static void replacement(void *server, void *user) { (void)server; (void)user; ++replacement_calls; }
static void replace_session(void *server, void *user) {
    (void)user;
    session_change(server, 0);
    session_change(server, 1); /* same address, different session */
    CHECK(server_post_session(session_generation(), replacement, NULL));
}
static void refill(void *server, void *user) {
    (void)server;
    CHECK((uintptr_t)user == (uintptr_t)fifo++);
    CHECK(server_post(replacement, NULL));
}
static void queue_dispatch_test(void) {
    void *server = (void *)(uintptr_t)1;
    o_tick = fake_tick;
    on_server_tick(count_tick, NULL);
    session_change(server, 1);
    CHECK(server_post(repost, NULL));
    tick_once(server);
    CHECK(dispatch_calls == 1 && tick_calls == 1 && g_posts.count == 1);
    tick_once(server);
    CHECK(dispatch_calls == 2 && tick_calls == 2 && g_posts.count == 1);
    tick_once(server);
    CHECK(dispatch_calls == 3 && tick_calls == 3 && g_posts.count == 0);
    CHECK(server_post(replace_session, NULL));
    CHECK(server_post(replacement, NULL)); /* belongs to the old session */
    tick_once(server);
    CHECK(replacement_calls == 0 && tick_calls == 3 && g_posts.count == 1);
    tick_once(server);
    CHECK(replacement_calls == 1 && tick_calls == 4 && g_posts.count == 0);
    for (uintptr_t i = 0; i < CC2X_POST_MAX; ++i) CHECK(server_post(refill, (void *)i));
    tick_once(server);
    CHECK(fifo == CC2X_POST_MAX && replacement_calls == 1 && g_posts.count == CC2X_POST_MAX);
    tick_once(server);
    CHECK(replacement_calls == 1 + CC2X_POST_MAX && g_posts.count == 0);
    /* A session-changing subscriber must stop later subscribers and time_scale
     * iterations as well, even when its new server reuses the old pointer. */
    g_ntick = 0;
    on_server_tick(replace_session, NULL); on_server_tick(count_tick, NULL);
    int before_ticks = tick_calls, before_engine = engine_ticks, before_replacements = replacement_calls;
    g_time_scale = 4;
    h_tick(server);
    CHECK(tick_calls == before_ticks && engine_ticks == before_engine + 1);
    CHECK(replacement_calls == before_replacements && g_posts.count == 1);
    g_time_scale = 1; g_ntick = 0;
    tick_once(server);
    CHECK(replacement_calls == before_replacements + 1 && g_posts.count == 0);
    session_change(server, 0);
    g_ntick = 0;
    puts("PASS bounded production dispatch: nested posts, FIFO refill, reused session address, subscriber replacement and accelerated ticks");
}
int main(void) {
    InitializeCriticalSection(&g_log_cs);InitializeCriticalSection(&g_post_cs);
    queue_dispatch_test();
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
