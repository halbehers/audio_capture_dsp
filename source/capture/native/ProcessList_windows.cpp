#include "../../../include/capture/ProcessList.h"

#include <windows.h>
#include <tlhelp32.h>

#include <cctype>
#include <cwchar>
#include <unordered_map>
#include <vector>

namespace audiocapture
{

namespace
{
    // Win32's process/window APIs hand back UTF-16 (wchar_t); ProcessInfo's fields are UTF-8
    // std::string, so every string obtained from them needs an explicit conversion - there's no
    // implicit wide -> std::string constructor the way there was with juce::String.
    std::string wideToUtf8(const wchar_t* wideStr, int wideLength)
    {
        if (wideStr == nullptr || wideLength <= 0)
            return {};

        int utf8Length = WideCharToMultiByte(CP_UTF8, 0, wideStr, wideLength, nullptr, 0, nullptr, nullptr);
        if (utf8Length <= 0)
            return {};

        std::string result((size_t) utf8Length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideStr, wideLength, result.data(), utf8Length, nullptr, nullptr);
        return result;
    }

    bool endsWithIgnoreCaseExe(const std::string& s)
    {
        if (s.size() < 4)
            return false;

        std::string tail = s.substr(s.size() - 4);
        for (auto& c : tail)
            c = (char) std::tolower((unsigned char) c);
        return tail == ".exe";
    }

    struct MainAppWindowInfo
    {
        std::string title;
    };

    // There's no Windows equivalent of macOS's NSRunningApplication.activationPolicy, so this
    // approximates "is this a real user-facing app" the way most similar tools do: does the
    // process own at least one top-level window that's visible, titled, ownerless, and not a
    // tool window.
    BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM lParam)
    {
        auto* map = reinterpret_cast<std::unordered_map<DWORD, MainAppWindowInfo>*>(lParam);

        if (! IsWindowVisible(hwnd))
            return TRUE;
        if (GetWindow(hwnd, GW_OWNER) != nullptr)
            return TRUE;
        if ((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
            return TRUE;

        int titleLength = GetWindowTextLengthW(hwnd);
        if (titleLength <= 0)
            return TRUE;

        std::vector<wchar_t> titleBuffer((size_t) titleLength + 1);
        GetWindowTextW(hwnd, titleBuffer.data(), titleLength + 1);

        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == 0)
            return TRUE;

        // First visible top-level window found for a given pid wins - later ones are ignored.
        map->emplace(pid, MainAppWindowInfo { wideToUtf8(titleBuffer.data(), titleLength) });

        return TRUE;
    }

    std::string getExecutablePath(DWORD processID)
    {
        HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processID);
        if (processHandle == nullptr)
            return {};

        wchar_t pathBuffer[MAX_PATH];
        DWORD pathLength = (DWORD) (sizeof(pathBuffer) / sizeof(pathBuffer[0]));

        std::string path;
        if (QueryFullProcessImageNameW(processHandle, 0, pathBuffer, &pathLength))
            path = wideToUtf8(pathBuffer, (int) pathLength);

        CloseHandle(processHandle);
        return path;
    }
}

bool ProcessList::isSupported()
{
    return true;
}

std::vector<ProcessInfo> ProcessList::getAllProcesses()
{
    std::vector<ProcessInfo> result;

    std::unordered_map<DWORD, MainAppWindowInfo> mainAppWindows;
    EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&mainAppWindows));

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            ProcessInfo info;
            info.processID = (int) entry.th32ProcessID;
            info.parentProcessID = (int) entry.th32ParentProcessID;
            info.executablePath = getExecutablePath(entry.th32ProcessID);

            std::string exeName = wideToUtf8(entry.szExeFile, (int) wcslen(entry.szExeFile));
            if (endsWithIgnoreCaseExe(exeName))
                exeName.resize(exeName.size() - 4);

            auto windowIt = mainAppWindows.find(entry.th32ProcessID);
            if (windowIt != mainAppWindows.end())
            {
                info.isMainApplication = true;
                info.name = ! windowIt->second.title.empty() ? windowIt->second.title : exeName;
            }
            else
            {
                info.isMainApplication = false;
                info.name = exeName;
            }

            result.push_back(std::move(info));
        }
        while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return result;
}

}
