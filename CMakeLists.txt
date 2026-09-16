cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(gyrovault C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "Gyrovault")
set(VITA_TITLEID  "GYRV00001")

# VITA_VERSION is derived from GV_VERSION in src/version.h so there is a single source of
# truth for the app version (previously hardcoded here and easy to drift from version.h).
# GV_VERSION "MAJOR.MINOR.PATCH" -> VITA_VERSION "MM.mm" (two-digit major.minor, no patch).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" GV_VERSION_LINE REGEX "^#define GV_VERSION ")
if(NOT GV_VERSION_LINE MATCHES "\"([0-9]+)\\.([0-9]+)\\.[0-9]+\"")
  message(FATAL_ERROR "Could not parse GV_VERSION out of src/version.h")
endif()
set(GV_VER_MAJOR "${CMAKE_MATCH_1}")
set(GV_VER_MINOR "${CMAKE_MATCH_2}")
if(GV_VER_MAJOR LESS 10)
  set(GV_VER_MAJOR "0${GV_VER_MAJOR}")
endif()
if(GV_VER_MINOR LESS 10)
  set(GV_VER_MINOR "0${GV_VER_MINOR}")
endif()
set(VITA_VERSION "${GV_VER_MAJOR}.${GV_VER_MINOR}")
message(STATUS "VITA_VERSION (derived from GV_VERSION in src/version.h): ${VITA_VERSION}")

option(GV_TEST_INPUT "Emulator-only: d-pad adds tilt (never in release)" OFF)

file(GLOB GV_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(gyrovault ${GV_SOURCES})
target_include_directories(gyrovault PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(gyrovault PRIVATE -Wall -Wextra -O2)
if(GV_TEST_INPUT)
  target_compile_definitions(gyrovault PRIVATE GV_TEST_INPUT)
endif()

target_link_libraries(gyrovault
  # in-app updater: self-update over HTTPS (libcurl.pc Libs: curl ... ssl crypto z zstd;
  # ScePromoterUtil installs the downloaded VPK)
  curl
  ssl
  crypto
  SceNet_stub
  SceNetCtl_stub
  SceHttp_stub
  SceSsl_stub
  ScePromoterUtil_stub
  ScePower_stub
  SceRtc_stub
  SceSysmem_stub
  SceKernelModulemgr_stub
  SceProcessmgr_stub

  # graphics / fonts
  vita2d
  freetype
  png16
  jpeg
  bz2
  z
  zstd
  m

  # system stubs (after vita2d: it needs SceAppMgr_stub for sceSharedFb*/GetBudgetInfo)
  SceAppMgr_stub
  SceDisplay_stub
  SceGxm_stub
  SceCtrl_stub
  SceSysmodule_stub
  SceCommonDialog_stub
  SceAppUtil_stub
  SceMotion_stub
  ScePgf_stub
  ScePvf_stub
  SceIofilemgr_stub
  SceKernelThreadMgr_stub
  SceLibKernel_stub
  SceAudio_stub
  pthread
  c
)

vita_create_self(eboot.bin gyrovault UNSAFE)

vita_create_vpk(gyrovault.vpk ${VITA_TITLEID} eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE assets/cacert.pem assets/cacert.pem
)
