#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdarg.h>
#include <cstring>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
// made by lol123love
static const char* kLogPath = "C:\\Users\\Public\\ltk_launcher.log";

static void Log(const char* fmt, ...)
{
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n >= (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 2;

    HANDLE h = CreateFileA(kLogPath, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    SetFilePointer(h, 0, NULL, FILE_END);
    DWORD w = 0;
    WriteFile(h, buf, (DWORD)n, &w, NULL);
    WriteFile(h, "\r\n", 2, &w, NULL);
    CloseHandle(h);
}

static void KillExistingLtkInstances()
{
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ltk-manager.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"LTK Manager.exe") == 0) {
                HANDLE hProc = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID);
                if (hProc) {
                    Log("[cleanup] terminating existing %ls (pid=%lu)", pe.szExeFile, pe.th32ProcessID);
                    TerminateProcess(hProc, 0);
                    WaitForSingleObject(hProc, 3000);
                    CloseHandle(hProc);
                }
                else {
                    Log("[warn] could not terminate existing %ls (pid=%lu) err=%lu (try running launcher as Admin)",
                        pe.szExeFile, pe.th32ProcessID, GetLastError());
                }
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
}

static DWORD FindLiveLtkPid()
{
    DWORD foundPid = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"ltk-manager.exe") == 0 ||
                _wcsicmp(pe.szExeFile, L"LTK Manager.exe") == 0) {
                foundPid = pe.th32ProcessID;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return foundPid;
}

static bool ResolveDllPath(wchar_t* outPath, size_t outLen)
{
    outPath[0] = 0;
    DWORD n = GetModuleFileNameW(NULL, outPath, (DWORD)outLen);
    if (n == 0 || n >= outLen) { Log("[err] GetModuleFileNameW"); return false; }
    wchar_t* slash = wcsrchr(outPath, L'\\');
    if (!slash) { Log("[err] no backslash"); return false; }
    *(slash + 1) = 0;
    if (wcslen(outPath) + 32 >= outLen) { Log("[err] buffer small"); return false; }
    wcscat_s(outPath, outLen, L"LeagueToolFlop.dll");

    if (GetFileAttributesW(outPath) == INVALID_FILE_ATTRIBUTES) {
        Log("[err] LeagueToolFlop.dll not found: %ls", outPath);
        return false;
    }
    Log("[resolve] LeagueToolFlop.dll = %ls", outPath);
    return true;
}

static void ResolveDefaultExe(wchar_t* outPath, size_t outLen)
{
    outPath[0] = 0;
    const wchar_t* directDefaults[] = {
        L"C:\\Program Files\\LTK Manager\\ltk-manager.exe",
        L"C:\\Program Files\\LTK Manager\\LTK Manager.exe",
    };
    for (auto candidate : directDefaults) {
        if (GetFileAttributesW(candidate) != INVALID_FILE_ATTRIBUTES) {
            wcscpy_s(outPath, outLen, candidate);
            Log("[resolve] found default exe: %ls", outPath);
            return;
        }
    }
    outPath[0] = 0;
    Log("[resolve] no candidate found");
}

static bool InjectDll(DWORD pid, const wchar_t* dllPath)
{
    HANDLE hProc = OpenProcess(
        PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
        PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
        FALSE, pid);
    if (!hProc) { Log("[err] OpenProcess(pid=%lu) err=%lu", pid, GetLastError()); return false; }

    SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, NULL, bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        Log("[err] VirtualAllocEx err=%lu", GetLastError());
        CloseHandle(hProc); return false;
    }

    if (!WriteProcessMemory(hProc, remote, dllPath, bytes, NULL)) {
        Log("[err] WriteProcessMemory err=%lu", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false;
    }

    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLib) {
        Log("[err] GetProcAddress");
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false;
    }

    HANDLE th = CreateRemoteThread(hProc, NULL, 0,
        (LPTHREAD_START_ROUTINE)loadLib, remote, 0, NULL);
    if (!th) {
        Log("[err] CreateRemoteThread err=%lu", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE); CloseHandle(hProc); return false;
    }

    DWORD wait = WaitForSingleObject(th, 15000);
    DWORD exitCode = 0;
    if (wait != WAIT_TIMEOUT) {
        GetExitCodeThread(th, &exitCode);
        Log("[inject] LoadLibraryW(pid=%lu) -> 0x%lX", pid, exitCode);
    }
    else {
        Log("[warn] inject timed out");
        exitCode = 1;
    }

    CloseHandle(th);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    CloseHandle(hProc);
    return exitCode != 0;
}

int main(int argc, char** argv)
{
    CreateDirectoryA("C:\\Users\\Public", NULL);
    Log("========== ltk_launcher ==========");

    wchar_t exePath[MAX_PATH] = {};
    bool    hideUI = true;
    bool    noHide = false;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-hide")) {
            noHide = true;
        }
        else if (!strcmp(argv[i], "--exe") && i + 1 < argc) {
            MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, exePath, MAX_PATH);
        }
        else if (argv[i][0] != '-') {
            if (exePath[0] == 0)
                MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, exePath, MAX_PATH);
        }
    }

    if (exePath[0] == 0) ResolveDefaultExe(exePath, MAX_PATH);

    if (exePath[0] == 0 || GetFileAttributesW(exePath) == INVALID_FILE_ATTRIBUTES) {
        Log("[err] ltk-manager.exe not found");
        MessageBoxW(NULL, L"ltk-manager.exe not found", L"LTK Bridge Launcher", MB_ICONERROR);
        return 2;
    }

    wchar_t dllPath[MAX_PATH] = {};
    if (!ResolveDllPath(dllPath, MAX_PATH)) {
        MessageBoxA(NULL, "LeagueToolFlop.dll must be in the same folder as ltk_launcher.exe.", "LTK Bridge Launcher", MB_ICONERROR);
        return 1;
    }

    KillExistingLtkInstances();

    SetEnvironmentVariableW(L"WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
        L"--remote-debugging-port=9222 --remote-allow-origins=*");

    if (noHide) {
        SetEnvironmentVariableW(L"LTK_NO_HIDE", L"1");
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hideUI && !noHide) {
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION pi{};

    wchar_t cmdLine[MAX_PATH + 256];
    swprintf_s(cmdLine, L"\"%s\"", exePath);

    wchar_t workDir[MAX_PATH] = {};
    wcscpy_s(workDir, exePath);
    if (wchar_t* slash = wcsrchr(workDir, L'\\')) *slash = 0;

    DWORD flags = CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP;

    if (!CreateProcessW(exePath, cmdLine, NULL, NULL,
        FALSE, flags, NULL, workDir[0] ? workDir : NULL, &si, &pi)) {
        Log("[err] CreateProcessW err=%lu", GetLastError());
        return 3;
    }
    Log("[launch] spawned pid = %lu (suspended)", pi.dwProcessId);

    if (!InjectDll(pi.dwProcessId, dllPath)) {
        Log("[err] injection failed — terminating ltk-manager.exe");
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return 4;
    }

    ResumeThread(pi.hThread);

    if (WaitForSingleObject(pi.hProcess, 500) == WAIT_OBJECT_0) {
        DWORD procExit = 0;
        GetExitCodeProcess(pi.hProcess, &procExit);
        Log("[warn] spawned pid=%lu exited with code %lu! Searching for live ltk-manager.exe...", pi.dwProcessId, procExit);
        DWORD livePid = FindLiveLtkPid();
        if (livePid && livePid != pi.dwProcessId) {
            Log("[re-inject] injecting into live ltk-manager.exe pid=%lu", livePid);
            InjectDll(livePid, dllPath);
        }
    }
    else {
        Log("[launch] pid=%lu is alive and running!", pi.dwProcessId);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    Log("========== launcher exiting ==========");
    return 0;
}
