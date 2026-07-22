#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>

#include "hlv1.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "winmm.lib")

namespace {

constexpr wchar_t kWindowClass[] = L"HLV1WindowsPlayer";
constexpr UINT_PTR kPlaybackTimer = 1;
constexpr size_t kVideoLeadFrames = 3;
constexpr size_t kAudioBufferCount = 8;

enum : UINT {
    ID_FILE_OPEN = 1001,
    ID_FILE_EXIT,
    ID_PLAY_PAUSE,
    ID_VIEW_FIT
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
};

void convert_frame(const HLV1Frame *source, VideoFrame &destination) {
    destination.width = source->width;
    destination.height = source->height;
    destination.pixels.resize(static_cast<size_t>(source->width) * source->height);

    for (int y = 0; y < source->height; ++y) {
        const uint8_t *luma = source->y + y * source->stride_y;
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
}

class AudioOutput {
public:
    ~AudioOutput() { close(); }

    bool open(unsigned sample_rate, std::wstring &error) {
        close();
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = sample_rate;
        format.wBitsPerSample = 8;
        format.nBlockAlign = 1;
        format.nAvgBytesPerSec = sample_rate;

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

class Player {
public:
    explicit Player(HWND window) : window_(window) {
        QueryPerformanceFrequency(&clock_frequency_);
    }

    ~Player() { close(); }

    bool open(const std::wstring &path) {
        close();
        error_.clear();
        path_ = path;

        if (_wfopen_s(&file_, path.c_str(), L"rb") != 0 || !file_)
            return fail(L"Cannot open the selected file");

        int result = hlv1_header_read(file_, &header_);
        if (result < 0)
            return fail(L"Invalid HLV header: " + widen_ascii(hlv1_strerror(result)));
        if (header_.width > 8192 || header_.height > 8192)
            return fail(L"The video dimensions are too large for this player");

        decoder_ = hlv1_decoder_create(&header_);
        if (!decoder_) return fail(L"Cannot allocate the HLV decoder");

        if (header_.flags & HLV1_FLAG_AUDIO) {
            std::wstring audio_error;
            if (!audio_.open(header_.audio_sample_rate, audio_error)) {
                audio_warning_ = L"Audio is disabled: " + audio_error;
            }
        }

        while (ready_.size() < kVideoLeadFrames + 1 && !end_of_file_) {
            if (!decode_one()) return fail(error_);
        }
        if (ready_.empty()) return fail(L"The HLV file contains no video frames");

        present_front();
        QueryPerformanceCounter(&clock_start_);
        audio_.restart();
        loaded_ = true;
        finished_ = false;
        paused_ = false;
        if (!SetTimer(window_, kPlaybackTimer, 1, nullptr))
            return fail(L"Cannot create the playback timer");
        update_title();
        InvalidateRect(window_, nullptr, FALSE);

        if (!audio_warning_.empty()) {
            MessageBoxW(window_, audio_warning_.c_str(), L"HLV Player",
                        MB_OK | MB_ICONWARNING);
        }
        return true;
    }

    void close() {
        if (window_) KillTimer(window_, kPlaybackTimer);
        audio_.close();
        if (decoder_) {
            hlv1_decoder_destroy(decoder_);
            decoder_ = nullptr;
        }
        if (file_) {
            fclose(file_);
            file_ = nullptr;
        }
        ready_.clear();
        recycled_.clear();
        current_ = {};
        std::memset(&header_, 0, sizeof header_);
        decoded_frames_ = 0;
        presented_frames_ = 0;
        end_of_file_ = false;
        loaded_ = false;
        paused_ = false;
        finished_ = false;
        audio_warning_.clear();
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
        InvalidateRect(window_, nullptr, FALSE);
    }

    void paint(HDC dc, const RECT &client) const {
        HBRUSH black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        FillRect(dc, &client, black);

        if (current_.pixels.empty()) {
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(210, 210, 210));
            RECT text_rect = client;
            const wchar_t *message = error_.empty()
                ? L"Open an .hlv file (Ctrl+O) or drag it into this window"
                : error_.c_str();
            DrawTextW(dc, message, -1, &text_rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return;
        }

        const int client_width = client.right - client.left;
        const int client_height = client.bottom - client.top;
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

        BITMAPINFO bitmap = {};
        bitmap.bmiHeader.biSize = sizeof bitmap.bmiHeader;
        bitmap.bmiHeader.biWidth = current_.width;
        bitmap.bmiHeader.biHeight = -current_.height;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, draw_x, draw_y, draw_width, draw_height,
                      0, 0, current_.width, current_.height,
                      current_.pixels.data(), &bitmap, DIB_RGB_COLORS, SRCCOPY);
    }

    bool paused() const { return paused_; }
    bool fit_to_window() const { return fit_to_window_; }

private:
    bool fail(const std::wstring &message) {
        error_ = message;
        const std::wstring saved_error = error_;
        close();
        error_ = saved_error;
        update_title();
        InvalidateRect(window_, nullptr, FALSE);
        MessageBoxW(window_, error_.c_str(), L"HLV Player",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    bool decode_one() {
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

        VideoFrame output;
        if (!recycled_.empty()) {
            output = std::move(recycled_.back());
            recycled_.pop_back();
        }
        convert_frame(decoded, output);
        ready_.push_back(std::move(output));

        if (audio_.active()) {
            const size_t audio_size = hlv1_packet_audio_size(&packet);
            const uint8_t *audio_data = hlv1_packet_audio_data(&packet);
            std::wstring audio_error;
            if (!audio_.submit(audio_data, audio_size, audio_error)) {
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
        InvalidateRect(window_, nullptr, FALSE);
    }

    void playback_failed() {
        KillTimer(window_, kPlaybackTimer);
        audio_.close();
        finished_ = true;
        update_title();
        MessageBoxW(window_, error_.c_str(), L"HLV Player",
                    MB_OK | MB_ICONERROR);
    }

    void finish() {
        KillTimer(window_, kPlaybackTimer);
        finished_ = true;
        update_title();
    }

    void update_title() const {
        std::wstring title = L"HLV Player";
        if (!path_.empty()) title += L" - " + file_name(path_);
        if (paused_) title += L" [paused]";
        else if (finished_) title += L" [finished]";
        SetWindowTextW(window_, title.c_str());
    }

    HWND window_ = nullptr;
    FILE *file_ = nullptr;
    HLV1Decoder *decoder_ = nullptr;
    HLV1Header header_ = {};
    AudioOutput audio_;
    std::deque<VideoFrame> ready_;
    std::vector<VideoFrame> recycled_;
    VideoFrame current_;
    std::wstring path_;
    std::wstring error_;
    std::wstring audio_warning_;
    LARGE_INTEGER clock_frequency_ = {};
    LARGE_INTEGER clock_start_ = {};
    LARGE_INTEGER pause_started_ = {};
    uint64_t decoded_frames_ = 0;
    uint64_t presented_frames_ = 0;
    bool loaded_ = false;
    bool paused_ = false;
    bool finished_ = false;
    bool end_of_file_ = false;
    bool fit_to_window_ = true;
};

Player *g_player = nullptr;

std::wstring choose_file(HWND owner) {
    std::vector<wchar_t> path(32768);
    OPENFILENAMEW dialog = {};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = L"HLV video (*.hlv)\0*.hlv\0All files (*.*)\0*.*\0\0";
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
        RECT client = {};
        GetClientRect(window, &client);
        g_player->paint(dc, client);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(window, nullptr, FALSE);
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
    HLV1Header header = {};
    int result = hlv1_header_read(file, &header);
    if (result < 0) {
        fclose(file);
        return 1;
    }
    HLV1Decoder *decoder = hlv1_decoder_create(&header);
    if (!decoder) {
        fclose(file);
        return 1;
    }

    uint64_t frames = 0;
    uint64_t audio_bytes = 0;
    uint64_t checksum = UINT64_C(14695981039346656037);
    VideoFrame converted;
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

    hlv1_decoder_destroy(decoder);
    fclose(file);
    if (result != HLV1_EOF) return 1;

    char output[256] = {};
    const int length = std::snprintf(
        output, sizeof output,
        "HLV check OK: %llu frames, %llu audio bytes, checksum %016llx\r\n",
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

    timeBeginPeriod(1);
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
        0, kWindowClass, L"HLV Player", WS_OVERLAPPEDWINDOW,
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

    MSG message = {};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    g_player = nullptr;
    timeEndPeriod(1);
    return static_cast<int>(message.wParam);
}
