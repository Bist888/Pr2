#include "BackupSystem.h"
#include <sstream>
#include <iomanip>
#include <mutex>
#include <algorithm>
#include <openssl/sha.h>

namespace {
    std::mutex objectsMutex;
    std::mutex restorePointsMutex;

    std::string calculateFileChecksum(const fs::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Не удалось открыть файл для подсчета контрольной суммы");
        }

        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        char buffer[4096];
        while (file.read(buffer, sizeof(buffer)).gcount() > 0) {
            SHA256_Update(&sha256, buffer, file.gcount());
        }

        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_Final(hash, &sha256);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
        }
        return ss.str();
    }
}

BackupObject::BackupObject(const fs::path& path) : path_(path) {
    if (path_.empty()) {
        throw std::invalid_argument("Путь не может быть пустым");
    }
    if (!path_.is_absolute()) {
        throw std::invalid_argument("Требуется абсолютный путь: " + path_.string());
    }
    storedChecksum_ = calculateChecksum();
}

const fs::path& BackupObject::getPath() const {
    return path_;
}

bool BackupObject::exists() const {
    std::error_code ec;
    bool exists = fs::exists(path_, ec);
    if (ec) {
        throw std::runtime_error("Ошибка при проверке существования файла: " + ec.message());
    }
    return exists;
}

std::string BackupObject::calculateChecksum() const {
    return calculateFileChecksum(path_);
}

bool BackupObject::verifyChecksum() const {
    if (!exists()) {
        return false;
    }
    return calculateChecksum() == storedChecksum_;
}

RestorePoint::RestorePoint(const std::vector<std::shared_ptr<BackupObject>>& objects,
                         const fs::path& location,
                         std::chrono::system_clock::time_point timestamp)
    : objects_(objects), location_(location), timestamp_(timestamp) {
    if (objects_.empty()) {
        throw std::invalid_argument("Список объектов не может быть пустым");
    }
    if (location_.empty()) {
        throw std::invalid_argument("Путь расположения не может быть пустым");
    }
}

const std::vector<std::shared_ptr<BackupObject>>& RestorePoint::getObjects() const {
    return objects_;
}

const fs::path& RestorePoint::getLocation() const {
    return location_;
}

std::chrono::system_clock::time_point RestorePoint::getTimestamp() const {
    return timestamp_;
}

bool RestorePoint::verifyIntegrity() const {
    for (const auto& obj : objects_) {
        if (!obj->verifyChecksum()) {
            return false;
        }
    }
    return true;
}

void RestorePoint::serialize(std::ostream& os) const {
    os << location_.string() << "\n";
    os << std::chrono::system_clock::to_time_t(timestamp_) << "\n";
    os << objects_.size() << "\n";
    for (const auto& obj : objects_) {
        os << obj->getPath().string() << "\n";
    }
}

std::shared_ptr<RestorePoint> RestorePoint::deserialize(std::istream& is) {
    std::string locationStr;
    std::getline(is, locationStr);
    
    std::time_t timeT;
    is >> timeT;
    is.ignore();

    size_t objectCount;
    is >> objectCount;
    is.ignore();

    std::vector<std::shared_ptr<BackupObject>> objects;
    for (size_t i = 0; i < objectCount; ++i) {
        std::string pathStr;
        std::getline(is, pathStr);
        objects.push_back(std::make_shared<BackupObject>(pathStr));
    }

    return std::make_shared<RestorePoint>(
        objects,
        fs::path(locationStr),
        std::chrono::system_clock::from_time_t(timeT)
    );
}

BackupJob::BackupJob(std::unique_ptr<IStorageStrategy> strategy, const fs::path& backupDir)
    : backupDirectory_(backupDir) {
    if (!strategy) {
        throw std::invalid_argument("Стратегия хранения не может быть nullptr");
    }
    storageStrategy_ = std::move(strategy);

    std::error_code ec;
    if (!fs::exists(backupDirectory_, ec)) {
        if (ec) {
            throw std::runtime_error("Ошибка при проверке директории резервных копий: " + ec.message());
        }
        if (!fs::create_directories(backupDirectory_, ec)) {
            throw std::runtime_error("Не удалось создать директорию для резервных копий: " + ec.message());
        }
    }
}

void BackupJob::addObject(const fs::path& path) {
    if (!fs::exists(path)) {
        throw std::runtime_error("Путь не существует: " + path.string());
    }

    auto newObject = std::make_shared<BackupObject>(path);
    
    std::lock_guard<std::mutex> lock(objectsMutex);
    auto it = std::find_if(objects_.begin(), objects_.end(),
                          [&path](const auto& obj) { return obj->getPath() == path; });
    if (it != objects_.end()) {
        throw std::runtime_error("Объект уже существует: " + path.string());
    }
    objects_.push_back(std::move(newObject));
}

void BackupJob::removeObject(const fs::path& path) {
    std::lock_guard<std::mutex> lock(objectsMutex);
    auto initialSize = objects_.size();
    objects_.erase(
        std::remove_if(objects_.begin(), objects_.end(),
                      [&path](const auto& obj) { return obj->getPath() == path; }),
        objects_.end());
    
    if (objects_.size() == initialSize) {
        throw std::runtime_error("Объект не найден: " + path.string());
    }
}

std::shared_ptr<RestorePoint> BackupJob::createRestorePoint() {
    std::vector<std::shared_ptr<BackupObject>> objectsCopy;
    {
        std::lock_guard<std::mutex> lock(objectsMutex);
        if (objects_.empty()) {
            throw std::runtime_error("Нет объектов для создания точки восстановления");
        }
        objectsCopy = objects_; // Создаем копию для безопасной работы
    }

    // Проверяем существование всех файлов перед созданием точки восстановления
    for (const auto& obj : objectsCopy) {
        if (!obj->exists()) {
            throw std::runtime_error("Файл больше не существует: " + obj->getPath().string());
        }
    }

    auto timestamp = std::chrono::system_clock::now();
    auto timeT = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << "restore_point_" << std::put_time(std::localtime(&timeT), "%Y%m%d_%H%M%S_%N");
    fs::path restorePointPath = backupDirectory_ / ss.str();

    // Создаем директорию для точки восстановления
    std::error_code ec;
    if (!fs::create_directories(restorePointPath, ec)) {
        throw std::runtime_error("Не удалось создать директорию для точки восстановления: " + ec.message());
    }

    try {
        storageStrategy_->store(objectsCopy, restorePointPath);
    } catch (const std::exception& e) {
        // В случае ошибки, пытаемся удалить созданную директорию
        fs::remove_all(restorePointPath, ec);
        throw std::runtime_error("Ошибка при сохранении точки восстановления: " + std::string(e.what()));
    }

    auto restorePoint = std::make_shared<RestorePoint>(objectsCopy, restorePointPath, timestamp);
    
    {
        std::lock_guard<std::mutex> lock(restorePointsMutex);
        restorePoints_.push_back(restorePoint);
    }

    return restorePoint;
}

const std::vector<std::shared_ptr<BackupObject>>& BackupJob::getObjects() const {
    std::lock_guard<std::mutex> lock(objectsMutex);
    return objects_;
}

const std::vector<std::shared_ptr<RestorePoint>>& BackupJob::getRestorePoints() const {
    std::lock_guard<std::mutex> lock(restorePointsMutex);
    return restorePoints_;
}

void BackupJob::restore(const RestorePoint& point, const fs::path& targetDir) {
    if (operationCancelled_) {
        throw std::runtime_error("Операция отменена пользователем");
    }

    if (!point.verifyIntegrity()) {
        throw std::runtime_error("Нарушена целостность точки восстановления");
    }

    std::error_code ec;
    if (!fs::exists(targetDir)) {
        fs::create_directories(targetDir, ec);
        if (ec) {
            throw std::runtime_error("Не удалось создать целевую директорию: " + ec.message());
        }
    }

    const auto& objects = point.getObjects();
    float progressStep = 1.0f / objects.size();
    float currentProgress = 0.0f;

    for (const auto& obj : objects) {
        if (operationCancelled_) {
            throw std::runtime_error("Операция отменена пользователем");
        }

        fs::path sourcePath = point.getLocation() / obj->getPath().filename();
        fs::path targetPath = targetDir / obj->getPath().filename();

        reportProgress(currentProgress, "Восстановление: " + obj->getPath().filename().string());

        fs::copy_file(sourcePath, targetPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("Ошибка при восстановлении файла: " + ec.message());
        }

        currentProgress += progressStep;
    }

    reportProgress(1.0f, "Восстановление завершено");
}

void BackupJob::saveState(const fs::path& statePath) const {
    std::ofstream file(statePath);
    if (!file) {
        throw std::runtime_error("Не удалось открыть файл для сохранения состояния");
    }

    std::lock_guard<std::mutex> lockObjects(objectsMutex);
    std::lock_guard<std::mutex> lockPoints(restorePointsMutex);

    file << objects_.size() << "\n";
    for (const auto& obj : objects_) {
        file << obj->getPath().string() << "\n";
    }

    file << restorePoints_.size() << "\n";
    for (const auto& point : restorePoints_) {
        point->serialize(file);
    }
}

void BackupJob::loadState(const fs::path& statePath) {
    std::ifstream file(statePath);
    if (!file) {
        throw std::runtime_error("Не удалось открыть файл состояния");
    }

    std::lock_guard<std::mutex> lockObjects(objectsMutex);
    std::lock_guard<std::mutex> lockPoints(restorePointsMutex);

    objects_.clear();
    restorePoints_.clear();

    size_t objectCount;
    file >> objectCount;
    file.ignore();

    for (size_t i = 0; i < objectCount; ++i) {
        std::string pathStr;
        std::getline(file, pathStr);
        objects_.push_back(std::make_shared<BackupObject>(pathStr));
    }

    size_t pointCount;
    file >> pointCount;
    file.ignore();

    for (size_t i = 0; i < pointCount; ++i) {
        restorePoints_.push_back(RestorePoint::deserialize(file));
    }
}

bool BackupJob::verifyBackup(const RestorePoint& point) const {
    return point.verifyIntegrity();
}

void BackupJob::setProgressCallback(ProgressCallback callback) {
    progressCallback_ = std::move(callback);
}

void BackupJob::cancelOperation() {
    operationCancelled_ = true;
}

void BackupJob::reportProgress(float progress, const std::string& message) {
    if (progressCallback_) {
        progressCallback_(progress, message);
    }
} 