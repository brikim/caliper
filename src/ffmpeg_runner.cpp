#include "ffmpeg_runner.h"

#include <array>
#include <charconv>
#include <cstdio>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <TlHelp32.h>
#else
#include <signal.h>
#endif

#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

FFmpegRunner::FFmpegRunner()
{}

FFmpegRunner::~FFmpegRunner()
{
   Stop();
}

bool FFmpegRunner::Start(const std::string& command)
{
   if (m_running)
      return false;

   if (m_worker.joinable())
   {
      m_worker.join();
   }

   m_running = true;
   m_paused = false;
   m_vmafScore = -1.0f;

   // Clear queues
   std::lock_guard<std::mutex> lock(m_logMutex);
   std::queue<std::string> empty;
   std::swap(m_logQueue, empty);

   {
      std::lock_guard<std::mutex> plock(m_progressMutex);
      m_progress = FFmpegProgress();
   }

   // Ensure stderr is redirected to stdout so we can read it
   std::string full_cmd = command;
   if (full_cmd.find("2>&1") == std::string::npos)
   {
      full_cmd += " 2>&1";
   }

   m_worker = std::jthread([this](std::stop_token st, std::string cmd) {
      RunThread(st, std::move(cmd));
   }, full_cmd);
   return true;
}

void FFmpegRunner::Stop()
{
   if (m_running)
   {
      m_worker.request_stop();

#ifdef _WIN32
      void* handle = m_processHandle.load();
      if (handle)
      {
         TerminateProcess(static_cast<HANDLE>(handle), 0);
      }
#endif
   }
   if (m_worker.joinable())
   {
      m_worker.join();
   }
}

void FFmpegRunner::Pause()
{
   if (!m_running || m_paused)
      return;

   m_paused = true;

#ifdef _WIN32
   void* handle = m_processHandle.load();
   if (!handle)
   {
      // The process hasn't been created yet. 
      // ExecuteCommand will see m_paused = true and suspend it immediately upon creation.
      return;
   }

   DWORD processId = GetProcessId(static_cast<HANDLE>(handle));
   if (processId == 0) return;

   HANDLE hThreadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
   if (hThreadSnapshot == INVALID_HANDLE_VALUE) return;

   THREADENTRY32 threadEntry;
   threadEntry.dwSize = sizeof(THREADENTRY32);

   if (Thread32First(hThreadSnapshot, &threadEntry))
   {
      do
      {
         if (threadEntry.th32OwnerProcessID == processId)
         {
            HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadEntry.th32ThreadID);
            if (hThread)
            {
               SuspendThread(hThread);
               CloseHandle(hThread);
            }
         }
      } while (Thread32Next(hThreadSnapshot, &threadEntry));
   }
   CloseHandle(hThreadSnapshot);
#else
   // Note: Your current POSIX implementation uses popen(), which hides the PID.
   // To use kill(pid, SIGSTOP), you would need to rewrite the POSIX ExecuteCommand 
   // to use fork() and exec() or posix_spawn() to capture the PID.
#endif
}

void FFmpegRunner::Resume()
{
   if (!m_running || !m_paused)
      return;

   m_paused = false;

#ifdef _WIN32
   void* handle = m_processHandle.load();
   if (!handle)
   {
      // If it hasn't started yet, clearing the flag above is all we needed to do.
      return;
   }

   DWORD processId = GetProcessId(static_cast<HANDLE>(handle));
   if (processId == 0) return;

   HANDLE hThreadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
   if (hThreadSnapshot == INVALID_HANDLE_VALUE) return;

   THREADENTRY32 threadEntry;
   threadEntry.dwSize = sizeof(THREADENTRY32);

   if (Thread32First(hThreadSnapshot, &threadEntry))
   {
      do
      {
         if (threadEntry.th32OwnerProcessID == processId)
         {
            HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, threadEntry.th32ThreadID);
            if (hThread)
            {
               ResumeThread(hThread);
               CloseHandle(hThread);
            }
         }
      } while (Thread32Next(hThreadSnapshot, &threadEntry));
   }
   CloseHandle(hThreadSnapshot);
#else
   // Requires captured PID (kill(pid, SIGCONT))
#endif
}

bool FFmpegRunner::IsRunning() const
{
   return m_running;
}

bool FFmpegRunner::IsPaused() const
{
   return m_paused;
}

std::vector<std::string> FFmpegRunner::GetNewLogs()
{
   std::vector<std::string> logs;
   std::lock_guard<std::mutex> lock(m_logMutex);
   while (!m_logQueue.empty())
   {
      logs.push_back(m_logQueue.front());
      m_logQueue.pop();
   }
   return logs;
}

FFmpegProgress FFmpegRunner::GetProgress() const
{
   std::lock_guard<std::mutex> lock(m_progressMutex);
   return m_progress;
}

float FFmpegRunner::GetVMAFScore() const
{
   return m_vmafScore;
}

bool FFmpegRunner::ExecuteCommand(const std::string& command, std::function<bool(const char* data, size_t size)> onChunk)
{
#ifdef _WIN32
   HANDLE hReadPipe, hWritePipe;
   SECURITY_ATTRIBUTES saAttr = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

   if (!CreatePipe(&hReadPipe, &hWritePipe, &saAttr, 0))
      return false;

   SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

   STARTUPINFOA si = {sizeof(STARTUPINFOA)};
   si.cb = sizeof(STARTUPINFOA);
   si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
   si.wShowWindow = SW_HIDE;
   si.hStdOutput = hWritePipe;
   si.hStdError = hWritePipe;

   std::string shellCmd = "cmd.exe /c \"" + command + "\"";
   std::vector<char> cmdBuffer(shellCmd.begin(), shellCmd.end());
   cmdBuffer.push_back('\0');

   // Inside ExecuteCommand (#ifdef _WIN32)
   PROCESS_INFORMATION pi = {0};

   if (!CreateProcessA(NULL, cmdBuffer.data(), NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
   {
      CloseHandle(hReadPipe);
      CloseHandle(hWritePipe);
      return false;
   }

   CloseHandle(hWritePipe);

   // Store the handle securely for Stop() to use
   m_processHandle.store(pi.hProcess);

   std::array<char, 4096> buffer;
   DWORD bytesRead;

   while (ReadFile(hReadPipe, buffer.data(), buffer.size(), &bytesRead, NULL) && bytesRead > 0)
   {
      if (!onChunk(buffer.data(), static_cast<size_t>(bytesRead)))
      {
         TerminateProcess(pi.hProcess, 0);
         break;
      }
   }

   WaitForSingleObject(pi.hProcess, INFINITE);
   CloseHandle(pi.hProcess);
   CloseHandle(pi.hThread);
   CloseHandle(hReadPipe);

   // Clear the handle when finished
   m_processHandle.store(nullptr);
   return true;

#else
   FILE* pipe = popen(command.c_str(), "r");
   if (!pipe)
      return false;

   std::array<char, 4096> buffer;
   while (true)
   {
      size_t bytesRead = fread(buffer.data(), 1, buffer.size(), pipe);
      if (bytesRead > 0)
      {
         if (!onChunk(buffer.data(), bytesRead))
            break;
      }
      else if (feof(pipe) || ferror(pipe))
      {
         break;
      }
   }

   pclose(pipe);
   return true;
#endif
}

void FFmpegRunner::RunThread(std::stop_token stoken, std::string command)
{
   std::string accumulated;

   ExecuteCommand(command, [this, &accumulated, &stoken](const char* data, size_t size) -> bool {
      // Check the jthread's built-in stop token
      if (stoken.stop_requested())
         return false;

      accumulated.append(data, size);

      // Process complete lines
      while (true)
      {
         size_t pos_n = accumulated.find('\n');
         size_t pos_r = accumulated.find('\r');
         size_t pos = std::string::npos;

         if (pos_n != std::string::npos && pos_r != std::string::npos)
            pos = (pos_n < pos_r) ? pos_n : pos_r;
         else if (pos_n != std::string::npos)
            pos = pos_n;
         else if (pos_r != std::string::npos)
            pos = pos_r;

         if (pos == std::string::npos)
            break;

         std::string line = accumulated.substr(0, pos);
         accumulated.erase(0, pos + 1);

         if (!line.empty())
         {
            {
               std::lock_guard<std::mutex> lock(m_logMutex);
               m_logQueue.push(line);
            }
            ParseLogLine(line);
         }
      }

      return true; // Keep reading
   });

   m_running = false;
}

void FFmpegRunner::ParseLogLine(const std::string& line)
{
   std::string_view sv(line);

   // 1. Parse progress
   if (sv.find("frame=") != std::string_view::npos &&
       sv.find("time=") != std::string_view::npos)
   {
      std::lock_guard<std::mutex> lock(m_progressMutex);

      // Helper lambda to find a key, skip spaces, and extract a number directly
      auto extractNum = [sv](std::string_view key, auto& outValue) {
         size_t pos = sv.find(key);
         if (pos != std::string_view::npos)
         {
            pos += key.length();
            // FFmpeg pads with spaces (e.g., "frame=  123")
            while (pos < sv.length() && sv[pos] == ' ')
            {
               pos++;
            }

            if (pos < sv.length())
            {
               std::from_chars(sv.data() + pos, sv.data() + sv.length(), outValue);
            }
         }
      };

      extractNum("frame=", m_progress.frame);
      extractNum("fps=", m_progress.fps);
      extractNum("bitrate=", m_progress.bitrate);
      extractNum("speed=", m_progress.speed);

      // Extract the time string (e.g., "time=00:00:04.10")
      constexpr std::string_view timeKey = "time=";
      size_t timePos = sv.find(timeKey);
      if (timePos != std::string_view::npos)
      {
         timePos += timeKey.length();

         // Find the end of the time string (next space or end of line)
         size_t endPos = sv.find(' ', timePos);
         if (endPos == std::string_view::npos)
         {
            endPos = sv.length();
         }

         m_progress.time = std::string(sv.substr(timePos, endPos - timePos));
      }
   }

   // 2. Parse VMAF
   constexpr std::string_view vmafKey = "VMAF score:";
   size_t vmafPos = sv.find(vmafKey);
   if (vmafPos != std::string_view::npos)
   {
      vmafPos += vmafKey.length();

      // Skip any spaces between the colon and the number
      while (vmafPos < sv.length() && sv[vmafPos] == ' ')
      {
         vmafPos++;
      }

      if (vmafPos < sv.length())
      {
         // Parse into a temporary raw float
         float tempScore = 0.0f;
         auto [ptr, ec] = std::from_chars(sv.data() + vmafPos, sv.data() + sv.length(), tempScore);

         // Only assign to the atomic if the parse was successful
         if (ec == std::errc())
         {
            m_vmafScore = tempScore;
         }
      }
   }
}

VideoMetadata FFmpegRunner::GetMetadata(const std::string& filepath)
{
   VideoMetadata meta;

   std::string cmd =
      "ffprobe -v quiet -print_format json -show_format -show_streams \"" +
      filepath + "\"";

   std::string result = "";

   ExecuteCommand(cmd, [&result](const char* data, size_t size) -> bool {
      result.append(data, size);
      return true; // Keep reading until EOF
   });

   if (result.empty())
      return meta;

   try
   {
      json j = json::parse(result);

      // Find video stream
      if (j.contains("streams") && j["streams"].is_array())
      {
         for (const auto& stream : j["streams"])
         {
            if (stream["codec_type"] == "video")
            {
               meta.codec = stream.value("codec_name", "");
               meta.pix_fmt = stream.value("pix_fmt", "");
               std::string bpp_str = stream.value("bits_per_raw_sample", "0");
               if (bpp_str == "0")
               {
                  // Fallback: some codecs don't report bits_per_raw_sample directly
                  if (meta.pix_fmt.find("10") != std::string::npos)
                     meta.bit_depth = 10;
                  else if (meta.pix_fmt.find("12") != std::string::npos)
                     meta.bit_depth = 12;
                  else
                     meta.bit_depth = 8;
               }
               else
               {
                  meta.bit_depth = std::stoi(bpp_str);
               }
               meta.width = stream.value("width", 0);
               meta.height = stream.value("height", 0);

               std::string r_frame_rate = stream.value("r_frame_rate", "0/1");
               size_t slash_pos = r_frame_rate.find('/');
               if (slash_pos != std::string::npos)
               {
                  float num = std::stof(r_frame_rate.substr(0, slash_pos));
                  float den = std::stof(r_frame_rate.substr(slash_pos + 1));
                  if (den != 0.0f)
                     meta.framerate = num / den;
               }
               meta.valid = true;
               break;
            }
         }
      }

      if (j.contains("format"))
      {
         std::string dur_str = j["format"].value("duration", "0.0");
         meta.duration = std::stof(dur_str);
      }
   }
   catch (const std::exception& e)
   {
      std::cerr << "JSON parse error in ffprobe output: " << e.what()
         << std::endl;
   }

   return meta;
}

std::optional<std::string> FFmpegRunner::GetFFmpegVersion()
{
   std::string versionOutput;
   // We use "ffmpeg -version" and capture output
   bool success = ExecuteCommand("ffmpeg -version", [&versionOutput](const char* data, size_t size) -> bool {
      versionOutput.append(data, size);
      return true; // Keep reading
   });

   if (!success || versionOutput.empty())
   {
      return std::nullopt;
   }

   // The string starts with "ffmpeg version X.X.X ..."
   std::string_view sv(versionOutput);
   constexpr std::string_view prefix = "ffmpeg version ";

   size_t startPos = sv.find(prefix);
   if (startPos == std::string_view::npos)
   {
      return std::nullopt;
   }

   startPos += prefix.length();
   size_t endPos = sv.find(' ', startPos);

   if (endPos == std::string_view::npos)
   {
      endPos = sv.length();
   }

   return std::string(sv.substr(startPos, endPos - startPos));
}
