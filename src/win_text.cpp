#include "win_text.h"

#include <objbase.h>
#include <shlwapi.h>
#include <wincodec.h>

namespace app {
namespace {

using view::FontId;

struct FontSpec {
    const wchar_t* family;
    int pointsTimesTen;
    int weight;
    bool italic;
    bool monospace;
};

// Sizes are in tenths of a point; converted with the current DPI. The scale
// mirrors VS Code's markdown preview: a 14px (10.5pt) body with headings at
// 2em / 1.5em / 1.25em / 1em / 0.875em / 0.85em, all weight 600.
const FontSpec kSpecs[static_cast<int>(FontId::Count)] = {
    {L"Segoe UI", 105, FW_NORMAL, false, false},    // Body
    {L"Segoe UI", 105, FW_BOLD, false, false},      // BodyBold
    {L"Segoe UI", 105, FW_NORMAL, true, false},     // BodyItalic
    {L"Segoe UI", 105, FW_BOLD, true, false},       // BodyBoldItalic
    {nullptr, 105, FW_NORMAL, false, true},         // Code
    {nullptr, 105, FW_BOLD, false, true},           // CodeBold
    {L"Segoe UI", 210, FW_SEMIBOLD, false, false},  // H1
    {L"Segoe UI", 158, FW_SEMIBOLD, false, false},  // H2
    {L"Segoe UI", 131, FW_SEMIBOLD, false, false},  // H3
    {L"Segoe UI", 105, FW_SEMIBOLD, false, false},  // H4
    {L"Segoe UI", 92, FW_SEMIBOLD, false, false},   // H5
    {L"Segoe UI", 89, FW_SEMIBOLD, false, false},   // H6
    {L"Segoe UI", 85, FW_NORMAL, false, false},     // Small
    {L"Segoe UI", 85, FW_SEMIBOLD, false, false},   // SmallBold
};

int CALLBACK enumFamilyProc(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM param) {
    *reinterpret_cast<bool*>(param) = true;
    return 0;
}

bool fontExists(const wchar_t* family) {
    HDC dc = GetDC(nullptr);
    LOGFONTW lf = {};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, family, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, enumFamilyProc, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

const wchar_t* monospaceFamily() {
    static const wchar_t* cached = nullptr;
    if (!cached) {
        // Consolas is VS Code's default editor font on Windows.
        if (fontExists(L"Consolas")) cached = L"Consolas";
        else if (fontExists(L"Cascadia Mono")) cached = L"Cascadia Mono";
        else cached = L"Courier New";
    }
    return cached;
}

} // namespace

FontCache::~FontCache() { destroy(); }

void FontCache::destroy() {
    if (dc_) {
        if (originalFont_) SelectObject(dc_, originalFont_);
        DeleteDC(dc_);
        dc_ = nullptr;
        originalFont_ = nullptr;
    }
    for (int i = 0; i < kCount; ++i) {
        if (fonts_[i]) {
            DeleteObject(fonts_[i]);
            fonts_[i] = nullptr;
        }
    }
    selected_ = -1;
}

void FontCache::rebuild(int dpi) {
    destroy();
    dpi_ = dpi > 0 ? dpi : 96;

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);

    for (int i = 0; i < kCount; ++i) {
        const FontSpec& spec = kSpecs[i];
        LOGFONTW lf = {};
        lf.lfHeight = -MulDiv(spec.pointsTimesTen, dpi_, 720);
        lf.lfWeight = spec.weight;
        lf.lfItalic = spec.italic ? TRUE : FALSE;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        lf.lfPitchAndFamily = spec.monospace ? FIXED_PITCH : VARIABLE_PITCH;
        wcsncpy_s(lf.lfFaceName, spec.monospace ? monospaceFamily() : spec.family, _TRUNCATE);
        fonts_[i] = CreateFontIndirectW(&lf);

        HGDIOBJ previous = SelectObject(dc, fonts_[i]);
        TEXTMETRICW tm = {};
        GetTextMetricsW(dc, &tm);
        ascents_[i] = tm.tmAscent;
        heights_[i] = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(dc, previous);
    }

    DeleteDC(dc);
}

HDC FontCache::measureDc() const {
    if (!dc_) {
        HDC screen = GetDC(nullptr);
        dc_ = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        selected_ = -1;
    }
    return dc_;
}

void FontCache::select(view::FontId font) const {
    int index = static_cast<int>(font);
    if (selected_ == index) return;
    HDC dc = measureDc();
    HGDIOBJ previous = SelectObject(dc, fonts_[index]);
    if (!originalFont_) originalFont_ = previous;
    selected_ = index;
}

int FontCache::width(view::FontId font, const wchar_t* text, size_t length) const {
    if (!text || length == 0) return 0;
    select(font);
    SIZE size = {0, 0};
    GetTextExtentPoint32W(dc_, text, static_cast<int>(length), &size);
    return size.cx;
}

int FontCache::lineHeight(view::FontId font) const { return heights_[static_cast<int>(font)]; }
int FontCache::ascent(view::FontId font) const { return ascents_[static_cast<int>(font)]; }

// ---------------------------------------------------------------- images

ImageStore::~ImageStore() {
    clear();
    if (comInitialized_) CoUninitialize();
}

void ImageStore::setBaseDirectory(const std::wstring& directory) {
    baseDirectory_ = directory;
    clear();
}

void ImageStore::clear() {
    for (auto& pair : cache_) {
        if (pair.second.bitmap) DeleteObject(pair.second.bitmap);
    }
    cache_.clear();
}

std::wstring ImageStore::resolve(const std::wstring& source) const {
    if (source.empty()) return source;
    // Remote images are not fetched; the alt text is rendered instead.
    if (source.compare(0, 5, L"http:") == 0 || source.compare(0, 6, L"https:") == 0 ||
        source.compare(0, 5, L"data:") == 0) {
        return std::wstring();
    }
    std::wstring path = source;
    for (wchar_t& c : path) {
        if (c == L'/') c = L'\\';
    }
    if (PathIsRelativeW(path.c_str()) && !baseDirectory_.empty()) {
        wchar_t combined[MAX_PATH * 2] = {0};
        PathCombineW(combined, baseDirectory_.c_str(), path.c_str());
        return combined;
    }
    return path;
}

ImageStore::Entry* ImageStore::load(const std::wstring& source) {
    auto existing = cache_.find(source);
    if (existing != cache_.end()) return &existing->second;

    Entry entry;
    std::wstring path = resolve(source);
    if (path.empty()) {
        cache_[source] = entry;
        return &cache_[source];
    }

    if (!comInitialized_) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        comInitialized_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }

    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnLoad, &decoder);
    }
    if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
    if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }

    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = converter->GetSize(&w, &h);

    if (SUCCEEDED(hr) && w > 0 && h > 0) {
        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(w);
        info.bmiHeader.biHeight = -static_cast<LONG>(h); // top-down
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (bitmap && bits) {
            hr = converter->CopyPixels(nullptr, w * 4, w * h * 4,
                                       static_cast<BYTE*>(bits));
            if (SUCCEEDED(hr)) {
                entry.bitmap = bitmap;
                entry.width = static_cast<int>(w);
                entry.height = static_cast<int>(h);
                entry.ok = true;
            } else {
                DeleteObject(bitmap);
            }
        }
    }

    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();

    cache_[source] = entry;
    return &cache_[source];
}

bool ImageStore::imageSize(const std::wstring& source, int* width, int* height) {
    Entry* entry = load(source);
    if (!entry || !entry->ok) return false;
    if (width) *width = entry->width;
    if (height) *height = entry->height;
    return true;
}

void ImageStore::draw(HDC dc, const std::wstring& source, int x, int y, int width, int height) {
    Entry* entry = load(source);
    if (!entry || !entry->ok || !entry->bitmap) return;

    HDC memory = CreateCompatibleDC(dc);
    HGDIOBJ previous = SelectObject(memory, entry->bitmap);
    BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    SetStretchBltMode(dc, HALFTONE);
    AlphaBlend(dc, x, y, width, height, memory, 0, 0, entry->width, entry->height, blend);
    SelectObject(memory, previous);
    DeleteDC(memory);
}

} // namespace app
