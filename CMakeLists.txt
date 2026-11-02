cmake_minimum_required(VERSION 3.21)
project(Harbinger VERSION 0.6.3 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network)

add_executable(Harbinger
    assets/Harbinger.qrc
    src/main.cpp
    src/Types.h
    src/Codec.h src/Codec.cpp
    src/SmartEncoding.h src/SmartEncoding.cpp
    src/Probe.h src/Probe.cpp
    src/AudioInfo.h src/AudioInfo.cpp
    src/VideoInfo.h src/VideoInfo.cpp
    src/Engine.h src/Engine.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/MuxPlan.h src/MuxPlan.cpp
    src/MuxEngine.h src/MuxEngine.cpp
    src/MuxWindow.h src/MuxWindow.cpp
    src/BatchMuxPlan.h src/BatchMuxPlan.cpp
    src/BatchMuxWindow.h src/BatchMuxWindow.cpp
    src/WaveMatch.h src/WaveMatch.cpp
    src/WaveSync.h src/WaveSync.cpp
    src/Hardware.h src/Hardware.cpp
    src/HardwareDialog.h src/HardwareDialog.cpp
    src/HiveProtocol.h
    src/HiveWorker.h src/HiveWorker.cpp
    src/HiveClient.h src/HiveClient.cpp
    src/HiveWindow.h src/HiveWindow.cpp
)
if(WIN32)
    target_sources(Harbinger PRIVATE assets/Harbinger.rc)
endif()
target_link_libraries(Harbinger PRIVATE Qt6::Core Qt6::Widgets Qt6::Network)
target_compile_definitions(Harbinger PRIVATE QT_NO_KEYWORDS)
set_target_properties(Harbinger PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(MSVC)
    target_compile_options(Harbinger PRIVATE /W4 /permissive-)
else()
    target_compile_options(Harbinger PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(CodecTests tests/CodecTests.cpp src/Codec.h src/Codec.cpp src/Types.h)
    add_executable(SmartEncodingTests tests/SmartEncodingTests.cpp src/SmartEncoding.cpp src/SmartEncoding.h src/Codec.cpp src/Codec.h src/Types.h)
    target_link_libraries(SmartEncodingTests PRIVATE Qt6::Core)
    target_compile_definitions(SmartEncodingTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME SmartEncodingTests COMMAND SmartEncodingTests)
    target_link_libraries(CodecTests PRIVATE Qt6::Core)
    target_compile_definitions(CodecTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME CodecTests COMMAND CodecTests)
    add_executable(HiveProtocolTests tests/HiveProtocolTests.cpp src/HiveProtocol.h src/Types.h)
    target_include_directories(HiveProtocolTests PRIVATE src)
    target_link_libraries(HiveProtocolTests PRIVATE Qt6::Core)
    target_compile_definitions(HiveProtocolTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME HiveProtocolTests COMMAND HiveProtocolTests)
    add_executable(HardwareTests tests/HardwareTests.cpp src/Hardware.h src/Hardware.cpp src/Codec.h src/Codec.cpp src/Types.h)
    target_link_libraries(HardwareTests PRIVATE Qt6::Core)
    target_compile_definitions(HardwareTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME HardwareTests COMMAND HardwareTests)
    add_executable(AudioInfoTests tests/AudioInfoTests.cpp src/AudioInfo.h src/AudioInfo.cpp src/Types.h)
    target_link_libraries(AudioInfoTests PRIVATE Qt6::Core)
    target_compile_definitions(AudioInfoTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME AudioInfoTests COMMAND AudioInfoTests)
    add_executable(VideoInfoTests tests/VideoInfoTests.cpp src/VideoInfo.h src/VideoInfo.cpp src/Types.h)
    target_link_libraries(VideoInfoTests PRIVATE Qt6::Core)
    target_compile_definitions(VideoInfoTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME VideoInfoTests COMMAND VideoInfoTests)
    add_executable(MuxTests tests/MuxTests.cpp src/MuxPlan.h src/MuxPlan.cpp)
    target_link_libraries(MuxTests PRIVATE Qt6::Core)
    target_compile_definitions(MuxTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME MuxTests COMMAND MuxTests)
    add_executable(BatchMuxTests tests/BatchMuxTests.cpp src/BatchMuxPlan.h src/BatchMuxPlan.cpp src/MuxPlan.cpp)
    target_link_libraries(BatchMuxTests PRIVATE Qt6::Core)
    target_compile_definitions(BatchMuxTests PRIVATE QT_NO_KEYWORDS)
    add_test(NAME BatchMuxTests COMMAND BatchMuxTests)
    add_executable(WaveMatchTests tests/WaveMatchTests.cpp src/WaveMatch.h src/WaveMatch.cpp)
    add_test(NAME WaveMatchTests COMMAND WaveMatchTests)
    add_executable(DubWaveMatchTests tests/DubWaveMatchTests.cpp src/WaveMatch.h src/WaveMatch.cpp)
    add_test(NAME DubWaveMatchTests COMMAND DubWaveMatchTests)
endif()

install(TARGETS Harbinger RUNTIME DESTINATION bin BUNDLE DESTINATION .)
