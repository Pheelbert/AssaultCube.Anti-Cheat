#ifndef INPUTTRACKER_H
#define INPUTTRACKER_H

#ifdef WIN32

#include <windows.h>
#include <mutex>
#include <atomic>
#include "ac_shared.h"

namespace PhantiCheat {

    //
    // InputTracker - Monitors Windows input paths to detect simulated/injected input.
    //
    // Hooks three layers of the Windows input stack:
    //   1. Raw Input API  (WM_INPUT)        - hardware-level events from HID devices
    //   2. Low-level hooks (WH_KEYBOARD_LL, - OS-level hooks that tag injected events
    //                       WH_MOUSE_LL)      with LLKHF_INJECTED / LLMHF_INJECTED
    //   3. SDL event counts                 - application-level events the game sees
    //
    // By comparing counts across these layers you can identify input that was
    // synthesized via SendInput, keybd_event, mouse_event, or other injection.
    //
    // Lifecycle:
    //   - Call initialize() after the game window is created (needs an HWND for Raw Input).
    //   - Call recordSdlKeyEvent() / recordSdlMouseEvent() from the SDL event loop.
    //   - Call snapshot() periodically to harvest counters and produce telemetry.
    //   - Call shutdown() before exit.
    //

    // Per-window counters that get atomically accumulated between snapshots.
    struct InputCounters {
        // Raw Input (hardware-sourced)
        std::atomic<uint32_t> rawKeyboardEvents;
        std::atomic<uint32_t> rawMouseEvents;

        // Low-level hook detections
        std::atomic<uint32_t> injectedKeyEvents;      // LLKHF_INJECTED set
        std::atomic<uint32_t> injectedMouseEvents;     // LLMHF_INJECTED set

        // SDL layer (what the game actually consumed)
        std::atomic<uint32_t> sdlKeyEvents;
        std::atomic<uint32_t> sdlMouseEvents;

        void reset();
    };

    class InputTracker {
    public:
        InputTracker();
        ~InputTracker();

        // Set up Raw Input registration and install low-level hooks.
        // hwnd: the game's main window handle (needed for Raw Input).
        // Returns false if setup fails (game continues without input tracking).
        bool initialize(HWND hwnd);

        // Remove hooks and unregister Raw Input. Safe to call multiple times.
        void shutdown();

        // Call from the SDL event loop for every SDL_KEYDOWN / SDL_KEYUP.
        void recordSdlKeyEvent();

        // Call from the SDL event loop for every SDL_MOUSEMOTION,
        // SDL_MOUSEBUTTONDOWN, SDL_MOUSEBUTTONUP, SDL_MOUSEWHEEL.
        void recordSdlMouseEvent();

        // Take a snapshot of accumulated counters, reset them, and produce
        // an AC_INPUT_ANOMALY_DATA payload.  Returns true if there is anything
        // worth reporting (i.e. at least one injected/synthetic event detected).
        bool snapshot(AC_INPUT_ANOMALY_DATA *out);

        // Process a WM_INPUT message. Call from the window procedure or
        // SDL event filter.  lParam is the HRAWINPUT handle.
        void processRawInput(LPARAM lParam);

        bool isActive() const { return m_active.load(); }

    private:
        // Low-level hook callbacks (static because SetWindowsHookEx requires it)
        static LRESULT CALLBACK llKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
        static LRESULT CALLBACK llMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

        // The singleton instance pointer so static callbacks can reach counters
        static InputTracker *s_instance;

        HWND m_hwnd;
        HHOOK m_keyboardHook;
        HHOOK m_mouseHook;
        std::atomic<bool> m_active;

        InputCounters m_counters;

        InputTracker(const InputTracker&) = delete;
        InputTracker& operator=(const InputTracker&) = delete;
    };

} // namespace PhantiCheat

#endif // WIN32
#endif // INPUTTRACKER_H
