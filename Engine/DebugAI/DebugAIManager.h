#pragma once

#include "DebugAIConfig.h"
#include "Protocol/DebugProtocol.h"
#include "Protocol/IGenericGameDebugAdapter.h"
#include "Transport/IDebugAITransport.h"
#include "DebugLogger.h"
#include "DebugAnomalyDetector.h"
#include "DebugGenericActionReplay.h"
#include "DebugInputReplay.h"
#include "DebugObservationEventRecorder.h"
#include "DebugReplayPlayer.h"
#include "DebugReplayRecorder.h"
#include "IDebugBot.h"
#include "IGameDebugAdapter.h"
#include "RandomDebugBot.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct DebugReplayObservationCheckpoint {
    std::uint64_t recordedFrame = 0;
    DebugObservation observation;
};

class DebugAIManager {
public:
    void Initialize(const std::string& logDirectory = "generated/debug_ai");
    void Initialize(const DebugAIConfig& config);
    void Shutdown();

    void SetConfig(const DebugAIConfig& config);
    const DebugAIConfig& Config() const { return config_; }

    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }
    bool IsWaitingForAction() const { return waitingForAction_; }
    void SetLoadingDetails(std::string status, std::vector<DebugAILoadingSourceFile> sourceFiles);
    const std::string& LoadingStatus() const { return loadingStatus_; }
    const std::vector<DebugAILoadingSourceFile>& LoadingSourceFiles() const { return loadingSourceFiles_; }

    void SetAdapter(IGameDebugAdapter* adapter) { adapter_ = adapter; }
    void SetGenericAdapter(IGenericGameDebugAdapter* adapter) { genericAdapter_ = adapter; }
    void SetBot(IDebugBot* bot);
    void ResetBotToRandom();
    const char* CurrentBotName() const { return bot_ ? bot_->Name() : "None"; }
    void InjectAction();
    void ProcessAfterUpdate(float dt);
    void ProcessControlCommands();
    void PrepareSimulationFrame();
    unsigned int ReplaySimulationUpdatesForHostFrame();
    void SetControlTransport(std::unique_ptr<IDebugAITransport> transport);
    void RecordExternalAction(
        const DebugGameState& stateBefore,
        const DebugAction& action,
        const DebugGameState& stateAfter);
    void CheckReplayDrift(const DebugGameState& actualState);

    bool StartLatestReplay();
    bool StartReplay(const std::string& replayPath);
    bool RestoreReplayInitialState();
    void StopReplay();
    bool StartReplaySessionRecording(std::string* outMessage = nullptr);
    bool StopReplaySessionRecording(std::string* outMessage = nullptr);
    bool IsReplaySessionRecording() const { return replaySessionRecording_; }
    bool HasPendingReplay() const { return !pendingReplayManifestPath_.empty(); }
    bool ConsumeSceneLoadRequest(std::string& outSceneId);
    // Backward-compatible name retained for existing hosts.
    bool ConsumeReplaySceneLoadRequest(std::string& outSceneId);
    bool StartPendingReplay(std::string* outMessage = nullptr);
    bool IsReplayPlaying() const {
        return (replayMode_ && replayPlayer_.IsPlaying()) ||
            inputReplay_.IsPlaying() || genericActionReplay_.IsPlaying() ||
            replayTimelineActionIndex_ < replayTimelineActions_.size() ||
            (replayValidationActive_ &&
                replayCheckpointIndex_ < replayCheckpoints_.size());
    }
    bool IsFirstReplayFrame() const { return isFirstReplayFrame_; }
    bool IsReplayPlaybackPaused() const {
        return IsReplayPlaying() && replayPlaybackPaused_;
    }
    double ReplayPlaybackSpeed() const { return replayPlaybackSpeed_; }

    const DebugLogger& Logger() const { return logger_; }
    const DebugReplayRecorder& ReplayRecorder() const { return replayRecorder_; }
    const DebugReplayRecorder& PlayerReplayRecorder() const { return playerReplayRecorder_; }
    const DebugReplayPlayer& ReplayPlayer() const { return replayPlayer_; }
    const DebugInputReplay& InputReplay() const { return inputReplay_; }
    const DebugGenericActionReplay& GenericActionReplay() const { return genericActionReplay_; }
    const DebugAction& LastAction() const { return lastAction_; }
    bool ShouldLogEvents() const { return config_.logActionResults || replayMode_; }
    void LogEvent(const DebugGameState& state, const std::string& eventName, const std::string& message);

private:
    DebugProtocolMessage ExecuteControlCommand_(const DebugProtocolMessage& request);
    bool ShouldWaitForAction_() const;
    bool RestoreReplayInitialState_();
    void DetectIssues_(const DebugGameState& state, float dt);
    void AddIssue_(DebugIssueSeverity severity, const DebugGameState& state, const std::string& message);
    bool IsFinite_(const Vector3& value) const;
    bool IsOutsideBounds_(const Vector3& value, const DebugMapBounds& bounds) const;
    bool IsSameState_(const DebugGameState& state) const;
    bool ExecuteGenericAction_(
        DebugGenericAction action,
        const std::string& source,
        std::uint64_t frame,
        bool record);
    void ProcessGenericReplayFrame_(std::uint64_t frame);
    bool HasReplayCheckpointDue_(std::uint64_t frame) const;
    void ValidateReplayObservation_(
        const DebugObservation& actual,
        std::uint64_t replayFrame);
    void ResetReplayValidation_();
    void RecordActorStateChanges_(const DebugObservation& observation);
    void EvaluateAnomalies_(const DebugObservation& observation);

private:
    bool enabled_ = false;
    DebugAIConfig config_;
    IGameDebugAdapter* adapter_ = nullptr;
    IGenericGameDebugAdapter* genericAdapter_ = nullptr;
    RandomDebugBot randomBot_;
    IDebugBot* bot_ = &randomBot_;
    DebugLogger logger_;
    DebugReplayRecorder replayRecorder_;
    DebugReplayRecorder playerReplayRecorder_;
    DebugReplayPlayer replayPlayer_;
    DebugInputReplay inputReplay_;
    DebugGenericActionReplay genericActionReplay_;
    DebugObservationEventRecorder eventRecorder_;
    DebugAnomalyDetector anomalyDetector_;
    std::unique_ptr<IDebugAITransport> controlTransport_;
    std::uint64_t controlSequence_ = 0;
    DebugAction lastAction_;
    bool replayMode_ = false;
    bool isFirstReplayFrame_ = false;

    DebugGameState pendingBeforeState_;
    DebugAction pendingAction_;
    bool hasPendingAction_ = false;
    bool waitingForAction_ = false;
    DebugAction heldAction_;
    unsigned int heldActionFramesRemaining_ = 0;
    struct HeldExternalAction {
        DebugGenericAction action;
        unsigned int framesRemaining = 0;
    };
    std::unordered_map<std::string, HeldExternalAction> heldExternalActions_;
    std::unordered_map<std::string, std::string> lastRecordedActorStates_;
    std::string replaySessionId_;
    std::string replayManifestPath_;
    std::string replayInitialObservationPath_;
    std::uint64_t replayRecordingStartFrame_ = 0;
    std::string replayRecordingSceneId_;
    std::string replayRecordingPhase_;
    bool replaySessionRecording_ = false;
    bool replayInitialStateRestored_ = false;
    std::string replayRestoreWarning_;
    std::string pendingReplayManifestPath_;
    std::string pendingReplaySceneId_;
    bool replaySceneLoadRequested_ = false;
    std::string pendingSceneLoadId_;
    bool sceneLoadRequested_ = false;
    std::vector<DebugGenericReplayEvent> replayTimelineActions_;
    std::size_t replayTimelineActionIndex_ = 0;
    std::vector<DebugReplayObservationCheckpoint> replayCheckpoints_;
    std::size_t replayCheckpointIndex_ = 0;
    std::size_t replayCheckpointMismatchCount_ = 0;
    std::uint64_t replayFirstMismatchFrame_ = 0;
    std::string replayFirstMismatch_;
    std::string replayLastMismatch_;
    bool replayValidationAvailable_ = false;
    bool replayValidationActive_ = false;
    bool replayValidationInterrupted_ = false;
    std::uint64_t replayTimelineOriginFrame_ = 0;
    std::uint64_t replayTimelineStartFrame_ = 0;
    std::uint64_t genericReplayClockFrame_ = 0;
    bool replayPlaybackPaused_ = false;
    unsigned int replayStepFramesPending_ = 0;
    double replayPlaybackSpeed_ = 1.0;
    double replayPlaybackAccumulator_ = 0.0;
    unsigned int idleAfterUpdateFrames_ = 0;
    unsigned int frameLogSampleFrames_ = 0;

    std::string lastStableStateKey_;
    std::string lastProgressKey_;
    float sameStateSeconds_ = 0.0f;
    float noProgressSeconds_ = 0.0f;
    float lowFpsSeconds_ = 0.0f;
    std::string loadingStatus_ = "Waiting for AI response.";
    std::vector<DebugAILoadingSourceFile> loadingSourceFiles_;

    std::unordered_map<std::string, unsigned long long> lastIssueFrameByMessage_;
};
