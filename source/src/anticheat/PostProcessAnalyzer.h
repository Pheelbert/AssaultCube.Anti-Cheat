#ifndef POSTPROCESSANALYZER_H
#define POSTPROCESSANALYZER_H

//
// PostProcessAnalyzer.h - Server-side "second pass" anomaly detection.
//
// Correlates game events (shots, hits, kills) with input telemetry snapshots
// over time windows to detect anomalies that single-layer checks cannot catch.
// For example: a player fires bullets but has zero input events, or lands
// impossibly accurate shots with no mouse movement.
//
// This runs entirely on the server and depends only on data already relayed
// via the game protocol (game events + SV_ANTICHEAT_TELEMETRY).
//

#include "ac_telemetry_types.h"
#include <stdint.h>

// Post-process anomaly flags (bitfield, reported per evaluation window)
#define PP_ANOMALY_NONE                     0x00000000
#define PP_ANOMALY_SHOTS_WITHOUT_INPUT      0x00000001  // shots fired but zero input events received
#define PP_ANOMALY_HITS_WITHOUT_MOUSE       0x00000002  // hits registered but no mouse activity
#define PP_ANOMALY_PERFECT_ACCURACY         0x00000004  // statistically implausible hit ratio in window
#define PP_ANOMALY_ACTION_WITHOUT_KEYSTATE  0x00000008  // movement/reload actions with no key transitions
#define PP_ANOMALY_INPUT_TELEMETRY_GAP      0x00000010  // expected telemetry snapshots never arrived
#define PP_ANOMALY_KERNEL_INPUT_INJECT     0x00000020  // kernel driver detected software-injected IRPs
#define PP_ANOMALY_KERNEL_DEVSTACK_TAMPER  0x00000040  // unknown filter drivers in keyboard/mouse stack
#define PP_ANOMALY_KERNEL_TIMING_INHUMAN   0x00000080  // sub-millisecond inter-keystroke timing at I/O level

// Severity levels for flagged anomalies
enum PPSeverity {
    PP_SEV_INFO = 0,    // informational, log only
    PP_SEV_SUSPECT,     // suspicious, warrants monitoring
    PP_SEV_HIGH         // strong signal, likely cheat
};

// Accumulated game-action counters for one evaluation window.
struct PPGameActivity {
    int shots;          // GE_SHOT events
    int hits;           // GE_HIT events (hits landed on others)
    int reloads;        // GE_RELOAD events
    int akimbos;        // GE_AKIMBO events
    int suicides;       // GE_SUICIDE events
    int frags;          // kills scored
    int deaths;         // deaths suffered

    void reset()
    {
        shots = hits = reloads = akimbos = suicides = frags = deaths = 0;
    }
};

// Accumulated input-telemetry counters for one evaluation window.
// Populated from AcInputAnomalyData snapshots received via SV_ANTICHEAT_TELEMETRY.
struct PPInputActivity {
    uint32_t totalSdlKeyEvents;
    uint32_t totalSdlMouseEvents;
    uint32_t totalRawKeyboardEvents;
    uint32_t totalRawMouseEvents;
    uint32_t totalKeyStateTransitions;
    uint32_t totalMouseButtonTransitions;
    uint32_t anomalyFlagsUnion;     // OR of all AnomalyFlags seen in window
    int      snapshotsReceived;     // how many telemetry snapshots arrived

    // Layer 0: kernel driver IRP-level input data
    uint32_t totalKernelHardwareKeyIrps;
    uint32_t totalKernelSoftwareKeyIrps;
    uint32_t totalKernelHardwareMouseIrps;
    uint32_t totalKernelSoftwareMouseIrps;
    uint32_t kernelMinInterKeystrokeUs;  // minimum across all snapshots in window
    uint32_t kernelMinInterMouseUs;
    uint32_t kernelAnomalyFlagsUnion;    // OR of all kernel anomaly flags
    int      kernelSnapshotsReceived;

    void reset()
    {
        totalSdlKeyEvents = totalSdlMouseEvents = 0;
        totalRawKeyboardEvents = totalRawMouseEvents = 0;
        totalKeyStateTransitions = totalMouseButtonTransitions = 0;
        anomalyFlagsUnion = 0;
        snapshotsReceived = 0;
        totalKernelHardwareKeyIrps = totalKernelSoftwareKeyIrps = 0;
        totalKernelHardwareMouseIrps = totalKernelSoftwareMouseIrps = 0;
        kernelMinInterKeystrokeUs = 0xFFFFFFFF;
        kernelMinInterMouseUs = 0xFFFFFFFF;
        kernelAnomalyFlagsUnion = 0;
        kernelSnapshotsReceived = 0;
    }
};

// Result produced by a single rule evaluation.
struct PPRuleResult {
    uint32_t    anomalyFlag;    // which PP_ANOMALY_* this triggers
    PPSeverity  severity;
    const char *description;    // static string, no allocation needed
};

// One complete evaluation result for a client window.
struct PPEvalResult {
    uint32_t anomalyFlags;              // OR of all triggered PP_ANOMALY_* flags
    PPSeverity maxSeverity;             // highest severity across all triggered rules
    PPRuleResult rules[8];              // individual rule results (fixed capacity)
    int ruleCount;                      // how many rules fired

    void reset()
    {
        anomalyFlags = PP_ANOMALY_NONE;
        maxSeverity = PP_SEV_INFO;
        ruleCount = 0;
    }

    void addRule(uint32_t flag, PPSeverity sev, const char *desc)
    {
        anomalyFlags |= flag;
        if(sev > maxSeverity) maxSeverity = sev;
        if(ruleCount < 8)
        {
            PPRuleResult &r = rules[ruleCount++];
            r.anomalyFlag = flag;
            r.severity = sev;
            r.description = desc;
        }
    }
};

// Per-client post-process state, embedded in the server's client struct.
struct PostProcessState {
    PPGameActivity  gameActivity;
    PPInputActivity inputActivity;
    int             windowStartMillis;  // servmillis when current window began
    int             evalCount;          // how many windows have been evaluated
    uint32_t        lifetimeFlags;      // OR of all anomaly flags ever seen for this client

    void reset()
    {
        gameActivity.reset();
        inputActivity.reset();
        windowStartMillis = 0;
        evalCount = 0;
        lifetimeFlags = 0;
    }

    void startWindow(int millis)
    {
        gameActivity.reset();
        inputActivity.reset();
        windowStartMillis = millis;
    }
};

// The evaluation window length in milliseconds (10 seconds).
#define PP_WINDOW_MILLIS 10000

// Minimum number of shots in a window before accuracy rules apply.
#define PP_MIN_SHOTS_FOR_ACCURACY 5

namespace PostProcess {

    // Record a game event into the client's current window.
    void recordGameEvent(PostProcessState &pps, int eventType);

    // Record a frag (kill) for the attacker.
    void recordFrag(PostProcessState &pps);

    // Record a death for the victim.
    void recordDeath(PostProcessState &pps);

    // Record a hit landed by the attacker.
    void recordHit(PostProcessState &pps);

    // Feed an input-telemetry snapshot into the client's current window.
    void recordInputTelemetry(PostProcessState &pps, const AcInputAnomalyData *data);

    // Feed kernel-level input telemetry into the client's current window.
    void recordKernelInputTelemetry(PostProcessState &pps, const AcKernelInputData *data);

    // Evaluate all rules for the current window. Returns true if any anomaly was detected.
    // The caller should check result.anomalyFlags and result.maxSeverity.
    bool evaluate(const PostProcessState &pps, PPEvalResult &result);

} // namespace PostProcess

#endif // POSTPROCESSANALYZER_H
