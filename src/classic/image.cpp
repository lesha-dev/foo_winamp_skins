#include "common/stdafx.h"
#include "classic/image.h"

#include <wincodec.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace wsz {
namespace {

void check(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(pfc::format("image decode: ", what, " failed (0x", pfc::format_hex((uint32_t)hr, 8), ")").c_str());
}

IWICImagingFactory* factory() {
    static ComPtr<IWICImagingFactory> f;
    if (!f) check(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f)), "CoCreateInstance(WIC)");
    return f.Get();
}

} // namespace

// WIC, not LoadImage as Winamp (draw.cpp draw_LBitmap): it also reads bitmaps LoadImage refuses - RLE-compressed
// with biSizeImage 0 or larger than the data in the file, more palette colours than the bit depth allows, 32 bpp
// with a V4 header - where Winamp falls back to its built-in
// bitmap. Showing the skin's art instead is a decision (see Skin::load_from).
Image decode_image(const void* data, size_t size) {
    if (size == 0 || size > 64u * 1024 * 1024) throw std::runtime_error("image decode: bad size");

    // As Windows' LoadImage (Winamp): a palette shorter than the header promises (biClrUsed 0 or 2^bpp, but only
    // room for fewer entries before bfOffBits, e.g. MuiAmpV1: 16 of 256) is read as what's there; WIC refuses it, so
    // it gets the real count.
    std::vector<uint8_t> patched;
    {
        auto p = static_cast<const uint8_t*>(data);
        auto u32 = [&](size_t o) { return uint32_t(p[o]) | uint32_t(p[o + 1]) << 8 | uint32_t(p[o + 2]) << 16 | uint32_t(p[o + 3]) << 24; };
        if (size >= 54 && p[0] == 'B' && p[1] == 'M' && u32(14) >= 40 && u32(14) <= 124) {
            const uint32_t off = u32(10), hdr = u32(14), used = u32(46);
            const unsigned bpp = p[28] | p[29] << 8;
            if (bpp <= 8 && off > 14 + hdr && off <= size) {
                const uint32_t declared = used ? used : 1u << bpp, room = (off - 14 - hdr) / 4;
                if (room > 0 && room < declared) {
                    patched.assign(p, p + size);
                    patched[46] = uint8_t(room); patched[47] = uint8_t(room >> 8); patched[48] = 0; patched[49] = 0;
                    data = patched.data();
                }
            }
        }
    }

    ComPtr<IWICStream> stream;
    check(factory()->CreateStream(&stream), "CreateStream");
    check(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size)), "InitializeFromMemory");

    ComPtr<IWICBitmapDecoder> decoder;
    check(factory()->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "CreateDecoderFromStream");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "GetFrame");

    // Palette images become their palette colours, black/white ones too (Winamp's monochrome DDBs take the target
    // DC's text/background colours instead; not reproduced by decision, see Skin::load_from).
    ComPtr<IWICFormatConverter> conv;
    check(factory()->CreateFormatConverter(&conv), "CreateFormatConverter");
    check(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom), "FormatConverter::Initialize");

    UINT w = 0, h = 0;
    check(conv->GetSize(&w, &h), "GetSize");
    if (w == 0 || h == 0 || w > 8192 || h > 8192) throw std::runtime_error("image decode: bad dimensions");

    Image img;
    img.width = static_cast<int>(w);
    img.height = static_cast<int>(h);
    img.pixels.resize(size_t(w) * h);
    check(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(img.pixels.size() * 4), reinterpret_cast<BYTE*>(img.pixels.data())), "CopyPixels");
    return img;
}

} // namespace wsz
