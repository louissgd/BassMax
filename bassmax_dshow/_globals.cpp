#include <windows.h>
#include <streams.h>
HINSTANCE g_hInst = nullptr;
DWORD     g_amPlatform = VER_PLATFORM_WIN32_NT;
OSVERSIONINFO g_osInfo = {};
CFactoryTemplate g_Templates[1] = {};
int       g_cTemplates = 0;
