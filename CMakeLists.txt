cmake_minimum_required(VERSION 3.20)
project(ScreenRecorder CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /W3)
endif()

set(FFMPEG_DIR ${CMAKE_SOURCE_DIR}/third_party/ffmpeg)
add_library(ffmpeg INTERFACE)
target_include_directories(ffmpeg INTERFACE ${FFMPEG_DIR}/include)
target_link_libraries(ffmpeg INTERFACE
  ${FFMPEG_DIR}/lib/avcodec.lib
  ${FFMPEG_DIR}/lib/avformat.lib
  ${FFMPEG_DIR}/lib/avutil.lib
  ${FFMPEG_DIR}/lib/swscale.lib
  ${FFMPEG_DIR}/lib/swresample.lib)

add_library(common STATIC
  common/ipc_message.cpp
  common/settings.cpp)
target_include_directories(common PUBLIC
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/third_party)

add_library(recorder_core STATIC
  recorder/video_encoder.cpp
  recorder/audio_encoder.cpp
  recorder/muxer.cpp
  recorder/audio_mixer.cpp
  recorder/ipc_server.cpp
  recorder/desktop_capture.cpp
  recorder/cursor_compositor.cpp
  recorder/wasapi_capture.cpp
  recorder/pipeline.cpp)
target_link_libraries(recorder_core PUBLIC
  common ffmpeg d3d11 dxgi ole32 oleaut32 uuid shlwapi)

add_executable(recorder recorder/main.cpp)
target_link_libraries(recorder PRIVATE recorder_core)

# 构建后把 ffmpeg dll 拷到输出目录
add_custom_command(TARGET recorder POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${FFMPEG_DIR}/bin $<TARGET_FILE_DIR:recorder>)

enable_testing()
set(TESTS
  test_ipc_message test_settings test_audio_mixer
  test_cursor_compositor test_encoder_mux test_ipc_server)
foreach(t ${TESTS})
  add_executable(${t} tests/${t}.cpp)
  target_link_libraries(${t} PRIVATE recorder_core)
  add_test(NAME ${t} COMMAND ${t})
endforeach()
