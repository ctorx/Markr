// The platform-free half of the updater: reading GitHub's "latest release"
// response and comparing version numbers. Kept free of Windows so the test
// suite can exercise it.
#pragma once

#include <string>

namespace update {

// What the app needs from a GitHub release.
struct ReleaseInfo {
    std::string tag;      // e.g. "v1.2.0"
    std::string assetUrl; // download URL of Markr.exe; empty when not attached
    std::string pageUrl;  // the release's page on github.com
};

// Pulls the release fields out of the JSON GitHub returns for
// /repos/{owner}/{repo}/releases/latest. False when there is no tag.
bool parseLatestRelease(const std::string& json, ReleaseInfo* out);

// Compares dotted version numbers, ignoring a leading "v" and any suffix
// after the numbers ("1.2.0-beta" compares as 1.2.0). Missing parts count
// as zero, so "1.2" equals "1.2.0". Returns <0, 0 or >0.
int compareVersions(const std::string& a, const std::string& b);

} // namespace update
