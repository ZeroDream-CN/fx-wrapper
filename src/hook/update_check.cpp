#include "update_check.h"

#include "fx_log.h"
#include "platform/platform.h"

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

#ifndef FX_WRAPPER_VERSION
#define FX_WRAPPER_VERSION "0.0.0"
#endif

namespace {

constexpr wchar_t kUpdateHost[] = L"cfdx.zerodream.net";
constexpr wchar_t kUpdatePathPrefix[] = L"/fivem/fxwrapper/?v=";
constexpr int kHttpResolveTimeoutMs = 3000;
constexpr int kHttpConnectTimeoutMs = 3000;
constexpr int kHttpSendTimeoutMs = 3000;
constexpr int kHttpReceiveTimeoutMs = 10000;

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

class WinHttpHandleGuard {
public:
    explicit WinHttpHandleGuard(HINTERNET handle) : handle_(handle) {}
    ~WinHttpHandleGuard() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandleGuard(const WinHttpHandleGuard&) = delete;
    WinHttpHandleGuard& operator=(const WinHttpHandleGuard&) = delete;

private:
    HINTERNET handle_;
};

std::wstring WidenFromUtf8(const std::string& value) {
    if (value.empty()) {
        return {};
    }

    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (size <= 0) {
        return {};
    }

    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        result.data(),
        size);
    return result;
}

bool ParseVersion(const std::string& value, Version& outVersion) {
    const auto dot1 = value.find('.');
    if (dot1 == std::string::npos) {
        return false;
    }

    const auto dot2 = value.find('.', dot1 + 1);
    if (dot2 == std::string::npos) {
        return false;
    }

    try {
        outVersion.major = std::stoi(value.substr(0, dot1));
        outVersion.minor = std::stoi(value.substr(dot1 + 1, dot2 - dot1 - 1));
        outVersion.patch = std::stoi(value.substr(dot2 + 1));
    } catch (...) {
        return false;
    }

    return true;
}

int CompareVersion(const Version& left, const Version& right) {
    if (left.major != right.major) {
        return left.major < right.major ? -1 : 1;
    }
    if (left.minor != right.minor) {
        return left.minor < right.minor ? -1 : 1;
    }
    if (left.patch != right.patch) {
        return left.patch < right.patch ? -1 : 1;
    }
    return 0;
}

std::string ExtractJsonStringField(const std::string& json, const char* fieldName) {
    const std::string key = std::string("\"") + fieldName + "\":\"";
    const auto start = json.find(key);
    if (start == std::string::npos) {
        return {};
    }

    size_t cursor = start + key.size();
    std::string value;
    value.reserve(128);

    while (cursor < json.size()) {
        const char ch = json[cursor++];
        if (ch == '"') {
            break;
        }

        if (ch == '\\' && cursor < json.size()) {
            const char escaped = json[cursor++];
            if (escaped == '/' || escaped == '\\' || escaped == '"') {
                value.push_back(escaped);
            } else {
                value.push_back('\\');
                value.push_back(escaped);
            }
            continue;
        }

        value.push_back(ch);
    }

    return value;
}

std::string ReplaceAll(std::string value, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return value;
    }

    size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }

    return value;
}

bool HttpGet(const std::wstring& host, const std::wstring& path, std::string& outBody) {
    outBody.clear();

    HINTERNET session = WinHttpOpen(
        L"FXWrapper/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr) {
        return false;
    }

    WinHttpHandleGuard sessionGuard(session);

    WinHttpSetTimeouts(
        session,
        kHttpResolveTimeoutMs,
        kHttpConnectTimeoutMs,
        kHttpSendTimeoutMs,
        kHttpReceiveTimeoutMs);

    HINTERNET connection = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connection == nullptr) {
        return false;
    }

    WinHttpHandleGuard connectionGuard(connection);

    HINTERNET request = WinHttpOpenRequest(
        connection,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
        return false;
    }

    WinHttpHandleGuard requestGuard(request);

    if (!WinHttpSendRequest(
            request,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0)) {
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        return false;
    }

    DWORD available = 0;
    do {
        if (!WinHttpQueryDataAvailable(request, &available)) {
            return false;
        }

        if (available == 0) {
            break;
        }

        std::vector<char> buffer(available);
        DWORD bytesRead = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &bytesRead)) {
            return false;
        }

        outBody.append(buffer.data(), buffer.data() + bytesRead);
    } while (available > 0);

    return true;
}

void CheckForUpdates() {
    const std::string currentVersion = FX_WRAPPER_VERSION;
    const std::wstring path = std::wstring(kUpdatePathPrefix) + WidenFromUtf8(currentVersion);

    std::string responseBody;
    if (!HttpGet(kUpdateHost, path, responseBody)) {
        return;
    }

    const std::string latestVersion = ExtractJsonStringField(responseBody, "version");
    const std::string updateNotes = ExtractJsonStringField(responseBody, "update");
    const std::string downloadTemplate = ExtractJsonStringField(responseBody, "url");
    if (latestVersion.empty() || downloadTemplate.empty()) {
        return;
    }

    Version current{};
    Version latest{};
    if (!ParseVersion(currentVersion, current) || !ParseVersion(latestVersion, latest)) {
        return;
    }

    if (CompareVersion(current, latest) >= 0) {
        return;
    }

    const std::string downloadUrl = ReplaceAll(downloadTemplate, "{VERSION}", latestVersion);
    std::string message = "^0==========================================================\n";
    message += "^2FXWrapper 有新的更新可以下载:^0 " + latestVersion + " (当前版本: " + currentVersion + ")";
    if (!updateNotes.empty()) {
        message += "\n\n[ 更新内容 ]\n" + updateNotes;
    }
    message += "\n\n[ 下载地址 ]\n" + downloadUrl + "\n";
    message += "==========================================================\n";

    WaitForLogReady();
    LogFxMessage(message.c_str());
}

}  // namespace

void StartUpdateCheckAsync() {
    CheckForUpdates();
}

#else

void StartUpdateCheckAsync() {}

#endif
