// SPDX-License-Identifier: GPL-3.0-only
/*
 *  BetterNMM - Nexus Mods mod manager (fork of Prism Launcher)
 *  Copyright (C) 2026 BetterNMM Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "Install.h"

#include <windows.h>

#include <inspectable.h>
#include <objbase.h>
#include <propidl.h>
#include <psapi.h>
#include <roapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winstring.h>

#include <algorithm>
#include <ctime>
#include <set>

#include "Files.h"
#include "Payload.h"

namespace Setup {

namespace {

const std::wstring k_uninstallPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\BetterNMM";
const std::wstring k_testKey = L"Software\\BetterNMM-SetupTest";

std::wstring knownFolder(REFKNOWNFOLDERID id)
{
    wchar_t* path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path))) {
        out = path;
    }
    CoTaskMemFree(path);
    return out;
}

const wchar_t* scopeName(Scope scope)
{
    return scope == Scope::User ? L"User" : L"Machine";
}

bool setString(HKEY key, const wchar_t* name, const std::wstring& value)
{
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool setDword(HKEY key, const wchar_t* name, DWORD value)
{
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value)) == ERROR_SUCCESS;
}

std::wstring getString(HKEY root, const std::wstring& path, const wchar_t* name)
{
    wchar_t buffer[2048];
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, path.c_str(), name, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

bool sameFile(const std::wstring& a, const std::wstring& b)
{
    return Files::lower(a) == Files::lower(b);
}

std::wstring exePath(const std::wstring& dir)
{
    return dir + L"\\" + k_exe;
}

/// The program a shortcut starts, or nothing when it can not be read
std::wstring shortcutTarget(const std::wstring& lnk)
{
    std::wstring out;
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        return out;
    }
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        if (SUCCEEDED(file->Load(lnk.c_str(), STGM_READ))) {
            wchar_t path[MAX_PATH] = {};
            if (SUCCEEDED(link->GetPath(path, MAX_PATH, nullptr, SLGP_RAWPATH))) {
                out = path;
            }
        }
        file->Release();
    }
    link->Release();
    return out;
}

bool makeShortcut(const std::wstring& lnk, const std::wstring& dir, std::wstring& error)
{
    if (!Files::makeParent(lnk)) {
        error = L"Could not create the folder for " + lnk;
        return false;
    }
    IShellLinkW* link = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link));
    if (FAILED(hr)) {
        error = L"Windows could not make a shortcut: " + Files::systemMessage(hr);
        return false;
    }
    const auto exe = exePath(dir);
    link->SetPath(exe.c_str());
    link->SetWorkingDirectory(dir.c_str());
    link->SetIconLocation(exe.c_str(), 0);
    link->SetDescription(L"Nexus Mods mod manager with a separate game instance per mod setup");
    // the shell matches the pin, the Start menu entry and the running windows by this
    IPropertyStore* store = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPropertyStore, reinterpret_cast<void**>(&store)))) {
        static const PROPERTYKEY appUserModelId = { { 0x9f4c2855, 0x9f79, 0x4b39, { 0xa8, 0xd0, 0xe1, 0xd4, 0x2d, 0xe1, 0xd5, 0xf3 } }, 5 };
        PROPVARIANT value;
        PropVariantInit(&value);
        value.vt = VT_LPWSTR;
        value.pwszVal = const_cast<wchar_t*>(k_appId);
        store->SetValue(appUserModelId, value);
        store->Commit();
        store->Release();
    }
    IPersistFile* file = nullptr;
    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
    if (SUCCEEDED(hr)) {
        hr = file->Save(lnk.c_str(), TRUE);
        file->Release();
    }
    link->Release();
    if (FAILED(hr)) {
        error = L"Could not save the shortcut " + lnk + L": " + Files::systemMessage(hr);
        return false;
    }
    return true;
}

/// Removes a shortcut only when it is BetterNMM's
void removeShortcut(const std::wstring& lnk, const std::wstring& dir)
{
    if (Files::exists(lnk) && sameFile(shortcutTarget(lnk), exePath(dir))) {
        DeleteFileW(lnk.c_str());
        log(L"removed shortcut " + lnk);
    }
}

std::vector<std::wstring> readFileList(const std::wstring& dir)
{
    std::vector<std::wstring> out;
    std::string data;
    if (!Files::readAll(dir + L"\\" + k_fileList, data)) {
        return out;
    }
    size_t at = 0;
    while (at < data.size()) {
        auto end = data.find('\n', at);
        if (end == std::string::npos) {
            end = data.size();
        }
        auto line = data.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // only plain relative paths: a damaged list must never reach outside the install folder
        if (!line.empty() && line.find("..") == std::string::npos && line.find(':') == std::string::npos && line[0] != '\\' &&
            line[0] != '/') {
            out.push_back(Files::wide(line));
        }
        at = end + 1;
    }
    return out;
}

std::wstring today()
{
    const auto now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t buffer[16];
    wcsftime(buffer, 16, L"%Y%m%d", &local);
    return buffer;
}

}  // namespace

HKEY Locations::uninstallRoot(Scope scope) const
{
    return testing() || scope == Scope::User ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
}

std::wstring Locations::uninstallKey(Scope scope) const
{
    return testing() ? k_testKey + L"\\" + scopeName(scope) + L"\\Uninstall\\BetterNMM" : k_uninstallPath;
}

std::wstring Locations::startMenuDir(Scope scope) const
{
    if (testing()) {
        return testRoot + L"\\StartMenu\\" + scopeName(scope);
    }
    return knownFolder(scope == Scope::User ? FOLDERID_Programs : FOLDERID_CommonPrograms);
}

std::wstring Locations::desktopDir(Scope scope) const
{
    if (testing()) {
        return testRoot + L"\\Desktop\\" + scopeName(scope);
    }
    return knownFolder(scope == Scope::User ? FOLDERID_Desktop : FOLDERID_PublicDesktop);
}

std::wstring Locations::classesKey() const
{
    return testing() ? k_testKey + L"\\Classes" : L"Software\\Classes";
}

std::wstring Options::toString() const
{
    return std::wstring(L"desktop=") + (desktop ? L"1" : L"0") + L";startmenu=" + (startMenu ? L"1" : L"0") + L";pin=" +
           (pin ? L"1" : L"0") + L";nxm=" + (nxm ? L"1" : L"0") + L";launch=" + (launch ? L"1" : L"0");
}

Options Options::fromString(const std::wstring& text, Options defaults)
{
    auto read = [&text](const wchar_t* name, bool fallback) {
        const auto at = text.find(std::wstring(name) + L"=");
        if (at == std::wstring::npos) {
            return fallback;
        }
        const auto value = at + wcslen(name) + 1;
        return value < text.size() ? text[value] == L'1' : fallback;
    };
    Options out;
    out.desktop = read(L"desktop", defaults.desktop);
    out.startMenu = read(L"startmenu", defaults.startMenu);
    out.pin = read(L"pin", defaults.pin);
    out.nxm = read(L"nxm", defaults.nxm);
    out.launch = read(L"launch", defaults.launch);
    return out;
}

Existing findExisting(const Locations& where)
{
    Existing out;
    for (const auto scope : { Scope::User, Scope::Machine }) {
        const auto dir = getString(where.uninstallRoot(scope), where.uninstallKey(scope), L"InstallLocation");
        if (dir.empty() || !Files::exists(exePath(dir))) {
            continue;
        }
        out.found = true;
        out.scope = scope;
        out.dir = dir;
        out.version = getString(where.uninstallRoot(scope), where.uninstallKey(scope), L"DisplayVersion");
        out.options = Options::fromString(getString(where.uninstallRoot(scope), where.uninstallKey(scope), L"BetterNMMOptions"), Options{});
        return out;
    }
    return out;
}

std::wstring defaultDir(Scope scope, const Locations& where)
{
    if (where.testing()) {
        return where.testRoot + L"\\Programs\\" + scopeName(scope) + L"\\BetterNMM";
    }
    return knownFolder(scope == Scope::User ? FOLDERID_UserProgramFiles : FOLDERID_ProgramFiles) + L"\\BetterNMM";
}

bool installFiles(const Plan& plan, const Progress& progress, std::wstring& error)
{
    log(L"installing into " + plan.dir + L" (" + scopeName(plan.scope) + L"), " + plan.options.toString());
    progress(0, L"Reading the setup file...");
    Payload::Index index;
    if (!Payload::read(plan.setupExe, index, error)) {
        return false;
    }
    // unpacked beside it first, so nothing changes until every file is there
    const unsigned long long margin = 64ull << 20;
    if (const auto free = Files::freeSpace(plan.dir); free < index.totalSize + margin) {
        error = L"BetterNMM needs " + formatSize(index.totalSize + margin) + L" on that drive, which has " + formatSize(free) + L" free.";
        return false;
    }
    if (!Files::makeDirs(plan.dir)) {
        error = L"Could not create " + plan.dir + L": " + Files::systemMessage(GetLastError());
        return false;
    }
    const auto staging = plan.dir + L"\\.setup-new";
    const auto aside = plan.dir + L"\\.setup-old";
    Files::removeTree(staging);
    Files::removeTree(aside);
    auto cleanup = [&] {
        Files::removeTree(staging);
        Files::removeTree(aside);
    };

    if (!Payload::extract(
            plan.setupExe, index, staging,
            [&](uint64_t done) {
                progress(0.9 * static_cast<double>(done) / static_cast<double>(std::max<uint64_t>(index.totalSize, 1)),
                         L"Unpacking files...");
            },
            error)) {
        cleanup();
        return false;
    }
    std::vector<std::wstring> files;
    for (const auto& file : index.files) {
        files.push_back(file.path);
    }
    // the uninstaller is this program without the files it carries
    std::string self;
    if (!Files::readAll(plan.setupExe, self) || self.size() < index.stubSize ||
        !Files::writeAll(staging + L"\\" + k_uninstaller, self.substr(0, static_cast<size_t>(index.stubSize)))) {
        error = L"Could not write the uninstaller: " + Files::systemMessage(GetLastError());
        cleanup();
        return false;
    }
    files.push_back(k_uninstaller);
    std::string list;
    for (const auto& file : files) {
        list += Files::utf8(file) + "\n";
    }
    if (!Files::writeAll(staging + L"\\" + k_fileList, list)) {
        error = L"Could not write the file list: " + Files::systemMessage(GetLastError());
        cleanup();
        return false;
    }
    files.push_back(k_fileList);

    // swap: what is there goes aside, the new file takes its place; any failure puts everything back
    progress(0.9, L"Putting the files in place...");
    std::vector<std::wstring> setAside;
    std::vector<std::wstring> placed;
    auto rollback = [&] {
        for (const auto& rel : placed) {
            DeleteFileW((plan.dir + L"\\" + rel).c_str());
        }
        for (const auto& rel : setAside) {
            MoveFileExW((aside + L"\\" + rel).c_str(), (plan.dir + L"\\" + rel).c_str(), MOVEFILE_REPLACE_EXISTING);
        }
        cleanup();
    };
    for (const auto& rel : files) {
        const auto target = plan.dir + L"\\" + rel;
        if (Files::exists(target)) {
            if (!Files::makeParent(aside + L"\\" + rel) ||
                !MoveFileExW(target.c_str(), (aside + L"\\" + rel).c_str(), MOVEFILE_REPLACE_EXISTING)) {
                error = L"Could not replace " + rel + L": " + Files::systemMessage(GetLastError()) +
                        L". A program may have it open. Nothing was changed.";
                rollback();
                return false;
            }
            setAside.push_back(rel);
        }
        if (!Files::makeParent(target) || !MoveFileExW((staging + L"\\" + rel).c_str(), target.c_str(), 0)) {
            error = L"Could not put " + rel + L" in place: " + Files::systemMessage(GetLastError()) + L". Nothing was changed.";
            rollback();
            return false;
        }
        placed.push_back(rel);
    }
    // files of the version before that this one no longer has
    std::set<std::wstring> current;
    for (const auto& rel : files) {
        current.insert(Files::lower(rel));
    }
    progress(0.95, L"Removing files of the old version...");
    for (const auto& rel : readFileList(aside)) {
        if (current.count(Files::lower(rel)) == 0) {
            const auto target = plan.dir + L"\\" + rel;
            if (DeleteFileW(target.c_str())) {
                Files::pruneEmpty(Files::parent(target), plan.dir);
                log(L"removed " + rel + L" of the old version");
            }
        }
    }
    cleanup();

    // the download window is a third of BetterNMM: compressed the way Windows compresses its own files, like a build
    // installs it (cmake --install); drives without NTFS keep it as it is
    if (Files::isDirectory(plan.dir + L"\\browser")) {
        progress(0.96, L"Compressing the Nexus Mods window's files...");
        std::wstring command = L"compact.exe /c /s:\"" + plan.dir + L"\\browser\" /exe:lzx /q /i";
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
            WaitForSingleObject(process.hProcess, 10 * 60 * 1000);
            CloseHandle(process.hProcess);
            CloseHandle(process.hThread);
        } else {
            log(L"compact could not run: " + Files::systemMessage(GetLastError()));
        }
    }

    // Apps & Features
    HKEY key = nullptr;
    const auto root = plan.where.uninstallRoot(plan.scope);
    if (RegCreateKeyExW(root, plan.where.uninstallKey(plan.scope).c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        error = L"Could not register BetterNMM with Windows' Apps list: " + Files::systemMessage(GetLastError());
        return false;
    }
    const auto uninstaller = plan.dir + L"\\" + k_uninstaller;
    bool ok = setString(key, L"DisplayName", k_appName) && setString(key, L"DisplayVersion", index.version) &&
              setString(key, L"Publisher", k_publisher) && setString(key, L"DisplayIcon", exePath(plan.dir) + L",0") &&
              setString(key, L"InstallLocation", plan.dir) && setString(key, L"UninstallString", L"\"" + uninstaller + L"\"") &&
              setString(key, L"QuietUninstallString", L"\"" + uninstaller + L"\" --silent") && setString(key, L"InstallDate", today()) &&
              setDword(key, L"EstimatedSize", static_cast<DWORD>(index.totalSize / 1024)) && setDword(key, L"NoModify", 1) &&
              setDword(key, L"NoRepair", 1) && setString(key, L"BetterNMMOptions", plan.options.toString());
    RegCloseKey(key);
    if (!ok) {
        error = L"Could not register BetterNMM with Windows' Apps list.";
        return false;
    }

    progress(0.98, L"Making shortcuts...");
    const auto startMenu = plan.where.startMenuDir(plan.scope) + L"\\BetterNMM.lnk";
    const auto desktop = plan.where.desktopDir(plan.scope) + L"\\BetterNMM.lnk";
    // a shortcut turned off on an update goes; only ever BetterNMM's own
    if (plan.options.startMenu) {
        if (!makeShortcut(startMenu, plan.dir, error)) {
            return false;
        }
    } else {
        removeShortcut(startMenu, plan.dir);
    }
    if (plan.options.desktop) {
        if (!makeShortcut(desktop, plan.dir, error)) {
            return false;
        }
    } else {
        removeShortcut(desktop, plan.dir);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    progress(1, L"Done");
    log(L"installed " + index.version + L": " + std::to_wstring(index.files.size()) + L" files");
    return true;
}

bool installForUser(const Plan& plan, std::wstring& error)
{
    if (!plan.options.nxm) {
        return true;
    }
    // the same entries BetterNMM's own Settings writes, so it sees itself as the handler
    const auto exe = exePath(plan.dir);
    const auto base = plan.where.classesKey() + L"\\nxm";
    struct Entry {
        std::wstring key;
        const wchar_t* name;
        std::wstring value;
    };
    const Entry entries[] = { { base, nullptr, L"URL:NXM Protocol" },
                              { base, L"URL Protocol", L"" },
                              { base + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0" },
                              { base + L"\\shell\\open\\command", nullptr, L"\"" + exe + L"\" \"%1\"" } };
    for (const auto& entry : entries) {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, entry.key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
            error = L"Windows did not accept the nxm:// link registration.";
            return false;
        }
        const bool ok = setString(key, entry.name, entry.value);
        RegCloseKey(key);
        if (!ok) {
            error = L"Windows did not accept the nxm:// link registration.";
            return false;
        }
    }
    log(L"registered for nxm:// links");
    return true;
}

// Windows.UI.Shell.TaskbarManager, called through its ABI: MinGW has no headers for it. These stay out of an anonymous
// namespace: with internal linkage GCC sees no class implementing them and turns every call into __cxa_pure_virtual.
namespace WinRtAbi {
struct ITaskbarManagerStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetDefault(IInspectable** manager) = 0;
};
struct IAsyncOperationOfBool : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(unsigned char* result) = 0;
};
struct ITaskbarManager : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_IsSupported(unsigned char* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsPinningAllowed(unsigned char* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsCurrentAppPinnedAsync(IAsyncOperationOfBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE IsAppListEntryPinnedAsync(IInspectable* entry, IAsyncOperationOfBool** operation) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestPinCurrentAppAsync(IAsyncOperationOfBool** operation) = 0;
};
struct IAsyncInfoAbi : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(unsigned* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0;
};
}  // namespace WinRtAbi
using namespace WinRtAbi;

namespace {

const IID IID_TaskbarManagerStatics = { 0xdb32ab74, 0xde52, 0x4fe6, { 0xb7, 0xb6, 0x95, 0xff, 0x9f, 0x83, 0x95, 0xdf } };
const IID IID_TaskbarManagerDesktopSupport = { 0xcdfefd63, 0xe879, 0x4134, { 0xb9, 0xa7, 0x82, 0x83, 0xf0, 0x5f, 0x94, 0x80 } };
const IID IID_TaskbarManager = { 0x87490a19, 0x1ad9, 0x49f4, { 0xb2, 0xe8, 0x86, 0x73, 0x8d, 0xc5, 0xac, 0x40 } };
const IID IID_AsyncInfo = { 0x00000036, 0x0000, 0x0000, { 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };

void pumpFor(DWORD milliseconds)
{
    const DWORD until = GetTickCount() + milliseconds;
    for (DWORD now = GetTickCount(); now < until; now = GetTickCount()) {
        MsgWaitForMultipleObjects(0, nullptr, FALSE, until - now, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

template <typename T>
void release(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

}  // namespace

bool requestPin(const Plan& plan, std::wstring& outcome)
{
    const std::wstring manual = L"Right-click BetterNMM in the Start menu and choose Pin to taskbar.";
    RoInitialize(RO_INIT_SINGLETHREADED);
    HSTRING name = nullptr;
    const wchar_t* className = L"Windows.UI.Shell.TaskbarManager";
    WindowsCreateString(className, static_cast<UINT32>(wcslen(className)), &name);
    ITaskbarManagerStatics* statics = nullptr;
    IInspectable* desktopSupport = nullptr;
    IInspectable* managerObject = nullptr;
    ITaskbarManager* manager = nullptr;
    IAsyncOperationOfBool* operation = nullptr;
    IAsyncInfoAbi* info = nullptr;
    bool pinned = false;
    unsigned char supported = 0;
    if (FAILED(RoGetActivationFactory(name, IID_TaskbarManagerStatics, reinterpret_cast<void**>(&statics))) ||
        FAILED(statics->QueryInterface(IID_TaskbarManagerDesktopSupport, reinterpret_cast<void**>(&desktopSupport)))) {
        outcome = L"This version of Windows does not let programs ask to be pinned. " + manual;
    } else if (FAILED(statics->GetDefault(&managerObject)) ||
               FAILED(managerObject->QueryInterface(IID_TaskbarManager, reinterpret_cast<void**>(&manager))) ||
               FAILED(manager->get_IsSupported(&supported)) || !supported) {
        outcome = L"Windows has no taskbar to pin to here.";
    } else if (plan.where.testing()) {
        // everything up to the question Windows would ask the person
        outcome = L"Windows supports asking to pin BetterNMM; the request itself is left out when testing.";
    } else {
        // Windows pins what the Start menu entry with this id starts; the entry can take a moment to appear
        if (plan.options.startMenu) {
            for (int tries = 0; tries < 25; tries++) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName((std::wstring(L"shell:AppsFolder\\") + k_appId).c_str(), nullptr, IID_IShellItem,
                                                          reinterpret_cast<void**>(&item)))) {
                    item->Release();
                    break;
                }
                pumpFor(200);
            }
        }
        SetCurrentProcessExplicitAppUserModelID(k_appId);
        const HRESULT hr = manager->RequestPinCurrentAppAsync(&operation);
        if (SUCCEEDED(hr) && SUCCEEDED(operation->QueryInterface(IID_AsyncInfo, reinterpret_cast<void**>(&info)))) {
            // Windows asks the person; this window keeps painting meanwhile
            int status = 0;
            while (SUCCEEDED(info->get_Status(&status)) && status == 0) {
                pumpFor(50);
            }
            unsigned char result = 0;
            if (status == 1 && SUCCEEDED(operation->GetResults(&result))) {
                pinned = result != 0;
            }
            outcome = pinned ? L"BetterNMM is pinned to the taskbar." : L"BetterNMM was not pinned. " + manual;
        } else {
            outcome = L"Windows did not take the request to pin BetterNMM (" + Files::systemMessage(hr) + L"). " + manual;
        }
        SetCurrentProcessExplicitAppUserModelID(L"io.github.betternmm.Setup");
    }
    release(info);
    release(operation);
    release(manager);
    release(managerObject);
    release(desktopSupport);
    release(statics);
    WindowsDeleteString(name);
    log(L"pin: " + outcome);
    return pinned;
}

bool launch(const Plan& plan)
{
    if (plan.where.testing()) {
        log(L"launch left out when testing");
        return false;
    }
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", exePath(plan.dir).c_str(), nullptr, plan.dir.c_str(), SW_SHOWNORMAL)) >
           32;
}

bool uninstallFiles(const Existing& existing, const Locations& where, const Progress& progress, std::wstring& error)
{
    log(L"uninstalling " + existing.dir);
    progress(0, L"Removing shortcuts...");
    removeShortcut(where.startMenuDir(existing.scope) + L"\\BetterNMM.lnk", existing.dir);
    removeShortcut(where.desktopDir(existing.scope) + L"\\BetterNMM.lnk", existing.dir);
    const auto files = readFileList(existing.dir);
    if (files.empty()) {
        error = L"The list of BetterNMM's files is missing from " + existing.dir + L"; nothing was removed.";
        return false;
    }
    size_t done = 0;
    std::vector<std::wstring> stuck;
    bool uninstallerLeft = false;
    for (const auto& rel : files) {
        const auto path = existing.dir + L"\\" + rel;
        if (!DeleteFileW(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) {
            // the uninstaller that started this copy may still be waiting for it to report back
            if (Files::lower(rel) == Files::lower(k_uninstaller)) {
                uninstallerLeft = true;
            } else {
                stuck.push_back(rel);
            }
        } else {
            Files::pruneEmpty(Files::parent(path), existing.dir);
        }
        progress(0.9 * static_cast<double>(++done) / static_cast<double>(files.size()), L"Removing files...");
    }
    if (!stuck.empty()) {
        error = L"Could not remove " + stuck.front() + L" and " + std::to_wstring(stuck.size() - 1) +
                L" other file(s): a program may have them open. Close BetterNMM and uninstall again.";
        return false;
    }
    DeleteFileW((existing.dir + L"\\" + k_fileList).c_str());
    if (uninstallerLeft) {
        deleteLater(existing.dir + L"\\" + k_uninstaller, existing.dir);
    }
    // only empty: a portable UserData folder or anything else someone put there stays
    RemoveDirectoryW(existing.dir.c_str());
    RegDeleteTreeW(where.uninstallRoot(existing.scope), where.uninstallKey(existing.scope).c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    progress(1, L"Done");
    log(L"uninstalled " + existing.dir);
    return true;
}

bool uninstallForUser(const Existing& existing, const Locations& where)
{
    const auto base = where.classesKey() + L"\\nxm";
    const auto command = getString(HKEY_CURRENT_USER, base + L"\\shell\\open\\command", nullptr);
    // links stay with Vortex or Mod Organizer when they took them over since
    if (!command.empty() && Files::lower(command) == Files::lower(L"\"" + exePath(existing.dir) + L"\" \"%1\"")) {
        RegDeleteTreeW(HKEY_CURRENT_USER, base.c_str());
        log(L"removed the nxm:// link registration");
    }
    return true;
}

void deleteLater(const std::wstring& file, const std::wstring& dir)
{
    std::wstring command = L"cmd.exe /c ping -n 4 127.0.0.1 >nul & del /f /q \"" + file + L"\"";
    if (!dir.empty()) {
        command += L" & rmdir \"" + dir + L"\"";
    }
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION process{};
    if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
    }
}

std::vector<std::wstring> runningFrom(const std::wstring& dir, DWORD ignore)
{
    std::vector<std::wstring> out;
    DWORD ids[4096];
    DWORD bytes = 0;
    if (!EnumProcesses(ids, sizeof(ids), &bytes)) {
        return out;
    }
    const auto prefix = Files::lower(dir) + L"\\";
    for (DWORD i = 0; i < bytes / sizeof(DWORD); i++) {
        if (ids[i] == GetCurrentProcessId() || ids[i] == ignore) {
            continue;
        }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ids[i]);
        if (!process) {
            continue;
        }
        wchar_t path[MAX_PATH * 2];
        DWORD size = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(process, 0, path, &size) && Files::lower(path).rfind(prefix, 0) == 0) {
            const std::wstring full = path;
            const auto name = full.substr(full.find_last_of(L'\\') + 1);
            if (std::find(out.begin(), out.end(), name) == out.end()) {
                out.push_back(name);
            }
        }
        CloseHandle(process);
    }
    return out;
}

bool isElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        return false;
    }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return elevated;
}

std::wstring formatSize(unsigned long long bytes)
{
    wchar_t buffer[32];
    if (bytes >= (1ull << 30)) {
        swprintf(buffer, 32, L"%.1f GB", bytes / 1073741824.0);
    } else {
        swprintf(buffer, 32, L"%.0f MB", bytes / 1048576.0);
    }
    return buffer;
}

void log(const std::wstring& line)
{
    wchar_t temp[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, temp)) {
        return;
    }
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t stamp[32];
    swprintf(stamp, 32, L"%04d-%02d-%02d %02d:%02d:%02d ", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    HANDLE file = CreateFileW((std::wstring(temp) + L"BetterNMM-Setup.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    const auto text = Files::utf8(stamp + line + L"\r\n");
    DWORD written = 0;
    WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
}

}  // namespace Setup
