#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <fstream>
#include <functional>

namespace fs = std::filesystem;

// Forward declarations
class RestorePoint;
class BackupObject;
class StorageStrategy;

// Callback для отслеживания прогресса
using ProgressCallback = std::function<void(float progress, const std::string& message)>;

// Interface for backup storage strategies
class IStorageStrategy {
public:
    virtual ~IStorageStrategy() = default;
    virtual void store(const std::vector<std::shared_ptr<BackupObject>>& objects, 
                      const fs::path& destination) = 0;
};

// Backup object representing a file or data to be backed up
class BackupObject {
public:
    explicit BackupObject(const fs::path& path);
    const fs::path& getPath() const;
    bool exists() const;
    bool verifyChecksum() const;

private:
    fs::path path_;
    std::string calculateChecksum() const;
    std::string storedChecksum_;
};

// Restore point representing a snapshot of backed up objects
class RestorePoint {
public:
    RestorePoint(const std::vector<std::shared_ptr<BackupObject>>& objects,
                const fs::path& location,
                std::chrono::system_clock::time_point timestamp);

    const std::vector<std::shared_ptr<BackupObject>>& getObjects() const;
    const fs::path& getLocation() const;
    std::chrono::system_clock::time_point getTimestamp() const;
    bool verifyIntegrity() const;

    // Сериализация
    void serialize(std::ostream& os) const;
    static std::shared_ptr<RestorePoint> deserialize(std::istream& is);

private:
    std::vector<std::shared_ptr<BackupObject>> objects_;
    fs::path location_;
    std::chrono::system_clock::time_point timestamp_;
};

// Backup job managing the backup process
class BackupJob {
public:
    explicit BackupJob(std::unique_ptr<IStorageStrategy> strategy, const fs::path& backupDir);

    void addObject(const fs::path& path);
    void removeObject(const fs::path& path);
    std::shared_ptr<RestorePoint> createRestorePoint();
    
    // Новые методы
    void restore(const RestorePoint& point, const fs::path& targetDir);
    void saveState(const fs::path& statePath) const;
    void loadState(const fs::path& statePath);
    bool verifyBackup(const RestorePoint& point) const;
    void setProgressCallback(ProgressCallback callback);
    void cancelOperation(); // Для отмены текущей операции

    const std::vector<std::shared_ptr<BackupObject>>& getObjects() const;
    const std::vector<std::shared_ptr<RestorePoint>>& getRestorePoints() const;

private:
    std::vector<std::shared_ptr<BackupObject>> objects_;
    std::vector<std::shared_ptr<RestorePoint>> restorePoints_;
    std::unique_ptr<IStorageStrategy> storageStrategy_;
    fs::path backupDirectory_;
    ProgressCallback progressCallback_;
    bool operationCancelled_;
    
    void reportProgress(float progress, const std::string& message);
}; 