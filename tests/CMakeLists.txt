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

# --- App link test: the real Elm327Connection against the real emulator over
# --- a loopback TCP socket (init, PIDs, DTCs, freeze frame, VIN) ---
qt_add_executable(ObdSuiteAppLinkTests
    test_app_link.cpp
    ${CMAKE_SOURCE_DIR}/src/Elm327Connection.cpp
    ${CMAKE_SOURCE_DIR}/src/ObdPidMonitor.cpp
    ${CMAKE_SOURCE_DIR}/src/ObdDtcClient.cpp
    ${CMAKE_SOURCE_DIR}/src/GvretConnection.cpp
    # ObdDtcClient::describeDtc now delegates to the DTC library, so this target
    # must link it and bundle the DTC JSON (via the .qrc) to resolve descriptions.
    ${CMAKE_SOURCE_DIR}/src/DtcLibrary.cpp
    ${CMAKE_SOURCE_DIR}/resources/dtc/dtc.qrc
    ${CMAKE_SOURCE_DIR}/src/emulator/Elm327EmulatorEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/emulator/ObdMessageModel.cpp
    ${CMAKE_SOURCE_DIR}/src/emulator/PidEncoders.cpp
    ${CMAKE_SOURCE_DIR}/src/emulator/EmulatorTransport.cpp
)
target_include_directories(ObdSuiteAppLinkTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ObdSuiteAppLinkTests
    PRIVATE Qt6::Core Qt6::Gui Qt6::Test Qt6::Network Qt6::SerialPort)
set_target_properties(ObdSuiteAppLinkTests PROPERTIES WIN32_EXECUTABLE OFF)
add_test(NAME ObdSuiteAppLinkTests COMMAND ObdSuiteAppLinkTests)

# --- VIN decoder tests (offline decode helpers only, no network round-trip) ---
qt_add_executable(ObdSuiteVinDecoderTests
    test_vin_decoder.cpp
    ${CMAKE_SOURCE_DIR}/src/VinDecoder.h
    ${CMAKE_SOURCE_DIR}/src/VinDecoder.cpp
)
target_include_directories(ObdSuiteVinDecoderTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ObdSuiteVinDecoderTests PRIVATE Qt6::Core Qt6::Test Qt6::Network)
set_target_properties(ObdSuiteVinDecoderTests PROPERTIES WIN32_EXECUTABLE OFF)
add_test(NAME ObdSuiteVinDecoderTests COMMAND ObdSuiteVinDecoderTests)

# --- DTC library tests (bundled DTC JSON compiled in via the .qrc) ---
qt_add_executable(ObdSuiteDtcTests
    test_dtc_library.cpp
    ${CMAKE_SOURCE_DIR}/src/DtcLibrary.h
    ${CMAKE_SOURCE_DIR}/src/DtcLibrary.cpp
    ${CMAKE_SOURCE_DIR}/resources/dtc/dtc.qrc
)
target_include_directories(ObdSuiteDtcTests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(ObdSuiteDtcTests PRIVATE Qt6::Core Qt6::Test)
set_target_properties(ObdSuiteDtcTests PROPERTIES WIN32_EXECUTABLE OFF)
add_test(NAME ObdSuiteDtcTests COMMAND ObdSuiteDtcTests)
