/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Own address-free executable, named carrier_command.exe in a temporary game
 * folder. Validate the exact IAT adapter before starting a real copied game. */
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char expected[MAX_PATH],actual[MAX_PATH];
    if(!GetEnvironmentVariableA("CC2X_TEST_PROFILE_ROOT",expected,MAX_PATH))return 2;
    HMODULE proxy=LoadLibraryW(L"steam_api.dll");
    if(!proxy){printf("load failed %lu\n",GetLastError());return 3;}
    if(!GetProcAddress(proxy,"SteamAPI_Init")){printf("forwarder failed %lu\n",GetLastError());return 6;}
    if(SHGetFolderPathA(NULL,CSIDL_APPDATA|CSIDL_FLAG_CREATE,NULL,0,actual)!=S_OK)return 4;
    if(_stricmp(actual,expected)){printf("profile was not isolated\n");return 5;}
    puts("PASS own executable: actual SHGetFolderPathA import returns only the private profile");
    return 0;
}
