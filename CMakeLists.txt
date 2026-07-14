cmake_minimum_required(VERSION 3.21)
project(ObdSuite LANGUAGES CXX)

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
)

target_link_libraries(ObdSuite PRIVATE Qt6::Widgets Qt6::SerialPort Qt6::Network)

set_target_properties(ObdSuite PROPERTIES
    WIN32_EXECUTABLE ON
)
