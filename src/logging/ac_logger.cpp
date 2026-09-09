#include "ac_logger.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>

void ACLogger::Init(const std::string& log_directory) {
    m_LogDir = log_directory;
    std::string filepath = m_LogDir + "/anticheat_" + GetCurrentDate() + ".log";
    m_LogFile.open(filepath, std::ios::app);
}

ACLogger::~ACLogger() {
    if (m_LogFile.is_open()) {
        m_LogFile.close();
    }
}

std::string ACLogger::GetCurrentDate() {
    auto now = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t_c), "%Y-%m-%d");
    return ss.str();
}

std::string ACLogger::GetCurrentTime() {
    auto now = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&t_c), "%H:%M:%S");
    return ss.str();
}

void ACLogger::Log(LogLevel level, const char* format, va_list args) {
    std::lock_guard<std::mutex> lock(m_Mutex);

    std::string prefix;
    std::string color;
    std::string reset_color = "\033[0m";

    switch (level) {
        case LogLevel::DEBUG:    prefix = "[DEBUG]";    color = "\033[36m"; break; // Cyan
        case LogLevel::INFO:     prefix = "[INFO]";     color = "\033[32m"; break; // Green
        case LogLevel::WARNING:  prefix = "[WARN]";     color = "\033[33m"; break; // Yellow
        case LogLevel::ALERT:    prefix = "[ALERT]";    color = "\033[35m"; break; // Magenta
        case LogLevel::CRITICAL: prefix = "[CRITICAL]"; color = "\033[31m"; break; // Red
    }

    std::vector<char> buffer(2048);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    std::string message(buffer.data());

    std::string console_msg = color + "[anticheat] " + prefix + " " + message + reset_color;
    std::string file_msg = "[" + GetCurrentTime() + "] [anticheat] " + prefix + " " + message;

    // Output to console
    std::cout << console_msg << std::endl;

    // Output to file
    if (m_LogFile.is_open()) {
        m_LogFile << file_msg << std::endl;
    }
}

void ACLogger::Debug(const char* format, ...) {
    va_list args; va_start(args, format); Log(LogLevel::DEBUG, format, args); va_end(args);
}
void ACLogger::Info(const char* format, ...) {
    va_list args; va_start(args, format); Log(LogLevel::INFO, format, args); va_end(args);
}
void ACLogger::Warning(const char* format, ...) {
    va_list args; va_start(args, format); Log(LogLevel::WARNING, format, args); va_end(args);
}
void ACLogger::Alert(const char* format, ...) {
    va_list args; va_start(args, format); Log(LogLevel::ALERT, format, args); va_end(args);
}
void ACLogger::Critical(const char* format, ...) {
    va_list args; va_start(args, format); Log(LogLevel::CRITICAL, format, args); va_end(args);
}
