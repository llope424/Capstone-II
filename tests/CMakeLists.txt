# Unit/integration tests for the pure-C++ backend (emulator engine, transports,
# DTC library). Sources are compiled directly into the test exes for now; when the
# app itself starts consuming them they can move into a shared static library.
find_package(Qt6 REQUIRED COMPONENTS Test Network SerialPort)

# Engine + data model + encoders, shared by the test executables.
set(EMU_CORE
    ${CMAKE_SOURCE_DIR}/src/emulator/Elm327EmulatorEngine.h
    ${CMAKE_SOURCE_DIR}/src/emulator/Elm327EmulatorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/emulator/ObdMessageModel.h
    ${CMAKE_SOURCE_DIR}/src/emulator/ObdMessageModel.cpp
    ${CMAKE_SOURCE_DIR}/src/emulator/PidEncoders.h
    ${CMAKE_SOURCE_DIR}/src/emulator/PidEncoders.cpp
)

# --- Engine behaviour tests (bundled scenario JSON compiled in via the .qrc) ---
qt_add_executable(ObdSuiteTests
    test_emulator_engine.cpp
    ${EMU_CORE}
    ${CMAKE_SOURCE_DIR}/resources/emulator/emulator.qrc
)
target_include_directories(ObdSuiteTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ObdSuiteTests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(ObdSuiteTests PROPERTIES WIN32_EXECUTABLE OFF)
add_test(NAME ObdSuiteTests COMMAND ObdSuiteTests)

# --- Transport (TCP/serial) integration tests ---
qt_add_executable(ObdSuiteTransportTests
    test_emulator_transport.cpp
    ${EMU_CORE}
    ${CMAKE_SOURCE_DIR}/src/emulator/EmulatorTransport.h
    ${CMAKE_SOURCE_DIR}/src/emulator/EmulatorTransport.cpp
)
target_include_directories(ObdSuiteTransportTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ObdSuiteTransportTests PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::SerialPort)
set_target_properties(ObdSuiteTransportTests PROPERTIES WIN32_EXECUTABLE OFF)
add_test(NAME ObdSuiteTransportTests COMMAND ObdSuiteTransportTests)
