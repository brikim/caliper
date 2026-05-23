#include "ffmpeg_runner.h"

#include <array>
#include <cstdio>
#include <iostream>
#include <regex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

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
   m_stopRequested = false;
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

   m_worker = std::thread(&FFmpegRunner::RunThread, this, full_cmd);
   return true;
}

void FFmpegRunner::Stop()
{
   if (m_running)
   {
      m_stopRequested = true;
#ifdef _WIN32
      // Forcefully kill ffmpeg instances on Windows to unblock ReadFile.
      // We use ExecuteCommand to avoid the visible console window that system() creates.
      ExecuteCommand("taskkill /IM ffmpeg.exe /F /T", [](const char*, size_t) -> bool {
         return true; // Ignore the output, just let the command finish
      });
#endif
   }
   if (m_worker.joinable())
   {
      m_worker.join();
   }
}

bool FFmpegRunner::IsRunning() const
{
   return m_running;
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

   PROCESS_INFORMATION pi = {0};

   std::string shellCmd = "cmd.exe /c \"" + command + "\"";
   std::vector<char> cmdBuffer(shellCmd.begin(), shellCmd.end());
   cmdBuffer.push_back('\0');

   if (!CreateProcessA(NULL, cmdBuffer.data(), NULL, NULL, TRUE,
                       CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
   {
      CloseHandle(hReadPipe);
      CloseHandle(hWritePipe);
      return false;
   }

   CloseHandle(hWritePipe);

   std::array<char, 4096> buffer;
   DWORD bytesRead;

   while (ReadFile(hReadPipe, buffer.data(), buffer.size(), &bytesRead, NULL) && bytesRead > 0)
   {
      // If the callback returns false, we kill the process early
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

void FFmpegRunner::RunThread(std::string command)
{
   std::string accumulated;

   ExecuteCommand(command, [this, &accumulated](const char* data, size_t size) -> bool {
      // Return false to tell ExecuteCommand to terminate the process
      if (m_stopRequested)
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
   // 1. Parse progress: "frame=  123 fps= 30 q=... size=... time=00:00:04.10
   // bitrate=123.4kbits/s speed=1.2x"
   if (line.find("frame=") != std::string::npos &&
       line.find("time=") != std::string::npos)
   {
      std::lock_guard<std::mutex> lock(m_progressMutex);

      // Very basic parsing using regex (could be optimized)
      std::smatch match;
      if (std::regex_search(line, match, std::regex(R"(frame=\s*(\d+))")))
      {
         m_progress.frame = std::stoi(match[1].str());
      }
      if (std::regex_search(line, match, std::regex(R"(fps=\s*([\d.]+))")))
      {
         m_progress.fps = std::stof(match[1].str());
      }
      if (std::regex_search(line, match, std::regex(R"(time=([\d:.]+))")))
      {
         m_progress.time = match[1].str();
      }
      if (std::regex_search(line, match,
                            std::regex(R"(bitrate=\s*([\d.]+)kbits/s)")))
      {
         m_progress.bitrate = std::stof(match[1].str());
      }
      if (std::regex_search(line, match, std::regex(R"(speed=\s*([\d.]+)x)")))
      {
         m_progress.speed = std::stof(match[1].str());
      }
   }

   // 2. Parse VMAF: "[libvmaf @ 0000021c...] VMAF score: 95.1234"
   if (line.find("VMAF score:") != std::string::npos)
   {
      std::smatch match;
      if (std::regex_search(line, match,
                            std::regex(R"(VMAF score:\s*([\d.]+))")))
      {
         m_vmafScore = std::stof(match[1].str());
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
