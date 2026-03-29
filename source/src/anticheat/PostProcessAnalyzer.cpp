//
// PostProcessAnalyzer.cpp - Server-side "second pass" anomaly detection.
//
// See PostProcessAnalyzer.h for architecture overview.
//

// This file is #included from server.cpp (like serverevents.h), so it has
// access to the server's types, macros, and logging facilities directly.
// It is NOT compiled as a standalone translation unit.

#include "anticheat/PostProcessAnalyzer.h"

namespace PostProcess {

// ---------------------------------------------------------------------------
// Event recording
// ---------------------------------------------------------------------------

void recordGameEvent(PostProcessState &pps, int eventType)
{
    switch(eventType)
    {
        case GE_SHOT:    pps.gameActivity.shots++;    break;
        case GE_RELOAD:  pps.gameActivity.reloads++;  break;
        case GE_AKIMBO:  pps.gameActivity.akimbos++;  break;
        case GE_SUICIDE: pps.gameActivity.suicides++; break;
        default: break;
    }
}

void recordFrag(PostProcessState &pps)
{
    pps.gameActivity.frags++;
}

void recordDeath(PostProcessState &pps)
{
    pps.gameActivity.deaths++;
}

void recordHit(PostProcessState &pps)
{
    pps.gameActivity.hits++;
}

void recordInputTelemetry(PostProcessState &pps, const AcInputAnomalyData *data)
{
    pps.inputActivity.totalSdlKeyEvents       += data->SdlKeyEvents;
    pps.inputActivity.totalSdlMouseEvents      += data->SdlMouseEvents;
    pps.inputActivity.totalRawKeyboardEvents   += data->RawKeyboardEvents;
    pps.inputActivity.totalRawMouseEvents      += data->RawMouseEvents;
    pps.inputActivity.totalKeyStateTransitions += data->KeyStateTransitions;
    pps.inputActivity.totalMouseButtonTransitions += data->MouseButtonTransitions;
    pps.inputActivity.anomalyFlagsUnion        |= data->AnomalyFlags;
    pps.inputActivity.snapshotsReceived++;
}

// ---------------------------------------------------------------------------
// Individual rules
// ---------------------------------------------------------------------------

// Rule: player fired shots but had absolutely zero input events.
// This catches aimbots/triggerbots that inject shots without producing any
// real keyboard/mouse activity.
static void ruleShotsWithoutInput(const PostProcessState &pps, PPEvalResult &result)
{
    if(pps.gameActivity.shots == 0) return;

    // Need at least one telemetry snapshot to make a judgement.
    if(pps.inputActivity.snapshotsReceived == 0)
    {
        // No telemetry at all but shots were fired - flag as gap.
        result.addRule(PP_ANOMALY_INPUT_TELEMETRY_GAP, PP_SEV_SUSPECT,
                       "shots fired but no input telemetry snapshots received in window");
        return;
    }

    uint32_t totalInput = pps.inputActivity.totalSdlKeyEvents
                        + pps.inputActivity.totalSdlMouseEvents
                        + pps.inputActivity.totalRawKeyboardEvents
                        + pps.inputActivity.totalRawMouseEvents;

    if(totalInput == 0)
    {
        result.addRule(PP_ANOMALY_SHOTS_WITHOUT_INPUT, PP_SEV_HIGH,
                       "shots fired with zero input events across all layers");
    }
}

// Rule: hits registered but no mouse activity at all.
// Aiming requires mouse movement; zero mouse events with hits is suspicious.
static void ruleHitsWithoutMouse(const PostProcessState &pps, PPEvalResult &result)
{
    if(pps.gameActivity.hits == 0) return;
    if(pps.inputActivity.snapshotsReceived == 0) return; // covered by gap rule

    uint32_t mouseActivity = pps.inputActivity.totalSdlMouseEvents
                           + pps.inputActivity.totalRawMouseEvents
                           + pps.inputActivity.totalMouseButtonTransitions;

    if(mouseActivity == 0)
    {
        result.addRule(PP_ANOMALY_HITS_WITHOUT_MOUSE, PP_SEV_HIGH,
                       "hits registered with zero mouse activity");
    }
}

// Rule: statistically implausible accuracy within the window.
static void rulePerfectAccuracy(const PostProcessState &pps, PPEvalResult &result)
{
    if(pps.gameActivity.shots < PP_MIN_SHOTS_FOR_ACCURACY) return;
    if(pps.gameActivity.hits <= 0) return;

    // 100% hit rate over a meaningful number of shots is highly suspicious.
    if(pps.gameActivity.hits >= pps.gameActivity.shots)
    {
        result.addRule(PP_ANOMALY_PERFECT_ACCURACY, PP_SEV_SUSPECT,
                       "100% hit accuracy over evaluation window");
    }
}

// Rule: movement/reload actions occurred but no key-state transitions seen.
static void ruleActionWithoutKeystate(const PostProcessState &pps, PPEvalResult &result)
{
    int keyActions = pps.gameActivity.reloads + pps.gameActivity.akimbos;
    if(keyActions == 0) return;
    if(pps.inputActivity.snapshotsReceived == 0) return; // covered by gap rule

    if(pps.inputActivity.totalKeyStateTransitions == 0 &&
       pps.inputActivity.totalSdlKeyEvents == 0)
    {
        result.addRule(PP_ANOMALY_ACTION_WITHOUT_KEYSTATE, PP_SEV_SUSPECT,
                       "reload/akimbo actions with zero keyboard activity");
    }
}

// ---------------------------------------------------------------------------
// Evaluation entry point
// ---------------------------------------------------------------------------

bool evaluate(const PostProcessState &pps, PPEvalResult &result)
{
    result.reset();

    ruleShotsWithoutInput(pps, result);
    ruleHitsWithoutMouse(pps, result);
    rulePerfectAccuracy(pps, result);
    ruleActionWithoutKeystate(pps, result);

    return result.anomalyFlags != PP_ANOMALY_NONE;
}

} // namespace PostProcess
