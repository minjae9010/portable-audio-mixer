#pragma once
#include "cable_bundle_manifest.hpp"
#include "virtual_microphone.hpp"
#include "cable_install_finish.hpp"
#include <windows.h>
#include <objbase.h>
#include <sddl.h>
#include <wintrust.h>
#include <softpub.h>
#include <string>
#include <vector>
#include <stdexcept>

namespace cable_installer {
struct Resource { const void* bytes; DWORD size; };
inline Resource resource(HMODULE module, const cable_bundle::Entry& entry) {
    HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(entry.id), RT_RCDATA);
    if (!found || SizeofResource(module, found) != entry.size) throw std::runtime_error("Missing embedded VB-CABLE resource");
    HGLOBAL loaded = LoadResource(module, found);
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (!data) throw std::runtime_error("Cannot read embedded VB-CABLE resource");
    return {data, entry.size};
}
inline bool trustedInstaller(const std::wstring& path) {
    WINTRUST_FILE_INFO file{}; file.cbStruct = sizeof(file); file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data{}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE; data.dwUnionChoice = WTD_CHOICE_FILE; data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    // Offline installation: use the local trust store without downloading certs.
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &policy, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &data);
    return result == ERROR_SUCCESS;
}
class Package {
    std::wstring folder_;
    HANDLE directory_ = INVALID_HANDLE_VALUE;
    std::vector<std::wstring> files_;
    std::vector<HANDLE> locks_;
    void cleanup() noexcept {
        for (HANDLE h : locks_) CloseHandle(h);
        locks_.clear();
        for (const auto& file : files_) DeleteFileW(file.c_str());
        if (directory_ != INVALID_HANDLE_VALUE) { CloseHandle(directory_); directory_ = INVALID_HANDLE_VALUE; }
        if (!folder_.empty()) RemoveDirectoryW(folder_.c_str());
    }
public:
    Package() {
        try {
            wchar_t temp[32768]{};
            DWORD length = GetTempPathW(32768, temp);
            if (!length || length >= 32768) throw std::runtime_error("Cannot locate temporary directory");
            GUID id{}; wchar_t guid[40]{};
            if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, guid, 40)) throw std::runtime_error("Cannot create package identifier");
            const std::wstring candidate = std::wstring(temp) + L"PortableMixer-VBCABLE-" + guid;
            // A privileged installer must not execute out of a directory writable
            // by a non-elevated user. Owner is Administrators, not the user SID.
            struct Descriptor {
                PSECURITY_DESCRIPTOR value = nullptr;
                ~Descriptor() { if (value) LocalFree(value); }
            } security;
            if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", SDDL_REVISION_1, &security.value, nullptr))
                throw std::runtime_error("Cannot secure extraction folder");
            SECURITY_ATTRIBUTES sa{sizeof(sa), security.value, FALSE};
            const BOOL created = CreateDirectoryW(candidate.c_str(), &sa);
            if (!created) throw std::runtime_error("Cannot create protected extraction folder");
            folder_ = candidate;
            directory_ = CreateFileW(folder_.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (directory_ == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock extraction folder");
            for (const auto& entry : cable_bundle::entries) {
                const auto data = resource(GetModuleHandleW(nullptr), entry);
                const std::wstring path = folder_ + L"\\" + entry.name;
                HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, &sa,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot extract driver package");
                locks_.push_back(file); files_.push_back(path);
                DWORD written = 0;
                if (!WriteFile(file, data.bytes, data.size, &written, nullptr) || written != data.size || !FlushFileBuffers(file))
                    throw std::runtime_error("Cannot write complete driver package");
                // Vendor readers need not share write access. Hold read-only
                // locks while setup runs; no replacement/deletion is allowed.
                CloseHandle(locks_.back()); locks_.pop_back();
                file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
                if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot lock extracted package");
                locks_.push_back(file);
            }
            if (!trustedInstaller(setup())) throw std::runtime_error("Embedded vendor installer signature is not trusted on this PC");
        } catch (...) { cleanup(); throw; }
    }
    ~Package() { cleanup(); }
    Package(const Package&) = delete;
    std::wstring setup() const { return folder_ + L"\\VBCABLE_Setup_x64.exe"; }
    void run() {
        std::wstring command = L"\"" + setup() + L"\"";
        STARTUPINFOW start{}; start.cb = sizeof(start); start.dwFlags = STARTF_USESHOWWINDOW; start.wShowWindow = SW_SHOWNORMAL;
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(setup().c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, folder_.c_str(), &start, &process))
            throw std::runtime_error("Cannot launch vendor installer");
        CloseHandle(process.hThread);
        const DWORD waited = WaitForSingleObject(process.hProcess, INFINITE);
        CloseHandle(process.hProcess);
        if (waited != WAIT_OBJECT_0) throw std::runtime_error("Cannot wait for vendor installer");
        // Vendor exit codes are not a reliable proof that a driver was installed.
    }
};
// Returns true after a vendor setup session; caller exits so the user can reboot.
inline bool offer(HWND owner) {
    const int choice = MessageBoxW(owner,
        L"가상 마이크를 사용하려면 VB-CABLE을 최초 1회 설치해야 합니다.\n\n"
        L"공식 원본 설치 파일이 이 EXE에 포함되어 있습니다. 지금 설치 창을 열까요?\n"
        L"Install Driver로 설치한 뒤 설치 창을 닫아 주세요. 장치를 바로 비활성화합니다.\n\n"
        L"VB-CABLE은 VB-Audio의 Donationware입니다. 유용하게 사용하면 기부/라이선스 구매를 해 주세요.\n"
        L"제조사·다운로드: https://vb-audio.com/Cable/\n"
        L"라이선스·기부 안내: https://vb-audio.com/Services/licensing.htm\n\n"
        L"[아니요]를 누르면 설치 없이 볼륨 믹서를 사용할 수 있습니다.",
        L"Portable Mixer — 내장 VB-CABLE 설치", MB_YESNO | MB_ICONINFORMATION | MB_DEFBUTTON2);
    if (choice != IDYES) return false;
    try {
        // Own the same lease as the runtime guard throughout installation and
        // cleanup, so another app instance cannot re-enable the new device.
        struct Lease {
            HANDLE handle = CreateMutexW(nullptr, FALSE, L"Global\\PortableMixer.VBCable.Lease.v1");
            bool owned = false;
            ~Lease() { if (owned) ReleaseMutex(handle); if (handle) CloseHandle(handle); }
        } lease;
        if (lease.handle) {
            const DWORD wait = WaitForSingleObject(lease.handle, 0);
            lease.owned = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
        if (!lease.owned) {
            MessageBoxW(owner, L"다른 Portable Mixer가 장치를 관리 중입니다. 해당 앱을 종료한 뒤 설치해 주세요.",
                L"Portable Mixer", MB_OK | MB_ICONINFORMATION);
            return false;
        }
        Package package;
        package.run();
        const auto result = cable_install_finish::settle(virtual_mic::cables,
            [](const std::wstring& id) { return virtual_mic::change(id, false); },
            [](const std::wstring& id) {
                DEVINST node = 0; ULONG flags = 0, problem = 0;
                return CM_Locate_DevNodeW(&node, const_cast<wchar_t*>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
                    CM_Get_DevNode_Status(&flags, &problem, node, 0) == CR_SUCCESS &&
                    (flags & DN_HAS_PROBLEM) && problem == CM_PROB_DISABLED;
            }, [] { Sleep(500); });
        using cable_install_finish::Status;
        std::wstring message;
        UINT icon = MB_ICONINFORMATION;
        if (result.status == Status::Disabled)
            message = L"VB-CABLE 설치 장치를 확인하고 바로 비활성화했습니다.\n\nPortable Mixer는 이제 종료됩니다. 다음 실행 때 장치를 활성화합니다.\n제조사에서 재부팅을 안내했다면 먼저 재부팅해 주세요.";
        else if (result.status == Status::NotFound)
            message = L"설치된 VB-CABLE 장치가 아직 감지되지 않았습니다.\n\n설치 취소·실패 또는 재부팅 대기 상태일 수 있어 비활성화를 확인하지 못했습니다.\n설치를 완료했다면 재부팅 후 앱을 실행하고 종료해 비활성화해 주세요.";
        else {
            icon = MB_ICONWARNING;
            message = result.status == Status::Ambiguous
                ? L"VB-CABLE이 여러 개 감지되어 자동 비활성화를 중단했습니다."
                : L"설치 후 VB-CABLE 비활성화에 실패했습니다. (CM " + std::to_wstring(result.error) + L")";
            message += L"\n\n장치 관리자에서 VB-Audio Virtual Cable을 사용 안 함으로 설정해 주세요.";
        }
        MessageBoxW(owner, message.c_str(), L"Portable Mixer — 설치 후 장치 상태", MB_OK | icon);
        return true;
    } catch (const std::exception& error) {
        const int length = MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, nullptr, 0);
        std::wstring detail(length > 0 ? length : 1, L'\0');
        if (length > 0) MultiByteToWideChar(CP_UTF8, 0, error.what(), -1, detail.data(), length);
        MessageBoxW(owner, (L"내장 설치 프로그램을 준비하지 못했습니다.\n\n" + detail).c_str(),
            L"Portable Mixer 설치 오류", MB_OK | MB_ICONERROR);
        return false;
    }
}
}
