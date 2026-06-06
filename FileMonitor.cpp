/**
 * @file FileMonitor.cpp
 * @brief Реализация класса FileMonitor
 */

#include "FileMonitor.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <windows.h>
#include <wincrypt.h>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

/**
 * @brief Преобразует время файловой системы в стандартное время
 * @param p Нормализованный путь к файлу
 * @return time_t Время последнего изменения файла в секундах с эпохи Unix
 */
time_t getFileTime(const fs::path& p) {
  auto ftime = fs::last_write_time(p);
  auto sys_time = chrono::time_point_cast<chrono::system_clock::duration>(
      ftime - fs::file_time_type::clock::now() + chrono::system_clock::now());
  return chrono::system_clock::to_time_t(sys_time);
}

FileMonitor::FileMonitor(const string& path, const string& logPath) {
  filePath = fs::u8path(path).generic_string();
  logFilePath = fs::u8path(logPath).generic_string();
  lastHash = "";
  lastSize = 0;
  lastModified = 0;
  fileExists = false;
  currentStatus = "Ожидание";
}

string FileMonitor::formatTime(time_t t) const {
  if (t == 0) return "-";
  tm* timeInfo = localtime(&t);
  stringstream ss;
  ss << put_time(timeInfo, "%d.%m.%Y %H:%M:%S");
  return ss.str();
}

string FileMonitor::calculateSHA256(const string& path) {
  HCRYPTPROV hProv = 0;
  HCRYPTHASH hHash = 0;
  string result = "";

  if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES,
                          CRYPT_VERIFYCONTEXT)) {
    if (CryptCreateHash(hProv, CALG_SHA_256, 0, 0, &hHash)) {
      ifstream file(fs::u8path(path), ios::binary);
      if (file) {
        char buffer[8192];
        while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
          CryptHashData(hHash, (BYTE*)buffer, (DWORD)file.gcount(), 0);
        }
        file.close();

        DWORD hashLen = 32;
        BYTE hashBytes[32];
        if (CryptGetHashParam(hHash, HP_HASHVAL, hashBytes, &hashLen, 0)) {
          stringstream ss;
          for (DWORD i = 0; i < hashLen; i++) {
            ss << hex << setw(2) << setfill('0') << (int)hashBytes[i];
          }
          result = ss.str();
        }
      }
      CryptDestroyHash(hHash);
    }
    CryptReleaseContext(hProv, 0);
  }
  return result;
}

void FileMonitor::writeToLog(const string& message) {
  bool logExists = fs::exists(fs::u8path(logFilePath));
  ofstream logFile(fs::u8path(logFilePath), ios::app);

  if (!logFile) throw runtime_error("Не удалось открыть лог: " + logFilePath);
  if (!logExists) logFile << (char)0xEF << (char)0xBB << (char)0xBF;

  logFile << "[" << formatTime(time(nullptr)) << "] " << message << endl;
}

void FileMonitor::initialize(mutex& consoleMtx, bool silent) {
  fs::path p = fs::u8path(filePath);

  if (!fs::exists(p)) {
    currentStatus = "Ошибка (Не найден)";
    throw runtime_error("Файл не существует: " + filePath);
  }

  lastSize = fs::file_size(p);
  lastModified = getFileTime(p);
  lastHash = calculateSHA256(filePath);
  fileExists = true;
  currentStatus = "Ок";

  writeToLog("Добавлен: " + filePath + " | SHA-256: " + lastHash);

  if (!silent) {
    lock_guard<mutex> lock(consoleMtx);
    cout << "\nФайл успешно добавлен!\nПуть: " << filePath << endl;
  }
}

void FileMonitor::checkFile(mutex& consoleMtx) {
  fs::path p = fs::u8path(filePath);
  bool currentlyExists = fs::exists(p);

  if (fileExists && !currentlyExists) {
    currentStatus = "Удален/Перемещен";
    writeToLog("УДАЛЕН: " + filePath);
    lock_guard<mutex> lock(consoleMtx);
    cout << "\n\n[ВНИМАНИЕ] ФАЙЛ УДАЛЕН/ПЕРЕМЕЩЁН: " << filePath << "\n> ";
    fileExists = false;
    return;
  }

  if (!fileExists && currentlyExists) {
    currentStatus = "Ок (Восстановлен)";
    writeToLog("ВОССТАНОВЛЕН: " + filePath);
    lock_guard<mutex> lock(consoleMtx);
    cout << "\n\n[ВНИМАНИЕ] ФАЙЛ ВОССТАНОВЛЕН: " << filePath << "\n> ";

    lastSize = fs::file_size(p);
    lastModified = getFileTime(p);
    lastHash = calculateSHA256(filePath);
    fileExists = true;
    return;
  }

  if (currentlyExists) {
    uintmax_t currentSize;
    try {
      currentSize = fs::file_size(p);
    } catch (...) {
      return;
    }

    string currentHash = calculateSHA256(filePath);
    if (currentHash.empty()) return;

    if (currentSize != lastSize || currentHash != lastHash) {
      currentStatus = "Изменен";
      writeToLog("ИЗМЕНЕН: " + filePath);

      lock_guard<mutex> lock(consoleMtx);
      cout << "\n\n[ВНИМАНИЕ] ФАЙЛ ИЗМЕНЕН: " << filePath << "\n> ";

      lastSize = currentSize;
      lastHash = currentHash;
      lastModified = getFileTime(p);
    }
  }
}