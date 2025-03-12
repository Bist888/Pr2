#pragma once
#include "BackupSystem.h"
#include <fstream>
#include <filesystem>
#include <zip.h>
#include <sstream>
#include <ctime>

// Стратегия раздельного хранения - каждый объект в отдельной директории
class SplitStorageStrategy : public IStorageStrategy {
public:
    void store(const std::vector<std::shared_ptr<BackupObject>>& objects,
               const fs::path& destination) override;
};

// Стратегия общего хранилища - все объекты в одной директории
class SingleStorageStrategy : public IStorageStrategy {
public:
    void store(const std::vector<std::shared_ptr<BackupObject>>& objects,
               const fs::path& destination) override;
};

class SimpleStorageStrategy : public IStorageStrategy {
public:
    void store(const std::vector<std::shared_ptr<BackupObject>>& objects,
               const fs::path& destination) override;
};

class ZipStorageStrategy : public IStorageStrategy {
public:
    void store(const std::vector<std::shared_ptr<BackupObject>>& objects,
               const fs::path& destination) override;
private:
    static void addToZip(zip_t* archive, const fs::path& filePath, const std::string& entryName);
}; 