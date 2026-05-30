#include "job_manager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <numeric>
#include <ranges>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

JobManager::JobManager()
{
   std::error_code ec;
   if (std::filesystem::exists(m_tempDir, ec))
   {
      std::filesystem::remove_all(m_tempDir, ec);
   }

   std::filesystem::create_directories(m_tempDir, ec);
}

JobManager::~JobManager()
{
   StopAllJobs();

   // Release all shared_ptrs so SegmentCache destructors fire and clean temp files
   m_jobs.clear();
   m_pendingJobs.clear();

#ifdef _WIN32
   if (m_sleepPrevented)
   {
      SetThreadExecutionState(ES_CONTINUOUS);
   }
#endif

   // If the temp directory exists, remove it and all its contents.
   // Any temp files from this session should be cleaned up by now, but this is a final safeguard.
   std::error_code ec;
   if (std::filesystem::exists(m_tempDir, ec))
   {
      std::filesystem::remove_all(m_tempDir, ec);
   }
}

void JobManager::Update()
{
   // 1. Append any newly spawned iteration jobs and sort BEFORE processing
   if (!m_pendingJobs.empty())
   {
      m_jobs.insert(m_jobs.end(), m_pendingJobs.begin(), m_pendingJobs.end());
      m_pendingJobs.clear();

      // Sort only once after the insertion
      std::stable_sort(m_jobs.begin(), m_jobs.end(), [](const auto& a, const auto& b) {
         if (a->jobId != b->jobId)
            return a->jobId < b->jobId;
         return a->iteration < b->iteration;
      });
   }

   // 2. Count already running jobs to strictly enforce concurrency limits
   int activeCount = 0;
   for (const auto& job : m_jobs)
   {
      if (job->state != JobState::INIT && !job->isComplete)
      {
         activeCount++;
      }
   }

   bool anyRunning = false;

   // 3. Process jobs
   for (auto& job : m_jobs)
   {
      if (job->isComplete)
         continue;

      bool isRunning = (job->state != JobState::INIT);
      bool canStart = (activeCount < m_maxConcurrentJobs);

      if (isRunning || canStart)
      {
         bool wasQueued = (job->state == JobState::INIT);
         ProcessJob(job);

         if (wasQueued && !job->isComplete)
         {
            activeCount++;
         }
      }

      if (!job->isComplete)
         anyRunning = true;
   }

   if (!m_pendingJobs.empty())
      anyRunning = true;

   // Handle OS Sleep Prevention
#ifdef _WIN32
   if (anyRunning && !m_sleepPrevented)
   {
      SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED);
      m_sleepPrevented = true;
   }
   else if (!anyRunning && m_sleepPrevented)
   {
      SetThreadExecutionState(ES_CONTINUOUS);
      m_sleepPrevented = false;
   }
#endif
}

void JobManager::AddJob(const std::filesystem::path& sourceFile,
                        const VideoMetadata& sourceMeta,
                        const EncodeProfile& profile, int segmentCount,
                        float segmentDuration)
{
   auto job = std::make_shared<BenchmarkJob>();
   job->jobId = m_nextJobId++;
   job->id = profile.name;
   job->profile = profile;
   job->searchActive = true;
   job->currentCrf = profile.startCrf;
   job->crfMin = 0;
   job->crfMax = (profile.codec == "libsvtav1") ? 63 : 51;
   job->iteration = 0;
   job->sourceFile = sourceFile;
   job->sourceMeta = sourceMeta;
   job->segmentDuration = segmentDuration;

   // Calculate segment start times
   float totalDur = job->sourceMeta.duration;

   if (segmentCount <= 1)
   {
      float mid = std::max(0.0f, (totalDur / 2.0f) - (segmentDuration / 2.0f));
      job->segments.push_back(SegmentData{
         .startTime = mid
      });
   }
   else
   {
      float chunkDur = totalDur / segmentCount;
      for (int i = 0; i < segmentCount; ++i)
      {
         float s = (chunkDur * i) + (chunkDur / 2.0f) - (segmentDuration / 2.0f);
         s = std::clamp(s, 0.0f, std::max(0.0f, totalDur - segmentDuration));

         // Push the struct with the known start time, zeroing the rest out
         job->segments.push_back(SegmentData{
            .startTime = s
         });
      }
   }

   job->segmentCache = std::make_shared<SegmentCache>();
   job->state = JobState::INIT;
   job->runner = std::make_unique<FFmpegRunner>();
   m_jobs.push_back(job);
}

void JobManager::StopAllJobs()
{
   for (auto& job : m_jobs)
   {
      if (!job->isComplete)
      {
         job->isComplete = true;
         job->isCanceled = true;
         job->state = JobState::DONE;
         if (job->runner)
            job->runner->Stop();

         std::error_code ec;
         if (!job->outputFile.empty())
            std::filesystem::remove(job->outputFile, ec);
         job->segmentCache.reset();
      }
   }

   for (auto& job : m_pendingJobs)
   {
      job->isComplete = true;
      job->isCanceled = true;
      job->state = JobState::DONE;
      if (job->runner)
         job->runner->Stop();
   }
   m_pendingJobs.clear();
}

void JobManager::ClearCompletedJobs()
{
   m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                               [](const auto& j) { return j->isComplete; }),
                m_jobs.end());

   if (m_jobs.empty())
      m_nextJobId = 1;
}

void JobManager::RemoveJob(int jobId)
{
   for (auto& job : m_jobs)
   {
      if (job->jobId == jobId && !job->isComplete)
      {
         job->isComplete = true;
         job->state = JobState::DONE;
         if (job->runner)
            job->runner->Stop();

         std::error_code ec;
         if (!job->outputFile.empty())
            std::filesystem::remove(job->outputFile, ec);
         job->segmentCache.reset();
      }
   }

   for (auto& job : m_pendingJobs)
   {
      if (job->jobId == jobId && !job->isComplete)
      {
         job->isComplete = true;
         job->state = JobState::DONE;
         if (job->runner)
            job->runner->Stop();
      }
   }

   m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                               [jobId](const auto& j) { return j->jobId == jobId; }),
                m_jobs.end());

   m_pendingJobs.erase(std::remove_if(m_pendingJobs.begin(), m_pendingJobs.end(),
                                      [jobId](const auto& j) { return j->jobId == jobId; }),
                       m_pendingJobs.end());

   if (m_jobs.empty() && m_pendingJobs.empty())
      m_nextJobId = 1;
}

void JobManager::CancelJob(int jobId)
{
   for (auto& job : m_jobs)
   {
      if (job->jobId == jobId && !job->isComplete)
      {
         job->isComplete = true;
         job->isCanceled = true;
         job->state = JobState::DONE;
         if (job->runner)
            job->runner->Stop();

         std::error_code ec;
         if (!job->outputFile.empty())
            std::filesystem::remove(job->outputFile, ec);
         job->segmentCache.reset();
      }
   }

   for (auto& job : m_pendingJobs)
   {
      if (job->jobId == jobId && !job->isComplete)
      {
         job->isComplete = true;
         job->isCanceled = true;
         job->state = JobState::DONE;
         if (job->runner)
            job->runner->Stop();
      }
   }
}

void JobManager::PauseJob(int jobId)
{
   for (auto& job : m_jobs)
   {
      if (job->jobId == jobId && job->runner && !job->isComplete)
      {
         job->runner->Pause();
         break;
      }
   }
}

void JobManager::ResumeJob(int jobId)
{
   for (auto& job : m_jobs)
   {
      if (job->jobId == jobId && job->runner && !job->isComplete)
      {
         job->runner->Resume();
         break;
      }
   }
}

std::filesystem::path JobManager::GenerateTempFileName(const std::shared_ptr<BenchmarkJob>& job,
                                                       std::string_view header, bool includeSegment)
{
   if (includeSegment)
   {
      return m_tempDir / std::format("{}_job{}_{}.mkv", header, job->jobId, job->currentSegmentIdx);
   }
   else
   {
      return m_tempDir / std::format("{}_job{}.mkv", header, job->jobId);
   }
}

void JobManager::ProcessInit(std::shared_ptr<BenchmarkJob>& job)
{
   job->currentSegmentIdx = 0;
   job->estimatedFullSize = 0.0f;
   job->commandStarted = false;
   job->outputFile = GenerateTempFileName(job, "temp_dist", false);

   // Zero out the dynamic data, leaving the startTimes intact
   for (auto& seg : job->segments)
   {
      seg.vmaf = 0.0f;
      seg.bitrate = 0.0f;
      seg.sizeBytes = 0;
   }

   if (job->segmentsReady)
   {
      // Segments already extracted by a previous iteration, skip to encoding
      job->state = JobState::ENCODING_SEGMENT;
   }
   else
   {
      job->state = JobState::EXTRACTING_SEGMENT;
   }
}

void JobManager::ProcessExtractingSegment(std::shared_ptr<BenchmarkJob>& job)
{
   if (!job->commandStarted)
   {
      float start = job->segments[job->currentSegmentIdx].startTime;
      float preSeek = (start > 5.0f) ? start - 5.0f : 0.0f;
      float postSeek = start - preSeek;
      auto refFile = GenerateTempFileName(job, "temp_ref", true);

      auto cmd = std::format(
         "ffmpeg -v error -y -ss {} -i \"{}\" -ss {} -t {} -map 0:v:0 -c:v ffv1 -an {}",
         preSeek,
         job->sourceFile.string(),
         postSeek,
         job->segmentDuration,
         refFile.string()
      );

      // Track the file path in the cache as we extract it
      job->segmentCache->refSegFiles.push_back(refFile);
      job->runner->Start(cmd);
      job->commandStarted = true;
   }
   else if (!job->runner->IsRunning())
   {
      job->commandStarted = false;
      std::error_code ec;
      const auto& refFile = job->segmentCache->refSegFiles[job->currentSegmentIdx];

      if (std::filesystem::exists(refFile, ec) &&
          std::filesystem::file_size(refFile, ec) > 0)
      {
         job->currentSegmentIdx++;
         if (job->currentSegmentIdx >= job->segments.size())
         {
            // All segments extracted, reset index and begin encoding
            job->segmentsReady = true;
            job->currentSegmentIdx = 0;
            job->state = JobState::ENCODING_SEGMENT;
         }
         // else: loop back for the next segment on next Update()
      }
      else
      {
         job->isComplete = true;
         job->state = JobState::DONE;
      }
   }
}

void JobManager::ProcessEncodingSegment(std::shared_ptr<BenchmarkJob>& job)
{
   if (!job->commandStarted)
   {
      std::string pixFmt = "yuv420p";
      if (job->profile.codec == "libsvtav1")
      {
         pixFmt = "yuv420p10le";
      }
      else if (job->profile.bitDepth == "10-bit")
      {
         if (job->profile.codec.find("nvenc") != std::string::npos)
            pixFmt = "p010le";
         else
            pixFmt = "yuv420p10le";
      }

      std::string extra = job->profile.extraArgs;

      if (job->profile.codec == "libsvtav1")
      {
         // 1. Build base SVT params directly into a single formatted string
         std::string svtParams = std::format("tune={}:film-grain-denoise=0:film-grain=0", job->profile.svtTune);

         if (!extra.empty())
         {
            constexpr std::string_view svtParamKey = "-svtav1-params";
            auto paramLocation = extra.find(svtParamKey);

            // If user put raw colon-delimited params (no leading dash), merge them
            if (paramLocation == std::string::npos && extra[0] != '-')
            {
               svtParams = std::format("{}:{}", svtParams, extra);
               extra.clear();
            }
            // If user manually specified -svtav1-params, merge the values
            else if (paramLocation != std::string::npos && (paramLocation + svtParamKey.length()) <= extra.size())
            {
               size_t valueStart = paramLocation + svtParamKey.length();
               while (valueStart < extra.size() && (extra[valueStart] == ' ' || extra[valueStart] == '"'))
                  valueStart++;

               size_t valueEnd = valueStart;
               while (valueEnd < extra.size() && extra[valueEnd] != ' ' && extra[valueEnd] != '"')
                  valueEnd++;

               std::string userParams = extra.substr(valueStart, valueEnd - valueStart);
               if (!userParams.empty())
               {
                  svtParams = std::format("{}:{}", svtParams, userParams);
               }

               // Remove the -svtav1-params portion from extra
               while (valueEnd < extra.size() && (extra[valueEnd] == '"' || extra[valueEnd] == ' '))
                  valueEnd++;

               extra = extra.substr(0, paramLocation) + extra.substr(valueEnd);
            }
         }

         // Append any remaining extra args
         std::string remaining = job->profile.extraArgs;
         if (remaining.find("-svtav1-params") != std::string::npos || (!remaining.empty() && remaining[0] != '-'))
         {
            remaining.clear(); // Already merged above
         }

         // 2. Format the final output cleanly without clunky + operations
         if (remaining.empty())
         {
            extra = std::format("-svtav1-params {}", svtParams);
         }
         else
         {
            extra = std::format("-svtav1-params {} {}", svtParams, remaining);
         }
      }
      else if (!extra.empty())
      {
         if (extra[0] != '-' && extra.find('=') != std::string::npos)
         {
            // 3. Inject variables directly inside quoted placeholders
            if (job->profile.codec == "libx264")
               extra = std::format("-x264-params \"{}\"", extra);
            else if (job->profile.codec == "libx265")
               extra = std::format("-x265-params \"{}\"", extra);
         }

         if (extra[0] != ' ')
         {
            extra = std::format(" {}", extra);
         }
      }

      // Final safety check for leading space if not empty
      if (!extra.empty() && extra[0] != ' ')
         extra = std::format(" {}", extra);

      std::string cmd = std::format(
         "ffmpeg -v warning -stats -y -i \"{}\" -c:v {} -preset {} -pix_fmt {} -crf {}{} \"{}\"",
         job->segmentCache->refSegFiles[job->currentSegmentIdx].string(),
         job->profile.codec,
         job->profile.preset,
         pixFmt,
         job->currentCrf,
         extra,
         job->outputFile.string()
      );

      job->runner->Start(cmd);
      job->commandStarted = true;
   }
   else if (!job->runner->IsRunning())
   {
      job->commandStarted = false;
      if (job->runner->GetProgress().bitrate > 0)
      {
         // Grab a reference to the current segment struct to keep code clean
         auto& currentSeg = job->segments[job->currentSegmentIdx];

         currentSeg.bitrate = job->runner->GetProgress().bitrate;

         std::error_code ec;
         currentSeg.sizeBytes = std::filesystem::file_size(job->outputFile, ec);

         // Update running averages for UI visibility
         float sumBitrate = 0.0f;
         int count = job->currentSegmentIdx + 1; // Segments completed so far
         for (int i = 0; i < count; ++i)
         {
            sumBitrate += job->segments[i].bitrate;
         }
         job->avgBitrate = sumBitrate / count;

         if (job->avgBitrate > 0.0f && job->sourceMeta.duration > 0)
         {
            // Calculate using average bitrate (kbps) * duration (s)
            job->estimatedFullSize = (job->avgBitrate * 1000.0f / 8.0f) *
               job->sourceMeta.duration / (1024.0f * 1024.0f);
         }

         job->state = JobState::VMAFFING_SEGMENT;
      }
      else
      {
         job->isComplete = true;
         job->state = JobState::DONE;
      }
   }
}

void JobManager::ProcessVmaffingSegment(std::shared_ptr<BenchmarkJob>& job)
{
   if (!job->commandStarted)
   {
      int threads = std::thread::hardware_concurrency();
      if (threads == 0)
         threads = 4;

      std::string cmd = std::format(
         "ffmpeg -v info -i \"{}\" -i \"{}\" -lavfi \"libvmaf=n_threads={}\" -f null -",
         job->outputFile.string(),
         job->segmentCache->refSegFiles[job->currentSegmentIdx].string(),
         threads
      );

      job->runner->Start(cmd);
      job->commandStarted = true;
   }
   else if (!job->runner->IsRunning())
   {
      job->commandStarted = false;

      if (float score = job->runner->GetVMAFScore();
          score > 0.0f)
      {
         // Assign directly instead of push_back
         job->segments[job->currentSegmentIdx].vmaf = score;

         // Calculate the average VMAF so far
         float sumVmaf = 0.0f;
         int segCompleted = job->currentSegmentIdx + 1;
         for (auto i = 0u; i < segCompleted; ++i)
         {
            sumVmaf += job->segments[i].vmaf;
         }
         job->avgVmaf = sumVmaf / segCompleted;

         job->currentSegmentIdx++;
         if (job->currentSegmentIdx >= job->segments.size())
         {
            job->state = JobState::CHECKING_SCORE;
         }
         else
         {
            job->state = JobState::ENCODING_SEGMENT;
         }
      }
      else
      {
         job->isComplete = true;
         job->state = JobState::DONE;
      }
   }
}

void JobManager::UpdateCrfBounds(std::shared_ptr<BenchmarkJob>& job, float diff)
{
   if (diff < 0.0f)
      job->crfMax = std::min(job->crfMax, job->currentCrf - 1);
   else if (diff > 0.0f)
      job->crfMin = std::max(job->crfMin, job->currentCrf + 1);
   else
   {
      job->crfMax = job->currentCrf;
      job->crfMin = job->currentCrf;
   }
}

int JobManager::CalculateNextCrf(const std::shared_ptr<BenchmarkJob>& job, float target, float diff)
{
   int bracketLowCrf = -1;
   int bracketHighCrf = -1;
   float bracketLowVmaf = 0.0f;
   float bracketHighVmaf = 0.0f;

   for (const auto& result : job->testHistory)
   {
      auto c = result.crf;
      auto v = result.vmaf;

      if (v >= target)
      {
         if (bracketLowCrf < 0 || c > bracketLowCrf)
         {
            bracketLowCrf = c; bracketLowVmaf = v;
         }
      }
      else
      {
         if (bracketHighCrf < 0 || c < bracketHighCrf)
         {
            bracketHighCrf = c; bracketHighVmaf = v;
         }
      }
   }

   if (bracketLowCrf >= 0 && bracketHighCrf >= 0)
   {
      float slope = (bracketLowVmaf - bracketHighVmaf) / static_cast<float>(bracketLowCrf - bracketHighCrf);
      if (std::abs(slope) > 0.001f)
         return static_cast<int>(std::round(bracketLowCrf + (target - bracketLowVmaf) / slope));

      return (bracketLowCrf + bracketHighCrf) / 2;
   }

   // No bracket fallback
   float multiplier = (job->profile.codec == "libsvtav1") ? 3.0f : 2.5f;
   int crfDelta = static_cast<int>(std::round(diff * multiplier));

   // C++17 clamp replaces the manual maxDelta clamping
   crfDelta = std::clamp(crfDelta, -8, 8);
   if (crfDelta == 0) crfDelta = (diff > 0) ? 1 : -1;

   return job->currentCrf + crfDelta;
}

void JobManager::FinalizeSearch(std::shared_ptr<BenchmarkJob>& job)
{
   job->searchActive = false;
   job->isComplete = true;
   job->state = JobState::DONE;

   auto calcDiff = [](const auto& j) {
      return j->avgVmaf - j->profile.targetVmaf;
   };

   auto completed_jobs = m_jobs | std::views::filter([&](const auto& other) {
      return other->jobId == job->jobId && other->state == JobState::DONE;
   });

   for (auto& other : completed_jobs)
   {
      other->isRecommended = false;
   }

   auto candidates = completed_jobs | std::views::filter([&](const auto& other) {
      return calcDiff(other) >= 0.0f;
   });

   auto it = std::ranges::max_element(candidates, std::less{}, [](const auto& other) {
      return other->currentCrf;
   });

   if (it != candidates.end())
   {
      (*it)->isRecommended = true;
   }
   else
   {
      if (auto fallback_it = std::ranges::max_element(completed_jobs, std::less{}, calcDiff);
          fallback_it != completed_jobs.end())
      {
         (*fallback_it)->isRecommended = true;
      }
   }

   for (auto& other : m_jobs)
   {
      if (other->jobId == job->jobId)
         other->segmentCache.reset();
   }

   std::error_code ec;
   std::filesystem::remove(job->outputFile, ec);
}

void JobManager::SpawnNextIteration(std::shared_ptr<BenchmarkJob>& job, int nextCrf)
{
   // Setup the next iteration job with the same parameters but updated CRF and iteration count
   auto nextJob = std::make_shared<BenchmarkJob>();
   nextJob->jobId = job->jobId;
   nextJob->id = job->id;
   nextJob->profile = job->profile;
   nextJob->searchActive = true;
   nextJob->currentCrf = nextCrf;
   nextJob->crfMin = job->crfMin;
   nextJob->crfMax = job->crfMax;
   nextJob->iteration = job->iteration + 1;
   nextJob->testHistory = job->testHistory;
   nextJob->segmentDuration = job->segmentDuration;
   for (const auto& oldSeg : job->segments)
   {
      nextJob->segments.push_back(SegmentData{
         .startTime = oldSeg.startTime
      });
   }
   nextJob->state = JobState::INIT;
   nextJob->runner = std::make_unique<FFmpegRunner>();
   nextJob->sourceFile = job->sourceFile;
   nextJob->sourceMeta = job->sourceMeta;
   nextJob->segmentCache = job->segmentCache;
   nextJob->segmentsReady = true;

   // Add the next iteration job to the pending jobs queue
   m_pendingJobs.push_back(nextJob);

   // Clear the old job
   job->searchActive = false;
   job->isComplete = true;
   job->state = JobState::DONE;
}

void JobManager::HandleEarlyExit(std::shared_ptr<BenchmarkJob>& job)
{
   job->isComplete = true;
   job->state = JobState::DONE;

   if (!job->searchActive)
      job->isRecommended = true;

   std::error_code ec;
   std::filesystem::remove(job->outputFile, ec);
}

void JobManager::ProcessCheckScore(std::shared_ptr<BenchmarkJob>& job)
{
   if (job->segments.empty() || !job->searchActive)
   {
      HandleEarlyExit(job);
      return;
   }

   float target = static_cast<float>(job->profile.targetVmaf);
   float diff = job->avgVmaf - target;
   job->testHistory.push_back({job->currentCrf, job->avgVmaf});

   UpdateCrfBounds(job, diff);

   int nextCrf = CalculateNextCrf(job, target, diff);
   nextCrf = std::clamp(nextCrf, job->crfMin, job->crfMax); // C++17 clamp replaces manual min/max bounds

   bool alreadyTested = std::ranges::find(job->testHistory, nextCrf, &CrfTestResult::crf) != job->testHistory.end();
   //bool alreadyTested = std::ranges::find(job->testedCrfs, nextCrf) != job->testedCrfs.end(); // C++20 ranges
   bool targetHit = (diff >= 0.0f && diff <= 0.2f);

   if (targetHit || job->crfMin > job->crfMax || alreadyTested || nextCrf == job->currentCrf)
   {
      FinalizeSearch(job);
   }
   else
   {
      SpawnNextIteration(job, nextCrf);
   }
}

void JobManager::ProcessJob(std::shared_ptr<BenchmarkJob>& job)
{
   if (job->isComplete)
      return;

   switch (job->state)
   {
      case JobState::INIT:
         ProcessInit(job);
         break;
      case JobState::EXTRACTING_SEGMENT:
         ProcessExtractingSegment(job);
         break;
      case JobState::ENCODING_SEGMENT:
         ProcessEncodingSegment(job);
         break;
      case JobState::VMAFFING_SEGMENT:
         ProcessVmaffingSegment(job);
         break;
      case JobState::CHECKING_SCORE:
         ProcessCheckScore(job);
         break;
      case JobState::DONE:
         break;
   }
}