#include "StorageStrategies.h"
#include <iostream>
#include <stdexcept>

void SplitStorageStrategy::store(const std::vector<std::shared_ptr<BackupObject>>& objects,
                                const fs::path& destination) {
    for (const auto& obj : objects) {
        if (!obj->exists()) {
            throw std::runtime_error("Объект для резервного копирования не существует: " + 
                                   obj->getPath().string());
        }

        // Создаем отдельную директорию для каждого объекта
        fs::path objDestination = destination / obj->getPath().filename();
        fs::create_directories(objDestination);

        // Копируем файл в новую директорию
        fs::copy(obj->getPath(), objDestination / obj->getPath().filename(),
                fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
}

void SingleStorageStrategy::store(const std::vector<std::shared_ptr<BackupObject>>& objects,
                                const fs::path& destination) {
    // Создаем одну общую директорию для всех объектов
    fs::create_directories(destination);

    for (const auto& obj : objects) {
        if (!obj->exists()) {
            throw std::runtime_error("Объект для резервного копирования не существует: " + 
                                   obj->getPath().string());
        }

        // Копируем все файлы в общую директорию
        fs::copy(obj->getPath(), destination / obj->getPath().filename(),
                fs::copy_options::recursive | fs::copy_options::overwrite_existing);
    }
}

void SimpleStorageStrategy::store(const std::vector<std::shared_ptr<BackupObject>>& objects,
                                const fs::path& destination) {
    std::error_code ec;
    
    for (const auto& obj : objects) {
        if (!obj->exists()) {
            throw std::runtime_error("Файл не существует: " + obj->getPath().string());
        }

        fs::path destPath = destination / obj->getPath().filename();
        
        if (fs::exists(destPath, ec)) {
            fs::remove(destPath, ec);
            if (ec) {
                throw std::runtime_error("Не удалось удалить существующий файл: " + ec.message());
            }
        }

        fs::copy_file(obj->getPath(), destPath, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            throw std::runtime_error("Ошибка копирования файла: " + ec.message());
        }
    }
}

void ZipStorageStrategy::store(const std::vector<std::shared_ptr<BackupObject>>& objects,
                             const fs::path& destination) {
    fs::path zipPath = destination;
    zipPath += ".zip";

    int error = 0;
    zip_t* archive = zip_open(zipPath.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
    if (!archive) {
        throw std::runtime_error("Не удалось создать ZIP архив");
    }

    try {
        for (const auto& obj : objects) {
            if (!obj->exists()) {
                throw std::runtime_error("Файл не существует: " + obj->getPath().string());
            }
            addToZip(archive, obj->getPath(), obj->getPath().filename().string());
        }
    } catch (...) {
        zip_close(archive);
        throw;
    }

    zip_close(archive);
}

void ZipStorageStrategy::addToZip(zip_t* archive, const fs::path& filePath, const std::string& entryName) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Не удалось открыть файл: " + filePath.string());
    }

    std::vector<char> buffer(8192);
    zip_source_t* source = zip_source_buffer(archive, buffer.data(), buffer.size(), 0);
    if (!source) {
        throw std::runtime_error("Не удалось создать источник данных для ZIP");
    }

    if (zip_file_add(archive, entryName.c_str(), source, ZIP_FL_OVERWRITE) < 0) {
        zip_source_free(source);
        throw std::runtime_error("Не удалось добавить файл в архив");
    }
} 