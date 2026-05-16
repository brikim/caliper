#pragma once

#include "ffmpeg_runner.h"

#include <memory>
#include <string>
#include <vector>

inline constexpr int DEFAULT_CRF = 20;

struct EncodeProfile
{
   std::string name = "Custom Profile";
   std::string codec = "libx264";
   std::string preset = "medium";
   int startCrf = DEFAULT_CRF;
   bool autoCrf = true;
   int targetVmaf = 96;
   std::string extraArgs = "";
   std::string bitDepth = "8-bit";
   bool isDefault = false;

   // SVT-AV1 specific
   bool filmGrainDenoise = false;
   int filmGrain = 10;
   int svtTune = 0; // 0=VQ, 1=PSNR, 2=SSIM
};

enum class JobState
{
   INIT,
   EXTRACTING_SEGMENT,
   ENCODING_SEGMENT,
   VMAFFING_SEGMENT,
   CHECKING_SCORE,
   DONE
};

struct BenchmarkJob
{
   int jobId = 0;
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
   int currentCrf = DEFAULT_CRF;
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

   // Source state capture
   std::string sourceFile;
   VideoMetadata sourceMeta;

   // Search History
   std::vector<int> testedCrfs;
};

class JobManager
{
public:
   JobManager();
   ~JobManager();

   // Call this once per frame in your main loop, independent of UI drawing
   void Update();

   // Enqueue a new benchmark task
   void AddJob(const std::string& sourceFile, const VideoMetadata& sourceMeta, const EncodeProfile& profile, int segmentCount, float segmentDuration);

   // Control and cleanup
   void StopAllJobs();
   void ClearCompletedJobs();

   // Accessors for the UI
   const std::vector<std::shared_ptr<BenchmarkJob>>& GetJobs() const
   {
      return m_jobs;
   }
   void SetMaxConcurrentJobs(int count)
   {
      m_maxConcurrentJobs = count;
   }

private:
   void ProcessJob(std::shared_ptr<BenchmarkJob>& job);

   std::vector<std::shared_ptr<BenchmarkJob>> m_jobs;
   std::vector<std::shared_ptr<BenchmarkJob>> m_pendingJobs;

   int m_maxConcurrentJobs = 1;
   int m_nextJobId = 1;
   bool m_sleepPrevented = false;
};