cmake_minimum_required(VERSION 3.21)
project(ObdSuite VERSION 1.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/Qt/6.8.0/msvc2022_64")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets SerialPort Network)

qt_standard_project_setup()

qt_add_executable(ObdSuite
    src/main.cpp
    src/MainWindow.h
    src/MainWindow.cpp
    src/GvretConnection.h
    src/GvretConnection.cpp
    src/NewConnectionDialog.h
    src/NewConnectionDialog.cpp
    src/ObdPidMonitor.h
    src/ObdPidMonitor.cpp
    src/ObdDtcClient.h
    src/ObdDtcClient.cpp
    src/ObdFreezeFrameClient.h
    src/ObdFreezeFrameClient.cpp
    src/ObdVehicleInfo.h
    src/ObdVehicleInfo.cpp
    src/Elm327Connection.h
    src/Elm327Connection.cpp
    src/SessionLogger.h
    src/SessionLogger.cpp
    src/ReportExporter.h
    src/ReportExporter.cpp
    src/VehicleStore.h
    src/VehicleStore.cpp
    src/GaugeWidget.h
    src/GaugeWidget.cpp
    src/LiveChartWidget.h
    src/LiveChartWidget.cpp
    src/FrameTableModel.h
    src/FrameTableModel.cpp
    src/FrameSummaryModel.h
    src/FrameSummaryModel.cpp
    src/CanFrame.h
    src/AppSettings.h
    src/AppSettings.cpp
    src/AppStyle.h
    src/AppStyle.cpp
    src/Units.h
    src/Units.cpp
    src/PreferencesDialog.h
    src/PreferencesDialog.cpp
    src/DashboardConfigDialog.h
    src/DashboardConfigDialog.cpp
    # --- In-app ELM327 emulator (Widgets control window + backend engine) ---
    src/emulator/Elm327EmulatorEngine.h
    src/emulator/Elm327EmulatorEngine.cpp
    src/emulator/ObdMessageModel.h
    src/emulator/ObdMessageModel.cpp
    src/emulator/PidEncoders.h
    src/emulator/PidEncoders.cpp
    src/emulator/EmulatorTransport.h
    src/emulator/EmulatorTransport.cpp
    src/emulator/EmulatorWindow.h
    src/emulator/EmulatorWindow.cpp
    resources/emulator/emulator.qrc
)

target_link_libraries(ObdSuite PRIVATE Qt6::Widgets Qt6::SerialPort Qt6::Network)

# Single source of truth for the app version (shown in the title bar and used
# by setApplicationVersion); keep GitHub release tags and ZIP names in sync.
target_compile_definitions(ObdSuite PRIVATE OBDSUITE_VERSION="${PROJECT_VERSION}")

# The self-update helper ships next to the exe (and therefore inside release
# ZIPs); the app launches a temp copy of it to swap files after exit.
add_custom_command(TARGET ObdSuite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/scripts/updater.ps1
        $<TARGET_FILE_DIR:ObdSuite>/updater.ps1)

set_target_properties(ObdSuite PROPERTIES
    WIN32_EXECUTABLE ON
)

# --- Unit tests (additive; does not affect the ObdSuite target) ---
enable_testing()
add_subdirectory(tests)
