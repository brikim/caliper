#pragma once

#include "ffmpeg_runner.h"
#include <string>
#include <vector>
#include <memory>

struct EncodeProfile {
    std::string name = "Custom Profile";
    std::string codec = "libx264";
    std::string preset = "medium";
    int startCrf = 23;
    bool autoCrf = true;
    int targetVmaf = 96;
    std::string extraArgs = "";
    std::string bitDepth = "8-bit";
    bool isDefault = false;
};

enum class JobState {
    INIT,
    EXTRACTING_SEGMENT,
    ENCODING_SEGMENT,
    VMAFFING_SEGMENT,
    CHECKING_SCORE,
    DONE
};

struct BenchmarkJob {
    std::string id;
    EncodeProfile profile;
    std::string outputFile;
    std::unique_ptr<FFmpegRunner> runner;
    bool isComplete = false;
    float finalVMAF = -1.0f;
    float avgSpeed = 0.0f;
    float avgBitrate = 0.0f; // kbps

    // CRF Search State
    bool searchActive = false;
    int currentCrf = 23;
    int crfMin = 0;
    int crfMax = 51;
    int iteration = 0;
    bool isRecommended = false;

    // Segment State Machine
    JobState state = JobState::INIT;
    bool commandStarted = false;
    int currentSegmentIdx = 0;
    std::vector<float> segmentStartTimes;
    std::vector<float> segmentVMAFs;
    std::vector<float> segmentBitrates;
    std::vector<int64_t> segmentSizes; // in bytes
    float estimatedFullSize = 0.0f; // in MB
    float segmentDuration = 60.0f;
    std::string refSegFile; // lossless reference segment for current segment idx
};

class UIManager {
public:
    UIManager();
    ~UIManager();

    void Draw();

    void LoadProfiles();
    void SaveProfiles();

private:
    void UpdateJob(std::shared_ptr<BenchmarkJob>& job);
    void DrawInputSection();
    void DrawProfileManager();
    void DrawJobQueue();
    void DrawResults();

    void StartBenchmark();
    std::string OpenFileDialog();

    // State
    std::string m_referenceVideo;
    VideoMetadata m_referenceMeta;

    std::vector<EncodeProfile> m_profiles;
    std::vector<std::shared_ptr<BenchmarkJob>> m_jobs;
    std::vector<std::shared_ptr<BenchmarkJob>> m_pendingJobs;

    bool m_sleepPrevented = false;
    int m_activeProfileIdx = 0; // The currently selected/active profile

    // Segment Settings
    int m_segmentCount = 3;
    float m_segmentDuration = 60.0f;
};
