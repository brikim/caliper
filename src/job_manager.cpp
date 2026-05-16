#include "job_manager.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <numeric>
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
      for (auto& j : m_pendingJobs)
      {
         m_jobs.push_back(j);
      }
      m_pendingJobs.clear();

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
      if (job->isComplete) continue;

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

   // Handle OS Sleep Prevention
#ifdef _WIN32
   if (anyRunning && !m_sleepPrevented)
   {
      SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
      m_sleepPrevented = true;
   }
   else if (!anyRunning && m_sleepPrevented)
   {
      SetThreadExecutionState(ES_CONTINUOUS);
      m_sleepPrevented = false;
   }
#endif
}

void JobManager::AddJob(const std::string& sourceFile, const VideoMetadata& sourceMeta, const EncodeProfile& profile, int segmentCount, float segmentDuration)
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
      if (mid < 0.0f) mid = 0.0f;
      job->segmentStartTimes.push_back(mid);
   }
   else
   {
      float chunkDur = totalDur / segmentCount;
      for (int i = 0; i < segmentCount; ++i)
      {
         float s = (chunkDur * i) + (chunkDur / 2.0f) - (segmentDuration / 2.0f);
         if (s < 0.0f) s = 0.0f;
         if (s + segmentDuration > totalDur) s = totalDur - segmentDuration;
         if (s < 0.0f) s = 0.0f;
         job->segmentStartTimes.push_back(s);
      }
   }

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
         job->state = JobState::DONE;
         if (job->runner)
            job->runner->Stop();

         std::error_code ec;
         if (!job->outputFile.empty())
            std::filesystem::remove(job->outputFile, ec);
         if (!job->refSegFile.empty())
            std::filesystem::remove(job->refSegFile, ec);
      }
   }
}

void JobManager::ClearCompletedJobs()
{
   m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                               [](const auto& j) { return j->isComplete; }), m_jobs.end());

   if (m_jobs.empty())
      m_nextJobId = 1;
}

void JobManager::ProcessJob(std::shared_ptr<BenchmarkJob>& job)
{
   if (job->isComplete) return;

   std::string jobIdx = std::to_string(std::hash<std::string>{}(job->id) % 1000);
   std::string tempDist = "temp_dist_" + jobIdx + ".mkv";
   job->outputFile = tempDist;

   switch (job->state)
   {
      case JobState::INIT:
      {
         job->currentSegmentIdx = 0;
         job->segmentVMAFs.clear();
         job->segmentBitrates.clear();
         job->segmentSizes.clear();
         job->estimatedFullSize = 0.0f;
         job->commandStarted = false;
         job->state = JobState::EXTRACTING_SEGMENT;
         break;
      }
      case JobState::EXTRACTING_SEGMENT:
      {
         if (!job->commandStarted)
         {
            float start = job->segmentStartTimes[job->currentSegmentIdx];
            float preSeek = (start > 5.0f) ? start - 5.0f : 0.0f;
            float postSeek = start - preSeek;
            std::string segIdx = std::to_string(job->currentSegmentIdx);
            job->refSegFile = "temp_ref_" + jobIdx + "_" + segIdx + ".mkv";

            std::string cmd = "ffmpeg -v error -y"
               " -ss " + std::to_string(preSeek) +
               " -i \"" + job->sourceFile + "\""
               " -ss " + std::to_string(postSeek) +
               " -t " + std::to_string(job->segmentDuration) +
               " -map 0:v:0 -c:v ffv1 -an " + job->refSegFile;

            job->runner->Start(cmd);
            job->commandStarted = true;
         }
         else if (!job->runner->IsRunning())
         {
            job->commandStarted = false;
            std::error_code ec;
            if (std::filesystem::exists(job->refSegFile, ec) &&
                std::filesystem::file_size(job->refSegFile, ec) > 0)
            {
               job->state = JobState::ENCODING_SEGMENT;
            }
            else
            {
               job->isComplete = true;
               job->state = JobState::DONE;
            }
         }
         break;
      }
      case JobState::ENCODING_SEGMENT:
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
               std::string svtParams = "tune=" + std::to_string(job->profile.svtTune);
               if (job->profile.filmGrainDenoise)
               {
                  svtParams += ":film-grain-denoise=1:film-grain=" + std::to_string(job->profile.filmGrain);
               }
               else
               {
                  svtParams += ":film-grain-denoise=0:film-grain=0";
               }
               
               if (!extra.empty())
               {
                  // If user put raw colon-delimited params (no leading dash), merge them
                  if (extra.find("-svtav1-params") == std::string::npos && extra[0] != '-')
                  {
                     svtParams += ":" + extra;
                     extra = "";
                  }
                  // If user manually specified -svtav1-params, merge the values
                  else if (extra.find("-svtav1-params") != std::string::npos)
                  {
                     // Extract the value after -svtav1-params and merge
                     size_t paramPos = extra.find("-svtav1-params");
                     size_t valueStart = paramPos + 14; // length of "-svtav1-params"
                     while (valueStart < extra.size() && (extra[valueStart] == ' ' || extra[valueStart] == '"'))
                        valueStart++;
                     size_t valueEnd = valueStart;
                     while (valueEnd < extra.size() && extra[valueEnd] != ' ' && extra[valueEnd] != '"')
                        valueEnd++;
                     std::string userParams = extra.substr(valueStart, valueEnd - valueStart);
                     if (!userParams.empty())
                        svtParams += ":" + userParams;
                     // Remove the -svtav1-params portion from extra
                     while (valueEnd < extra.size() && (extra[valueEnd] == '"' || extra[valueEnd] == ' '))
                        valueEnd++;
                     extra = extra.substr(0, paramPos) + extra.substr(valueEnd);
                  }
               }
               
               extra = "-svtav1-params " + svtParams;
               // Append any remaining extra args
               std::string remaining = job->profile.extraArgs;
               if (remaining.find("-svtav1-params") != std::string::npos || (!remaining.empty() && remaining[0] != '-'))
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

            std::string cmd = "ffmpeg -v warning -stats -y"
               " -i " + job->refSegFile +
               " -c:v " + job->profile.codec +
               " -preset " + job->profile.preset +
               " -pix_fmt " + pixFmt +
               " -crf " + std::to_string(job->currentCrf) +
               extra + " " + tempDist;

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
               job->segmentSizes.push_back(std::filesystem::file_size(tempDist, ec));
               job->state = JobState::VMAFFING_SEGMENT;
            }
            else
            {
               job->isComplete = true;
               job->state = JobState::DONE;
            }
         }
         break;
      }
      case JobState::VMAFFING_SEGMENT:
      {
         if (!job->commandStarted)
         {
            int threads = std::thread::hardware_concurrency();
            if (threads == 0) threads = 4;

            std::string cmd = "ffmpeg -v info"
               " -i " + tempDist +
               " -i " + job->refSegFile +
               " -lavfi \"libvmaf=n_threads=" + std::to_string(threads) + "\""
               " -f null -";

            job->runner->Start(cmd);
            job->commandStarted = true;
         }
         else if (!job->runner->IsRunning())
         {
            job->commandStarted = false;
            float score = job->runner->GetVMAFScore();

            std::error_code ec;
            std::filesystem::remove(job->refSegFile, ec);

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
                  job->state = JobState::EXTRACTING_SEGMENT;
               }
            }
            else
            {
               job->isComplete = true;
               job->state = JobState::DONE;
            }
         }
         break;
      }
      case JobState::CHECKING_SCORE:
      {
         if (job->segmentVMAFs.empty())
         {
            job->isComplete = true;
            job->state = JobState::DONE;
            std::error_code ec;
            std::filesystem::remove(job->outputFile, ec);
            break;
         }

         float sumVmaf = std::accumulate(job->segmentVMAFs.begin(), job->segmentVMAFs.end(), 0.0f);
         job->finalVMAF = sumVmaf / job->segmentVMAFs.size();

         float sumBitrate = std::accumulate(job->segmentBitrates.begin(), job->segmentBitrates.end(), 0.0f);
         job->avgBitrate = sumBitrate / job->segmentBitrates.size();

         float sumSize = std::accumulate(job->segmentSizes.begin(), job->segmentSizes.end(), 0.0f);
         float totalSampleDur = job->segmentSizes.size() * job->segmentDuration;

         if (totalSampleDur > 0 && job->sourceMeta.duration > 0)
         {
            job->estimatedFullSize = (sumSize / totalSampleDur) * job->sourceMeta.duration / (1024.0f * 1024.0f);
         }

         if (!job->searchActive)
         {
            job->isComplete = true;
            job->state = JobState::DONE;
            job->isRecommended = true;
            std::error_code ec;
            std::filesystem::remove(job->outputFile, ec);
         }
         else
         {
            float diff = job->finalVMAF - job->profile.targetVmaf;
            
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

            float multiplier = 2.5f;
            int maxDelta = 12;

            if (job->profile.codec == "libsvtav1")
            {
               multiplier = 10.0f;
               maxDelta = 35;
            }

            int crfDelta = static_cast<int>(std::round(diff * multiplier));

            if (crfDelta > maxDelta) crfDelta = maxDelta;
            if (crfDelta < -maxDelta) crfDelta = -maxDelta;
            if (crfDelta == 0) crfDelta = (diff > 0) ? 1 : -1;

            int nextCrf = job->currentCrf + crfDelta;
            
            if (nextCrf < job->crfMin) nextCrf = job->crfMin;
            if (nextCrf > job->crfMax) nextCrf = job->crfMax;

            bool alreadyTested = false;
            for (int c : job->testedCrfs)
            {
               if (c == nextCrf)
               {
                  alreadyTested = true;
                  break;
               }
            }

            if (job->crfMin > job->crfMax || alreadyTested || nextCrf == job->currentCrf)
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
               nextJob->segmentDuration = job->segmentDuration;
               nextJob->segmentStartTimes = job->segmentStartTimes;
               nextJob->state = JobState::INIT;
               nextJob->runner = std::make_unique<FFmpegRunner>();
               nextJob->sourceFile = job->sourceFile;
               nextJob->sourceMeta = job->sourceMeta;

               job->searchActive = false;
               job->isComplete = true;
               job->state = JobState::DONE;

               m_pendingJobs.push_back(nextJob);
            }
         }
         break;
      }
      case JobState::DONE:
         break;
   }
}