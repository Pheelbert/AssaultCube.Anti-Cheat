#ifdef WIN32

#include "InputTracker.h"
#include <iostream>

namespace PhantiCheat {

    // ---------------------------------------------------------------------------
    // InputCounters
    // ---------------------------------------------------------------------------

    void InputCounters::reset()
    {
        rawKeyboardEvents.store(0);
        rawMouseEvents.store(0);
        injectedKeyEvents.store(0);
        injectedMouseEvents.store(0);
        sdlKeyEvents.store(0);
        sdlMouseEvents.store(0);
        keyStateTransitions.store(0);
        mouseButtonTransitions.store(0);
    }

    // ---------------------------------------------------------------------------
    // InputDebugConfig
    // ---------------------------------------------------------------------------

    void InputDebugConfig::reset()
    {
        active.store(false);
        simulatedSdlKeys.store(0);
        simulatedSdlMouse.store(0);
        simulatedRawKeys.store(0);
        simulatedRawMouse.store(0);
        simulatedKeyStateKeys.store(0);
        simulatedInjectedKeys.store(0);
        simulatedInjectedMouse.store(0);
        forceAnomalyFlags.store(0);
    }

    void InputDebugConfig::apply(const AC_INPUT_DEBUG_CONFIG &cfg)
    {
        simulatedSdlKeys.store(cfg.SimulatedSdlKeys);
        simulatedSdlMouse.store(cfg.SimulatedSdlMouse);
        simulatedRawKeys.store(cfg.SimulatedRawKeys);
        simulatedRawMouse.store(cfg.SimulatedRawMouse);
        simulatedKeyStateKeys.store(cfg.SimulatedKeyStateKeys);
        simulatedInjectedKeys.store(cfg.SimulatedInjectedKeys);
        simulatedInjectedMouse.store(cfg.SimulatedInjectedMouse);
        forceAnomalyFlags.store(cfg.ForceAnomalyFlags);
        active.store(cfg.Active != 0);
    }

    // ---------------------------------------------------------------------------
    // Static singleton pointer for hook callbacks
    // ---------------------------------------------------------------------------

    InputTracker *InputTracker::s_instance = nullptr;

    // ---------------------------------------------------------------------------
    // Construction / destruction
    // ---------------------------------------------------------------------------

    InputTracker::InputTracker()
        : m_hwnd(NULL)
        , m_keyboardHook(NULL)
        , m_mouseHook(NULL)
        , m_active(false)
    {
        m_counters.reset();
        m_debugConfig.reset();
        memset(m_prevKeyState, 0, sizeof(m_prevKeyState));
        memset(m_prevMouseState, 0, sizeof(m_prevMouseState));
    }

    InputTracker::~InputTracker()
    {
        shutdown();
    }

    // ---------------------------------------------------------------------------
    // initialize - register Raw Input devices and install low-level hooks
    // ---------------------------------------------------------------------------

    bool InputTracker::initialize(HWND hwnd)
    {
        if (m_active.load())
            return true;

        if (!hwnd)
        {
            std::cerr << "[PhantiCheat:Input] NULL HWND, cannot initialize input tracking." << std::endl;
            return false;
        }

        m_hwnd = hwnd;
        s_instance = this;

        // --- Register for Raw Input (keyboard + mouse) ---
        // RIDEV_INPUTSINK lets us receive WM_INPUT even when not in foreground,
        // which is important for detecting background injectors.
        RAWINPUTDEVICE rid[2];

        // Keyboard: usage page 0x01, usage 0x06
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage     = 0x06;
        rid[0].dwFlags     = RIDEV_INPUTSINK;
        rid[0].hwndTarget  = m_hwnd;

        // Mouse: usage page 0x01, usage 0x02
        rid[1].usUsagePage = 0x01;
        rid[1].usUsage     = 0x02;
        rid[1].dwFlags     = RIDEV_INPUTSINK;
        rid[1].hwndTarget  = m_hwnd;

        if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
        {
            DWORD err = GetLastError();
            std::cerr << "[PhantiCheat:Input] RegisterRawInputDevices failed (error "
                      << err << ")." << std::endl;
            return false;
        }

        std::cout << "[PhantiCheat:Input] Raw Input registered for keyboard + mouse." << std::endl;

        // --- Install low-level hooks ---
        // These run in the context of the thread that installed them and require
        // a message pump (which SDL provides).

        m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, llKeyboardProc, NULL, 0);
        if (!m_keyboardHook)
        {
            std::cerr << "[PhantiCheat:Input] WH_KEYBOARD_LL hook failed (error "
                      << GetLastError() << ")." << std::endl;
            // Non-fatal: we still have Raw Input
        }

        m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, llMouseProc, NULL, 0);
        if (!m_mouseHook)
        {
            std::cerr << "[PhantiCheat:Input] WH_MOUSE_LL hook failed (error "
                      << GetLastError() << ")." << std::endl;
        }

        m_counters.reset();
        m_active.store(true);

        std::cout << "[PhantiCheat:Input] Input tracking active (hooks: kb="
                  << (m_keyboardHook ? "yes" : "no")
                  << " mouse=" << (m_mouseHook ? "yes" : "no") << ")." << std::endl;
        return true;
    }

    // ---------------------------------------------------------------------------
    // shutdown
    // ---------------------------------------------------------------------------

    void InputTracker::shutdown()
    {
        if (!m_active.load())
            return;

        m_active.store(false);

        if (m_keyboardHook)
        {
            UnhookWindowsHookEx(m_keyboardHook);
            m_keyboardHook = NULL;
        }

        if (m_mouseHook)
        {
            UnhookWindowsHookEx(m_mouseHook);
            m_mouseHook = NULL;
        }

        // Unregister Raw Input
        RAWINPUTDEVICE rid[2];
        rid[0].usUsagePage = 0x01;
        rid[0].usUsage     = 0x06;
        rid[0].dwFlags     = RIDEV_REMOVE;
        rid[0].hwndTarget  = NULL;

        rid[1].usUsagePage = 0x01;
        rid[1].usUsage     = 0x02;
        rid[1].dwFlags     = RIDEV_REMOVE;
        rid[1].hwndTarget  = NULL;

        RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE));

        if (s_instance == this)
            s_instance = nullptr;

        std::cout << "[PhantiCheat:Input] Input tracking shut down." << std::endl;
    }

    // ---------------------------------------------------------------------------
    // SDL event recording (called from main.cpp event loop)
    // ---------------------------------------------------------------------------

    void InputTracker::recordSdlKeyEvent()
    {
        m_counters.sdlKeyEvents.fetch_add(1, std::memory_order_relaxed);
    }

    void InputTracker::recordSdlMouseEvent()
    {
        m_counters.sdlMouseEvents.fetch_add(1, std::memory_order_relaxed);
    }

    // ---------------------------------------------------------------------------
    // processRawInput - handle WM_INPUT messages
    // ---------------------------------------------------------------------------

    void InputTracker::processRawInput(LPARAM lParam)
    {
        UINT size = 0;
        GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
        if (size == 0)
            return;

        // Stack-allocate for typical sizes, heap for large
        BYTE stackBuf[128];
        BYTE *buf = (size <= sizeof(stackBuf)) ? stackBuf : new BYTE[size];

        if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size)
        {
            if (buf != stackBuf) delete[] buf;
            return;
        }

        RAWINPUT *raw = (RAWINPUT *)buf;

        if (raw->header.dwType == RIM_TYPEKEYBOARD)
        {
            m_counters.rawKeyboardEvents.fetch_add(1, std::memory_order_relaxed);
        }
        else if (raw->header.dwType == RIM_TYPEMOUSE)
        {
            m_counters.rawMouseEvents.fetch_add(1, std::memory_order_relaxed);
        }

        if (buf != stackBuf) delete[] buf;
    }

    // ---------------------------------------------------------------------------
    // pollKeyState - sample the kernel key-state table via GetAsyncKeyState
    // ---------------------------------------------------------------------------
    //
    // GetAsyncKeyState reads the asynchronous key state from win32k.sys, which
    // reflects the actual hardware interrupt-driven key state table maintained
    // by the kernel input subsystem (via the RIT - Raw Input Thread).
    //
    // This is a fundamentally different data path than:
    //   - Raw Input (WM_INPUT) which delivers queued HID reports
    //   - Low-level hooks which intercept the input message pipeline
    //   - SDL events which are posted to the application message queue
    //
    // A sophisticated injector that hooks at the HID minidriver or filter-driver
    // level might produce events visible in Raw Input but NOT reflected in the
    // kernel key-state table (since the RIT updates state from the real hardware
    // interrupt path).  Conversely, a simple SendInput call updates both the
    // message queue AND the async key state.
    //
    // By tracking transitions here we get a ground-truth reference from the
    // closest-to-kernel layer accessible from user mode.

    void InputTracker::pollKeyState()
    {
        if (!m_active.load())
            return;

        // Poll all virtual key codes (skip 0 which is undefined)
        for (int vk = 1; vk < 256; vk++)
        {
            // Skip mouse button VKs - handled separately below
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON ||
                vk == VK_XBUTTON1 || vk == VK_XBUTTON2)
                continue;

            SHORT state = GetAsyncKeyState(vk);
            bool isDown = (state & 0x8000) != 0;

            if (isDown != m_prevKeyState[vk])
            {
                m_counters.keyStateTransitions.fetch_add(1, std::memory_order_relaxed);
                m_prevKeyState[vk] = isDown;
            }
        }

        // Poll mouse buttons separately
        static const int mouseVks[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON,
                                         VK_XBUTTON1, VK_XBUTTON2 };
        for (int i = 0; i < 5; i++)
        {
            int vk = mouseVks[i];
            SHORT state = GetAsyncKeyState(vk);
            bool isDown = (state & 0x8000) != 0;

            if (isDown != m_prevMouseState[vk])
            {
                m_counters.mouseButtonTransitions.fetch_add(1, std::memory_order_relaxed);
                m_prevMouseState[vk] = isDown;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Debug config
    // ---------------------------------------------------------------------------

    void InputTracker::setDebugConfig(const AC_INPUT_DEBUG_CONFIG &cfg)
    {
        m_debugConfig.apply(cfg);
        std::cout << "[PhantiCheat:Input] Debug simulation "
                  << (cfg.Active ? "ENABLED" : "DISABLED")
                  << " (sdlK+" << cfg.SimulatedSdlKeys
                  << " sdlM+" << cfg.SimulatedSdlMouse
                  << " rawK+" << cfg.SimulatedRawKeys
                  << " rawM+" << cfg.SimulatedRawMouse
                  << " ksK+" << cfg.SimulatedKeyStateKeys
                  << " injK+" << cfg.SimulatedInjectedKeys
                  << " injM+" << cfg.SimulatedInjectedMouse
                  << " flags=0x" << std::hex << cfg.ForceAnomalyFlags << std::dec
                  << ")" << std::endl;
    }

    void InputTracker::clearDebugConfig()
    {
        m_debugConfig.reset();
        std::cout << "[PhantiCheat:Input] Debug simulation cleared." << std::endl;
    }

    // ---------------------------------------------------------------------------
    // snapshot - harvest counters, apply debug offsets, cross-compare layers
    // ---------------------------------------------------------------------------

    bool InputTracker::snapshot(AC_INPUT_ANOMALY_DATA *out)
    {
        if (!out || !m_active.load())
            return false;

        // Poll kernel key state one final time before snapshotting
        pollKeyState();

        // Atomically swap counters to zero
        uint32_t rawKb   = m_counters.rawKeyboardEvents.exchange(0);
        uint32_t rawMs   = m_counters.rawMouseEvents.exchange(0);
        uint32_t injKb   = m_counters.injectedKeyEvents.exchange(0);
        uint32_t injMs   = m_counters.injectedMouseEvents.exchange(0);
        uint32_t sdlKb   = m_counters.sdlKeyEvents.exchange(0);
        uint32_t sdlMs   = m_counters.sdlMouseEvents.exchange(0);
        uint32_t ksKeys  = m_counters.keyStateTransitions.exchange(0);
        uint32_t ksMouse = m_counters.mouseButtonTransitions.exchange(0);

        // Apply debug simulation offsets if active
        bool debugActive = m_debugConfig.active.load();
        if (debugActive)
        {
            sdlKb  += m_debugConfig.simulatedSdlKeys.load();
            sdlMs  += m_debugConfig.simulatedSdlMouse.load();
            rawKb  += m_debugConfig.simulatedRawKeys.load();
            rawMs  += m_debugConfig.simulatedRawMouse.load();
            ksKeys += m_debugConfig.simulatedKeyStateKeys.load();
            injKb  += m_debugConfig.simulatedInjectedKeys.load();
            injMs  += m_debugConfig.simulatedInjectedMouse.load();
        }

        // --- Cross-layer anomaly detection ---
        uint32_t anomalyFlags = AC_ANOMALY_NONE;

        // Synthetic = SDL events that had no corresponding Raw Input source
        uint32_t synthKb = (sdlKb > rawKb) ? (sdlKb - rawKb) : 0;
        uint32_t synthMs = (sdlMs > rawMs) ? (sdlMs - rawMs) : 0;

        if (synthKb > 0 || synthMs > 0)
            anomalyFlags |= AC_ANOMALY_SDL_WITHOUT_RAW;

        // SDL key events with no kernel key-state backing:
        // If SDL saw many more key events than the kernel state table tracked,
        // something is injecting above the kernel input subsystem.
        if (sdlKb > 0 && ksKeys == 0)
            anomalyFlags |= AC_ANOMALY_SDL_WITHOUT_KEYSTATE;

        // Raw Input events with no kernel key-state backing:
        // Raw Input is delivered via HID but the actual kernel RIT state didn't
        // change -- could indicate a filter-driver level injection.
        if (rawKb > ksKeys + 2 && ksKeys > 0) // +2 tolerance for polling granularity
            anomalyFlags |= AC_ANOMALY_RAW_WITHOUT_KEYSTATE;

        // LL hook tagged injected events
        if (injKb > 0 || injMs > 0)
            anomalyFlags |= AC_ANOMALY_LLHOOK_INJECTED;

        // Kernel state changed but SDL saw nothing (possible input suppression)
        if (ksKeys > 0 && sdlKb == 0)
            anomalyFlags |= AC_ANOMALY_KEYSTATE_WITHOUT_SDL;

        // General cross-layer count mismatch: all 4 layers should roughly agree.
        // Large divergence (>50% relative difference) is suspicious.
        if (sdlKb > 0 && rawKb > 0 && ksKeys > 0)
        {
            uint32_t maxVal = sdlKb;
            if (rawKb > maxVal) maxVal = rawKb;
            if (ksKeys > maxVal) maxVal = ksKeys;
            uint32_t minVal = sdlKb;
            if (rawKb < minVal) minVal = rawKb;
            if (ksKeys < minVal) minVal = ksKeys;

            if (maxVal > minVal * 2)
                anomalyFlags |= AC_ANOMALY_LAYER_COUNT_MISMATCH;
        }

        // Apply forced debug flags
        if (debugActive)
        {
            anomalyFlags |= m_debugConfig.forceAnomalyFlags.load();
            anomalyFlags |= AC_ANOMALY_DEBUG_SIMULATED;
        }

        // Fill output
        out->TimestampMs            = GetTickCount();
        out->InjectedKeyCount       = injKb;
        out->InjectedMouseCount     = injMs;
        out->SyntheticKeyCount      = synthKb;
        out->SyntheticMouseCount    = synthMs;
        out->RawKeyboardEvents      = rawKb;
        out->RawMouseEvents         = rawMs;
        out->SdlKeyEvents           = sdlKb;
        out->SdlMouseEvents         = sdlMs;
        out->KeyStateTransitions    = ksKeys;
        out->MouseButtonTransitions = ksMouse;
        out->AnomalyFlags           = anomalyFlags;

        return (anomalyFlags != AC_ANOMALY_NONE);
    }

    // ---------------------------------------------------------------------------
    // Low-level keyboard hook callback
    // ---------------------------------------------------------------------------

    LRESULT CALLBACK InputTracker::llKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode == HC_ACTION && s_instance)
        {
            KBDLLHOOKSTRUCT *kb = (KBDLLHOOKSTRUCT *)lParam;
            if (kb->flags & LLKHF_INJECTED)
            {
                s_instance->m_counters.injectedKeyEvents.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

    // ---------------------------------------------------------------------------
    // Low-level mouse hook callback
    // ---------------------------------------------------------------------------

    LRESULT CALLBACK InputTracker::llMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
    {
        if (nCode == HC_ACTION && s_instance)
        {
            MSLLHOOKSTRUCT *ms = (MSLLHOOKSTRUCT *)lParam;
            if (ms->flags & LLMHF_INJECTED)
            {
                s_instance->m_counters.injectedMouseEvents.fetch_add(1, std::memory_order_relaxed);
            }
        }
        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }

} // namespace PhantiCheat

#endif // WIN32
