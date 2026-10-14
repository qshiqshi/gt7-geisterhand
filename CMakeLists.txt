cmake_minimum_required(VERSION 3.24)
project(Geisterhand VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/deps.cmake)
include(cmake/embed.cmake)

# ---- third party: SDL3 (window, joystick, force feedback, USB HID) and Dear ImGui -----------
gh_source(SDL_DIR sdl3
  https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
  9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
  SDL3-3.4.18)
gh_source(IMGUI_DIR imgui
  https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
  21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
  imgui-1.92.9b)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_AUDIO OFF CACHE BOOL "" FORCE)    # nothing here makes a sound,
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)   # takes a picture
set(SDL_GPU OFF CACHE BOOL "" FORCE)      # or needs more than the 2D renderer
add_subdirectory("${SDL_DIR}" sdl EXCLUDE_FROM_ALL)

# ---- the program ---------------------------------------------------------------------------
set(GENERATED "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GENERATED}")
gh_embed("${GENERATED}/fonts.cpp"
  kFontRegular "${CMAKE_SOURCE_DIR}/assets/fonts/Inter-Regular.ttf"
  kFontSemiBold "${CMAKE_SOURCE_DIR}/assets/fonts/Inter-SemiBold.ttf")

set(SOURCES
  src/main.cpp
  src/engine.cpp
  src/wheel.cpp
  src/telemetry.cpp
  src/net.cpp
  src/controller.cpp
  src/playout.cpp
  src/gt7_packet.cpp
  src/salsa20.cpp
  src/settings.cpp
  src/selftest.cpp
  "${GENERATED}/fonts.cpp"
  "${IMGUI_DIR}/imgui.cpp"
  "${IMGUI_DIR}/imgui_draw.cpp"
  "${IMGUI_DIR}/imgui_tables.cpp"
  "${IMGUI_DIR}/imgui_widgets.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
  "${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp")

if(APPLE)
  set(ICON "${CMAKE_SOURCE_DIR}/assets/icon/Geisterhand.icns")
  set_source_files_properties("${ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  add_executable(Geisterhand MACOSX_BUNDLE ${SOURCES} "${ICON}")
  set_target_properties(Geisterhand PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/Info.plist.in")
elseif(WIN32)
  if(MINGW)
    # windres hands paths to a shell without quoting them, so a project folder with a space
    # or an "&" in its name breaks it. Run it inside packaging/ with relative names instead.
    set(RESOURCES "${CMAKE_BINARY_DIR}/windows-resources.o")
    add_custom_command(OUTPUT "${RESOURCES}"
      COMMAND "${CMAKE_RC_COMPILER}" -O coff -i windows.rc -o "${RESOURCES}"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/packaging"
      DEPENDS packaging/windows.rc assets/icon/Geisterhand.ico
      VERBATIM)
    set_source_files_properties("${RESOURCES}" PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
    add_executable(Geisterhand WIN32 ${SOURCES} "${RESOURCES}")
    target_link_options(Geisterhand PRIVATE -static -s)   # one file, no runtime DLLs next to it, no symbols
  else()
    add_executable(Geisterhand WIN32 ${SOURCES} packaging/windows.rc)
  endif()
  target_link_libraries(Geisterhand PRIVATE ws2_32 iphlpapi)
else()
  add_executable(Geisterhand ${SOURCES})
endif()

target_include_directories(Geisterhand PRIVATE src "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_compile_definitions(Geisterhand PRIVATE GEISTERHAND_VERSION="${PROJECT_VERSION}" IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(Geisterhand PRIVATE SDL3::SDL3-static)
if(NOT MSVC)
  set_source_files_properties(src/main.cpp src/engine.cpp src/wheel.cpp src/telemetry.cpp src/net.cpp src/controller.cpp
    src/playout.cpp src/gt7_packet.cpp src/salsa20.cpp src/settings.cpp src/selftest.cpp
    PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
endif()
