#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <cstdarg>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ALERT,
    CRITICAL
};

class ACLogger {
public:
    static ACLogger& GetInstance() {
        static ACLogger instance;
        return instance;
    }

    void Init(const std::string& log_directory);
    
    void Debug(const char* format, ...);
    void Info(const char* format, ...);
    void Warning(const char* format, ...);
    void Alert(const char* format, ...);
    void Critical(const char* format, ...);

private:
    ACLogger() = default;
    ~ACLogger();

    void Log(LogLevel level, const char* format, va_list args);
    std::string GetCurrentDate();
    std::string GetCurrentTime();

    std::mutex m_Mutex;
    std::ofstream m_LogFile;
    std::string m_LogDir;
};
