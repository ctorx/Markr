// The Windows half of the updater: asks GitHub for the latest release,
// downloads it, and swaps the running executable for the new one.
#pragma once

#include "update_check.h"

#include <string>

namespace app {

// The version this build reports, e.g. "1.0.0".
const char* currentVersion();

struct UpdateCheck {
    enum class Outcome {
        UpToDate,
        Available,  // release.tag is newer than this build
        NoReleases, // the repository has no published release yet
        Failed,     // network or parse failure; see error
    };
    Outcome outcome = Outcome::Failed;
    update::ReleaseInfo release;
    std::wstring error;
};

// Blocking: run it off the UI thread.
UpdateCheck checkForUpdate();

// Downloads the release executable into the temp folder. Blocking. Returns
// the downloaded file's path, or an empty string and sets `error`.
std::wstring downloadUpdate(const std::string& url, std::wstring* error);

// Replaces the running executable with `downloaded`. The running file is
// renamed to "<exe>.old" (Windows allows renaming a running program, not
// overwriting it) and the new file moved into its place. On failure the
// original is put back and `error` explains why.
bool installUpdate(const std::wstring& downloaded, std::wstring* error);

// Starts the (new) executable, reopening `document` when it is not empty.
bool relaunch(const std::wstring& document);

// Deletes the "<exe>.old" left by a previous update. The old process may
// still be exiting, so this retries briefly on a background thread.
void cleanUpAfterUpdate();

} // namespace app
