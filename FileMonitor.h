/**
 * @file FileMonitor.h
 * @brief Заголовочный файл класса FileMonitor
 */

#ifndef FILE_MONITOR_H
#define FILE_MONITOR_H

#include <string>
#include <ctime>
#include <mutex>
#include <stdexcept>

/**
 * @class FileMonitor
 * @brief Класс для отслеживания состояния  файла
 * @details Хранит эталонные параметры файла и сравнивает их с текущими
 * для обнаружения модификаций, удаления или блокировки файла
 */
class FileMonitor {
 private:
  /** @brief Путь к отслеживаемому файлу */
  std::string filePath;

  /** @brief Путь к лог-файлу */
  std::string logFilePath;

  /** @brief Эталонный SHA-256 хеш файла */
  std::string lastHash;

  /** @brief Эталонный размер файла в байтах */
  uintmax_t lastSize;

  /** @brief Эталонное время последнего изменения файла */
  std::time_t lastModified;

  /** @brief Флаг существования файла на диске */
  bool fileExists;

  /** @brief Текущий статус файла */
  std::string currentStatus;

  /**
   * @brief Вычисляет SHA-256 хеш файла с помощью Windows CryptoAPI
   * @param path Путь к файлу для вычисления хеша
   * @return std::string Хеш-сумма в шестнадцатеричном формате или пустая строка
   * при ошибке
   */
  std::string calculateSHA256(const std::string& path);

  /**
   * @brief Записывает информацию о событии в лог-файл
   * @param message Текст сообщения
   * @throws std::runtime_error Если лог-файл недоступен для записи
   */
  void writeToLog(const std::string& message);

  /**
   * @brief Форматирует время типа std::time_t в читаемую строку
   * @param t Время
   * @return std::string Строка формата "ДД.ММ.ГГГГ ЧЧ:ММ:СС"
   */
  std::string formatTime(std::time_t t) const;

 public:
  /**
   * @brief Конструктор класса FileMonitor
   * @param path Путь к файлу
   * @param logPath Путь к лог-файлу
   */
  FileMonitor(const std::string& path, const std::string& logPath);

  /**
   * @brief Выполняет первичный осмотр файла и записывает эталонные данные
   * @param consoleMtx Мьютекс для синхронизации вывода в консоль
   * @param silent Флаг тихого режима
   * @throws std::runtime_error Если файл не существует или недоступен для
   * чтения
   */
  void initialize(std::mutex& consoleMtx, bool silent = false);

  /**
   * @brief Выполняет проверку состояния файла
   * @details Сравнивает текущие размер и SHA-256 с эталонными. При обнаружении
   * изменений обновляет эталоны и делает запись в лог
   * @param consoleMtx Мьютекс для синхронизации вывода в консоль
   */
  void checkFile(std::mutex& consoleMtx);

  /**
   * @brief Возвращает путь к наблюдаемому файлу
   * @return std::string Путь к файлу
   */
  std::string getFilePath() const { return filePath; }

  /**
   * @brief Возвращает текущий статус файла
   * @return std::string Статус файла
   */
  std::string getStatus() const { return currentStatus; }

  /**
   * @brief Устанавливает статус файла
   * @param status Новый статус
   */
  void setStatus(const std::string& status) { currentStatus = status; }

  /**
   * @brief Возвращает хеш файла
   * @return std::string SHA-256 хеш
   */
  std::string getHash() const { return lastHash; }

  /**
   * @brief Возвращает размер файла
   * @return uintmax_t Размер в байтах
   */
  uintmax_t getSize() const { return lastSize; }

  /**
   * @brief Возвращает отформатированную дату последнего изменения
   * @return std::string Строка с датой и временем
   */
  std::string getLastModifiedStr() const { return formatTime(lastModified); }
};

#endif  // FILE_MONITOR_H