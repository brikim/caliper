#include "job_manager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <numeric>
#include <ranges>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

JobManager::JobManager() = default;

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

void JobManager::AddJob(const std::string& sourceFile,
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
   job->testedCrfs.push_back(job->currentCrf);
   job->segmentDuration = segmentDuration;

   // Calculate segment start times
   float totalDur = job->sourceMeta.duration;

   if (segmentCount <= 1)
   {
      float mid = (totalDur / 2.0f) - (segmentDuration / 2.0f);
      if (mid < 0.0f)
         mid = 0.0f;
      job->segmentStartTimes.push_back(mid);
   }
   else
   {
      float chunkDur = totalDur / segmentCount;
      for (int i = 0; i < segmentCount; ++i)
      {
         float s = (chunkDur * i) + (chunkDur / 2.0f) - (segmentDuration / 2.0f);
         if (s < 0.0f)
            s = 0.0f;
         if (s + segmentDuration > totalDur)
            s = totalDur - segmentDuration;
         if (s < 0.0f)
            s = 0.0f;
         job->segmentStartTimes.push_back(s);
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

std::string JobManager::GenerateTempFileName(const std::shared_ptr<BenchmarkJob>& job,
                                             std::string_view header, bool includeSegment)
{
   if (includeSegment)
   {
      return std::format("{}_job{}_{}.mkv", header, job->jobId, job->currentSegmentIdx);
   }
   else
   {
      return std::format("{}_job{}.mkv", header, job->jobId);
   }
}

void JobManager::ProcessInit(std::shared_ptr<BenchmarkJob>& job)
{
   job->currentSegmentIdx = 0;
   job->segmentVMAFs.clear();
   job->segmentBitrates.clear();
   job->segmentSizes.clear();
   job->estimatedFullSize = 0.0f;
   job->commandStarted = false;

   auto tempDist = GenerateTempFileName(job, "temp_dist", false);
   job->outputFile = tempDist;

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
      float start = job->segmentStartTimes[job->currentSegmentIdx];
      float preSeek = (start > 5.0f) ? start - 5.0f : 0.0f;
      float postSeek = start - preSeek;
      auto refFile = GenerateTempFileName(job, "temp_ref", true);

      std::string cmd = "ffmpeg -v error -y"
         " -ss " +
         std::format("{}", preSeek) + " -i \"" + job->sourceFile +
         "\""
         " -ss " +
         std::format("{}", postSeek) + " -t " +
         std::format("{}", job->segmentDuration) +
         " -map 0:v:0 -c:v ffv1 -an " + refFile;

      // Track the file path in the cache as we extract it
      job->segmentCache->refSegFiles.push_back(refFile);
      job->runner->Start(cmd);
      job->commandStarted = true;
   }
   else if (!job->runner->IsRunning())
   {
      job->commandStarted = false;
      std::error_code ec;
      const std::string& refFile =
         job->segmentCache->refSegFiles[job->currentSegmentIdx];

      if (std::filesystem::exists(refFile, ec) &&
          std::filesystem::file_size(refFile, ec) > 0)
      {
         job->currentSegmentIdx++;
         if (job->currentSegmentIdx >= job->segmentStartTimes.size())
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
         std::string svtParams = "tune=" + std::format("{}", job->profile.svtTune);
         // Disable SVT-AV1's internal denoiser and film grain synthesis. This setting does not work with VMAF.
         svtParams += ":film-grain-denoise=0:film-grain=0";

         if (!extra.empty())
         {
            constexpr std::string_view svtParamKey = "-svtav1-params";
            auto paramLocation = extra.find(svtParamKey);

            // If user put raw colon-delimited params (no leading dash), merge
            // them
            if (paramLocation == std::string::npos &&
                extra[0] != '-')
            {
               svtParams += ":" + extra;
               extra = "";
            }
            // If user manually specified -svtav1-params, merge the values
            else if (paramLocation != std::string::npos && (paramLocation + svtParamKey.length()) <= extra.size())
            {
               // Extract the value after -svtav1-params and merge
               size_t valueStart = paramLocation + svtParamKey.length();
               while (valueStart < extra.size() &&
                      (extra[valueStart] == ' ' || extra[valueStart] == '"'))
                  valueStart++;
               size_t valueEnd = valueStart;
               while (valueEnd < extra.size() && extra[valueEnd] != ' ' &&
                      extra[valueEnd] != '"')
                  valueEnd++;
               std::string userParams =
                  extra.substr(valueStart, valueEnd - valueStart);
               if (!userParams.empty())
                  svtParams += ":" + userParams;
               // Remove the -svtav1-params portion from extra
               while (valueEnd < extra.size() &&
                      (extra[valueEnd] == '"' || extra[valueEnd] == ' '))
                  valueEnd++;
               extra = extra.substr(0, paramLocation) + extra.substr(valueEnd);
            }
         }

         extra = "-svtav1-params " + svtParams;
         // Append any remaining extra args
         std::string remaining = job->profile.extraArgs;
         if (remaining.find("-svtav1-params") != std::string::npos ||
             (!remaining.empty() && remaining[0] != '-'))
            remaining = ""; // Already merged above
         if (!remaining.empty())
            extra += " " + remaining;
      }
      else if (!extra.empty())
      {
         if (extra[0] != '-' && extra.find('=') != std::string::npos)
         {
            if (job->profile.codec == "libx264")
               extra = "-x264-params \"" + extra + "\"";
            else if (job->profile.codec == "libx265")
               extra = "-x265-params \"" + extra + "\"";
         }
         if (extra[0] != ' ')
            extra = " " + extra;
      }

      // Final safety check for leading space if not empty
      if (!extra.empty() && extra[0] != ' ')
         extra = " " + extra;

      const std::string& refFile =
         job->segmentCache->refSegFiles[job->currentSegmentIdx];

      std::string cmd = "ffmpeg -v warning -stats -y"
         " -i " +
         refFile + " -c:v " + job->profile.codec +
         " -preset " + job->profile.preset + " -pix_fmt " +
         pixFmt + " -crf " + std::format("{}", job->currentCrf) +
         extra + " " + job->outputFile;

      job->runner->Start(cmd);
      job->commandStarted = true;
   }
   else if (!job->runner->IsRunning())
   {
      job->commandStarted = false;
      if (job->runner->GetProgress().bitrate > 0)
      {
         job->segmentBitrates.push_back(job->runner->GetProgress().bitrate);
         std::error_code ec;
         job->segmentSizes.push_back(std::filesystem::file_size(job->outputFile, ec));

         // Update running averages immediately for UI visibility
         float sumBitrate = std::accumulate(job->segmentBitrates.begin(),
                                            job->segmentBitrates.end(), 0.0f);
         job->avgBitrate = sumBitrate / job->segmentBitrates.size();

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

      const std::string& refFile =
         job->segmentCache->refSegFiles[job->currentSegmentIdx];

      std::string cmd =
         "ffmpeg -v info"
         " -i " +
         job->outputFile + " -i " + refFile +
         " -lavfi \"libvmaf=n_threads=" + std::format("{}", threads) +
         "\""
         " -f null -";

      job->runner->Start(cmd);
      job->commandStarted = true;
   }
   else if (!job->runner->IsRunning())
   {
      job->commandStarted = false;
      float score = job->runner->GetVMAFScore();
      // Reference segments are NOT deleted here — owned by SegmentCache

      if (score > 0.0f)
      {
         job->segmentVMAFs.push_back(score);
         job->currentSegmentIdx++;
         if (job->currentSegmentIdx >= job->segmentStartTimes.size())
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

void JobManager::ProcessCheckScore(std::shared_ptr<BenchmarkJob>& job)
{
   if (job->segmentVMAFs.empty() || !job->searchActive)
   {
      job->isComplete = true;
      job->state = JobState::DONE;

      if (!job->searchActive)
      {
         job->isRecommended = true;
      }

      std::error_code ec;
      std::filesystem::remove(job->outputFile, ec);
      return;
   }

   float sumVmaf = std::accumulate(job->segmentVMAFs.begin(),
                                   job->segmentVMAFs.end(), 0.0f);
   job->finalVMAF = sumVmaf / job->segmentVMAFs.size();

   float sumBitrate = std::accumulate(job->segmentBitrates.begin(),
                                      job->segmentBitrates.end(), 0.0f);
   job->avgBitrate = sumBitrate / job->segmentBitrates.size();

   if (job->avgBitrate > 0.0f && job->sourceMeta.duration > 0)
   {
      job->estimatedFullSize = (job->avgBitrate * 1000.0f / 8.0f) *
         job->sourceMeta.duration / (1024.0f * 1024.0f);
   }

   float target = static_cast<float>(job->profile.targetVmaf);
   float diff = job->finalVMAF - target;

   // Record this result in the search history
   job->testedVmafs.push_back(job->finalVMAF);

   if (diff < 0.0f)
   {
      job->crfMax = std::min(job->crfMax, job->currentCrf - 1);
   }
   else if (diff > 0.0f)
   {
      job->crfMin = std::max(job->crfMin, job->currentCrf + 1);
   }
   else
   {
      job->crfMax = job->currentCrf;
      job->crfMin = job->currentCrf;
   }

   int nextCrf = job->currentCrf;

   // Try interpolation if we have at least 2 data points
   // Find the tightest bracket: highest CRF with VMAF >= target,
   // lowest CRF with VMAF < target
   int bracketLowCrf = -1;  // lower CRF  (higher quality side)
   float bracketLowVmaf = 0.0f;
   int bracketHighCrf = -1; // higher CRF (lower quality side)
   float bracketHighVmaf = 0.0f;

   for (size_t i = 0; i < job->testedCrfs.size(); ++i)
   {
      int c = job->testedCrfs[i];
      float v = job->testedVmafs[i];

      if (v >= target)
      {
         // This CRF meets the target — want the highest such CRF
         if (bracketLowCrf < 0 || c > bracketLowCrf)
         {
            bracketLowCrf = c;
            bracketLowVmaf = v;
         }
      }
      else
      {
         // This CRF is below target — want the lowest such CRF
         if (bracketHighCrf < 0 || c < bracketHighCrf)
         {
            bracketHighCrf = c;
            bracketHighVmaf = v;
         }
      }
   }

   if (bracketLowCrf >= 0 && bracketHighCrf >= 0)
   {
      // We have a bracket — interpolate (secant method)
      float slope = (bracketLowVmaf - bracketHighVmaf) /
         static_cast<float>(bracketLowCrf - bracketHighCrf);

      if (std::abs(slope) > 0.001f)
      {
         float estimate = bracketLowCrf +
            (target - bracketLowVmaf) / slope;
         nextCrf = static_cast<int>(std::round(estimate));
      }
      else
      {
         // Flat slope — bisect the bracket
         nextCrf = (bracketLowCrf + bracketHighCrf) / 2;
      }
   }
   else
   {
      // No bracket yet — use a conservative initial step
      // AV1 CRF range is wider (0-63) so use a slightly larger
      // multiplier than x264/x265
      float multiplier = 2.5f;
      if (job->profile.codec == "libsvtav1")
         multiplier = 3.0f;

      int crfDelta = static_cast<int>(std::round(diff * multiplier));

      int maxDelta = 8;
      if (crfDelta > maxDelta)
         crfDelta = maxDelta;
      if (crfDelta < -maxDelta)
         crfDelta = -maxDelta;
      if (crfDelta == 0)
         crfDelta = (diff > 0) ? 1 : -1;

      nextCrf = job->currentCrf + crfDelta;
   }

   if (nextCrf < job->crfMin)
      nextCrf = job->crfMin;
   if (nextCrf > job->crfMax)
      nextCrf = job->crfMax;

   bool alreadyTested = std::find(job->testedCrfs.begin(), job->testedCrfs.end(), nextCrf) != job->testedCrfs.end();
   bool targetHit = (diff >= 0.0f && diff <= 0.2f);

   if (targetHit || job->crfMin > job->crfMax || alreadyTested ||
       nextCrf == job->currentCrf)
   {
      job->searchActive = false;
      job->isComplete = true;
      job->state = JobState::DONE;

      std::shared_ptr<BenchmarkJob> bestJob = nullptr;
      float bestDiff = -9999.0f;

      for (auto& other : m_jobs)
      {
         if (other->jobId == job->jobId && other->state == JobState::DONE)
         {
            other->isRecommended = false;

            float diff = other->finalVMAF - other->profile.targetVmaf;
            if (diff >= 0.0f)
            {
               if (!bestJob || other->currentCrf > bestJob->currentCrf)
               {
                  bestJob = other;
               }
            }
         }
      }

      if (!bestJob)
      {
         for (auto& other : m_jobs)
         {
            if (other->jobId == job->jobId && other->state == JobState::DONE)
            {
               float diff = other->finalVMAF - other->profile.targetVmaf;
               if (!bestJob || diff > bestDiff)
               {
                  bestJob = other;
                  bestDiff = diff;
               }
            }
         }
      }

      if (bestJob)
      {
         bestJob->isRecommended = true;
      }

      // Clear the segment cache for all iterations of this job now that the search is done.
      // This triggers the RAII cleanup of the temp_ref files immediately.
      for (auto& other : m_jobs)
      {
         if (other->jobId == job->jobId)
            other->segmentCache.reset();
      }

      std::error_code ec;
      std::filesystem::remove(job->outputFile, ec);
   }
   else
   {
      auto nextJob = std::make_shared<BenchmarkJob>();
      nextJob->jobId = job->jobId;
      nextJob->id = job->id;
      nextJob->profile = job->profile;
      nextJob->searchActive = true;
      nextJob->currentCrf = nextCrf;
      nextJob->crfMin = job->crfMin;
      nextJob->crfMax = job->crfMax;
      nextJob->iteration = job->iteration + 1;
      nextJob->testedCrfs = job->testedCrfs;
      nextJob->testedCrfs.push_back(nextCrf);
      nextJob->testedVmafs = job->testedVmafs;
      nextJob->segmentDuration = job->segmentDuration;
      nextJob->segmentStartTimes = job->segmentStartTimes;
      nextJob->state = JobState::INIT;
      nextJob->runner = std::make_unique<FFmpegRunner>();
      nextJob->sourceFile = job->sourceFile;
      nextJob->sourceMeta = job->sourceMeta;

      // Share the pre-extracted segment cache with the next iteration
      nextJob->segmentCache = job->segmentCache;
      nextJob->segmentsReady = true;

      job->searchActive = false;
      job->isComplete = true;
      job->state = JobState::DONE;

      m_pendingJobs.push_back(nextJob);
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