#include "common/stdafx.h"
#include "common/config.h"
#include "common/version.h"
#include "classic/main_window.h"

// foobar2000.exe "/winamp_skin:<path>" selects a classic skin from the command line (also forwarded to a running
// foobar2000). It loads at once when the UI runs, else it's remembered for the UI's next start.
// "/winamp_skin_clock:HH:MM[:SS[.mmm]]" is accepted and has no effect (classic skins show no clock; the test tools
// pass it for every skin). "/winamp_skin_vis:off" turns the running UI's visualizer off (the tracks state is captured
// that way, in the same foobar2000 as the start). Used by the test tools (unix/harness/fb2k_capture) with a clean profile; harmless otherwise.
namespace wsz {
namespace {

constexpr char prefix[] = "/winamp_skin:";
constexpr char clock_prefix[] = "/winamp_skin_clock:";
constexpr char vis_off_token[] = "/winamp_skin_vis:off";

// Set when a /winamp_skin: skin is loaded (or selected for the UI's start): the event
// "Local\\foo_winamp_skins_loaded_<pid>" that unix/harness/fb2k_capture waits for.
struct signal_loaded {
    ~signal_loaded() {
        const std::wstring name = L"Local\\foo_winamp_skins_loaded_" + std::to_wstring(GetCurrentProcessId());
        if (HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, name.c_str())) {
            SetEvent(ev);
            // kept open for the process's life, so the tool still finds it set
        }
    }
};

class skin_commandline : public commandline_handler {
public:
    result on_token(const char* token) override {
        if (_stricmp(token, vis_off_token) == 0) {
            if (MainWindow* w = MainWindow::instance()) w->set_vis_mode(VisMode::off);
            return RESULT_PROCESSED;
        }
        if (_strnicmp(token, clock_prefix, sizeof(clock_prefix) - 1) == 0) return RESULT_PROCESSED;
        const size_t n = sizeof(prefix) - 1;
        if (_strnicmp(token, prefix, n) != 0) return RESULT_NOT_OURS;
        const std::wstring arg = pfc::stringcvt::string_wide_from_utf8(token + n).get_ptr();
        wchar_t full[MAX_PATH * 4];
        const DWORD len = GetFullPathNameW(arg.c_str(), DWORD(std::size(full)), full, nullptr);
        const std::wstring path = len && len < std::size(full) ? std::wstring(full) : arg;
        const pfc::string8 path8 = pfc::stringcvt::string_utf8_from_wide(path.c_str()).get_ptr();
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            FB2K_console_formatter() << COMPONENT_NAME ": /winamp_skin: not found: " << path8;
            return RESULT_PROCESSED;
        }
        signal_loaded signal; // the test tools wait for it instead of a fixed time
        if (MainWindow* w = MainWindow::instance()) {
            w->load_skin(path8, false);
        } else {
            config::skin_path = path8;
            FB2K_console_formatter() << COMPONENT_NAME ": /winamp_skin: the classic UI isn't running; selected for its next start";
        }
        return RESULT_PROCESSED;
    }
};

FB2K_SERVICE_FACTORY(skin_commandline);

} // namespace
} // namespace wsz
