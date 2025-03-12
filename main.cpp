#include "BackupSystem.h"
#include "StorageStrategies.h"
#include <iostream>
#include <string>

void printHelp() {
    std::cout << "Команды:" << std::endl;
    std::cout << "1. add <путь_к_файлу> - добавить файл для резервного копирования" << std::endl;
    std::cout << "2. backup - создать точку восстановления" << std::endl;
    std::cout << "3. restore <номер_точки> <путь_для_восстановления> - восстановить файлы" << std::endl;
    std::cout << "4. list - показать все точки восстановления" << std::endl;
    std::cout << "5. exit - выход" << std::endl;
}

int main() {
    setlocale(LC_ALL, "Russian");
    
    try {
        // Создаем директорию для резервных копий
        fs::path backupDir = fs::current_path() / "backups";
        
        // Создаем задачу резервного копирования с ZIP-стратегией
        auto strategy = std::make_unique<ZipStorageStrategy>();
        BackupJob backup(std::move(strategy), backupDir);
        
        // Устанавливаем callback для отображения прогресса
        backup.setProgressCallback([](float progress, const std::string& message) {
            std::cout << message << ": " << (progress * 100) << "%" << std::endl;
        });

        std::string command;
        printHelp();

        while (true) {
            std::cout << "\nВведите команду: ";
            std::getline(std::cin, command);

            if (command == "exit") {
                break;
            }
            else if (command.substr(0, 3) == "add") {
                if (command.length() > 4) {
                    std::string path = command.substr(4);
                    try {
                        backup.addObject(path);
                        std::cout << "Файл добавлен: " << path << std::endl;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Ошибка при добавлении файла: " << e.what() << std::endl;
                    }
                }
                else {
                    std::cout << "Укажите путь к файлу: add <путь_к_файлу>" << std::endl;
                }
            }
            else if (command == "backup") {
                try {
                    auto point = backup.createRestorePoint();
                    std::cout << "Создана точка восстановления: " 
                             << point->getLocation().string() << std::endl;
                }
                catch (const std::exception& e) {
                    std::cerr << "Ошибка при создании точки восстановления: " << e.what() << std::endl;
                }
            }
            else if (command.substr(0, 7) == "restore") {
                try {
                    auto points = backup.getRestorePoints();
                    if (points.empty()) {
                        std::cout << "Нет доступных точек восстановления" << std::endl;
                        continue;
                    }

                    size_t pointIndex;
                    fs::path restorePath;
                    std::stringstream ss(command.substr(8));
                    ss >> pointIndex;
                    ss >> restorePath;

                    if (pointIndex >= points.size()) {
                        std::cout << "Неверный номер точки восстановления" << std::endl;
                        continue;
                    }

                    backup.restore(*points[pointIndex], restorePath);
                    std::cout << "Восстановление завершено" << std::endl;
                }
                catch (const std::exception& e) {
                    std::cerr << "Ошибка при восстановлении: " << e.what() << std::endl;
                }
            }
            else if (command == "list") {
                auto points = backup.getRestorePoints();
                if (points.empty()) {
                    std::cout << "Нет точек восстановления" << std::endl;
                }
                else {
                    std::cout << "Точки восстановления:" << std::endl;
                    for (size_t i = 0; i < points.size(); ++i) {
                        auto timeT = std::chrono::system_clock::to_time_t(points[i]->getTimestamp());
                        std::cout << i << ". " << std::ctime(&timeT)
                                << "   Путь: " << points[i]->getLocation().string() << std::endl;
                    }
                }
            }
            else if (command == "help") {
                printHelp();
            }
            else {
                std::cout << "Неизвестная команда. Введите 'help' для списка команд" << std::endl;
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Критическая ошибка: " << e.what() << std::endl;
        return 1;
    }

    return 0;
} 