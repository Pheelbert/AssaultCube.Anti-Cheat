#ifndef INPUTTRACKER_H
#define INPUTTRACKER_H

#ifdef WIN32

#include <windows.h>
#include <mutex>
#include <atomic>
#include "ac_shared.h"

namespace PhantiCheat {

    //
    // InputTracker - Monitors four layers of the Windows input stack to detect
    // simulated/injected input by cross-comparing data from each layer.
    //
    // Layer 1 (kernel):  GetAsyncKeyState    - polls win32k.sys key state table
    //                                          (closest to kernel, updated by RIT)
    // Layer 2 (HID):     Raw Input (WM_INPUT) - hardware-level HID device reports
    // Layer 3 (hooks):   WH_KEYBOARD_LL /     - OS-level hooks that tag injected
    //                    WH_MOUSE_LL            events with LLKHF/LLMHF_INJECTED
    // Layer 4 (app):     SDL event counts     - application-level events the game sees
    //
    // Different injection techniques produce different layer signatures:
    //   SendInput/keybd_event: visible in layers 2-4, but NOT in layer 1
    //   Filter-driver inject:  visible in layer 2 (Raw Input) but NOT in layer 1
    //   Message-level inject:  visible in layer 4 (SDL) but NOT in layers 1-2
    //   Input suppression:     visible in layer 1 (kernel) but NOT in layer 4
    //
    // Lifecycle:
    //   - Call initialize() after the game window is created (needs an HWND).
    //   - Call recordSdlKeyEvent() / recordSdlMouseEvent() from the SDL event loop.
    //   - Call pollKeyState() each frame to sample the kernel key-state table.
    //   - Call snapshot() periodically to harvest counters and produce telemetry.
    //   - Call shutdown() before exit.
    //
    // Debug simulation:
    //   Use setDebugConfig() to inject artificial counter offsets that simulate
    //   what different cheat injection methods look like across the 4 layers.
    //   The "acdebug N" console command provides preset scenarios (0=off, 1-4).
    //   The "acdebugcustom" command allows fine-grained counter control.
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

        // Kernel key-state polling (GetAsyncKeyState)
        std::atomic<uint32_t> keyStateTransitions;     // key down->up or up->down
        std::atomic<uint32_t> mouseButtonTransitions;  // mouse button state changes

        void reset();
    };

    // Debug configuration for simulating input anomalies.
    // When active, artificial counter offsets are injected at snapshot time
    // to simulate what different cheat injection methods would look like.
    struct InputDebugConfig {
        std::atomic<bool>     active;
        std::atomic<uint32_t> simulatedSdlKeys;
        std::atomic<uint32_t> simulatedSdlMouse;
        std::atomic<uint32_t> simulatedRawKeys;
        std::atomic<uint32_t> simulatedRawMouse;
        std::atomic<uint32_t> simulatedKeyStateKeys;
        std::atomic<uint32_t> simulatedInjectedKeys;
        std::atomic<uint32_t> simulatedInjectedMouse;
        std::atomic<uint32_t> forceAnomalyFlags;

        void reset();
        void apply(const AC_INPUT_DEBUG_CONFIG &cfg);
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

        // Poll the kernel key-state table via GetAsyncKeyState for all keys
        // and mouse buttons.  Call once per frame or per snapshot interval.
        void pollKeyState();

        bool isActive() const { return m_active.load(); }

        // Debug simulation: configure artificial anomaly injection.
        void setDebugConfig(const AC_INPUT_DEBUG_CONFIG &cfg);
        void clearDebugConfig();
        bool isDebugActive() const { return m_debugConfig.active.load(); }

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
        InputDebugConfig m_debugConfig;

        // Previous key states for GetAsyncKeyState transition detection.
        // Index 0..255 = virtual key codes, tracks whether each key was down.
        bool m_prevKeyState[256];
        // Mouse buttons: VK_LBUTTON(1), VK_RBUTTON(2), VK_MBUTTON(4),
        //                VK_XBUTTON1(5), VK_XBUTTON2(6)
        bool m_prevMouseState[8];

        InputTracker(const InputTracker&) = delete;
        InputTracker& operator=(const InputTracker&) = delete;
    };

} // namespace PhantiCheat

#endif // WIN32
#endif // INPUTTRACKER_H
