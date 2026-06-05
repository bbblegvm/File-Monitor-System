#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#define TESTING
#include <doctest/doctest.h>
#include "main.cpp"

std::string createTestFile(const std::string& name,
                           const std::string& content) {
  std::string path = normalizePath(getExeDirectory() + name);
  std::ofstream f(fs::u8path(path));
  f << content;
  f.close();
  return path;
}

TEST_CASE("Настройка кодировки") {
  SetConsoleOutputCP(CP_UTF8);
  setlocale(LC_ALL, "ru_RU.UTF-8");
}

TEST_CASE("Тесты глобальных функций из main.cpp") {
  SUBCASE("normalizePath: замена слешей") {
    CHECK(normalizePath("C:\\Windows\\System32") == "C:/Windows/System32");
    CHECK(normalizePath("folder/subfolder/") == "folder/subfolder/");
  }

  SUBCASE("isValidFile: фильтрация системного мусора") {
    CHECK(isValidFile("C:/test.txt", "test.txt") == true);
    CHECK(isValidFile("C:/~$doc.docx", "~$doc.docx") == false);
    CHECK(isValidFile("C:/.gitignore", ".gitignore") == false);
  }

  SUBCASE("getAppCurrentTime: формат времени") {
    std::string t = getAppCurrentTime();
    CHECK(t.length() == 19);  // "ДД.ММ.ГГГГ ЧЧ:ММ:СС"
    CHECK(t[2] == '.');
    CHECK(t[10] == ' ');
  }
}

TEST_CASE("Тесты класса FileMonitor") {
  std::string log = getExeDirectory() + "test_log.txt";
  std::string filePath = createTestFile("monitor_me.txt", "initial content");
  std::mutex mtx;

  FileMonitor m(filePath, log);

  SUBCASE("Инициализация и хеширование") {
    m.initialize(mtx, true);
    CHECK(m.getStatus() == "Ок");
    CHECK(m.getSize() == 15);
    CHECK(m.getHash().length() == 64);  // SHA-256
  }

  SUBCASE("Обнаружение изменений") {
    m.initialize(mtx, true);
    std::string oldHash = m.getHash();

    std::ofstream f(fs::u8path(filePath));
    f << "new content";
    f.close();

    m.checkFile(mtx);
    CHECK(m.getStatus() == "Изменен");
    CHECK(m.getHash() != oldHash);
  }

  SUBCASE("Обнаружение удаления") {
    m.initialize(mtx, true);
    fs::remove(fs::u8path(filePath));

    m.checkFile(mtx);
    CHECK(m.getStatus() == "Удален/Перемещен");
  }

  fs::remove(fs::u8path(filePath));
  fs::remove(fs::u8path(log));
}

TEST_CASE("Сравнение хешей разных файлов") {
  std::string log = getExeDirectory() + "test_log.txt";
  std::mutex mtx;

  std::string f1 = createTestFile("f1.txt", "same");
  std::string f2 = createTestFile("f2.txt", "same");
  std::string f3 = createTestFile("f3.txt", "different");

  FileMonitor m1(f1, log);
  FileMonitor m2(f2, log);
  FileMonitor m3(f3, log);

  m1.initialize(mtx, true);
  m2.initialize(mtx, true);
  m3.initialize(mtx, true);

  CHECK(m1.getHash() == m2.getHash());
  CHECK(m1.getHash() != m3.getHash());

  fs::remove(fs::u8path(f1));
  fs::remove(fs::u8path(f2));
  fs::remove(fs::u8path(f3));
  fs::remove(fs::u8path(log));
}

TEST_CASE("isFileMonitored: поиск в векторе") {
  std::lock_guard<std::mutex> lock(listMtx);
  monitors.clear();
  monitoredFolders.clear();
  ignoredFiles.clear();
  monitors.push_back(make_unique<FileMonitor>("C:/test.txt", "log.txt"));
  CHECK(isFileMonitored("C:/test.txt") == true);
  CHECK(isFileMonitored("C:/fake.txt") == false);
}

TEST_CASE("Генерация отчета") {
  CHECK_NOTHROW(generateHTMLReport());
  CHECK(fs::exists(fs::u8path(getExeDirectory() + "report.html")));
}

TEST_CASE("Формат времени изменения") {
  std::mutex mtx;
  string f = createTestFile("time.txt", "data");
  FileMonitor m(f, "log.txt");
  m.initialize(mtx, true);
  CHECK(m.getLastModifiedStr().length() == 19);
}