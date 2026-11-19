cmake_minimum_required(VERSION 3.20)

# 버전은 version.h의 LHPAD_VERSION_MAJOR/MINOR/PATCH에서 읽는다.

file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/version.h" LHPAD_VERSION_LINES REGEX "^#define LHPAD_VERSION_(MAJOR|MINOR|PATCH) +[0-9]+")

foreach(LHPAD_PART MAJOR MINOR PATCH)

  string(REGEX MATCH "#define LHPAD_VERSION_${LHPAD_PART} +([0-9]+)" _ "${LHPAD_VERSION_LINES}")

  if(NOT CMAKE_MATCH_1 MATCHES "^[0-9]+$")

    message(FATAL_ERROR "version.h is missing LHPAD_VERSION_${LHPAD_PART}.")

  endif()

  set(LHPAD_VERSION_${LHPAD_PART} ${CMAKE_MATCH_1})

endforeach()

project(LHPad VERSION ${LHPAD_VERSION_MAJOR}.${LHPAD_VERSION_MINOR}.${LHPAD_VERSION_PATCH} LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC)

  message(FATAL_ERROR "LHPad requires the MSVC Windows x64 toolchain.")

endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)

  message(FATAL_ERROR "Select the x64 architecture.")

endif()

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(LHPad WIN32

  main.cpp core.cpp diff.cpp format.cpp dialogs.cpp app.cpp tabs.cpp documents.cpp editing.cpp search.cpp outline.cpp theme.cpp complete.cpp

  compare.cpp hex.cpp preview.cpp tests.cpp shell_package.cpp shell_registration.cpp LHPad.rc

  vendor/pugixml/pugixml.cpp vendor/md4c/md4c.c vendor/md4c/md4c-html.c vendor/md4c/entity.c)

target_include_directories(LHPad PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(LHPad PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)

target_compile_options(LHPad PRIVATE

  "$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/EHsc>"

  "$<$<COMPILE_LANGUAGE:C>:/W3>")

target_link_options(LHPad PRIVATE /DYNAMICBASE /NXCOMPAT /CETCOMPAT /MANIFEST:NO)

target_link_libraries(LHPad PRIVATE user32 gdi32 comctl32 comdlg32 shell32 shlwapi

  ole32 oleaut32 advapi32 bcrypt gdiplus uuid uxtheme windowsapp)

set(LHPAD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

foreach(native Scintilla Lexilla)

  if(NOT EXISTS "${LHPAD_ROOT}/native/${native}.dll")

    message(FATAL_ERROR "Rebuild the bundled Scintilla/Lexilla native engines first.")

  endif()

  add_custom_command(TARGET LHPad POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different

    "${LHPAD_ROOT}/native/${native}.dll" "$<TARGET_FILE_DIR:LHPad>/${native}.dll")

endforeach()

add_custom_command(TARGET LHPad POST_BUILD

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different

    "${CMAKE_CURRENT_SOURCE_DIR}/vendor/webview2/WebView2Loader.dll" "$<TARGET_FILE_DIR:LHPad>/WebView2Loader.dll"

  COMMAND "${CMAKE_COMMAND}" -E copy_directory "${LHPAD_ROOT}/data" "$<TARGET_FILE_DIR:LHPad>/data"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/README.txt" "$<TARGET_FILE_DIR:LHPad>/README.txt"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LHPAD_ROOT}/README.md" "$<TARGET_FILE_DIR:LHPad>/README.md"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LHPAD_ROOT}/LICENSE" "$<TARGET_FILE_DIR:LHPad>/LICENSE"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LHPAD_ROOT}/THIRD-PARTY-NOTICES.txt" "$<TARGET_FILE_DIR:LHPad>/THIRD-PARTY-NOTICES.txt")

enable_testing()

add_test(NAME native-regression COMMAND LHPad --self-test)



add_library(LHPadShell SHARED shell_extension.cpp shell_extension.def shell_icon.rc)

target_compile_definitions(LHPadShell PRIVATE UNICODE _UNICODE)

target_compile_options(LHPadShell PRIVATE /utf-8 /W4 /EHsc)

target_link_options(LHPadShell PRIVATE /DYNAMICBASE /NXCOMPAT /CETCOMPAT)

target_link_libraries(LHPadShell PRIVATE ole32 shell32 uuid)

add_dependencies(LHPad LHPadShell)

add_custom_command(TARGET LHPad POST_BUILD

  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:LHPad>/tools"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LHPAD_ROOT}/Register-LHPad.cmd" "$<TARGET_FILE_DIR:LHPad>/Register-LHPad.cmd"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LHPAD_ROOT}/tools/install-explorer.ps1" "$<TARGET_FILE_DIR:LHPad>/tools/install-explorer.ps1"

  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${LHPAD_ROOT}/tools/register-explorer.ps1" "$<TARGET_FILE_DIR:LHPad>/tools/register-explorer.ps1"

  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${LHPAD_ROOT}/tools/build-modern-shell.ps1" -Destination "$<TARGET_FILE_DIR:LHPad>"

  COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${LHPAD_ROOT}/tools/sign-release.ps1" -Directory "$<TARGET_FILE_DIR:LHPad>")



add_executable(LHPad-shell-tests shell_tests.cpp)
add_executable(LHPad-registration-tests shell_registration_tests.cpp shell_registration.cpp)
target_compile_definitions(LHPad-registration-tests PRIVATE UNICODE _UNICODE)
target_compile_options(LHPad-registration-tests PRIVATE /utf-8 /W4 /EHsc)
target_link_libraries(LHPad-registration-tests PRIVATE shell32 advapi32)
add_test(NAME explorer-registration-regression COMMAND LHPad-registration-tests)

target_compile_definitions(LHPad-shell-tests PRIVATE UNICODE _UNICODE)

target_compile_options(LHPad-shell-tests PRIVATE /utf-8 /W4 /EHsc)

target_link_libraries(LHPad-shell-tests PRIVATE ole32 shell32 uuid user32)

add_test(NAME explorer-command-regression COMMAND LHPad-shell-tests "$<TARGET_FILE:LHPadShell>" "${CMAKE_CURRENT_BINARY_DIR}/shell-test-fixture")

