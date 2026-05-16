#include "job_manager.h"

#include <cmath>
#include <filesystem>
#include <functional>
#include <numeric>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
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
   int activeCount = 0;
   bool anyRunning = false;

   // Process currently active jobs and start new ones if we have capacity
   for (auto& job : m_jobs)
   {
      bool isRunning = (job->state != JobState::INIT && !job->isComplete);
      bool willRun = (isRunning || (activeCount < m_maxConcurrentJobs && !job->isComplete));

      if (willRun)
      {
         ProcessJob(job);
         if (!job->isComplete)
            activeCount++;
      }

      if (!job->isComplete)
         anyRunning = true;
   }

   // Append any newly spawned iteration jobs from the search algorithm
   if (!m_pendingJobs.empty())
   {
      for (auto& j : m_pendingJobs)
      {
         m_jobs.push_back(j);
      }
      m_pendingJobs.clear();
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
   job->crfMax = 51;
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
            if (job->profile.bitDepth == "10-bit")
            {
               if (job->profile.codec.find("nvenc") != std::string::npos)
                  pixFmt = "p010le";
               else
                  pixFmt = "yuv420p10le";
            }

            std::string extra = job->profile.extraArgs;
            if (!extra.empty())
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
            int crfDelta = static_cast<int>(std::round(diff * 2.5f));

            if (crfDelta > 12) crfDelta = 12;
            if (crfDelta < -12) crfDelta = -12;
            if (crfDelta == 0) crfDelta = (diff > 0) ? 1 : -1;

            int nextCrf = job->currentCrf + crfDelta;
            if (nextCrf < 0) nextCrf = 0;
            if (nextCrf > 51) nextCrf = 51;

            bool alreadyTested = false;
            for (int c : job->testedCrfs)
            {
               if (c == nextCrf)
               {
                  alreadyTested = true;
                  break;
               }
            }

            if (alreadyTested || nextCrf == job->currentCrf)
            {
               job->searchActive = false;
               job->isComplete = true;
               job->state = JobState::DONE;
               job->isRecommended = true;
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