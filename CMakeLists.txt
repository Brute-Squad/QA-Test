cmake_minimum_required(VERSION 3.21)
project(QATest VERSION 1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Sql)

# The database, which the program and its tests share.
add_library(QaData STATIC
    src/qaconfig.cpp
    src/qaconfig.h
    src/qadatabase.cpp
    src/qadatabase.h
    src/qabackup.cpp
    src/qabackup.h
    src/qashare.cpp
    src/qashare.h
)
target_include_directories(QaData PUBLIC src)
target_link_libraries(QaData PUBLIC Qt6::Core Qt6::Sql)
if(WIN32)
    # What a connected drive is connected to (WNetGetConnection, qashare.cpp).
    target_link_libraries(QaData PUBLIC mpr)
endif()

# The program. WIN32: no console window opens with it.
add_executable(QATest WIN32
    src/main.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/casepanel.cpp
    src/casepanel.h
    src/runpanel.cpp
    src/runpanel.h
    src/runmode.cpp
    src/runmode.h
    src/dashboard.cpp
    src/dashboard.h
    src/report.cpp
    src/report.h
    # Its icon: in the .exe (what the Explorer and a shortcut show) and for its windows.
    src/qatest.rc
    src/qatest.qrc
)
target_link_libraries(QATest PRIVATE QaData Qt6::Gui Qt6::Widgets)
# (The .exe is made again when only the icon changed.)
set_source_files_properties(src/qatest.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/icons/qatest.ico")

# Draws the icon and writes icons/qatest.ico and the pictures (icons/makeicon.cpp).
# Not part of the build: only when the icon is to change -
#   cmake --build build --target MakeIcon   and then   build\MakeIcon.exe icons
add_executable(MakeIcon EXCLUDE_FROM_ALL icons/makeicon.cpp)
target_link_libraries(MakeIcon PRIVATE Qt6::Gui)

# The tests: exit code 0 = every check passed. They drive the program's own
# window and panels offscreen, on a database in memory, and read the test
# scripts in scripts/ to see that every file of them can be imported.
add_executable(QATestTests
    tests/qatests.cpp
    src/mainwindow.cpp
    src/mainwindow.h
    src/casepanel.cpp
    src/casepanel.h
    src/runpanel.cpp
    src/runpanel.h
    src/runmode.cpp
    src/runmode.h
    src/dashboard.cpp
    src/dashboard.h
    src/report.cpp
    src/report.h
)
target_include_directories(QATestTests PRIVATE src)
target_compile_definitions(QATestTests PRIVATE QA_SCRIPTS_DIR="${CMAKE_SOURCE_DIR}/scripts")
target_link_libraries(QATestTests PRIVATE QaData Qt6::Gui Qt6::Widgets)

enable_testing()
add_test(NAME QATestTests COMMAND QATestTests)
set_tests_properties(QATestTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
