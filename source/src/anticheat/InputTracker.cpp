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
    // snapshot - harvest counters and produce telemetry
    // ---------------------------------------------------------------------------

    bool InputTracker::snapshot(AC_INPUT_ANOMALY_DATA *out)
    {
        if (!out || !m_active.load())
            return false;

        // Atomically swap counters to zero
        uint32_t rawKb   = m_counters.rawKeyboardEvents.exchange(0);
        uint32_t rawMs   = m_counters.rawMouseEvents.exchange(0);
        uint32_t injKb   = m_counters.injectedKeyEvents.exchange(0);
        uint32_t injMs   = m_counters.injectedMouseEvents.exchange(0);
        uint32_t sdlKb   = m_counters.sdlKeyEvents.exchange(0);
        uint32_t sdlMs   = m_counters.sdlMouseEvents.exchange(0);

        // Synthetic = SDL events that had no corresponding Raw Input source.
        // If SDL saw more events than Raw Input produced, the excess was injected
        // at a level above the HID driver (SendInput, keybd_event, etc.).
        uint32_t synthKb = (sdlKb > rawKb) ? (sdlKb - rawKb) : 0;
        uint32_t synthMs = (sdlMs > rawMs) ? (sdlMs - rawMs) : 0;

        out->TimestampMs        = GetTickCount();
        out->InjectedKeyCount   = injKb;
        out->InjectedMouseCount = injMs;
        out->SyntheticKeyCount  = synthKb;
        out->SyntheticMouseCount = synthMs;
        out->RawKeyboardEvents  = rawKb;
        out->RawMouseEvents     = rawMs;
        out->SdlKeyEvents       = sdlKb;
        out->SdlMouseEvents     = sdlMs;

        // Only report if something suspicious was seen
        return (injKb > 0 || injMs > 0 || synthKb > 0 || synthMs > 0);
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
