/**
 * @file main.cpp
 * @brief Главный файл программы мониторинга файлов.
 */

#include "FileMonitor.h"
#include <iostream>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <thread>
#include <vector>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <memory>

using namespace std;
namespace fs = std::filesystem;

/** @brief Вектор уникальных указателей на объекты мониторинга файлов */
vector<unique_ptr<FileMonitor>> monitors;

/** @brief Вектор строк с путями к папкам, внутри которых отслеживается
 * появление новых файлов */
vector<string> monitoredFolders;

/** @brief Вектор путей к файлам, исключенным из мониторинга пользователем */
vector<string> ignoredFiles;

/** @brief Мьютекс для защиты операций изменения векторов мониторинга */
mutex listMtx;

/** @brief Мьютекс для защиты потока стандартного вывода */
mutex consoleMtx;

/** @brief Флаг работы программы */
bool appRunning = true;

/** @brief Путь к лог-файлу */
string logPath;

/**
 * @brief Получает текущее системное время в виде отформатированной строки
 * @return std::string Строка времени формата "ДД.ММ.ГГГГ ЧЧ:ММ:СС"
 */
string getAppCurrentTime() {
  time_t now = time(nullptr);
  stringstream ss;
  ss << put_time(localtime(&now), "%d.%m.%Y %H:%M:%S");
  return ss.str();
}

/**
 * @brief Нормализует путь файла (заменяет обратные слеши на прямые)
 * @param p Исходный путь
 * @return std::string Нормализованный путь формата UTF-8
 */
string normalizePath(const string& p) {
  if (p.empty()) return "";
  return fs::u8path(p).generic_string();
}

/**
 * @brief Конвертирует строку формата Windows UTF-16 в UTF-8
 * @param wstr Исходная широкая строка
 * @return std::string Конвертированная и нормализованная строка UTF-8
 */
string wstringToUtf8(const wstring& wstr) {
  if (wstr.empty()) return "";
  int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], -1, NULL, 0, NULL, NULL);
  string result(size - 1, 0);
  WideCharToMultiByte(CP_UTF8, 0, &wstr[0], -1, &result[0], size, NULL, NULL);
  return normalizePath(result);
}

/**
 * @brief Вычисляет абсолютный путь к папке, в которой находится запущенный
 * исполняемый файл
 * @return std::string Путь к папке программы с закрывающим слешем
 */
string getExeDirectory() {
  wchar_t buffer[MAX_PATH];
  GetModuleFileNameW(NULL, buffer, MAX_PATH);
  string fullPath = wstringToUtf8(buffer);
  size_t pos = fullPath.find_last_of("/");
  return fullPath.substr(0, pos) + "/";
}

/**
 * @brief Открывает системное диалоговое окно для выбора файла пользователем
 * @return std::string Путь к выбранному файлу (UTF-8) или пустая строка в
 * случае отмены
 */
string selectFile() {
  OPENFILENAMEW ofn = {0};
  wchar_t fileName[MAX_PATH] = L"";
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFile = fileName;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrFilter = L"Все файлы (*.*)\0*.*\0";
  ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

  if (GetOpenFileNameW(&ofn)) return wstringToUtf8(fileName);
  return "";
}

/**
 * @brief Открывает системное диалоговое окно для выбора папки пользователем
 * @return std::string Путь к выбранной папке (UTF-8) или пустая строка в случае
 * отмены
 */
string selectFolder() {
  BROWSEINFOW bi = {0};
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  bi.lpszTitle = L"Выберите папку для мониторинга";
  LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);

  if (pidl != 0) {
    wchar_t path[MAX_PATH];
    if (SHGetPathFromIDListW(pidl, path)) return wstringToUtf8(path);
  }
  return "";
}

/**
 * @brief Проверяет, отслеживается ли указанный файл в данный момент
 * @param path Путь к файлу для проверки
 * @return true Если файл найден в векторе monitors
 * @return false Если файл не отслеживается
 */
bool isFileMonitored(const string& path) {
  string normPath = normalizePath(path);
  for (const auto& m : monitors) {
    if (m->getFilePath() == normPath) return true;
  }
  return false;
}

/**
 * @brief Проверяет файл на принадлежность к мусорным, системным или
 * игнорируемым
 * @param path Полный путь к файлу
 * @param filename Имя файла с расширением
 * @return true Если файл пригоден для мониторинга
 * @return false Если файл является скрытым, системным или находится в черном
 * списке
 */
bool isValidFile(const string& path, const string& filename) {
  string normPath = normalizePath(path);
  for (const string& ignored : ignoredFiles) {
    if (normPath == ignored) return false;
  }

  if (filename.find("~$") == 0 || fs::path(filename).extension() == ".tmp")
    return false;
  if (filename.find(".") == 0 && filename.length() > 1) return false;

  DWORD attr = GetFileAttributesA(path.c_str());
  if (attr != INVALID_FILE_ATTRIBUTES &&
      (attr & FILE_ATTRIBUTE_HIDDEN || attr & FILE_ATTRIBUTE_SYSTEM))
    return false;

  return true;
}

/**
 * @brief Генерирует HTML-отчет со списком наблюдаемых файлов и их статусами
 * @details Отчет сохраняется в папку с исполняемым файлом под именем
 * report.html
 * @throws std::runtime_error Если не удается создать или записать файл отчета
 */
void generateHTMLReport() {
  lock_guard<mutex> listLock(listMtx);
  string reportPath = getExeDirectory() + "report.html";
  ofstream report(fs::u8path(reportPath));

  if (!report) throw runtime_error("Невозможно создать HTML-отчет");

  report << (char)0xEF << (char)0xBB << (char)0xBF;
  report << "<!DOCTYPE html>\n<html>\n<head>\n";
  report << "<meta charset=\"UTF-8\">\n";
  report << "<title>Отчет мониторинга</title>\n";
  report << "</head>\n<body>\n";

  report << "<h1>Отчет о состоянии файлов</h1>\n";
  report << "<p><b>Время формирования:</b> " << getAppCurrentTime() << "<br>\n";
  report << "<b>Всего файлов:</b> " << monitors.size() << "</p>\n";

  report << "<table border=\"1\" cellpadding=\"5\" cellspacing=\"0\">\n";
  report << "<tr><th>Путь к файлу</th><th>Статус</th><th>Размер "
            "(байт)</th><th>Изменен</th><th>SHA-256</th></tr>\n";

  for (const auto& m : monitors) {
    string st = m->getStatus();
    string color = "green";
    if (st.find("Удален") != string::npos || st.find("Ошибка") != string::npos)
      color = "red";
    else if (st.find("Изменен") != string::npos)
      color = "orange";

    report << "<tr>\n<td>" << m->getFilePath() << "</td>\n";
    report << "<td style=\"color: " << color << "; font-weight: bold;\">" << st
           << "</td>\n";
    report << "<td>" << m->getSize() << "</td>\n<td>" << m->getLastModifiedStr()
           << "</td>\n";
    report << "<td>" << m->getHash() << "</td>\n</tr>\n";
  }

  report << "</table>\n</body>\n</html>";
  report.close();
  cout << "\nHTML-отчет успешно создан!\nПуть: " << reportPath << endl;
}

/**
 * @brief Управляет добавлением и удалением программы из реестра автозапуска
 * Windows
 * @throws std::runtime_error В случае отказа в доступе к реестру
 */
void toggleStartup() {
  HKEY hKey;
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);

  if (RegOpenKeyExA(HKEY_CURRENT_USER,
                    "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_ALL_ACCESS, &hKey) != ERROR_SUCCESS)
    throw runtime_error("Нет доступа к реестру");

  if (RegQueryValueExA(hKey, "FileMonitor_App", NULL, NULL, NULL, NULL) ==
      ERROR_SUCCESS) {
    RegDeleteValueA(hKey, "FileMonitor_App");
    cout << "\nАвтозапуск выключен | OFF" << endl;
  } else {
    RegSetValueExA(hKey, "FileMonitor_App", 0, REG_SZ, (BYTE*)exePath,
                   strlen(exePath) + 1);
    cout << "\nАвтозапуск включён | ON" << endl;
  }
  RegCloseKey(hKey);
}

/**
 * @brief Рабочая функция фонового потока
 */
void backgroundMonitoringThread() {
  while (appRunning) {
    this_thread::sleep_for(chrono::seconds(2));
    try {
      lock_guard<mutex> lock(listMtx);

      for (const auto& m : monitors) m->checkFile(consoleMtx);

      int newlyAddedCount = 0;
      string lastAddedPath = "";

      for (const string& folder : monitoredFolders) {
        for (const auto& entry : fs::recursive_directory_iterator(
                 fs::u8path(folder),
                 fs::directory_options::skip_permission_denied)) {
          if (entry.is_regular_file()) {
            string path = normalizePath(entry.path().u8string());
            string filename = entry.path().filename().u8string();

            if (isValidFile(path, filename) && !isFileMonitored(path)) {
              try {
                auto newM = make_unique<FileMonitor>(path, logPath);
                newM->initialize(consoleMtx, true);
                newM->setStatus("Новый (Создан)");
                monitors.push_back(std::move(newM));

                newlyAddedCount++;
                lastAddedPath = path;
              } catch (...) {
                lock_guard<mutex> lock(consoleMtx);
                cout << "\n[ОШИБКА ДОСТУПА]: " << filename << endl;
              }
            }
          }
        }
      }

      if (newlyAddedCount == 1) {
        lock_guard<mutex> cLock(consoleMtx);
        cout << "\n\n[НОВЫЙ ФАЙЛ] " << lastAddedPath << "\n> Команда (1-8): ";
      } else if (newlyAddedCount > 1) {
        lock_guard<mutex> cLock(consoleMtx);
        cout << "\n\n[НОВАЯ ПАПКА / МАССОВОЕ СОЗДАНИЕ] Добавлено файлов: "
             << newlyAddedCount << "\n> Команда (1-8): ";
      }
    } catch (...) {
      lock_guard<mutex> lock(consoleMtx);
      cout << "\n[ОШИБКА МОНИТОРИНГА]" << endl;
    }
  }
}

/**
 * @brief Выводит текстовое меню пользователя в консоль с использованием
 * блокировки мьютекса
 */
void showMenu() {
  lock_guard<mutex> lock(consoleMtx);
  cout << "\n1. Добавить файл  | 2. Добавить папку | 3. Список файлов\n";
  cout << "4. Очистить список| 5. HTML-отчет     | 6. Автозапуск\n";
  cout << "7. Удалить объект | 8. Выход\n> Команда (1-8): ";
}

/**
 * @brief Точка входа в программу
 * @details Настраивает кодировки, парсит аргументы командной строки,
 * инициализирует лог, запускает фоновый поток мониторинга и начинает цикл
 * обработки команд пользователя
 * @return int Код завершения программы (0 при успехе, 1 при фатальной ошибке)
 */

#ifndef TESTING
int main() {
  CoInitialize(NULL);
  SetConsoleOutputCP(CP_UTF8);
  setlocale(LC_ALL, "ru_RU.UTF-8");

  logPath = getExeDirectory() + "log.txt";

  cout << "==================================================" << endl;
  cout << "     Система мониторинга целостности файлов       " << endl;
  cout << "==================================================" << endl;

  try {
    ofstream testLog(fs::u8path(logPath), ios::app);
    if (!testLog) throw runtime_error("Нет прав на создание файла лога");
    testLog << "\n--- Запуск программы мониторинга ---" << endl;
    testLog.close();
    cout << "[OK] Лог-файл: " << logPath << endl;

  } catch (const exception& e) {
    cout << "[ОШИБКА ЗАПУСКА] " << e.what() << endl;
    return 1;
  }

  thread bgThread(backgroundMonitoringThread);

  int choice;
  while (appRunning) {
    showMenu();
    if (!(cin >> choice)) {
      cin.clear();
      cin.ignore(1000, '\n');
      continue;
    }

    try {
      if (choice == 1) {
        string path = selectFile();
        if (!path.empty() && !isFileMonitored(path)) {
          auto m = make_unique<FileMonitor>(path, logPath);
          m->initialize(consoleMtx);
          lock_guard<mutex> listLock(listMtx);
          monitors.push_back(std::move(m));
        } else if (isFileMonitored(path)) {
          lock_guard<mutex> lock(consoleMtx);
          cout << "Этот файл уже находится под наблюдением" << endl;
        }
      } else if (choice == 2) {
        string folder = selectFolder();
        if (!folder.empty()) {
          lock_guard<mutex> lock(consoleMtx);
          cout << "Запуск сканирования папки..." << endl;

          lock_guard<mutex> listLock(listMtx);
          monitoredFolders.push_back(folder);

          int addedCount = 0;
          for (const auto& entry : fs::recursive_directory_iterator(
                   fs::u8path(folder),
                   fs::directory_options::skip_permission_denied)) {
            if (entry.is_regular_file()) {
              string path = normalizePath(entry.path().u8string());
              string filename = entry.path().filename().u8string();

              if (isValidFile(path, filename) && !isFileMonitored(path)) {
                try {
                  auto newM = make_unique<FileMonitor>(path, logPath);
                  newM->initialize(consoleMtx, true);
                  monitors.push_back(std::move(newM));
                  addedCount++;
                } catch (...) {
                  lock_guard<mutex> lock(consoleMtx);
                  cout << "\n[ОШИБКА ДОСТУПА]: " << filename << endl;
                }
              }
            }
          }
          cout << "Папка добавлена в мониторинг. Файлов: " << addedCount
               << endl;
        }
      } else if (choice == 3) {
        lock_guard<mutex> lock(consoleMtx);
        if (monitors.empty())
          cout << "Список пуст.\n";
        else
          for (size_t i = 0; i < monitors.size(); i++)
            cout << i + 1 << ". " << monitors[i]->getFilePath()
                 << " | Состояние: " << monitors[i]->getStatus() << "\n";
      } else if (choice == 4) {
        lock_guard<mutex> lock(consoleMtx);
        lock_guard<mutex> listLock(listMtx);
        int removed = 0;
        for (auto it = monitors.begin(); it != monitors.end();) {
          if ((*it)->getStatus() == "Удален/Перемещен") {
            it = monitors.erase(it);
            removed++;
          } else
            ++it;
        }
        cout << "\nСписок очищен. Удалено файлов-призраков: " << removed
             << "\n";
      } else if (choice == 5) {
        generateHTMLReport();
      } else if (choice == 6) {
        lock_guard<mutex> lock(consoleMtx);
        toggleStartup();
      } else if (choice == 7) {
        lock_guard<mutex> lock(consoleMtx);
        if (monitors.empty() && monitoredFolders.empty()) {
          cout << "\nСписок мониторинга пуст.\n";
          continue;
        }

        cout << "\n--- Остановка мониторинта ---\n0. Отмена\n1. Файла\n2. "
                "Папки\n> Выбор: ";
        int rmChoice;
        if (!(cin >> rmChoice)) {
          cin.clear();
          cin.ignore(1000, '\n');
          continue;
        }

        lock_guard<mutex> listLock(listMtx);

        if (rmChoice == 1 && !monitors.empty()) {
          for (size_t i = 0; i < monitors.size(); i++)
            cout << i + 1 << ". " << monitors[i]->getFilePath() << "\n";
          cout << "Номер файла для удаления: ";
          int fNum;
          if (cin >> fNum && fNum > 0 && fNum <= monitors.size()) {
            ignoredFiles.push_back(monitors[fNum - 1]->getFilePath());
            monitors.erase(monitors.begin() + fNum - 1);
            cout << "Файл удален из мониторинга.\n";
          }
        } else if (rmChoice == 2 && !monitoredFolders.empty()) {
          for (size_t i = 0; i < monitoredFolders.size(); i++)
            cout << i + 1 << ". " << monitoredFolders[i] << "\n";
          cout << "Номер папки: ";
          int dNum;
          if (cin >> dNum && dNum > 0 && dNum <= monitoredFolders.size()) {
            string folderToRemove = monitoredFolders[dNum - 1];
            if (folderToRemove.back() != '/') folderToRemove += "/";
            monitoredFolders.erase(monitoredFolders.begin() + dNum - 1);

            int removedFiles = 0;
            for (auto it = monitors.begin(); it != monitors.end();) {
              if ((*it)->getFilePath().find(folderToRemove) == 0) {
                it = monitors.erase(it);
                removedFiles++;
              } else
                ++it;
            }
            cout << "Папка удалена! А также автоматически удалено "
                 << removedFiles << " файлов из неё.\n";
          }
        }
      } else if (choice == 8) {
        generateHTMLReport();
        lock_guard<mutex> lock(consoleMtx);
        cout << "\nОстановка фонового мониторинга..." << endl;
        appRunning = false;
      }
    } catch (const exception& e) {
      lock_guard<mutex> lock(consoleMtx);
      cout << "\n[ОШИБКА] " << e.what() << endl;
    }
  }

  bgThread.join();
  CoUninitialize();
  return 0;
}
#endif