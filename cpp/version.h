#pragma once
// LHPad 버전의 유일한 기준. 릴리스할 때는 이 세 숫자만 바꾼다.
// C++ 코드와 LHPad.rc는 이 헤더를 include하고, CMake와 tools/의 스크립트는
// 같은 #define 줄을 읽는다(tools/version.ps1, tools/publish-htools.mjs).
#define LHPAD_VERSION_MAJOR 1
#define LHPAD_VERSION_MINOR 3
#define LHPAD_VERSION_PATCH 0

#define LHPAD_VERSION_STRINGIZE_(x) #x
#define LHPAD_VERSION_STRINGIZE(x) LHPAD_VERSION_STRINGIZE_(x)
// "1.1.2" — rc.exe와 C++ 모두 인접 문자열 리터럴을 이어 붙인다.
#define LHPAD_VERSION_STRING                                                                       \
    LHPAD_VERSION_STRINGIZE(LHPAD_VERSION_MAJOR) "." LHPAD_VERSION_STRINGIZE(LHPAD_VERSION_MINOR) \
    "." LHPAD_VERSION_STRINGIZE(LHPAD_VERSION_PATCH)
