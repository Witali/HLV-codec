#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <wrl/client.h>

#include "hlv1.h"
#include "ima_adpcm.h"
#include "amrnb_3gp.h"
#include "bpv1.h"
#include "h263_3gp.h"
#include "pl_mpeg.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kWindowClass[] = L"HLV1WindowsPlayer";
constexpr wchar_t kVideoWindowClass[] = L"HLV1WindowsPlayerVideo";
constexpr UINT_PTR kPlaybackTimer = 1;
constexpr size_t kVideoLeadFrames = 3;
constexpr size_t kAudioBufferCount = 8;
constexpr int kSeekBarHeight = 38;
constexpr int kSeekMargin = 6;
constexpr int kTimeLabelWidth = 118;

enum : UINT {
    ID_FILE_OPEN = 1001,
    ID_FILE_EXIT,
    ID_PLAY_PAUSE,
    ID_VIEW_FIT,
    ID_SEEK_BAR,
    ID_TIME_LABEL
};

int clamp8(int value) {
    return value < 0 ? 0 : value > 255 ? 255 : value;
}

std::wstring widen_ascii(const char *text) {
    std::wstring result;
    if (!text) return result;
    while (*text) result.push_back(static_cast<unsigned char>(*text++));
    return result;
}

std::wstring file_name(const std::wstring &path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring multimedia_error(MMRESULT result) {
    wchar_t text[MAXERRORLENGTH] = {};
    if (waveOutGetErrorTextW(result, text, MAXERRORLENGTH) == MMSYSERR_NOERROR)
        return text;
    return L"Windows multimedia error " + std::to_wstring(result);
}

struct VideoFrame {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    std::vector<uint8_t> nv12;
};

enum class D3DInputFormat {
    kNv12,
    kBgra
};

enum class VideoCodec {
    kNone,
    kHlv,
    kBpv,
    kMpeg1,
    kH263,
    kMpeg4Simple
};

bool is_packet_video_codec(VideoCodec codec) {
    return codec == VideoCodec::kH263 ||
           codec == VideoCodec::kMpeg4Simple;
}

int packet_video_library_codec(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple
               ? H263_VIDEO_CODEC_MPEG4_SIMPLE
               : H263_VIDEO_CODEC_H263;
}

const wchar_t *packet_video_codec_name(VideoCodec codec) {
    return codec == VideoCodec::kMpeg4Simple ? L"MPEG-4 SP" : L"H.263";
}

void convert_frame(const HLV1Frame *source, VideoFrame &destination) {
    destination.width = source->width;
    destination.height = source->height;
    destination.pixels.resize(static_cast<size_t>(source->width) * source->height);
    const size_t luma_size =
        static_cast<size_t>(source->width) * source->height;
    destination.nv12.resize(luma_size + luma_size / 2U);

    for (int y = 0; y < source->height; ++y) {
        const uint8_t *luma = source->y + y * source->stride_y;
        std::memcpy(destination.nv12.data() +
                        static_cast<size_t>(y) * source->width,
                    luma, static_cast<size_t>(source->width));
        const uint8_t *urow = source->u + (y >> 1) * source->stride_u;
        const uint8_t *vrow = source->v + (y >> 1) * source->stride_v;
        uint32_t *output = destination.pixels.data() +
                           static_cast<size_t>(y) * source->width;
        for (int x = 0; x < source->width; x += 2) {
            const int u = static_cast<int>(urow[x >> 1]) - 128;
            const int v = static_cast<int>(vrow[x >> 1]) - 128;
            const int red_add = 409 * v + 128;
            const int green_add = -100 * u - 208 * v + 128;
            const int blue_add = 516 * u + 128;
            for (int pixel = x; pixel < x + 2 && pixel < source->width; ++pixel) {
                const int l = 298 * (luma[pixel] > 16 ? luma[pixel] - 16 : 0);
                const uint32_t red = static_cast<uint32_t>(
                    clamp8((l + red_add) >> 8));
                const uint32_t green = static_cast<uint32_t>(
                    clamp8((l + green_add) >> 8));
                const uint32_t blue = static_cast<uint32_t>(
                    clamp8((l + blue_add) >> 8));
                output[pixel] = (red << 16) | (green << 8) | blue;
            }
        }
    }

    uint8_t *chroma = destination.nv12.data() + luma_size;
    for (int y = 0; y < source->height / 2; ++y) {
        const uint8_t *urow = source->u + y * source->stride_u;
        const uint8_t *vrow = source->v + y * source->stride_v;
        uint8_t *output =
            chroma + static_cast<size_t>(y) * source->width;
        for (int x = 0; x < source->width / 2; ++x) {
            output[x * 2] = urow[x];
            output[x * 2 + 1] = vrow[x];
        }
    }
}

void convert_mpeg_frame(plm_frame_t *source, VideoFrame &destination) {
    destination.width = static_cast<int>(source->width);
    destination.height = static_cast<int>(source->height);
    const size_t luma_size =
        static_cast<size_t>(source->width) * source->height;
    destination.pixels.resize(luma_size);
    destination.nv12.resize(luma_size + luma_size / 2U);
    plm_frame_to_bgra(
        source, reinterpret_cast<uint8_t *>(destination.pixels.data()),
        static_cast<int>(source->width * sizeof(uint32_t)));

    for (unsigned y = 0; y < source->height; ++y) {
        std::memcpy(destination.nv12.data() +
                        static_cast<size_t>(y) * source->width,
                    source->y.data + static_cast<size_t>(y) * source->y.stride,
                    source->width);
    }
    uint8_t *chroma = destination.nv12.data() + luma_size;
    for (unsigned y = 0; y < source->height / 2U; ++y) {
        const uint8_t *cb =
            source->cb.data + static_cast<size_t>(y) * source->cb.stride;
        const uint8_t *cr =
            source->cr.data + static_cast<size_t>(y) * source->cr.stride;
        uint8_t *output =
            chroma + static_cast<size_t>(y) * source->width;
        for (unsigned x = 0; x < source->width / 2U; ++x) {
            output[x * 2U] = cb[x];
            output[x * 2U + 1U] = cr[x];
        }
    }
}

void convert_h263_frame(const H2633gpFrame *source,
                        VideoFrame &destination) {
    HLV1Frame adapted = {};
    adapted.width = source->width;
    adapted.height = source->height;
    std::vector<uint8_t> unpacked;
    if (source->storage_mode == H263_FRAME_STORAGE_Y6_U5_V5) {
        const int padded_width = source->compact.width;
        const int padded_height = source->compact.height;
        const size_t y_bytes =
            static_cast<size_t>(padded_width) * padded_height;
        unpacked.resize(y_bytes + y_bytes / 2U);
        adapted.padded_width = padded_width;
        adapted.padded_height = padded_height;
        adapted.stride_y = padded_width;
        adapted.stride_u = padded_width / 2;
        adapted.stride_v = padded_width / 2;
        adapted.y = unpacked.data();
        adapted.u = adapted.y + y_bytes;
        adapted.v = adapted.u + y_bytes / 4U;
        compact_yuv420_unpack_plane(
            &source->compact.y, adapted.y, adapted.stride_y);
        compact_yuv420_unpack_plane(
            &source->compact.u, adapted.u, adapted.stride_u);
        compact_yuv420_unpack_plane(
            &source->compact.v, adapted.v, adapted.stride_v);
    } else {
        adapted.padded_width = source->y_stride;
        adapted.padded_height = source->height;
        adapted.stride_y = source->y_stride;
        adapted.stride_u = source->chroma_stride;
        adapted.stride_v = source->chroma_stride;
        adapted.y = const_cast<uint8_t *>(source->y);
        adapted.u = const_cast<uint8_t *>(source->u);
        adapted.v = const_cast<uint8_t *>(source->v);
    }
    convert_frame(&adapted, destination);
}

bool mpeg_fps_rational(double fps, uint16_t *numerator,
                       uint16_t *denominator) {
    struct Rate {
        double value;
        uint16_t numerator;
        uint16_t denominator;
    };
    static constexpr Rate rates[] = {
        {24000.0 / 1001.0, 24000, 1001},
        {24.0, 24, 1},
        {25.0, 25, 1},
        {30000.0 / 1001.0, 30000, 1001},
        {30.0, 30, 1},
        {50.0, 50, 1},
        {60000.0 / 1001.0, 60000, 1001},
        {60.0, 60, 1},
    };
    for (const Rate &rate : rates) {
        if (std::fabs(fps - rate.value) < 0.01) {
            *numerator = rate.numerator;
            *denominator = rate.denominator;
            return true;
        }
    }
    return false;
}

uint8_t mpeg_sample_to_u8(float left, float right) {
    const float mono = std::clamp((left + right) * 0.5f, -1.0f, 1.0f);
    const long value = std::lround(128.0f + mono * 127.0f);
    return static_cast<uint8_t>(std::clamp(value, 0L, 255L));
}

bool convert_bpv_frame(const BPV1Header *header, const BPV1Frame *source,
                       VideoFrame &destination) {
    if (!header || !source) return false;
    destination.width = source->width;
    destination.height = source->height;
    destination.pixels.resize(
        static_cast<size_t>(source->width) * source->height);
    destination.nv12.clear();
    std::vector<uint8_t> row(
        static_cast<size_t>(source->width) * 3U);

    for (uint16_t y = 0; y < source->height; ++y) {
        if (bpv1_frame_render_rgb24_row(
                header, source, y, row.data(),
                static_cast<size_t>(source->width) * 3U) != BPV1_OK) {
            return false;
        }
        uint32_t *output = destination.pixels.data() +
                           static_cast<size_t>(y) * source->width;
        for (uint16_t x = 0; x < source->width; ++x) {
            const size_t pixel = static_cast<size_t>(x) * 3U;
            output[x] =
                (static_cast<uint32_t>(row[pixel]) << 16) |
                (static_cast<uint32_t>(row[pixel + 1U]) << 8) |
                static_cast<uint32_t>(row[pixel + 2U]);
        }
    }
    return true;
}

std::wstring hresult_error(const wchar_t *operation, HRESULT result) {
    wchar_t code[32] = {};
    swprintf_s(code, L" (HRESULT 0x%08lX)",
               static_cast<unsigned long>(result));
    return std::wstring(operation) + code;
}

class D3DVideoRenderer {
public:
    bool open(HWND window, unsigned width, unsigned height,
              unsigned fps_num, unsigned fps_den,
              D3DInputFormat input_format, std::wstring &error) {
        close();
        window_ = window;
        source_width_ = width;
        source_height_ = height;
        fps_num_ = fps_num;
        fps_den_ = fps_den;
        input_format_ = input_format;

        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL selected_level = D3D_FEATURE_LEVEL_10_0;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
                     D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        HRESULT result = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION, device_.GetAddressOf(), &selected_level,
            context_.GetAddressOf());
        if (result == E_INVALIDARG) {
            result = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels + 1, static_cast<UINT>(std::size(levels) - 1),
                D3D11_SDK_VERSION, device_.GetAddressOf(), &selected_level,
                context_.GetAddressOf());
        }
        if (FAILED(result)) {
            error = hresult_error(L"Cannot create the D3D11 video device", result);
            close();
            return false;
        }

        result = device_.As(&video_device_);
        if (SUCCEEDED(result)) result = context_.As(&video_context_);
        if (FAILED(result)) {
            error = hresult_error(
                L"The D3D11 device has no video-processor interface", result);
            close();
            return false;
        }

        ComPtr<IDXGIDevice1> dxgi_device;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        result = device_.As(&dxgi_device);
        if (SUCCEEDED(result)) result = dxgi_device->GetAdapter(&adapter);
        if (SUCCEEDED(result)) {
            result = adapter->GetParent(
                IID_PPV_ARGS(factory.GetAddressOf()));
        }
        if (FAILED(result)) {
            error = hresult_error(L"Cannot obtain the DXGI factory", result);
            close();
            return false;
        }
        dxgi_device->SetMaximumFrameLatency(1);

        RECT client = {};
        GetClientRect(window_, &client);
        const UINT client_width =
            static_cast<UINT>(std::max<LONG>(1, client.right - client.left));
        const UINT client_height =
            static_cast<UINT>(std::max<LONG>(1, client.bottom - client.top));

        DXGI_SWAP_CHAIN_DESC1 swap_desc = {};
        swap_desc.Width = client_width;
        swap_desc.Height = client_height;
        swap_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swap_desc.SampleDesc.Count = 1;
        swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_desc.BufferCount = 2;
        swap_desc.Scaling = DXGI_SCALING_STRETCH;
        swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        result = factory->CreateSwapChainForHwnd(
            device_.Get(), window_, &swap_desc, nullptr, nullptr,
            swap_chain_.GetAddressOf());
        if (FAILED(result)) {
            swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            result = factory->CreateSwapChainForHwnd(
                device_.Get(), window_, &swap_desc, nullptr, nullptr,
                swap_chain_.GetAddressOf());
        }
        if (FAILED(result)) {
            error = hresult_error(
                L"Cannot create the two-buffer DXGI flip swap chain", result);
            close();
            return false;
        }
        factory->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER);

        detect_overlay_support();
        result = create_video_resources(client_width, client_height);
        if (FAILED(result)) {
            error = hresult_error(
                L"Cannot initialize D3D11 video processing", result);
            close();
            return false;
        }

        active_ = true;
        return true;
    }

    void close() {
        active_ = false;
        overlay_capable_ = false;
        nv12_overlay_capable_ = false;
        output_view_.Reset();
        for (auto &view : input_views_) view.Reset();
        for (auto &texture : input_textures_) texture.Reset();
        processor_.Reset();
        enumerator_.Reset();
        swap_chain_.Reset();
        video_context_.Reset();
        video_device_.Reset();
        context_.Reset();
        device_.Reset();
        client_width_ = 0;
        client_height_ = 0;
        source_width_ = 0;
        source_height_ = 0;
        input_index_ = 0;
        window_ = nullptr;
    }

    bool active() const { return active_; }

    std::wstring label() const {
        std::wstring result = input_format_ == D3DInputFormat::kBgra
                                  ? L"D3D11 BGRA flip"
                                  : L"D3D11 NV12 flip";
        if (input_format_ == D3DInputFormat::kNv12 &&
            overlay_capable_ && nv12_overlay_capable_)
            result += L", MPO-capable";
        return result;
    }

    bool present(const VideoFrame &frame, bool fit_to_window,
                 std::wstring &error) {
        if (!active_ || !swap_chain_ || frame.width <= 0 || frame.height <= 0)
            return false;
        const bool bgra = input_format_ == D3DInputFormat::kBgra;
        const size_t pixel_count =
            static_cast<size_t>(frame.width) * frame.height;
        const size_t expected_size =
            bgra ? pixel_count * sizeof(uint32_t)
                 : pixel_count * 3U / 2U;
        if ((bgra && frame.pixels.size() != pixel_count) ||
            (!bgra && frame.nv12.size() != expected_size)) {
            error = bgra
                        ? L"The decoded frame has an invalid BGRA buffer"
                        : L"The decoded frame has an invalid NV12 buffer";
            close();
            return false;
        }

        RECT client = {};
        GetClientRect(window_, &client);
        const LONG raw_width = client.right - client.left;
        const LONG raw_height = client.bottom - client.top;
        if (raw_width <= 0 || raw_height <= 0) return true;
        const UINT width = static_cast<UINT>(raw_width);
        const UINT height = static_cast<UINT>(raw_height);
        HRESULT result = ensure_size(width, height);
        if (FAILED(result)) {
            error = hresult_error(L"Cannot resize the DXGI video buffers", result);
            close();
            return false;
        }

        const size_t slot = input_index_++ % input_textures_.size();
        const void *input = bgra
                                ? static_cast<const void *>(
                                      frame.pixels.data())
                                : static_cast<const void *>(
                                      frame.nv12.data());
        context_->UpdateSubresource(
            input_textures_[slot].Get(), 0, nullptr, input,
            static_cast<UINT>(
                frame.width * (bgra ? sizeof(uint32_t) : 1U)),
            static_cast<UINT>(expected_size));

        RECT source_rect = {0, 0, frame.width, frame.height};
        RECT target_rect = {0, 0, static_cast<LONG>(width),
                            static_cast<LONG>(height)};
        RECT destination_rect = target_rect;
        int draw_width = frame.width;
        int draw_height = frame.height;
        if (fit_to_window) {
            if (static_cast<int64_t>(width) * frame.height <=
                static_cast<int64_t>(height) * frame.width) {
                draw_width = static_cast<int>(width);
                draw_height = static_cast<int>(
                    static_cast<int64_t>(width) * frame.height / frame.width);
            } else {
                draw_height = static_cast<int>(height);
                draw_width = static_cast<int>(
                    static_cast<int64_t>(height) *
                    frame.width / frame.height);
            }
        }
        destination_rect.left =
            (static_cast<int>(width) - draw_width) / 2;
        destination_rect.top =
            (static_cast<int>(height) - draw_height) / 2;
        destination_rect.right = destination_rect.left + draw_width;
        destination_rect.bottom = destination_rect.top + draw_height;

        D3D11_VIDEO_COLOR background = {};
        background.RGBA.A = 1.0f;
        video_context_->VideoProcessorSetOutputBackgroundColor(
            processor_.Get(), FALSE, &background);
        video_context_->VideoProcessorSetOutputTargetRect(
            processor_.Get(), TRUE, &target_rect);
        video_context_->VideoProcessorSetStreamSourceRect(
            processor_.Get(), 0, TRUE, &source_rect);
        video_context_->VideoProcessorSetStreamDestRect(
            processor_.Get(), 0, TRUE, &destination_rect);

        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.pInputSurface = input_views_[slot].Get();
        result = video_context_->VideoProcessorBlt(
            processor_.Get(), output_view_.Get(), 0, 1, &stream);
        if (SUCCEEDED(result)) result = swap_chain_->Present(1, 0);
        if (result == DXGI_STATUS_OCCLUDED) return true;
        if (FAILED(result)) {
            error = hresult_error(L"D3D11 video presentation failed", result);
            close();
            return false;
        }
        return true;
    }

private:
    void detect_overlay_support() {
        ComPtr<IDXGIOutput> output;
        if (FAILED(swap_chain_->GetContainingOutput(&output))) return;
        ComPtr<IDXGIOutput2> output2;
        if (SUCCEEDED(output.As(&output2)))
            overlay_capable_ = output2->SupportsOverlays() != FALSE;
        ComPtr<IDXGIOutput3> output3;
        UINT flags = 0;
        if (SUCCEEDED(output.As(&output3)) &&
            SUCCEEDED(output3->CheckOverlaySupport(
                DXGI_FORMAT_NV12, device_.Get(), &flags))) {
            nv12_overlay_capable_ = flags != 0;
        }
    }

    HRESULT ensure_size(UINT width, UINT height) {
        if (width == client_width_ && height == client_height_)
            return S_OK;
        output_view_.Reset();
        for (auto &view : input_views_) view.Reset();
        processor_.Reset();
        enumerator_.Reset();
        context_->Flush();
        HRESULT result = swap_chain_->ResizeBuffers(
            2, width, height, DXGI_FORMAT_B8G8R8A8_UNORM, 0);
        if (FAILED(result)) return result;
        return create_video_resources(width, height);
    }

    HRESULT create_video_resources(UINT output_width, UINT output_height) {
        D3D11_VIDEO_PROCESSOR_CONTENT_DESC content = {};
        content.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        content.InputFrameRate.Numerator = fps_num_;
        content.InputFrameRate.Denominator = fps_den_;
        content.InputWidth = source_width_;
        content.InputHeight = source_height_;
        content.OutputFrameRate = content.InputFrameRate;
        content.OutputWidth = output_width;
        content.OutputHeight = output_height;
        content.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        HRESULT result = video_device_->CreateVideoProcessorEnumerator(
            &content, enumerator_.GetAddressOf());
        UINT format_flags = 0;
        const DXGI_FORMAT input_format =
            input_format_ == D3DInputFormat::kBgra
                ? DXGI_FORMAT_B8G8R8A8_UNORM
                : DXGI_FORMAT_NV12;
        if (SUCCEEDED(result)) {
            result = enumerator_->CheckVideoProcessorFormat(
                input_format, &format_flags);
        }
        if (SUCCEEDED(result) &&
            !(format_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_INPUT)) {
            result = DXGI_ERROR_UNSUPPORTED;
        }
        format_flags = 0;
        if (SUCCEEDED(result)) {
            result = enumerator_->CheckVideoProcessorFormat(
                DXGI_FORMAT_B8G8R8A8_UNORM, &format_flags);
        }
        if (SUCCEEDED(result) &&
            !(format_flags & D3D11_VIDEO_PROCESSOR_FORMAT_SUPPORT_OUTPUT)) {
            result = DXGI_ERROR_UNSUPPORTED;
        }
        if (SUCCEEDED(result)) {
            result = video_device_->CreateVideoProcessor(
                enumerator_.Get(), 0, processor_.GetAddressOf());
        }
        if (FAILED(result)) return result;

        D3D11_TEXTURE2D_DESC texture_desc = {};
        texture_desc.Width = source_width_;
        texture_desc.Height = source_height_;
        texture_desc.MipLevels = 1;
        texture_desc.ArraySize = 1;
        texture_desc.Format = input_format;
        texture_desc.SampleDesc.Count = 1;
        texture_desc.Usage = D3D11_USAGE_DEFAULT;
        texture_desc.BindFlags = 0;

        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC input_desc = {};
        input_desc.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        for (size_t index = 0; index < input_textures_.size(); ++index) {
            if (!input_textures_[index]) {
                result = device_->CreateTexture2D(
                    &texture_desc, nullptr,
                    input_textures_[index].GetAddressOf());
            }
            if (SUCCEEDED(result)) {
                result = video_device_->CreateVideoProcessorInputView(
                    input_textures_[index].Get(), enumerator_.Get(),
                    &input_desc, input_views_[index].GetAddressOf());
            }
            if (FAILED(result)) return result;
        }

        ComPtr<ID3D11Texture2D> back_buffer;
        result = swap_chain_->GetBuffer(
            0, IID_PPV_ARGS(back_buffer.GetAddressOf()));
        if (FAILED(result)) return result;
        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC output_desc = {};
        output_desc.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        result = video_device_->CreateVideoProcessorOutputView(
            back_buffer.Get(), enumerator_.Get(), &output_desc,
            output_view_.GetAddressOf());
        if (FAILED(result)) return result;

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE input_color = {};
        if (input_format_ == D3DInputFormat::kBgra) {
            input_color.RGB_Range = 0;
            input_color.Nominal_Range =
                D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        } else {
            input_color.YCbCr_Matrix = 0;
            input_color.Nominal_Range =
                D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        }
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE output_color = {};
        output_color.RGB_Range = 0;
        output_color.Nominal_Range =
            D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        video_context_->VideoProcessorSetStreamColorSpace(
            processor_.Get(), 0, &input_color);
        video_context_->VideoProcessorSetOutputColorSpace(
            processor_.Get(), &output_color);

        client_width_ = output_width;
        client_height_ = output_height;
        return S_OK;
    }

    HWND window_ = nullptr;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<ID3D11VideoDevice> video_device_;
    ComPtr<ID3D11VideoContext> video_context_;
    ComPtr<IDXGISwapChain1> swap_chain_;
    ComPtr<ID3D11VideoProcessorEnumerator> enumerator_;
    ComPtr<ID3D11VideoProcessor> processor_;
    std::array<ComPtr<ID3D11Texture2D>, 2> input_textures_;
    std::array<ComPtr<ID3D11VideoProcessorInputView>, 2> input_views_;
    ComPtr<ID3D11VideoProcessorOutputView> output_view_;
    UINT source_width_ = 0;
    UINT source_height_ = 0;
    UINT fps_num_ = 0;
    UINT fps_den_ = 1;
    UINT client_width_ = 0;
    UINT client_height_ = 0;
    size_t input_index_ = 0;
    D3DInputFormat input_format_ = D3DInputFormat::kNv12;
    bool active_ = false;
    bool overlay_capable_ = false;
    bool nv12_overlay_capable_ = false;
};

class AudioOutput {
public:
    ~AudioOutput() { close(); }

    bool open(unsigned sample_rate, unsigned bits_per_sample,
              std::wstring &error) {
        close();
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = sample_rate;
        format.wBitsPerSample = static_cast<WORD>(bits_per_sample);
        format.nBlockAlign = static_cast<WORD>(bits_per_sample / 8U);
        format.nAvgBytesPerSec = sample_rate * format.nBlockAlign;

        MMRESULT result = waveOutOpen(&handle_, WAVE_MAPPER, &format,
                                      0, 0, CALLBACK_NULL);
        if (result != MMSYSERR_NOERROR) {
            error = multimedia_error(result);
            handle_ = nullptr;
            return false;
        }
        result = waveOutPause(handle_);
        if (result != MMSYSERR_NOERROR) {
            error = multimedia_error(result);
            close();
            return false;
        }
        return true;
    }

    bool submit(const uint8_t *samples, size_t size, std::wstring &error) {
        if (!handle_ || !size) return true;
        if (!samples || size > MAXDWORD) {
            error = L"Invalid PCM audio packet";
            return false;
        }

        for (AudioBuffer &buffer : buffers_) {
            if (buffer.prepared && !(buffer.header.dwFlags & WHDR_DONE))
                continue;
            if (buffer.prepared) {
                MMRESULT result = waveOutUnprepareHeader(
                    handle_, &buffer.header, sizeof buffer.header);
                if (result != MMSYSERR_NOERROR) continue;
                buffer.prepared = false;
            }

            buffer.samples.assign(samples, samples + size);
            std::memset(&buffer.header, 0, sizeof buffer.header);
            buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
            buffer.header.dwBufferLength = static_cast<DWORD>(size);
            MMRESULT result = waveOutPrepareHeader(
                handle_, &buffer.header, sizeof buffer.header);
            if (result != MMSYSERR_NOERROR) {
                error = multimedia_error(result);
                return false;
            }
            buffer.prepared = true;
            result = waveOutWrite(handle_, &buffer.header, sizeof buffer.header);
            if (result != MMSYSERR_NOERROR) {
                error = multimedia_error(result);
                waveOutUnprepareHeader(handle_, &buffer.header,
                                       sizeof buffer.header);
                buffer.prepared = false;
                return false;
            }
            return true;
        }

        error = L"Audio output queue is full";
        return false;
    }

    void pause() {
        if (handle_) waveOutPause(handle_);
    }

    void restart() {
        if (handle_) waveOutRestart(handle_);
    }

    void close() {
        if (!handle_) return;
        waveOutReset(handle_);
        for (AudioBuffer &buffer : buffers_) {
            if (buffer.prepared) {
                waveOutUnprepareHeader(handle_, &buffer.header,
                                       sizeof buffer.header);
                buffer.prepared = false;
            }
            buffer.samples.clear();
        }
        waveOutClose(handle_);
        handle_ = nullptr;
    }

    bool active() const { return handle_ != nullptr; }

private:
    struct AudioBuffer {
        WAVEHDR header = {};
        std::vector<uint8_t> samples;
        bool prepared = false;
    };

    HWAVEOUT handle_ = nullptr;
    std::array<AudioBuffer, kAudioBufferCount> buffers_ = {};
};

bool submit_packet_audio(AudioOutput &output, uint8_t codec,
                         const uint8_t *encoded, size_t encoded_size,
                         std::wstring &error) {
    if (!encoded_size) return true;
    if (codec != HLV1_AUDIO_IMA_ADPCM)
        return output.submit(encoded, encoded_size, error);
    IMAADPCMState state{};
    uint16_t count = 0;
    if (ima_adpcm_block_header_read(encoded, encoded_size, &state, &count)) {
        error = L"Invalid IMA ADPCM block header";
        return false;
    }
    std::vector<int16_t> samples(count);
    size_t decoded_count = 0;
    if (ima_adpcm_decode_block(encoded, encoded_size, samples.data(),
                               samples.size(), &decoded_count)) {
        error = L"Invalid IMA ADPCM audio block";
        return false;
    }
    return output.submit(reinterpret_cast<const uint8_t *>(samples.data()),
                         decoded_count * sizeof samples[0], error);
}

int decode_h263_avi_audio_chunk(H263AviPcmReader *reader, FILE *file,
                                const H2633gpInfo &info,
                                std::vector<uint8_t> &pcm,
                                size_t &sample_count) {
    pcm.clear();
    sample_count = 0;
    if (info.audio_format_tag == 1U) {
        H263AviPcmFrame frame = {};
        const int result =
            h263_avi_pcm_reader_decode_next(reader, file, &frame);
        if (result != H263_3GP_OK) return result;
        pcm.assign(frame.samples, frame.samples + frame.sample_count);
        sample_count = frame.sample_count;
        return H263_3GP_OK;
    }
    if (info.audio_format_tag != 0x11U || !info.audio_block_align ||
        info.audio_block_align > H263_AVI_IMA_MAX_BLOCK_BYTES) {
        return H263_3GP_ERR_FORMAT;
    }

    uint32_t payload_size = 0;
    int result = h263_avi_audio_reader_next_chunk(reader, file,
                                                   &payload_size);
    if (result != H263_3GP_OK) return result;
    if (!payload_size || payload_size % info.audio_block_align) {
        return H263_3GP_ERR_FORMAT;
    }
    std::vector<uint8_t> encoded(payload_size);
    if (std::fread(encoded.data(), 1, encoded.size(), file) !=
        encoded.size()) {
        return H263_3GP_ERR_IO;
    }

    const size_t samples_per_block =
        ima_adpcm_wav_mono_sample_count(info.audio_block_align);
    if (!samples_per_block ||
        samples_per_block != info.audio_samples_per_block) {
        return H263_3GP_ERR_FORMAT;
    }
    const size_t block_count = payload_size / info.audio_block_align;
    std::vector<int16_t> decoded(samples_per_block);
    pcm.reserve(block_count * samples_per_block * sizeof(int16_t));
    for (size_t block = 0; block < block_count; ++block) {
        size_t decoded_count = 0;
        if (ima_adpcm_decode_wav_mono_block(
                encoded.data() + block * info.audio_block_align,
                info.audio_block_align, decoded.data(), decoded.size(),
                &decoded_count) ||
            decoded_count != samples_per_block) {
            return H263_3GP_ERR_DECODE;
        }
        const uint8_t *bytes =
            reinterpret_cast<const uint8_t *>(decoded.data());
        pcm.insert(pcm.end(), bytes,
                   bytes + decoded_count * sizeof(decoded[0]));
        sample_count += decoded_count;
    }
    return H263_3GP_OK;
}

struct SeekIndexEntry {
    __int64 packet_offset = 0;
    uint64_t keyframe_index = 0;
};

class Player {
public:
    explicit Player(HWND window) : window_(window) {
        QueryPerformanceFrequency(&clock_frequency_);
        create_controls();
        layout_controls();
    }

    ~Player() { close(); }

    bool open(const std::wstring &path) {
        close();
        error_.clear();
        path_ = path;

        if (_wfopen_s(&file_, path.c_str(), L"rb") != 0 || !file_)
            return fail(L"Cannot open the selected file");

        uint8_t signature[12] = {};
        if (std::fread(signature, 1, sizeof signature, file_) !=
                sizeof signature ||
            _fseeki64(file_, 0, SEEK_SET) != 0) {
            return fail(L"Cannot read the selected file header");
        }
        if (!std::memcmp(signature, "HLV1", 4)) {
            codec_ = VideoCodec::kHlv;
            const int result = hlv1_header_read(file_, &header_);
            if (result < 0) {
                return fail(L"Invalid HLV header: " +
                            widen_ascii(hlv1_strerror(result)));
            }
        } else if (!std::memcmp(signature, "BPV1", 4)) {
            codec_ = VideoCodec::kBpv;
            const int result = bpv1_header_read(file_, &bpv_header_);
            if (result < 0) {
                return fail(L"Invalid BPV1 header: " +
                            widen_ascii(bpv1_strerror(result)));
            }
            header_ = {};
            header_.width = bpv_header_.width;
            header_.height = bpv_header_.height;
            header_.fps_num = bpv_header_.fps_num;
            header_.fps_den = bpv_header_.fps_den;
            header_.frame_count = bpv_header_.frame_count;
            header_.gop = bpv_header_.keyframe_interval;
            header_.version = bpv_header_.version;
            header_.search_radius = bpv_header_.search_radius;
            if (bpv_header_.audio_codec == BPV1_AUDIO_PCM_U8 ||
                bpv_header_.audio_codec == BPV1_AUDIO_IMA_ADPCM) {
                header_.flags |= HLV1_FLAG_AUDIO;
                header_.audio_codec =
                    bpv_header_.audio_codec == BPV1_AUDIO_IMA_ADPCM
                        ? HLV1_AUDIO_IMA_ADPCM : HLV1_AUDIO_PCM_U8;
                header_.audio_sample_rate = bpv_header_.audio_sample_rate;
                header_.audio_channels = bpv_header_.audio_channels;
            }
        } else if (signature[0] == 0x00 && signature[1] == 0x00 &&
                   signature[2] == 0x01 && signature[3] == 0xba) {
            codec_ = VideoCodec::kMpeg1;
            mpeg_ = plm_create_with_file(file_, FALSE);
            if (!mpeg_) return fail(L"Cannot allocate the MPEG-1 decoder");
            header_ = {};
            const int width = plm_get_width(mpeg_);
            const int height = plm_get_height(mpeg_);
            const double fps = plm_get_framerate(mpeg_);
            if (width <= 0 || height <= 0 ||
                width > UINT16_MAX || height > UINT16_MAX ||
                !mpeg_fps_rational(
                    fps, &header_.fps_num, &header_.fps_den)) {
                return fail(L"Invalid or unsupported MPEG-1 video stream");
            }
            header_.width = static_cast<uint16_t>(width);
            header_.height = static_cast<uint16_t>(height);
            const double duration = plm_get_duration(mpeg_);
            if (!(duration > 0.0)) {
                return fail(L"Cannot determine the MPEG-1 duration");
            }
            header_.frame_count = static_cast<uint32_t>(std::max(
                1.0, std::floor(
                    duration * header_.fps_num / header_.fps_den + 0.5)));
            if (plm_get_num_audio_streams(mpeg_) > 0) {
                const int sample_rate = plm_get_samplerate(mpeg_);
                if (sample_rate <= 0 || sample_rate > UINT16_MAX) {
                    return fail(L"Invalid MPEG-1 Layer II audio stream");
                }
                header_.flags = HLV1_FLAG_AUDIO;
                header_.audio_codec = HLV1_AUDIO_PCM_U8;
                header_.audio_sample_rate =
                    static_cast<uint16_t>(sample_rate);
                header_.audio_channels = 1;
            }
        } else if (!std::memcmp(signature + 4, "ftyp", 4) ||
                   (!std::memcmp(signature, "RIFF", 4) &&
                    !std::memcmp(signature + 8, "AVI ", 4))) {
            codec_ = VideoCodec::kH263;
            h263_decoder_ = h263_3gp_decoder_create();
            const int result =
                h263_decoder_
                    ? h263_3gp_decoder_open(
                          h263_decoder_, file_, &h263_info_)
                    : H263_3GP_ERR_MEMORY;
            if (result != H263_3GP_OK) {
                return fail((!std::memcmp(signature, "RIFF", 4)
                                 ? L"Invalid AVI video: "
                                 : L"Invalid H.263 video: ") +
                            widen_ascii(h263_3gp_strerror(result)));
            }
            codec_ =
                h263_info_.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
                    ? VideoCodec::kMpeg4Simple
                    : VideoCodec::kH263;
            if (h263_info_.fps_num > UINT16_MAX ||
                h263_info_.fps_den > UINT16_MAX) {
                return fail(std::wstring(L"The ") +
                            packet_video_codec_name(codec_) +
                            L" frame rate is unsupported");
            }
            header_ = {};
            header_.width = h263_info_.width;
            header_.height = h263_info_.height;
            header_.fps_num =
                static_cast<uint16_t>(h263_info_.fps_num);
            header_.fps_den =
                static_cast<uint16_t>(h263_info_.fps_den);
            header_.frame_count = h263_info_.frame_count;

            if (h263_info_.container == H263_CONTAINER_AVI &&
                h263_info_.audio_sample_rate) {
                if (_wfopen_s(&h263_audio_file_, path.c_str(), L"rb") != 0 ||
                    !h263_audio_file_) {
                    return fail(L"Cannot open the AVI audio track");
                }
                h263_avi_audio_reader_ =
                    h263_avi_pcm_reader_create();
                H2633gpInfo audio_info = {};
                const int audio_result =
                    h263_avi_audio_reader_
                        ? h263_avi_pcm_reader_open(
                              h263_avi_audio_reader_,
                              h263_audio_file_, &audio_info)
                        : H263_3GP_ERR_MEMORY;
                if (audio_result != H263_3GP_OK) {
                    return fail(L"Invalid AVI audio: " +
                                widen_ascii(
                                    h263_3gp_strerror(audio_result)));
                }
                header_.flags = HLV1_FLAG_AUDIO;
                header_.audio_codec =
                    audio_info.audio_format_tag == 0x11U
                        ? HLV1_AUDIO_IMA_ADPCM
                        : HLV1_AUDIO_PCM_U8;
                header_.audio_sample_rate =
                    static_cast<uint16_t>(
                        audio_info.audio_sample_rate);
                header_.audio_channels = audio_info.audio_channels;
            } else if (h263_info_.container == H263_CONTAINER_3GP) {
                if (_wfopen_s(&h263_audio_file_, path.c_str(), L"rb") != 0 ||
                    !h263_audio_file_) {
                    return fail(L"Cannot open the 3GP audio track");
                }
                h263_audio_decoder_ = amrnb_3gp_decoder_create();
                const int audio_result =
                    h263_audio_decoder_
                        ? amrnb_3gp_decoder_open(
                              h263_audio_decoder_, h263_audio_file_,
                              &amrnb_info_)
                        : AMRNB_3GP_ERR_MEMORY;
                if (audio_result == AMRNB_3GP_OK) {
                    header_.flags = HLV1_FLAG_AUDIO;
                    header_.audio_codec = HLV1_AUDIO_PCM_U8;
                    header_.audio_sample_rate = amrnb_info_.sample_rate;
                    header_.audio_channels = amrnb_info_.channels;
                } else if (audio_result == AMRNB_3GP_ERR_UNSUPPORTED) {
                    amrnb_3gp_decoder_destroy(h263_audio_decoder_);
                    h263_audio_decoder_ = nullptr;
                    fclose(h263_audio_file_);
                    h263_audio_file_ = nullptr;
                } else {
                    return fail(L"Invalid AMR-NB/3GP audio: " +
                                widen_ascii(
                                    amrnb_3gp_strerror(audio_result)));
                }
            }
        } else {
            return fail(
                L"Unsupported video signature; expected HLV1, BPV1, "
                L"MPEG-PS, 3GP/H.263, AVI/H.263 or AVI/MPEG-4 SP");
        }
        if (header_.width > 8192 || header_.height > 8192)
            return fail(L"The video dimensions are too large for this player");
        first_packet_offset_ =
            codec_ == VideoCodec::kMpeg1 ||
                    is_packet_video_codec(codec_)
                ? 0
                : _ftelli64(file_);
        if (first_packet_offset_ < 0)
            return fail(L"Cannot determine the first video packet offset");

        if (!build_seek_index()) return fail(error_);

        if (codec_ == VideoCodec::kHlv) {
            decoder_ = hlv1_decoder_create(&header_);
            if (!decoder_) return fail(L"Cannot allocate the HLV decoder");
        } else if (codec_ == VideoCodec::kBpv) {
            bpv_decoder_ = bpv1_decoder_create(&bpv_header_);
            if (!bpv_decoder_)
                return fail(L"Cannot allocate the BPV1 decoder");
        }

        std::wstring video_error;
        const D3DInputFormat input_format =
            codec_ == VideoCodec::kBpv
                ? D3DInputFormat::kBgra
                : D3DInputFormat::kNv12;
        if (!video_window_ || !video_renderer_.open(
                video_window_, header_.width, header_.height,
                header_.fps_num, header_.fps_den, input_format,
                video_error)) {
            video_warning_ = L"D3D11 output is unavailable; "
                             L"using double-buffered GDI: " + video_error;
        }

        if (header_.flags & HLV1_FLAG_AUDIO) {
            std::wstring audio_error;
            if (!audio_.open(
                    header_.audio_sample_rate,
                    header_.audio_codec == HLV1_AUDIO_IMA_ADPCM ? 16U : 8U,
                    audio_error)) {
                audio_warning_ = L"Audio is disabled: " + audio_error;
            }
        }

        while (ready_.size() < kVideoLeadFrames + 1 && !end_of_file_) {
            if (!decode_one()) return fail(error_);
        }
        if (ready_.empty())
            return fail(L"The selected file contains no video frames");

        present_front();
        QueryPerformanceCounter(&clock_start_);
        audio_.restart();
        loaded_ = true;
        finished_ = false;
        paused_ = false;
        configure_seek_control();
        if (!SetTimer(window_, kPlaybackTimer, 1, nullptr))
            return fail(L"Cannot create the playback timer");
        update_title();
        InvalidateRect(window_, nullptr, FALSE);

        std::wstring warning = video_warning_;
        if (!warning.empty() && !audio_warning_.empty()) warning += L"\n\n";
        warning += audio_warning_;
        if (!warning.empty()) {
            MessageBoxW(window_, warning.c_str(), L"Video Player",
                        MB_OK | MB_ICONWARNING);
        }
        return true;
    }

    void close() {
        if (window_) KillTimer(window_, kPlaybackTimer);
        audio_.close();
        video_renderer_.close();
        if (decoder_) {
            hlv1_decoder_destroy(decoder_);
            decoder_ = nullptr;
        }
        if (bpv_decoder_) {
            bpv1_decoder_destroy(bpv_decoder_);
            bpv_decoder_ = nullptr;
        }
        if (mpeg_) {
            plm_destroy(mpeg_);
            mpeg_ = nullptr;
        }
        if (h263_decoder_) {
            h263_3gp_decoder_destroy(h263_decoder_);
            h263_decoder_ = nullptr;
        }
        if (h263_audio_decoder_) {
            amrnb_3gp_decoder_destroy(h263_audio_decoder_);
            h263_audio_decoder_ = nullptr;
        }
        if (h263_avi_audio_reader_) {
            h263_avi_pcm_reader_destroy(h263_avi_audio_reader_);
            h263_avi_audio_reader_ = nullptr;
        }
        if (h263_audio_file_) {
            fclose(h263_audio_file_);
            h263_audio_file_ = nullptr;
        }
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
        ready_.clear();
        recycled_.clear();
        current_ = {};
        seek_index_.clear();
        std::memset(&header_, 0, sizeof header_);
        std::memset(&bpv_header_, 0, sizeof bpv_header_);
        std::memset(&h263_info_, 0, sizeof h263_info_);
        std::memset(&amrnb_info_, 0, sizeof amrnb_info_);
        mpeg_audio_samples_ = 0;
        amrnb_audio_samples_ = 0;
        codec_ = VideoCodec::kNone;
        first_packet_offset_ = 0;
        decoded_frames_ = 0;
        presented_frames_ = 0;
        end_of_file_ = false;
        loaded_ = false;
        paused_ = false;
        finished_ = false;
        audio_warning_.clear();
        video_warning_.clear();
        seek_dragging_ = false;
        seek_range_max_ = 0;
        if (seek_bar_ && IsWindow(seek_bar_)) {
            EnableWindow(seek_bar_, FALSE);
            SendMessageW(seek_bar_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 0));
            SendMessageW(seek_bar_, TBM_SETPOS, TRUE, 0);
        }
        set_time_label(0);
    }

    void tick() {
        if (!loaded_ || paused_ || finished_) return;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        for (;;) {
            const long double target_offset =
                static_cast<long double>(presented_frames_) *
                clock_frequency_.QuadPart * header_.fps_den / header_.fps_num;
            const LONGLONG target = clock_start_.QuadPart +
                                    static_cast<LONGLONG>(target_offset);
            if (now.QuadPart < target) break;

            if (ready_.empty()) {
                if (end_of_file_) {
                    finish();
                    break;
                }
                if (!decode_one()) {
                    playback_failed();
                    break;
                }
            }
            if (ready_.empty()) {
                finish();
                break;
            }

            present_front();
            while (ready_.size() < kVideoLeadFrames && !end_of_file_) {
                if (!decode_one()) {
                    playback_failed();
                    return;
                }
            }
            QueryPerformanceCounter(&now);
        }
    }

    void toggle_pause() {
        if (!loaded_ || finished_) return;
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (!paused_) {
            paused_ = true;
            pause_started_ = now;
            audio_.pause();
        } else {
            clock_start_.QuadPart += now.QuadPart - pause_started_.QuadPart;
            paused_ = false;
            audio_.restart();
        }
        update_title();
    }

    void toggle_fit() {
        fit_to_window_ = !fit_to_window_;
        CheckMenuItem(GetMenu(window_), ID_VIEW_FIT,
                      MF_BYCOMMAND | (fit_to_window_ ? MF_CHECKED : MF_UNCHECKED));
        render_current();
    }

    void resize() {
        layout_controls();
        render_current();
    }

    void handle_seek_scroll(UINT request) {
        if (!loaded_ || seek_index_.empty() || !seek_bar_) return;
        const int position = static_cast<int>(
            SendMessageW(seek_bar_, TBM_GETPOS, 0, 0));
        const uint64_t target = frame_from_seek_position(position);

        if (request == TB_THUMBTRACK || request == TB_THUMBPOSITION) {
            seek_dragging_ = true;
            set_time_label(target);
            return;
        }
        if (request == TB_ENDTRACK) {
            if (!seek_dragging_) return;
            seek_dragging_ = false;
        } else if (request != TB_LINEUP && request != TB_LINEDOWN &&
                   request != TB_PAGEUP && request != TB_PAGEDOWN &&
                   request != TB_TOP && request != TB_BOTTOM) {
            return;
        }

        if (!seek_to(target)) playback_failed();
    }

    HBRUSH control_color(HDC dc, HWND control) const {
        if (control != time_label_) return nullptr;
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(220, 220, 220));
        return static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    }

    void paint_video(HDC dc, const RECT &client) const {
        if (video_renderer_.active()) return;

        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
        HDC memory_dc = nullptr;
        HBITMAP bitmap = nullptr;
        HGDIOBJ previous_bitmap = nullptr;
        HDC target = dc;
        if (client_width > 0 && client_height > 0) {
            memory_dc = CreateCompatibleDC(dc);
            bitmap = CreateCompatibleBitmap(dc, client_width, client_height);
            if (memory_dc && bitmap) {
                previous_bitmap = SelectObject(memory_dc, bitmap);
                target = memory_dc;
            }
        }

        HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        FillRect(target, &client, black);

        if (current_.pixels.empty()) {
            SetBkMode(target, TRANSPARENT);
            SetTextColor(target, RGB(210, 210, 210));
            RECT text_rect = client;
            const wchar_t *message = error_.empty()
                ? L"Open an .hlv or .bpv1 file (Ctrl+O), or drag it here"
                : error_.c_str();
            DrawTextW(target, message, -1, &text_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        } else {
            int draw_width = current_.width;
            int draw_height = current_.height;
            if (fit_to_window_ && client_width > 0 && client_height > 0) {
                if (static_cast<int64_t>(client_width) * current_.height <=
                    static_cast<int64_t>(client_height) * current_.width) {
                    draw_width = client_width;
                    draw_height = static_cast<int>(
                        static_cast<int64_t>(client_width) * current_.height /
                        current_.width);
                } else {
                    draw_height = client_height;
                    draw_width = static_cast<int>(
                        static_cast<int64_t>(client_height) * current_.width /
                        current_.height);
                }
            }
            const int draw_x = (client_width - draw_width) / 2;
            const int draw_y = (client_height - draw_height) / 2;

            BITMAPINFO info = {};
            info.bmiHeader.biSize = sizeof info.bmiHeader;
            info.bmiHeader.biWidth = current_.width;
            info.bmiHeader.biHeight = -current_.height;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            SetStretchBltMode(target, COLORONCOLOR);
            StretchDIBits(target, draw_x, draw_y, draw_width, draw_height,
                          0, 0, current_.width, current_.height,
                          current_.pixels.data(), &info,
                          DIB_RGB_COLORS, SRCCOPY);
        }

        if (target == memory_dc) {
            BitBlt(dc, 0, 0, client_width, client_height,
                   memory_dc, 0, 0, SRCCOPY);
            SelectObject(memory_dc, previous_bitmap);
        }
        if (bitmap) DeleteObject(bitmap);
        if (memory_dc) DeleteDC(memory_dc);
    }

    bool paused() const { return paused_; }
    bool fit_to_window() const { return fit_to_window_; }

private:
    void create_controls() {
        const HINSTANCE instance = reinterpret_cast<HINSTANCE>(
            GetWindowLongPtrW(window_, GWLP_HINSTANCE));
        video_window_ = CreateWindowExW(
            WS_EX_ACCEPTFILES, kVideoWindowClass, L"",
            WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
            0, 0, 0, 0, window_, nullptr, instance, this);
        seek_bar_ = CreateWindowExW(
            0, TRACKBAR_CLASSW, L"",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS | TBS_TOOLTIPS,
            0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_SEEK_BAR)),
            instance, nullptr);
        time_label_ = CreateWindowExW(
            0, L"STATIC", L"0:00 / 0:00",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            0, 0, 0, 0, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(ID_TIME_LABEL)),
            instance, nullptr);
        const HFONT font = static_cast<HFONT>(
            GetStockObject(DEFAULT_GUI_FONT));
        if (seek_bar_) {
            SendMessageW(seek_bar_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(font), TRUE);
            EnableWindow(seek_bar_, FALSE);
        }
        if (time_label_) {
            SendMessageW(time_label_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    void layout_controls() const {
        RECT client = {};
        GetClientRect(window_, &client);
        const int width = std::max<LONG>(0, client.right - client.left);
        const int height = std::max<LONG>(0, client.bottom - client.top);
        const int top = std::max(0, height - kSeekBarHeight);
        if (video_window_) {
            MoveWindow(video_window_, 0, 0, width, top, TRUE);
        }
        if (!seek_bar_ || !time_label_) return;
        const int label_width = std::min(
            kTimeLabelWidth, std::max(0, width - 2 * kSeekMargin));
        const int label_x = std::max(
            kSeekMargin, width - label_width - kSeekMargin);
        const int seek_width = std::max(
            0, label_x - 2 * kSeekMargin);
        MoveWindow(seek_bar_, kSeekMargin, top + 3,
                   seek_width, kSeekBarHeight - 6, TRUE);
        MoveWindow(time_label_, label_x, top + 7,
                   label_width, kSeekBarHeight - 14, TRUE);
    }

    std::wstring format_time(uint64_t frame) const {
        uint64_t seconds = 0;
        if (header_.fps_num) {
            const long double value =
                static_cast<long double>(frame) * header_.fps_den /
                header_.fps_num;
            seconds = value >= static_cast<long double>(UINT64_MAX)
                ? UINT64_MAX
                : static_cast<uint64_t>(value);
        }
        const uint64_t hours = seconds / 3600;
        const uint64_t minutes = (seconds / 60) % 60;
        const uint64_t remaining = seconds % 60;
        wchar_t text[64] = {};
        if (hours) {
            swprintf_s(text, L"%llu:%02llu:%02llu",
                       static_cast<unsigned long long>(hours),
                       static_cast<unsigned long long>(minutes),
                       static_cast<unsigned long long>(remaining));
        } else {
            swprintf_s(text, L"%llu:%02llu",
                       static_cast<unsigned long long>(seconds / 60),
                       static_cast<unsigned long long>(remaining));
        }
        return text;
    }

    void set_time_label(uint64_t frame) const {
        if (!time_label_ || !IsWindow(time_label_)) return;
        const std::wstring current = format_time(frame);
        const std::wstring total = format_time(seek_index_.size());
        const std::wstring text = current + L" / " + total;
        SetWindowTextW(time_label_, text.c_str());
    }

    bool build_seek_index() {
        seek_index_.clear();
        if (codec_ == VideoCodec::kMpeg1 ||
            is_packet_video_codec(codec_)) {
            for (uint32_t frame = 0; frame < header_.frame_count; ++frame)
                seek_index_.push_back({0, 0});
            if (seek_index_.empty()) {
                error_ = L"The stream contains no video frames";
                return false;
            }
            if (codec_ == VideoCodec::kMpeg1) plm_rewind(mpeg_);
            return true;
        }
        if (_fseeki64(file_, first_packet_offset_, SEEK_SET) != 0) {
            error_ = L"Cannot seek to the first video packet";
            return false;
        }

        uint64_t last_keyframe = UINT64_MAX;
        if (codec_ == VideoCodec::kBpv) {
            if (_fseeki64(file_, 0, SEEK_END) != 0) {
                error_ = L"Cannot determine the BPV1 file size";
                return false;
            }
            const __int64 file_end = _ftelli64(file_);
            if (file_end < first_packet_offset_ ||
                _fseeki64(file_, first_packet_offset_, SEEK_SET) != 0) {
                error_ = L"Invalid BPV1 file size";
                return false;
            }
            for (uint32_t frame = 0; frame < bpv_header_.frame_count;
                 ++frame) {
                const __int64 offset = _ftelli64(file_);
                if (offset < 0) {
                    error_ = L"Cannot read the BPV1 packet position";
                    return false;
                }
                BPV1FrameInfo info = {};
                const int result =
                    bpv1_frame_info_read(file_, &bpv_header_, &info);
                if (result != BPV1_OK) {
                    error_ = L"Cannot build the BPV1 seek index at frame " +
                             std::to_wstring(frame) + L": " +
                             widen_ascii(bpv1_strerror(result));
                    return false;
                }
                if (info.keyframe) last_keyframe = frame;
                if (last_keyframe == UINT64_MAX) {
                    error_ = L"The first BPV1 packet is not a keyframe";
                    return false;
                }
                seek_index_.push_back({offset, last_keyframe});
                const __int64 payload_offset = _ftelli64(file_);
                if (payload_offset < 0 || payload_offset > file_end ||
                    static_cast<uint64_t>(info.frame_bytes) +
                            info.audio_bytes >
                        static_cast<uint64_t>(file_end - payload_offset) ||
                    _fseeki64(file_,
                              payload_offset + info.frame_bytes +
                                  info.audio_bytes,
                              SEEK_SET) != 0) {
                    error_ = L"Truncated BPV1 frame " +
                             std::to_wstring(frame);
                    return false;
                }
            }
            if (_ftelli64(file_) != file_end) {
                error_ = L"Trailing data follows the BPV1 stream";
                return false;
            }
        } else {
            for (;;) {
                const __int64 offset = _ftelli64(file_);
                if (offset < 0) {
                    error_ = L"Cannot read the HLV packet position";
                    return false;
                }
                HLV1Packet packet = {};
                const int result = hlv1_packet_read(file_, &packet);
                if (result == HLV1_EOF) break;
                if (result < 0) {
                    error_ = L"Cannot build the seek index at frame " +
                             std::to_wstring(seek_index_.size()) + L": " +
                             widen_ascii(hlv1_strerror(result));
                    hlv1_packet_free(&packet);
                    return false;
                }
                if (packet.frame_type == HLV1_FRAME_KEY)
                    last_keyframe = seek_index_.size();
                if (last_keyframe == UINT64_MAX) {
                    error_ = L"The first HLV packet is not a keyframe";
                    hlv1_packet_free(&packet);
                    return false;
                }
                seek_index_.push_back({offset, last_keyframe});
                hlv1_packet_free(&packet);
            }
        }
        if (seek_index_.empty()) {
            error_ = L"The selected file contains no video frames";
            return false;
        }
        if (_fseeki64(file_, seek_index_.front().packet_offset, SEEK_SET) != 0) {
            error_ = L"Cannot rewind the video after indexing";
            return false;
        }
        return true;
    }

    void configure_seek_control() {
        if (!seek_bar_ || seek_index_.empty()) return;
        const uint64_t last_frame = seek_index_.size() - 1;
        seek_range_max_ = static_cast<int>(
            std::min<uint64_t>(last_frame, INT_MAX));
        SendMessageW(seek_bar_, TBM_SETRANGEMIN, FALSE, 0);
        SendMessageW(seek_bar_, TBM_SETRANGEMAX, TRUE, seek_range_max_);

        const long double units_per_second =
            last_frame && header_.fps_num
                ? static_cast<long double>(seek_range_max_) *
                  header_.fps_num /
                  (static_cast<long double>(last_frame) * header_.fps_den)
                : 1.0L;
        const int line = std::max(
            1, static_cast<int>(units_per_second + 0.5L));
        const int page = std::max(
            line, static_cast<int>(units_per_second * 10.0L + 0.5L));
        SendMessageW(seek_bar_, TBM_SETLINESIZE, 0, line);
        SendMessageW(seek_bar_, TBM_SETPAGESIZE, 0, page);
        SendMessageW(seek_bar_, TBM_SETPOS, TRUE, 0);
        EnableWindow(seek_bar_, seek_range_max_ > 0);
        set_time_label(0);
    }

    uint64_t frame_from_seek_position(int position) const {
        if (seek_index_.size() <= 1 || seek_range_max_ <= 0) return 0;
        position = std::clamp(position, 0, seek_range_max_);
        const long double frame =
            static_cast<long double>(position) *
            (seek_index_.size() - 1) / seek_range_max_;
        return static_cast<uint64_t>(frame + 0.5L);
    }

    int seek_position_from_frame(uint64_t frame) const {
        if (seek_index_.size() <= 1 || seek_range_max_ <= 0) return 0;
        frame = std::min<uint64_t>(frame, seek_index_.size() - 1);
        const long double position =
            static_cast<long double>(frame) * seek_range_max_ /
            (seek_index_.size() - 1);
        return static_cast<int>(position + 0.5L);
    }

    void update_seek_position(uint64_t frame) const {
        if (seek_dragging_) return;
        if (seek_bar_ && IsWindow(seek_bar_)) {
            SendMessageW(seek_bar_, TBM_SETPOS, TRUE,
                         seek_position_from_frame(frame));
        }
        set_time_label(frame);
    }

    void recycle_queued_frames() {
        if (!current_.pixels.empty())
            recycled_.push_back(std::move(current_));
        current_ = {};
        while (!ready_.empty()) {
            recycled_.push_back(std::move(ready_.front()));
            ready_.pop_front();
        }
    }

    bool reopen_audio() {
        audio_.close();
        audio_warning_.clear();
        if (!(header_.flags & HLV1_FLAG_AUDIO)) return true;
        std::wstring audio_error;
        if (audio_.open(
                header_.audio_sample_rate,
                header_.audio_codec == HLV1_AUDIO_IMA_ADPCM ? 16U : 8U,
                audio_error)) return true;
        audio_warning_ = L"Audio is disabled: " + audio_error;
        return true;
    }

    bool seek_to(uint64_t target) {
        if (!loaded_ || seek_index_.empty()) return true;
        target = std::min<uint64_t>(target, seek_index_.size() - 1);
        const bool resume = !paused_;
        KillTimer(window_, kPlaybackTimer);
        audio_.close();
        recycle_queued_frames();

        if (codec_ == VideoCodec::kMpeg1) {
            if (mpeg_) {
                plm_destroy(mpeg_);
                mpeg_ = nullptr;
            }
            if (_fseeki64(file_, 0, SEEK_SET) != 0) {
                error_ = L"Cannot rewind the MPEG-1 stream";
                return false;
            }
            mpeg_ = plm_create_with_file(file_, FALSE);
            if (!mpeg_ || plm_get_width(mpeg_) != header_.width ||
                plm_get_height(mpeg_) != header_.height) {
                error_ = L"Cannot reset the MPEG-1 decoder for seeking";
                return false;
            }
            mpeg_audio_samples_ = 0;
        } else if (is_packet_video_codec(codec_)) {
            if (h263_decoder_) {
                h263_3gp_decoder_destroy(h263_decoder_);
                h263_decoder_ = nullptr;
            }
            if (_fseeki64(file_, 0, SEEK_SET) != 0) {
                error_ = std::wstring(L"Cannot rewind the ") +
                         packet_video_codec_name(codec_) + L" stream";
                return false;
            }
            H2633gpInfo info = {};
            h263_decoder_ = h263_3gp_decoder_create();
            const int result =
                h263_decoder_
                    ? h263_3gp_decoder_open(h263_decoder_, file_, &info)
                    : H263_3GP_ERR_MEMORY;
            if (result != H263_3GP_OK ||
                info.width != h263_info_.width ||
                info.height != h263_info_.height ||
                info.frame_count != h263_info_.frame_count) {
                error_ = std::wstring(L"Cannot reset the ") +
                         packet_video_codec_name(codec_) +
                         L" decoder for seeking";
                return false;
            }
            if (h263_avi_audio_reader_) {
                h263_avi_pcm_reader_destroy(
                    h263_avi_audio_reader_);
                h263_avi_audio_reader_ = nullptr;
                if (!h263_audio_file_ ||
                    _fseeki64(h263_audio_file_, 0, SEEK_SET) != 0) {
                    error_ = L"Cannot rewind the AVI audio track";
                    return false;
                }
                H2633gpInfo audio_info = {};
                h263_avi_audio_reader_ =
                    h263_avi_pcm_reader_create();
                const int audio_result =
                    h263_avi_audio_reader_
                        ? h263_avi_pcm_reader_open(
                              h263_avi_audio_reader_,
                              h263_audio_file_, &audio_info)
                        : H263_3GP_ERR_MEMORY;
                if (audio_result != H263_3GP_OK ||
                    audio_info.audio_sample_rate !=
                        h263_info_.audio_sample_rate ||
                    audio_info.audio_format_tag !=
                        h263_info_.audio_format_tag) {
                    error_ =
                        L"Cannot reset the AVI audio reader for seeking";
                    return false;
                }
                amrnb_audio_samples_ = 0;
            }
            if (h263_audio_decoder_) {
                amrnb_3gp_decoder_destroy(h263_audio_decoder_);
                h263_audio_decoder_ = nullptr;
                if (!h263_audio_file_ ||
                    _fseeki64(h263_audio_file_, 0, SEEK_SET) != 0) {
                    error_ = L"Cannot rewind the AMR-NB/3GP audio track";
                    return false;
                }
                AmrNb3gpInfo audio_info = {};
                h263_audio_decoder_ = amrnb_3gp_decoder_create();
                const int audio_result =
                    h263_audio_decoder_
                        ? amrnb_3gp_decoder_open(
                              h263_audio_decoder_, h263_audio_file_,
                              &audio_info)
                        : AMRNB_3GP_ERR_MEMORY;
                if (audio_result != AMRNB_3GP_OK ||
                    audio_info.frame_count != amrnb_info_.frame_count ||
                    audio_info.sample_rate != amrnb_info_.sample_rate) {
                    error_ =
                        L"Cannot reset the AMR-NB decoder for seeking";
                    return false;
                }
                amrnb_audio_samples_ = 0;
            }
        } else if (codec_ == VideoCodec::kBpv) {
            if (!bpv_decoder_) {
                error_ = L"The BPV1 decoder is unavailable";
                return false;
            }
            bpv1_decoder_reset(bpv_decoder_);
        } else {
            if (decoder_) hlv1_decoder_destroy(decoder_);
            decoder_ = hlv1_decoder_create(&header_);
            if (!decoder_) {
                error_ = L"Cannot reset the HLV decoder for seeking";
                return false;
            }
        }

        const uint64_t keyframe =
            is_packet_video_codec(codec_)
                ? 0
                : seek_index_[target].keyframe_index;
        if (codec_ != VideoCodec::kMpeg1 &&
            !is_packet_video_codec(codec_) &&
            _fseeki64(file_, seek_index_[keyframe].packet_offset,
                      SEEK_SET) != 0) {
                error_ = L"Cannot seek to the selected video keyframe";
                return false;
        }
        decoded_frames_ = keyframe;
        presented_frames_ = target;
        end_of_file_ = false;
        finished_ = false;
        error_.clear();

        while (decoded_frames_ < target) {
            const uint64_t before = decoded_frames_;
            if (!decode_one(false, false)) return false;
            if (end_of_file_ || decoded_frames_ == before) {
                error_ = L"The video ended before the selected frame";
                return false;
            }
        }

        reopen_audio();
        while (ready_.size() < kVideoLeadFrames + 1 && !end_of_file_) {
            if (!decode_one(true, true)) return false;
        }
        if (ready_.empty()) {
            error_ = L"The selected video frame could not be decoded";
            return false;
        }
        present_front();

        LARGE_INTEGER now = {};
        QueryPerformanceCounter(&now);
        const long double target_offset =
            static_cast<long double>(target) * clock_frequency_.QuadPart *
            header_.fps_den / header_.fps_num;
        clock_start_.QuadPart =
            now.QuadPart - static_cast<LONGLONG>(target_offset);
        if (resume) {
            paused_ = false;
            audio_.restart();
        } else {
            paused_ = true;
            pause_started_ = now;
            audio_.pause();
        }
        if (!SetTimer(window_, kPlaybackTimer, 1, nullptr)) {
            error_ = L"Cannot recreate the playback timer after seeking";
            return false;
        }
        update_title();
        return true;
    }

    bool fail(const std::wstring &message) {
        error_ = message;
        const std::wstring saved_error = error_;
        close();
        error_ = saved_error;
        update_title();
        InvalidateRect(window_, nullptr, FALSE);
        MessageBoxW(window_, error_.c_str(), L"Video Player",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    bool decode_one(bool queue_video = true, bool queue_audio = true) {
        if (is_packet_video_codec(codec_)) {
            H2633gpFrame decoded = {};
            const int result = h263_3gp_decoder_decode_next(
                h263_decoder_, file_, &decoded);
            if (result == H263_3GP_EOF) {
                end_of_file_ = true;
                return true;
            }
            if (result != H263_3GP_OK) {
                error_ = std::wstring(packet_video_codec_name(codec_)) +
                         L" frame " +
                         std::to_wstring(decoded_frames_) +
                         L" failed to decode: " +
                         widen_ascii(h263_3gp_codec_strerror(
                             packet_video_library_codec(codec_), result));
                return false;
            }
            if (queue_video) {
                VideoFrame output;
                if (!recycled_.empty()) {
                    output = std::move(recycled_.back());
                    recycled_.pop_back();
                }
                convert_h263_frame(&decoded, output);
                ready_.push_back(std::move(output));
            }
            if (h263_audio_decoder_ ||
                h263_avi_audio_reader_) {
                const uint64_t target_samples =
                    ((decoded_frames_ + 1U) *
                     header_.audio_sample_rate * header_.fps_den) /
                    header_.fps_num;
                std::vector<uint8_t> pcm;
                if (queue_audio && audio_.active()) {
                    pcm.reserve(static_cast<size_t>(
                        target_samples > amrnb_audio_samples_
                            ? target_samples - amrnb_audio_samples_ +
                                  AMRNB_SAMPLES_PER_FRAME
                            : AMRNB_SAMPLES_PER_FRAME));
                }
                while (amrnb_audio_samples_ < target_samples) {
                    if (h263_avi_audio_reader_) {
                        std::vector<uint8_t> audio_chunk;
                        size_t audio_sample_count = 0;
                        const int audio_result = decode_h263_avi_audio_chunk(
                            h263_avi_audio_reader_, h263_audio_file_,
                            h263_info_, audio_chunk, audio_sample_count);
                        if (audio_result == H263_3GP_EOF) break;
                        if (audio_result != H263_3GP_OK) {
                            error_ = L"AVI audio chunk failed: " +
                                     widen_ascii(
                                         h263_3gp_strerror(
                                             audio_result));
                            return false;
                        }
                        amrnb_audio_samples_ += audio_sample_count;
                        if (queue_audio && audio_.active()) {
                            pcm.insert(pcm.end(), audio_chunk.begin(),
                                       audio_chunk.end());
                        }
                        continue;
                    }
                    AmrNb3gpFrame audio_frame = {};
                    const int audio_result = amrnb_3gp_decoder_decode_next(
                        h263_audio_decoder_, h263_audio_file_,
                        &audio_frame);
                    if (audio_result == AMRNB_3GP_EOF) break;
                    if (audio_result != AMRNB_3GP_OK) {
                        error_ = L"AMR-NB frame " +
                                 std::to_wstring(
                                     amrnb_audio_samples_ /
                                     AMRNB_SAMPLES_PER_FRAME) +
                                 L" failed to decode: " +
                                 widen_ascii(
                                     amrnb_3gp_strerror(audio_result));
                        return false;
                    }
                    amrnb_audio_samples_ += audio_frame.sample_count;
                    if (queue_audio && audio_.active()) {
                        for (uint16_t i = 0;
                             i < audio_frame.sample_count; ++i) {
                            pcm.push_back(static_cast<uint8_t>(
                                (static_cast<int32_t>(
                                     audio_frame.samples[i]) +
                                 32768) >>
                                8));
                        }
                    }
                }
                if (!pcm.empty()) {
                    std::wstring audio_error;
                    if (!audio_.submit(pcm.data(), pcm.size(),
                                       audio_error)) {
                        audio_warning_ = L"Audio stopped: " + audio_error;
                        audio_.close();
                    }
                }
            }
            ++decoded_frames_;
            return true;
        }
        if (codec_ == VideoCodec::kMpeg1) {
            plm_frame_t *decoded = plm_decode_video(mpeg_);
            if (!decoded) {
                end_of_file_ = true;
                return true;
            }
            if (queue_video) {
                VideoFrame output;
                if (!recycled_.empty()) {
                    output = std::move(recycled_.back());
                    recycled_.pop_back();
                }
                convert_mpeg_frame(decoded, output);
                ready_.push_back(std::move(output));
            }

            if (header_.flags & HLV1_FLAG_AUDIO) {
                const uint64_t target_samples =
                    ((decoded_frames_ + 1U) *
                     header_.audio_sample_rate * header_.fps_den) /
                    header_.fps_num;
                while (mpeg_audio_samples_ < target_samples) {
                    plm_samples_t *samples = plm_decode_audio(mpeg_);
                    if (!samples) break;
                    std::array<uint8_t, PLM_AUDIO_SAMPLES_PER_FRAME> pcm{};
                    for (unsigned i = 0; i < samples->count; ++i) {
                        pcm[i] = mpeg_sample_to_u8(
                            samples->interleaved[i * 2U],
                            samples->interleaved[i * 2U + 1U]);
                    }
                    mpeg_audio_samples_ += samples->count;
                    if (queue_audio && audio_.active()) {
                        std::wstring audio_error;
                        if (!audio_.submit(
                                pcm.data(), samples->count, audio_error)) {
                            audio_warning_ =
                                L"Audio stopped: " + audio_error;
                            audio_.close();
                        }
                    }
                }
            }
            ++decoded_frames_;
            return true;
        }

        if (codec_ == VideoCodec::kBpv) {
            BPV1Packet packet = {};
            int result =
                bpv1_decoder_read_packet(bpv_decoder_, file_, &packet);
            if (result == BPV1_EOF) {
                end_of_file_ = true;
                return true;
            }
            if (result < 0) {
                error_ = L"BPV1 packet read failed: " +
                         widen_ascii(bpv1_strerror(result));
                return false;
            }

            const BPV1Frame *decoded = nullptr;
            result = bpv1_decoder_decode(bpv_decoder_, &packet, &decoded);
            if (result < 0) {
                error_ = L"BPV1 frame " +
                         std::to_wstring(decoded_frames_) +
                         L" failed to decode: " +
                         widen_ascii(bpv1_strerror(result));
                return false;
            }
            if (queue_video) {
                VideoFrame output;
                if (!recycled_.empty()) {
                    output = std::move(recycled_.back());
                    recycled_.pop_back();
                }
                if (!convert_bpv_frame(&bpv_header_, decoded, output)) {
                    error_ = L"Cannot render the decoded BPV1 frame";
                    return false;
                }
                ready_.push_back(std::move(output));
            }
            if (queue_audio && audio_.active()) {
                const size_t audio_size =
                    bpv1_packet_audio_size(&packet);
                const uint8_t *audio_data =
                    bpv1_packet_audio_data(&packet);
                std::wstring audio_error;
                if (!submit_packet_audio(audio_, header_.audio_codec,
                                         audio_data, audio_size,
                                         audio_error)) {
                    audio_warning_ = L"Audio stopped: " + audio_error;
                    audio_.close();
                }
            }
            ++decoded_frames_;
            return true;
        }

        HLV1Packet packet = {};
        int result = hlv1_packet_read(file_, &packet);
        if (result == HLV1_EOF) {
            end_of_file_ = true;
            return true;
        }
        if (result < 0) {
            error_ = L"Packet read failed: " + widen_ascii(hlv1_strerror(result));
            hlv1_packet_free(&packet);
            return false;
        }

        const HLV1Frame *decoded = nullptr;
        result = hlv1_decoder_decode(decoder_, &packet, &decoded);
        if (result < 0) {
            error_ = L"Frame " + std::to_wstring(decoded_frames_) +
                     L" failed to decode: " + widen_ascii(hlv1_strerror(result));
            hlv1_packet_free(&packet);
            return false;
        }

        if (queue_video) {
            VideoFrame output;
            if (!recycled_.empty()) {
                output = std::move(recycled_.back());
                recycled_.pop_back();
            }
            convert_frame(decoded, output);
            ready_.push_back(std::move(output));
        }

        if (queue_audio && audio_.active()) {
            const size_t audio_size = hlv1_packet_audio_size(&packet);
            const uint8_t *audio_data = hlv1_packet_audio_data(&packet);
            std::wstring audio_error;
            if (!submit_packet_audio(audio_, header_.audio_codec,
                                     audio_data, audio_size, audio_error)) {
                audio_warning_ = L"Audio stopped: " + audio_error;
                audio_.close();
            }
        }
        hlv1_packet_free(&packet);
        ++decoded_frames_;
        return true;
    }

    void present_front() {
        if (!current_.pixels.empty())
            recycled_.push_back(std::move(current_));
        current_ = std::move(ready_.front());
        ready_.pop_front();
        ++presented_frames_;
        update_seek_position(presented_frames_ - 1);
        render_current();
    }

    void render_current() {
        if (video_renderer_.active() && !current_.pixels.empty()) {
            std::wstring render_error;
            if (video_renderer_.present(
                    current_, fit_to_window_, render_error)) {
                return;
            }
            video_warning_ =
                L"D3D11 output stopped; using double-buffered GDI: " +
                render_error;
            update_title();
        }
        if (video_window_) InvalidateRect(video_window_, nullptr, FALSE);
    }

    void playback_failed() {
        KillTimer(window_, kPlaybackTimer);
        audio_.close();
        finished_ = true;
        update_title();
        MessageBoxW(window_, error_.c_str(), L"Video Player",
                    MB_OK | MB_ICONERROR);
    }

    void finish() {
        KillTimer(window_, kPlaybackTimer);
        finished_ = true;
        update_title();
    }

    void update_title() const {
        std::wstring title =
            L"HLV/BPV/MPEG-1/H.263/MPEG-4 SP Player";
        if (!path_.empty()) title += L" - " + file_name(path_);
        if (loaded_) {
            title += L" [";
            title += video_renderer_.active()
                ? video_renderer_.label()
                : L"double-buffered GDI";
            title += L"]";
        }
        if (paused_) title += L" [paused]";
        else if (finished_) title += L" [finished]";
        SetWindowTextW(window_, title.c_str());
    }

    HWND window_ = nullptr;
    HWND video_window_ = nullptr;
    HWND seek_bar_ = nullptr;
    HWND time_label_ = nullptr;
    FILE *file_ = nullptr;
    FILE *h263_audio_file_ = nullptr;
    HLV1Decoder *decoder_ = nullptr;
    BPV1Decoder *bpv_decoder_ = nullptr;
    plm_t *mpeg_ = nullptr;
    H2633gpDecoder *h263_decoder_ = nullptr;
    AmrNb3gpDecoder *h263_audio_decoder_ = nullptr;
    H263AviPcmReader *h263_avi_audio_reader_ = nullptr;
    HLV1Header header_ = {};
    BPV1Header bpv_header_ = {};
    H2633gpInfo h263_info_ = {};
    AmrNb3gpInfo amrnb_info_ = {};
    VideoCodec codec_ = VideoCodec::kNone;
    AudioOutput audio_;
    D3DVideoRenderer video_renderer_;
    std::deque<VideoFrame> ready_;
    std::vector<VideoFrame> recycled_;
    std::vector<SeekIndexEntry> seek_index_;
    VideoFrame current_;
    std::wstring path_;
    std::wstring error_;
    std::wstring audio_warning_;
    std::wstring video_warning_;
    LARGE_INTEGER clock_frequency_ = {};
    LARGE_INTEGER clock_start_ = {};
    LARGE_INTEGER pause_started_ = {};
    uint64_t decoded_frames_ = 0;
    uint64_t presented_frames_ = 0;
    uint64_t mpeg_audio_samples_ = 0;
    uint64_t amrnb_audio_samples_ = 0;
    __int64 first_packet_offset_ = 0;
    int seek_range_max_ = 0;
    bool loaded_ = false;
    bool paused_ = false;
    bool finished_ = false;
    bool end_of_file_ = false;
    bool fit_to_window_ = true;
    bool seek_dragging_ = false;
};

LRESULT CALLBACK video_window_proc(HWND window, UINT message,
                                   WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        SetWindowLongPtrW(
            window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto *player = reinterpret_cast<Player *>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        RECT client = {};
        GetClientRect(window, &client);
        if (player) {
            player->paint_video(dc, client);
        } else {
            FillRect(
                dc, &client,
                static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        }
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DROPFILES:
        return SendMessageW(GetParent(window), message, wparam, lparam);
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

Player *g_player = nullptr;

std::wstring choose_file(HWND owner) {
    std::vector<wchar_t> path(32768);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.lpstrFilter =
        L"Supported video (*.hlv;*.bpv1;*.mpg;*.mpeg;*.3gp;*.avi)\0"
            L"*.hlv;*.bpv1;*.mpg;*.mpeg;*.3gp;*.avi\0"
        L"HLV video (*.hlv)\0*.hlv\0"
        L"BPV1 video (*.bpv1)\0*.bpv1\0"
        L"MPEG-1 Program Stream (*.mpg;*.mpeg)\0*.mpg;*.mpeg\0"
        L"H.263 3GP video (*.3gp)\0*.3gp\0"
        L"H.263 AVI video (*.avi)\0*.avi\0"
        L"MPEG-4 SP AVI video (*.avi)\0*.avi\0"
        L"All files (*.*)\0*.*\0\0";
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::wstring(path.data()) : std::wstring();
}

void open_interactively(HWND window) {
    std::wstring path = choose_file(window);
    if (!path.empty()) g_player->open(path);
}

HMENU create_main_menu() {
    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    HMENU playback = CreatePopupMenu();
    HMENU view = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, ID_FILE_OPEN, L"&Open...\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, ID_FILE_EXIT, L"E&xit\tEsc");
    AppendMenuW(playback, MF_STRING, ID_PLAY_PAUSE, L"&Pause/Resume\tSpace");
    AppendMenuW(view, MF_STRING | MF_CHECKED, ID_VIEW_FIT,
                L"&Fit to window\tF");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(playback),
                L"&Playback");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&View");
    return menu;
}

LRESULT CALLBACK window_proc(HWND window, UINT message,
                             WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_FILE_OPEN: open_interactively(window); return 0;
        case ID_FILE_EXIT: DestroyWindow(window); return 0;
        case ID_PLAY_PAUSE: g_player->toggle_pause(); return 0;
        case ID_VIEW_FIT: g_player->toggle_fit(); return 0;
        default: break;
        }
        break;
    case WM_KEYDOWN:
        if (wparam == VK_SPACE) g_player->toggle_pause();
        else if (wparam == VK_ESCAPE) DestroyWindow(window);
        else if (wparam == 'O' && (GetKeyState(VK_CONTROL) & 0x8000))
            open_interactively(window);
        else if (wparam == 'F') g_player->toggle_fit();
        return 0;
    case WM_TIMER:
        if (wparam == kPlaybackTimer) g_player->tick();
        return 0;
    case WM_HSCROLL:
        if (g_player && reinterpret_cast<HWND>(lparam) &&
            GetDlgCtrlID(reinterpret_cast<HWND>(lparam)) == ID_SEEK_BAR) {
            g_player->handle_seek_scroll(LOWORD(wparam));
            return 0;
        }
        break;
    case WM_CTLCOLORSTATIC:
        if (g_player) {
            HBRUSH brush = g_player->control_color(
                reinterpret_cast<HDC>(wparam),
                reinterpret_cast<HWND>(lparam));
            if (brush) return reinterpret_cast<LRESULT>(brush);
        }
        break;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wparam);
        const UINT length = DragQueryFileW(drop, 0, nullptr, 0);
        std::vector<wchar_t> path(static_cast<size_t>(length) + 1);
        DragQueryFileW(drop, 0, path.data(), static_cast<UINT>(path.size()));
        DragFinish(drop);
        g_player->open(path.data());
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC dc = BeginPaint(window, &paint);
        FillRect(
            dc, &paint.rcPaint,
            static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        if (g_player && wparam != SIZE_MINIMIZED) g_player->resize();
        else InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        g_player->close();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int check_file(const wchar_t *path) {
    FILE *file = nullptr;
    if (_wfopen_s(&file, path, L"rb") != 0 || !file) return 1;
    uint8_t signature[12] = {};
    if (std::fread(signature, 1, sizeof signature, file) !=
            sizeof signature ||
        _fseeki64(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 1;
    }

    uint64_t frames = 0;
    uint64_t audio_bytes = 0;
    uint64_t checksum = UINT64_C(14695981039346656037);
    VideoFrame converted;
    const char *label = nullptr;
    const char *failure_detail = nullptr;
    bool valid = false;

    if (!std::memcmp(signature, "HLV1", 4)) {
        HLV1Header header = {};
        int result = hlv1_header_read(file, &header);
        HLV1Decoder *decoder =
            result < 0 ? nullptr : hlv1_decoder_create(&header);
        if (!decoder) {
            fclose(file);
            return 1;
        }
        for (;;) {
            HLV1Packet packet = {};
            result = hlv1_packet_read(file, &packet);
            if (result == HLV1_EOF) break;
            if (result < 0) {
                hlv1_packet_free(&packet);
                break;
            }
            const HLV1Frame *frame = nullptr;
            result = hlv1_decoder_decode(decoder, &packet, &frame);
            if (result < 0) {
                hlv1_packet_free(&packet);
                break;
            }
            const size_t audio_size = hlv1_packet_audio_size(&packet);
            if (audio_size && !hlv1_packet_audio_data(&packet)) {
                result = HLV1_ERR_FORMAT;
                hlv1_packet_free(&packet);
                break;
            }
            audio_bytes += audio_size;
            convert_frame(frame, converted);
            for (uint32_t pixel : converted.pixels) {
                checksum ^= pixel;
                checksum *= UINT64_C(1099511628211);
            }
            ++frames;
            hlv1_packet_free(&packet);
        }
        valid = result == HLV1_EOF;
        label = "HLV";
        hlv1_decoder_destroy(decoder);
    } else if (!std::memcmp(signature, "BPV1", 4)) {
        BPV1Header header = {};
        int result = bpv1_header_read(file, &header);
        BPV1Decoder *decoder =
            result < 0 ? nullptr : bpv1_decoder_create(&header);
        if (!decoder) {
            fclose(file);
            return 1;
        }
        for (uint32_t index = 0; index < header.frame_count; ++index) {
            BPV1Packet packet = {};
            const BPV1Frame *frame = nullptr;
            result = bpv1_decoder_read_packet(decoder, file, &packet);
            if (result == BPV1_OK)
                result = bpv1_decoder_decode(decoder, &packet, &frame);
            if (result != BPV1_OK || !frame ||
                !convert_bpv_frame(&header, frame, converted)) {
                if (result == BPV1_OK) result = BPV1_ERR_DECODE;
                break;
            }
            const size_t packet_audio_bytes =
                bpv1_packet_audio_size(&packet);
            if (packet_audio_bytes &&
                !bpv1_packet_audio_data(&packet)) {
                result = BPV1_ERR_FORMAT;
                break;
            }
            audio_bytes += packet_audio_bytes;
            for (uint32_t pixel : converted.pixels) {
                checksum ^= pixel;
                checksum *= UINT64_C(1099511628211);
            }
            ++frames;
        }
        if (result == BPV1_OK) {
            BPV1Packet trailing = {};
            result = bpv1_decoder_read_packet(decoder, file, &trailing);
        }
        valid = result == BPV1_EOF && frames == header.frame_count;
        label = "BPV1";
        bpv1_decoder_destroy(decoder);
    } else if (!std::memcmp(signature + 4, "ftyp", 4) ||
               (!std::memcmp(signature, "RIFF", 4) &&
                !std::memcmp(signature + 8, "AVI ", 4))) {
        H2633gpInfo info = {};
        H2633gpDecoder *decoder = h263_3gp_decoder_create();
        int result =
            decoder
                ? h263_3gp_decoder_open(decoder, file, &info)
                : H263_3GP_ERR_MEMORY;
        label = !std::memcmp(signature, "RIFF", 4)
                    ? "AVI video"
                    : "H.263/3GP";
        if (result != H263_3GP_OK)
            failure_detail = h263_3gp_strerror(result);
        else
            label = info.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
                        ? "MPEG-4 SP/AVI"
                        : (info.container == H263_CONTAINER_AVI
                               ? "H.263/AVI"
                               : "H.263/3GP");
        while (result == H263_3GP_OK) {
            H2633gpFrame frame = {};
            result = h263_3gp_decoder_decode_next(
                decoder, file, &frame);
            if (result != H263_3GP_OK) break;
            convert_h263_frame(&frame, converted);
            for (uint32_t pixel : converted.pixels) {
                checksum ^= pixel;
                checksum *= UINT64_C(1099511628211);
            }
            ++frames;
        }
        valid = result == H263_3GP_EOF &&
                frames == info.frame_count;
        if (result != H263_3GP_EOF &&
            info.video_codec != H263_VIDEO_CODEC_UNKNOWN) {
            failure_detail = h263_3gp_codec_strerror(
                info.video_codec, result);
        }
        if (info.container == H263_CONTAINER_AVI &&
            info.audio_sample_rate) {
            FILE *audio_file = nullptr;
            if (_wfopen_s(&audio_file, path, L"rb") != 0 ||
                !audio_file) {
                valid = false;
            } else {
                H2633gpInfo audio_info = {};
                H263AviPcmReader *audio_reader =
                    h263_avi_pcm_reader_create();
                int audio_result =
                    audio_reader
                        ? h263_avi_pcm_reader_open(
                              audio_reader, audio_file, &audio_info)
                        : H263_3GP_ERR_MEMORY;
                while (audio_result == H263_3GP_OK) {
                    std::vector<uint8_t> audio_chunk;
                    size_t sample_count = 0;
                    audio_result = decode_h263_avi_audio_chunk(
                        audio_reader, audio_file, audio_info,
                        audio_chunk, sample_count);
                    if (audio_result != H263_3GP_OK) break;
                    audio_bytes += audio_chunk.size();
                }
                valid = valid && audio_result == H263_3GP_EOF;
                h263_avi_pcm_reader_destroy(audio_reader);
                fclose(audio_file);
            }
        } else if (info.container == H263_CONTAINER_3GP) {
            FILE *audio_file = nullptr;
            if (_wfopen_s(&audio_file, path, L"rb") != 0 ||
                !audio_file) {
                valid = false;
            } else {
                AmrNb3gpInfo audio_info = {};
                AmrNb3gpDecoder *audio_decoder =
                    amrnb_3gp_decoder_create();
                int audio_result =
                    audio_decoder
                        ? amrnb_3gp_decoder_open(
                              audio_decoder, audio_file, &audio_info)
                        : AMRNB_3GP_ERR_MEMORY;
                uint32_t audio_frames = 0;
                if (audio_result == AMRNB_3GP_OK) {
                    while (audio_result == AMRNB_3GP_OK) {
                        AmrNb3gpFrame audio_frame = {};
                        audio_result = amrnb_3gp_decoder_decode_next(
                            audio_decoder, audio_file, &audio_frame);
                        if (audio_result != AMRNB_3GP_OK) break;
                        audio_bytes +=
                            static_cast<uint64_t>(
                                audio_frame.sample_count) *
                            sizeof(int16_t);
                        ++audio_frames;
                    }
                    valid = valid &&
                            audio_result == AMRNB_3GP_EOF &&
                            audio_frames == audio_info.frame_count;
                } else if (
                    audio_result != AMRNB_3GP_ERR_UNSUPPORTED) {
                    valid = false;
                }
                amrnb_3gp_decoder_destroy(audio_decoder);
                fclose(audio_file);
            }
        }
        if (info.container == H263_CONTAINER_AVI) {
            label =
                info.video_codec == H263_VIDEO_CODEC_MPEG4_SIMPLE
                    ? "MPEG-4 SP/AVI"
                    : "H.263/AVI";
        }
        h263_3gp_decoder_destroy(decoder);
    } else if (signature[0] == 0x00 && signature[1] == 0x00 &&
               signature[2] == 0x01 && signature[3] == 0xba) {
        plm_t *mpeg = plm_create_with_file(file, FALSE);
        const int width = mpeg ? plm_get_width(mpeg) : 0;
        const int height = mpeg ? plm_get_height(mpeg) : 0;
        const double fps = mpeg ? plm_get_framerate(mpeg) : 0.0;
        const int sample_rate =
            mpeg && plm_get_num_audio_streams(mpeg) > 0
                ? plm_get_samplerate(mpeg)
                : 0;
        uint64_t decoded_audio_samples = 0;
        if (mpeg && width > 0 && height > 0 && fps > 0.0) {
            for (;;) {
                plm_frame_t *frame = plm_decode_video(mpeg);
                if (!frame) break;
                convert_mpeg_frame(frame, converted);
                for (uint32_t pixel : converted.pixels) {
                    checksum ^= pixel;
                    checksum *= UINT64_C(1099511628211);
                }
                ++frames;
                if (sample_rate > 0) {
                    const uint64_t target = static_cast<uint64_t>(
                        std::floor(frames * sample_rate / fps));
                    while (decoded_audio_samples < target) {
                        plm_samples_t *samples = plm_decode_audio(mpeg);
                        if (!samples) break;
                        decoded_audio_samples += samples->count;
                    }
                }
            }
            audio_bytes = decoded_audio_samples;
            valid = frames > 0 && plm_has_ended(mpeg);
        }
        label = "MPEG-1";
        if (mpeg) plm_destroy(mpeg);
    }
    fclose(file);
    if (!valid || !label) {
        char error[160] = {};
        const int length = std::snprintf(
            error, sizeof error,
            "%s check failed after %llu frames and %llu audio bytes: %s\r\n",
            label ? label : "Unknown format",
            static_cast<unsigned long long>(frames),
            static_cast<unsigned long long>(audio_bytes),
            failure_detail ? failure_detail : "invalid stream");
        HANDLE stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
        if (length > 0 && stderr_handle &&
            stderr_handle != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(stderr_handle, error,
                      static_cast<DWORD>(length), &written, nullptr);
        }
        return 1;
    }

    char output[256] = {};
    const int length = std::snprintf(
        output, sizeof output,
        "%s check OK: %llu frames, %llu audio bytes, checksum %016llx\r\n",
        label,
        static_cast<unsigned long long>(frames),
        static_cast<unsigned long long>(audio_bytes),
        static_cast<unsigned long long>(checksum));
    HANDLE stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (length > 0 && stdout_handle && stdout_handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(stdout_handle, output, static_cast<DWORD>(length), &written, nullptr);
    }
    return 0;
}

int dump_h263_nv12(const wchar_t *input_path, const wchar_t *output_path) {
    FILE *input = nullptr;
    FILE *output = nullptr;
    if (_wfopen_s(&input, input_path, L"rb") != 0 || !input) return 1;
    if (_wfopen_s(&output, output_path, L"wb") != 0 || !output) {
        fclose(input);
        return 1;
    }

    H2633gpInfo info = {};
    H2633gpDecoder *decoder = h263_3gp_decoder_create();
    int result =
        decoder
            ? h263_3gp_decoder_open(decoder, input, &info)
            : H263_3GP_ERR_MEMORY;
    uint64_t frames = 0;
    VideoFrame converted;
    while (result == H263_3GP_OK) {
        H2633gpFrame frame = {};
        result = h263_3gp_decoder_decode_next(decoder, input, &frame);
        if (result != H263_3GP_OK) break;
        convert_h263_frame(&frame, converted);
        if (converted.nv12.empty() ||
            std::fwrite(converted.nv12.data(), 1, converted.nv12.size(),
                        output) != converted.nv12.size()) {
            result = H263_3GP_ERR_IO;
            break;
        }
        ++frames;
    }

    const bool valid =
        result == H263_3GP_EOF && frames == info.frame_count &&
        std::fflush(output) == 0 && std::ferror(output) == 0;
    h263_3gp_decoder_destroy(decoder);
    fclose(output);
    fclose(input);
    return valid ? 0 : 1;
}

int analyze_mpeg4(const wchar_t *input_path) {
    FILE *input = nullptr;
    if (_wfopen_s(&input, input_path, L"rb") != 0 || !input) return 1;

    H2633gpInfo info = {};
    H2633gpDecoder *decoder = h263_3gp_decoder_create();
    int result =
        decoder
            ? h263_3gp_decoder_open(decoder, input, &info)
            : H263_3GP_ERR_MEMORY;
    if (result == H263_3GP_OK &&
        info.video_codec != H263_VIDEO_CODEC_MPEG4_SIMPLE) {
        result = H263_3GP_ERR_UNSUPPORTED;
    }
    h263_3gp_decoder_decode_profile_reset(decoder);
    uint64_t frames = 0;
    while (result == H263_3GP_OK) {
        H2633gpFrame frame = {};
        result = h263_3gp_decoder_decode_next(decoder, input, &frame);
        if (result == H263_3GP_OK) ++frames;
    }

    const H263DecodeProfile *profile =
        h263_3gp_decoder_decode_profile(decoder);
    const bool valid =
        result == H263_3GP_EOF && frames == info.frame_count && profile;
    char output[1024] = {};
    int length = 0;
    if (valid) {
        length = std::snprintf(
            output, sizeof output,
            "{\"frames\":%u,\"i_frames\":%u,\"p_frames\":%u,"
            "\"macroblocks\":%u,\"skipped_macroblocks\":%u,"
            "\"intra_macroblocks\":%u,\"inter_macroblocks\":%u,"
            "\"cbp_zero_macroblocks\":%u,\"coded_blocks\":%u,"
            "\"dc_only_blocks\":%u,\"sparse_blocks\":%u,"
            "\"dense_blocks\":%u,\"one_row_blocks\":%u,"
            "\"one_column_blocks\":%u,\"two_column_blocks\":%u,"
            "\"integer_predictions\":%u,\"horizontal_predictions\":%u,"
            "\"vertical_predictions\":%u,\"diagonal_predictions\":%u,"
            "\"edge_predictions\":%u}\r\n",
            profile->frames, profile->i_frames, profile->p_frames,
            profile->macroblocks, profile->skipped_macroblocks,
            profile->intra_macroblocks, profile->inter_macroblocks,
            profile->cbp_zero_macroblocks, profile->coded_blocks,
            profile->dc_only_blocks, profile->sparse_blocks,
            profile->dense_blocks, profile->one_row_blocks,
            profile->one_column_blocks, profile->two_column_blocks,
            profile->compact_integer_predictions,
            profile->compact_horizontal_predictions,
            profile->compact_vertical_predictions,
            profile->compact_diagonal_predictions,
            profile->compact_edge_predictions);
    } else {
        length = std::snprintf(
            output, sizeof output,
            "MPEG-4 analysis failed after %llu frames: %s\r\n",
            static_cast<unsigned long long>(frames),
            profile
                ? h263_3gp_codec_strerror(
                      H263_VIDEO_CODEC_MPEG4_SIMPLE, result)
                : "player was built without H.263 stage profiling");
    }
    HANDLE handle = GetStdHandle(
        valid ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
    if (length > 0 && handle && handle != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(handle, output, static_cast<DWORD>(length), &written, nullptr);
    }
    h263_3gp_decoder_destroy(decoder);
    fclose(input);
    return valid ? 0 : 1;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    int argument_count = 0;
    wchar_t **arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments && argument_count == 3 &&
        std::wcscmp(arguments[1], L"--check") == 0) {
        const int result = check_file(arguments[2]);
        LocalFree(arguments);
        return result;
    }
    if (arguments && argument_count == 3 &&
        std::wcscmp(arguments[1], L"--analyze-mpeg4") == 0) {
        const int result = analyze_mpeg4(arguments[2]);
        LocalFree(arguments);
        return result;
    }
    if (arguments && argument_count == 4 &&
        std::wcscmp(arguments[1], L"--dump-h263-nv12") == 0) {
        const int result = dump_h263_nv12(arguments[2], arguments[3]);
        LocalFree(arguments);
        return result;
    }

    timeBeginPeriod(1);
    INITCOMMONCONTROLSEX common_controls = {};
    common_controls.dwSize = sizeof common_controls;
    common_controls.dwICC = ICC_BAR_CLASSES;
    if (!InitCommonControlsEx(&common_controls)) {
        if (arguments) LocalFree(arguments);
        timeEndPeriod(1);
        return 1;
    }
    WNDCLASSEXW video_window_class = {};
    video_window_class.cbSize = sizeof video_window_class;
    video_window_class.style = CS_HREDRAW | CS_VREDRAW;
    video_window_class.lpfnWndProc = video_window_proc;
    video_window_class.hInstance = instance;
    video_window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    video_window_class.hbrBackground =
        static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    video_window_class.lpszClassName = kVideoWindowClass;
    if (!RegisterClassExW(&video_window_class)) {
        if (arguments) LocalFree(arguments);
        timeEndPeriod(1);
        return 1;
    }
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof window_class;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc = window_proc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    window_class.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&window_class)) {
        if (arguments) LocalFree(arguments);
        timeEndPeriod(1);
        return 1;
    }

    RECT window_rect = {0, 0, 640, 480};
    AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, TRUE);
    HWND window = CreateWindowExW(
        0, kWindowClass, L"HLV/BPV Player",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        window_rect.right - window_rect.left,
        window_rect.bottom - window_rect.top,
        nullptr, create_main_menu(), instance, nullptr);
    if (!window) {
        if (arguments) LocalFree(arguments);
        timeEndPeriod(1);
        return 1;
    }

    Player player(window);
    g_player = &player;
    DragAcceptFiles(window, TRUE);
    ShowWindow(window, show_command);
    UpdateWindow(window);

    if (arguments && argument_count >= 2) player.open(arguments[1]);
    LocalFree(arguments);

    ACCEL accelerator_entries[] = {
        {FVIRTKEY | FCONTROL, 'O', ID_FILE_OPEN},
        {FVIRTKEY, VK_SPACE, ID_PLAY_PAUSE},
        {FVIRTKEY, 'F', ID_VIEW_FIT},
        {FVIRTKEY, VK_ESCAPE, ID_FILE_EXIT}
    };
    HACCEL accelerators = CreateAcceleratorTableW(
        accelerator_entries,
        static_cast<int>(std::size(accelerator_entries)));
    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (accelerators &&
            TranslateAcceleratorW(window, accelerators, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (accelerators) DestroyAcceleratorTable(accelerators);
    g_player = nullptr;
    timeEndPeriod(1);
    return static_cast<int>(message.wParam);
}
