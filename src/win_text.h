// GDI font cache (text measurement for the layout engine) and WIC image loading.
#pragma once

#include "layout.h"

#include <map>
#include <string>
#include <windows.h>

namespace app {

class FontCache : public view::IMeasurer {
public:
    FontCache() = default;
    ~FontCache();

    FontCache(const FontCache&) = delete;
    FontCache& operator=(const FontCache&) = delete;

    void rebuild(int dpi);
    int dpi() const { return dpi_; }
    HFONT handle(view::FontId font) const { return fonts_[static_cast<int>(font)]; }

    // view::IMeasurer
    int width(view::FontId font, const wchar_t* text, size_t length) const override;
    int lineHeight(view::FontId font) const override;
    int ascent(view::FontId font) const override;

private:
    void destroy();
    HDC measureDc() const;
    void select(view::FontId font) const;

    static const int kCount = static_cast<int>(view::FontId::Count);
    HFONT fonts_[kCount] = {nullptr};
    int heights_[kCount] = {0};
    int ascents_[kCount] = {0};
    int dpi_ = 96;

    mutable HDC dc_ = nullptr;
    mutable HGDIOBJ originalFont_ = nullptr;
    mutable int selected_ = -1;
};

// Loads images referenced by the document, relative to the document's folder.
class ImageStore : public view::IImageSource {
public:
    ~ImageStore();

    void setBaseDirectory(const std::wstring& directory);
    void clear();

    bool imageSize(const std::wstring& source, int* width, int* height) override;
    // Draws a previously sized image; silently does nothing if unavailable.
    void draw(HDC dc, const std::wstring& source, int x, int y, int width, int height);

private:
    struct Entry {
        HBITMAP bitmap = nullptr;
        int width = 0;
        int height = 0;
        bool ok = false;
    };

    Entry* load(const std::wstring& source);
    std::wstring resolve(const std::wstring& source) const;

    std::wstring baseDirectory_;
    std::map<std::wstring, Entry> cache_;
    bool comInitialized_ = false;
};

} // namespace app
