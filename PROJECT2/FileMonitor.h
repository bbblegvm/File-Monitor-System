#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

#include <string>
#include <ctime>
#include <fstream>
#include <mutex>
#include <stdexcept>

class FileMonitor
{
private:
    std::string filePath;
    std::string logFilePatg;
    std::string lastHash;
    unsigned long long lastSize;
    std::time_t lastModified;
    bool fileExists;
    std::string currentStatus;
    bool isOpenedByOther;

    std::string calculateSHA256(const std::string &path);
    void writeToLong(const std::string &message);
    std::string getCurrentTime();

public:
    FileMonitor(const std::string &path, const std::string &logPath);
    void initialize(std::mutex &consoleMtx, bool silent = false);
    void checkFile(std::mutex &consoleMtx);
    std::string getFilePath() const { return filePath; }
    std::string getStatus() const { return currentStatus; }
    void setStatus(const std::string &status) { currentStatus = status; }
    std::string getHash() const { return lastHash; }
    unsigned long long getSize() const { return lastSize; }
    std::string getLastModifiedStr() const;
};

#endif
