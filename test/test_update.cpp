// The updater's platform-free logic: reading GitHub's release JSON and
// comparing version numbers.
#include "../src/update_check.h"
#include "test_framework.h"

namespace {

const char* const kRelease = R"({
  "url": "https://api.github.com/repos/ctorx/SimpleMarkDownViewer/releases/1",
  "html_url": "https://github.com/ctorx/SimpleMarkDownViewer/releases/tag/v1.2.0",
  "tag_name" : "v1.2.0",
  "author": { "login": "ctorx", "html_url": "https://github.com/ctorx" },
  "assets": [
    { "name": "notes.txt",
      "browser_download_url": "https://github.com/ctorx/SimpleMarkDownViewer/releases/download/v1.2.0/notes.txt" },
    { "name": "Markr.exe",
      "browser_download_url": "https://github.com/ctorx/SimpleMarkDownViewer/releases/download/v1.2.0/Markr.exe" }
  ],
  "body": "Fixes \"quoted\" things é"
})";

} // namespace

TEST(Update, ParsesTagPageAndExeAsset) {
    update::ReleaseInfo info;
    CHECK_TRUE(update::parseLatestRelease(kRelease, &info));
    CHECK_EQ(info.tag, std::string("v1.2.0"));
    CHECK_EQ(info.pageUrl,
             std::string("https://github.com/ctorx/SimpleMarkDownViewer/releases/tag/v1.2.0"));
    CHECK_EQ(info.assetUrl, std::string("https://github.com/ctorx/SimpleMarkDownViewer/releases/"
                                        "download/v1.2.0/Markr.exe"));
}

TEST(Update, ReleaseWithoutExeHasNoAsset) {
    update::ReleaseInfo info;
    CHECK_TRUE(update::parseLatestRelease(R"({"tag_name":"v2.0.0","assets":[]})", &info));
    CHECK_EQ(info.tag, std::string("v2.0.0"));
    CHECK_TRUE(info.assetUrl.empty());
}

TEST(Update, ResponseWithoutTagIsRejected) {
    update::ReleaseInfo info;
    CHECK_TRUE(!update::parseLatestRelease(R"({"message":"Not Found"})", &info));
}

TEST(Update, ComparesVersions) {
    CHECK_TRUE(update::compareVersions("v1.2.0", "1.1.9") > 0);
    CHECK_TRUE(update::compareVersions("1.10.0", "1.9.0") > 0);
    CHECK_TRUE(update::compareVersions("1.0.0", "v1.0.1") < 0);
    CHECK_EQ(update::compareVersions("v1.2", "1.2.0"), 0);
    CHECK_EQ(update::compareVersions("1.2.0-beta", "1.2.0"), 0);
}
