#include "win_update.h"

#include "version.h"

#include <thread>
#include <windows.h>
#include <winhttp.h>

namespace app {
namespace {

const wchar_t* const kLatestReleaseUrl =
    L"https://api.github.com/repos/ctorx/Markr/releases/latest";
// GitHub's API rejects requests without a User-Agent.
const wchar_t* const kUserAgent = L"Markr-Updater/" TEXT(MARKR_VERSION_TEXT);

// A real Markr.exe is several hundred KB; anything tiny is an error page.
constexpr size_t kMinimumExeSize = 64 * 1024;

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                     nullptr, 0);
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &out[0],
                        length);
    return out;
}

std::wstring systemMessage(DWORD code) {
    wchar_t* buffer = nullptr;
    HMODULE winhttp = GetModuleHandleW(L"winhttp.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                  FORMAT_MESSAGE_FROM_SYSTEM | (winhttp ? FORMAT_MESSAGE_FROM_HMODULE : 0);
    FormatMessageW(flags, winhttp, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = buffer ? buffer : L"";
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
        text.pop_back();
    if (text.empty()) text = L"Error " + std::to_wstring(code);
    return text;
}

// Closes a WinHTTP handle on scope exit.
struct HttpHandle {
    HINTERNET handle = nullptr;
    explicit HttpHandle(HINTERNET h) : handle(h) {}
    ~HttpHandle() {
        if (handle) WinHttpCloseHandle(handle);
    }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
    explicit operator bool() const { return handle != nullptr; }
};

// GETs an https URL into memory, following redirects (release downloads
// redirect to GitHub's object storage). False on a transport failure; an
// HTTP error status still returns true with `status` set.
bool httpGet(const std::wstring& url, const wchar_t* accept, DWORD* status, std::string* body,
             std::wstring* error) {
    URL_COMPONENTS parts = {sizeof(parts)};
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    parts.lpszHostName = host;
    parts.dwHostNameLength = ARRAYSIZE(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = ARRAYSIZE(path);
    parts.dwExtraInfoLength = 1; // ask for the query string to be included in the path
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        *error = L"Bad URL: " + url;
        return false;
    }
    std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.lpszExtraInfo && parts.dwExtraInfoLength) {
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    HttpHandle session(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        *error = systemMessage(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session.handle, 10000, 10000, 15000, 30000);

    HttpHandle connection(WinHttpConnect(session.handle, host, parts.nPort, 0));
    if (!connection) {
        *error = systemMessage(GetLastError());
        return false;
    }
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HttpHandle request(WinHttpOpenRequest(connection.handle, L"GET", object.c_str(), nullptr,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                          flags));
    if (!request) {
        *error = systemMessage(GetLastError());
        return false;
    }

    std::wstring headers = std::wstring(L"Accept: ") + accept + L"\r\n";
    if (!WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(-1L),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.handle, nullptr)) {
        *error = systemMessage(GetLastError());
        return false;
    }

    DWORD code = 0;
    DWORD size = sizeof(code);
    WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    *status = code;

    // Content-Length, when sent, guards against a truncated download.
    DWORD expected = 0;
    size = sizeof(expected);
    bool haveLength = WinHttpQueryHeaders(
                          request.handle, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &expected, &size,
                          WINHTTP_NO_HEADER_INDEX) != FALSE;

    body->clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.handle, &available)) {
            *error = systemMessage(GetLastError());
            return false;
        }
        if (available == 0) break;
        size_t offset = body->size();
        body->resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.handle, &(*body)[offset], available, &read)) {
            *error = systemMessage(GetLastError());
            return false;
        }
        body->resize(offset + read);
    }
    if (haveLength && body->size() != expected) {
        *error = L"The download was incomplete.";
        return false;
    }
    return true;
}

std::wstring exePath() {
    wchar_t buffer[MAX_PATH * 2] = {0};
    DWORD length = GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
    return std::wstring(buffer, length);
}

} // namespace

const char* currentVersion() { return MARKR_VERSION_TEXT; }

UpdateCheck checkForUpdate() {
    UpdateCheck result;
    DWORD status = 0;
    std::string body;
    if (!httpGet(kLatestReleaseUrl, L"application/vnd.github+json", &status, &body,
                 &result.error)) {
        return result;
    }
    if (status == 404) {
        result.outcome = UpdateCheck::Outcome::NoReleases;
        return result;
    }
    if (status != 200) {
        result.error = L"GitHub answered with HTTP status " + std::to_wstring(status) + L".";
        return result;
    }
    if (!update::parseLatestRelease(body, &result.release)) {
        result.error = L"GitHub's reply did not describe a release.";
        return result;
    }
    result.outcome = update::compareVersions(result.release.tag, MARKR_VERSION_TEXT) > 0
                         ? UpdateCheck::Outcome::Available
                         : UpdateCheck::Outcome::UpToDate;
    return result;
}

std::wstring downloadUpdate(const std::string& url, std::wstring* error) {
    DWORD status = 0;
    std::string body;
    if (!httpGet(widen(url), L"application/octet-stream", &status, &body, error)) {
        return std::wstring();
    }
    if (status != 200) {
        *error = L"The download failed with HTTP status " + std::to_wstring(status) + L".";
        return std::wstring();
    }
    // Every Windows executable starts with "MZ".
    if (body.size() < kMinimumExeSize || body[0] != 'M' || body[1] != 'Z') {
        *error = L"The downloaded file is not a Windows program.";
        return std::wstring();
    }

    wchar_t folder[MAX_PATH] = {0};
    GetTempPathW(ARRAYSIZE(folder), folder);
    std::wstring target = std::wstring(folder) + L"Markr.update.exe";
    HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = L"Could not save the download: " + systemMessage(GetLastError());
        return std::wstring();
    }
    DWORD written = 0;
    BOOL ok = WriteFile(file, body.data(), static_cast<DWORD>(body.size()), &written, nullptr);
    CloseHandle(file);
    if (!ok || written != body.size()) {
        DeleteFileW(target.c_str());
        *error = L"Could not save the download.";
        return std::wstring();
    }
    return target;
}

bool installUpdate(const std::wstring& downloaded, std::wstring* error) {
    const std::wstring exe = exePath();
    const std::wstring old = exe + L".old";
    DeleteFileW(old.c_str()); // left over from an earlier update, if any

    if (!MoveFileExW(exe.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        *error = L"Markr could not rename itself (" + systemMessage(GetLastError()) +
                 L"). It may be in a folder that needs administrator rights.";
        return false;
    }
    // The download sits in the temp folder, possibly on another drive.
    if (!MoveFileExW(downloaded.c_str(), exe.c_str(),
                     MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING)) {
        *error = L"Markr could not move the new version into place (" +
                 systemMessage(GetLastError()) + L").";
        MoveFileExW(old.c_str(), exe.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    return true;
}

bool relaunch(const std::wstring& document) {
    std::wstring exe = exePath();
    std::wstring command = L"\"" + exe + L"\"";
    if (!document.empty()) command += L" \"" + document + L"\"";
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(exe.c_str(), &command[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void cleanUpAfterUpdate() {
    std::wstring old = exePath() + L".old";
    if (GetFileAttributesW(old.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    std::thread([old]() {
        for (int attempt = 0; attempt < 20; ++attempt) {
            if (DeleteFileW(old.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND) return;
            Sleep(500);
        }
    }).detach();
}

} // namespace app
