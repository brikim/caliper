#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

struct FFmpegProgress
{
   int frame = 0;
   float fps = 0.0f;
   float bitrate = 0.0f; // in kbps
   float speed = 0.0f;
   std::string time;
};

struct VideoMetadata
{
   std::string codec;
   std::string pix_fmt;
   int bit_depth = 0;
   int width = 0;
   int height = 0;
   float duration = 0.0f;
   float framerate = 0.0f;
   bool valid = false;
};

class FFmpegRunner
{
public:
   FFmpegRunner();
   ~FFmpegRunner();

   // Start an ffmpeg command asynchronously
   bool Start(const std::string& command);

   // Attempt to stop the process
   void Stop();

   // Attempt to pause the process
   void Pause();

   // Attempt to resume the process
   void Resume();

   // Check if the worker thread is running
   bool IsRunning() const;

   // Check if the worker thread is paused
   bool IsPaused() const;

   // Retrieve unread log lines
   std::vector<std::string> GetNewLogs();

   // Get the latest parsed progress
   FFmpegProgress GetProgress() const;

   // Get final VMAF score if calculated (-1.0f if not yet found)
   float GetVMAFScore() const;

   // Fetch video metadata synchronously using ffprobe
   VideoMetadata GetMetadata(const std::string& filepath);

   // Returns the version string, or throws/returns empty if not found
   std::optional<std::string> GetFFmpegVersion();

private:
   bool ExecuteCommand(const std::string& command, std::function<bool(const char* data, size_t size)> onChunk);
   void RunThread(std::stop_token stoken, std::string command);
   void ParseLogLine(const std::string& line);

   std::jthread m_worker;
   std::atomic<bool> m_running{false};
   std::atomic<bool> m_paused{false};

   mutable std::mutex m_logMutex;
   std::queue<std::string> m_logQueue;

   mutable std::mutex m_progressMutex;
   FFmpegProgress m_progress;
   std::atomic<float> m_vmafScore{-1.0f};

   std::atomic<void*> m_processHandle;
};
