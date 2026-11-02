#include "stdafx.h"
#include "image.h"

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

Image decode_image(const void* data, size_t size) {
    if (size == 0 || size > 64u * 1024 * 1024) throw std::runtime_error("image decode: bad size");

    ComPtr<IWICStream> stream;
    check(factory()->CreateStream(&stream), "CreateStream");
    check(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size)), "InitializeFromMemory");

    ComPtr<IWICBitmapDecoder> decoder;
    check(factory()->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder), "CreateDecoderFromStream");
    ComPtr<IWICBitmapFrameDecode> frame;
    check(decoder->GetFrame(0, &frame), "GetFrame");

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
