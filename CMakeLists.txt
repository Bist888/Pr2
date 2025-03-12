cmake_minimum_required(VERSION 3.10)
project(BackupSystem)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Находим необходимые библиотеки
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)

# Добавляем исходные файлы
add_executable(backup_system
    main.cpp
    BackupSystem.cpp
    StorageStrategies.cpp
)

# Подключаем заголовочные файлы
target_include_directories(backup_system PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Подключаем библиотеки
target_link_libraries(backup_system PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    ZLIB::ZLIB
) 