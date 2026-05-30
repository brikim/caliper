#pragma once

#include "ffmpeg_runner.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
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

   int segmentCount = 4;
   float segmentDuration = 60.0f;

   // SVT-AV1 specific
   int svtTune = 0; // 0=VQ, 1=PSNR, 2=SSIM
};

// Owns pre-extracted lossless reference segments for a job.
// Shared across all CRF search iterations of the same jobId via shared_ptr.
// Files are automatically cleaned up when the last iteration is destroyed.
struct SegmentCache
{
   std::vector<std::filesystem::path> refSegFiles;

   ~SegmentCache()
   {
      std::error_code ec;
      for (const auto& f : refSegFiles)
         std::filesystem::remove(f, ec);
   }
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

struct CrfTestResult
{
   int crf = -1;
   float vmaf = 0.0f;
};

struct BenchmarkJob
{
   int jobId = 0;
   std::string id;
   EncodeProfile profile;
   std::filesystem::path outputFile;
   std::unique_ptr<FFmpegRunner> runner;
   bool isComplete = false;
   bool isCanceled = false;
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
   float estimatedFullSize = 0.0f;    // in MB
   float segmentDuration = 60.0f;

   // Shared segment cache (pre-extracted lossless reference segments)
   std::shared_ptr<SegmentCache> segmentCache;
   bool segmentsReady = false;

   // Source state capture
   std::filesystem::path sourceFile;
   VideoMetadata sourceMeta;

   // Search History
   std::vector<CrfTestResult> testHistory;
};

class JobManager
{
public:
   JobManager();
   ~JobManager();

   // Call this once per frame in your main loop, independent of UI drawing
   void Update();

   // Enqueue a new benchmark task
   void AddJob(const std::filesystem::path& sourceFile, const VideoMetadata& sourceMeta,
               const EncodeProfile& profile, int segmentCount,
               float segmentDuration);

   // Control and cleanup
   void StopAllJobs();
   void ClearCompletedJobs();
   void RemoveJob(int jobId);
   void CancelJob(int jobId);
   void PauseJob(int jobId);
   void ResumeJob(int jobId);

   // Accessors for the UI
   const std::vector<std::shared_ptr<BenchmarkJob>>& GetJobs() const
   {
      return m_jobs;
   }

private:
   std::filesystem::path GenerateTempFileName(const std::shared_ptr<BenchmarkJob>& job, std::string_view header, bool includeSegment);
   void ProcessInit(std::shared_ptr<BenchmarkJob>& job);
   void ProcessExtractingSegment(std::shared_ptr<BenchmarkJob>& job);
   void ProcessEncodingSegment(std::shared_ptr<BenchmarkJob>& job);
   void ProcessVmaffingSegment(std::shared_ptr<BenchmarkJob>& job);
   void ProcessJob(std::shared_ptr<BenchmarkJob>& job);

   void UpdateJobMetrics(std::shared_ptr<BenchmarkJob>& job);
   void UpdateCrfBounds(std::shared_ptr<BenchmarkJob>& job, float diff);
   int CalculateNextCrf(const std::shared_ptr<BenchmarkJob>& job, float target, float diff);
   void FinalizeSearch(std::shared_ptr<BenchmarkJob>& job);
   void SpawnNextIteration(std::shared_ptr<BenchmarkJob>& job, int nextCrf);
   void HandleEarlyExit(std::shared_ptr<BenchmarkJob>& job);
   void ProcessCheckScore(std::shared_ptr<BenchmarkJob>& job);

   std::filesystem::path m_tempDir = "temp";

   std::vector<std::shared_ptr<BenchmarkJob>> m_jobs;
   std::vector<std::shared_ptr<BenchmarkJob>> m_pendingJobs;

   int m_maxConcurrentJobs = 1;
   int m_nextJobId = 1;
   bool m_sleepPrevented = false;
};