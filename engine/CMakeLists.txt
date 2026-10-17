add_library(chq_core
  src/core/byte_reader.cpp
  src/core/data_path.cpp
  src/core/mz_image.cpp
  src/core/save_file.cpp
  src/gfx/ega_surface.cpp
  src/gfx/chq_font.cpp
  src/dos/bios.cpp
  src/dos/dos.cpp
  src/dos/ega.cpp
  src/dos/interpreter.cpp
  src/dos/keymap.cpp
  src/dos/machine.cpp
  src/dos/rom_font.cpp
  src/dos/speaker.cpp
  src/app/mouse_control.cpp
)
target_include_directories(chq_core PUBLIC src)
target_link_libraries(chq_core PUBLIC chq_flags)

# SDL is needed by the core too (logging), so it is always fetched.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
  URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz)
FetchContent_MakeAvailable(SDL3)
target_link_libraries(chq_core PUBLIC SDL3::SDL3)

# The recompiled game, generated from the user's CHQ.EXE (+ EGRAPHIC.EXE) by scripts/recomp.zsh.
# Without it the app still builds and asks for the game.
set(CHQ_GENERATED_DIR "${PROJECT_SOURCE_DIR}/build/generated" CACHE PATH "Output of chqre recomp")
file(GLOB CHQ_GENERATED_SOURCES CONFIGURE_DEPENDS "${CHQ_GENERATED_DIR}/*.cpp")
if(CHQ_GENERATED_SOURCES)
  add_library(chq_game STATIC ${CHQ_GENERATED_SOURCES})
  target_link_libraries(chq_game PUBLIC chq_core)
  # Huge functions of straight-line code: optimise lightly to keep build times sane.
  # Under Emscripten, -O0 keeps compiler memory low enough to build in containers.
  if(EMSCRIPTEN)
    target_compile_options(chq_game PRIVATE -O0 -w)
  else()
    target_compile_options(chq_game PRIVATE -O1 -w)
  endif()
  message(STATUS "Command HQ: building recompiled game from ${CHQ_GENERATED_DIR}")
  if(NOT EMSCRIPTEN)
    add_executable(chq_headless src/app/headless.cpp)
    target_link_libraries(chq_headless PRIVATE chq_game)
    add_executable(chq_lockstep src/app/lockstep.cpp)
    target_link_libraries(chq_lockstep PRIVATE chq_game)
  endif()
else()
  message(STATUS "Command HQ: no recompiled game in ${CHQ_GENERATED_DIR}; run scripts/recomp.zsh")
endif()

if(CHQ_BUILD_APP)
  set(CHQ_APP_SOURCES src/app/main.cpp src/app/host_keys.cpp)
  if(APPLE AND NOT EMSCRIPTEN)
    list(APPEND CHQ_APP_SOURCES src/app/macos/macos_bridge.mm)
    set_source_files_properties(src/app/macos/macos_bridge.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set(MACOSX_BUNDLE_ICON_FILE "AppIcon.icns")
    set(CHQ_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/AppIcon.icns")
    if(EXISTS "${CHQ_APP_ICON}")
      set_source_files_properties("${CHQ_APP_ICON}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
      list(APPEND CHQ_APP_SOURCES "${CHQ_APP_ICON}")
    endif()
  endif()
  add_executable(comhq MACOSX_BUNDLE ${CHQ_APP_SOURCES})
  target_link_libraries(comhq PRIVATE chq_core SDL3::SDL3)
  if(APPLE AND NOT EMSCRIPTEN)
    target_link_libraries(comhq PRIVATE "-framework Cocoa")
  endif()
  if(TARGET chq_game)
    target_link_libraries(comhq PRIVATE chq_game)
    target_compile_definitions(comhq PRIVATE CHQ_HAVE_GAME=1)
  endif()
  set_target_properties(comhq PROPERTIES
    OUTPUT_NAME "Command HQ"
    MACOSX_BUNDLE_GUI_IDENTIFIER com.mabino.comhq-port
    MACOSX_BUNDLE_BUNDLE_NAME "Command HQ"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_BUNDLE_VERSION "1"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in")
  if(EMSCRIPTEN)
    set_target_properties(comhq PROPERTIES OUTPUT_NAME comhq SUFFIX .html)
    target_link_options(comhq PRIVATE -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -lidbfs.js
      "--shell-file=${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
    if(DEFINED ENV{CHQ_DATA} AND EXISTS "$ENV{CHQ_DATA}/CHQ.EXE")
      target_link_options(comhq PRIVATE "--preload-file=$ENV{CHQ_DATA}@/data")
    endif()
    set_property(TARGET comhq APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html")
  endif()
endif()

if(CHQ_BUILD_TESTS)
  FetchContent_Declare(doctest
    URL https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz)
  set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(doctest)
  add_executable(chq_tests
    tests/main.cpp
    tests/byte_reader_test.cpp
    tests/data_path_test.cpp
    tests/mz_image_test.cpp
    tests/save_file_test.cpp
    tests/ega_surface_test.cpp
    tests/chq_font_test.cpp
    tests/machine_test.cpp
    tests/ega_test.cpp
    tests/sound_state_test.cpp
    tests/window_geometry_test.cpp
    tests/mouse_control_test.cpp
  )
  target_link_libraries(chq_tests PRIVATE chq_core doctest::doctest)
  add_test(NAME unit COMMAND chq_tests)

  # Instruction corpus with Unicorn as the reference CPU, generated at build
  # time (needs uv; skipped where it is missing, e.g. in the wasm container).
  find_program(UV_EXECUTABLE uv)
  if(UV_EXECUTABLE AND NOT EMSCRIPTEN)
    set(CORPUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/corpus")
    file(GLOB CHQRE_SOURCES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/tools/chqre/src/chqre/*.py")
    add_custom_command(
      OUTPUT "${CORPUS_DIR}/ops_corpus.inc" "${CORPUS_DIR}/ops_corpus_recomp.cpp"
      COMMAND "${UV_EXECUTABLE}" --directory "${PROJECT_SOURCE_DIR}/tools/chqre" run chqre corpus --out "${CORPUS_DIR}"
      DEPENDS ${CHQRE_SOURCES}
      COMMENT "Generating the instruction corpus with Unicorn"
      VERBATIM)
    add_executable(chq_corpus_tests tests/main.cpp tests/ops_corpus_test.cpp "${CORPUS_DIR}/ops_corpus_recomp.cpp")
    target_include_directories(chq_corpus_tests PRIVATE "${CORPUS_DIR}")
    target_compile_options(chq_corpus_tests PRIVATE -O1 -w)
    target_link_libraries(chq_corpus_tests PRIVATE chq_core doctest::doctest)
    add_test(NAME instruction_corpus COMMAND chq_corpus_tests)
  else()
    message(STATUS "Command HQ: uv not found; instruction corpus test disabled")
  endif()
  # With the recompiled game: play a scripted session headless, and check it
  # block by block against the interpreter.
  if(TARGET chq_headless)
    set(CHQ_TEST_KEYS "{ENTER}{ENTER}{WAIT 60}{ENTER}{WAIT 60}{ENTER}{WAIT 60}{ESC}")
    add_test(NAME game_headless COMMAND chq_headless --frames 300 --keys "${CHQ_TEST_KEYS}")
    add_test(NAME game_lockstep COMMAND chq_lockstep --blocks 2000000 --seconds 240 --keys "${CHQ_TEST_KEYS}")
    set_tests_properties(game_headless game_lockstep PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy" TIMEOUT 600)
  endif()
  if(CHQ_BUILD_APP AND NOT EMSCRIPTEN)
    add_test(NAME app_smoke COMMAND comhq --smoke)
    set_tests_properties(app_smoke PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()
endif()
