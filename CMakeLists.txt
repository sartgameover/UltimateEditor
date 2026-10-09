cmake_minimum_required(VERSION 3.16)
project(UltimateEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)

add_executable(uve
  src/main.cpp
  src/Project.cpp
  src/Decoder.cpp
  src/Peaks.cpp
  src/Effects.cpp
  src/Compositor.cpp
  src/AudioEngine.cpp
  src/PlayerView.cpp
  src/Timeline.cpp
  src/Panels.cpp
  src/Mixer.cpp
  src/Transport.cpp
  src/Export.cpp
  src/MainWindow.cpp
  src/VoiceRecorder.cpp
  src/SettingsDialog.cpp
  src/AudioFx.cpp
  src/ChannelFx.cpp
  src/Water.cpp
  src/ScreenRecorder.cpp
  src/Branding.cpp
  src/StartScreen.cpp
  src/Installer.cpp
  src/Theme.cpp
  src/Fonts.cpp
  src/ExtraPanels.cpp
)
target_link_libraries(uve PRIVATE Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Multimedia PkgConfig::FFMPEG)
target_compile_definitions(uve PRIVATE UVE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")
