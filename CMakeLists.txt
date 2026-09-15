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
set(VITA_VERSION  "01.00")

option(GV_TEST_INPUT "Emulator-only: d-pad adds tilt (never in release)" OFF)

file(GLOB GV_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

add_executable(gyrovault ${GV_SOURCES})
target_include_directories(gyrovault PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(gyrovault PRIVATE -Wall -Wextra -O2)
if(GV_TEST_INPUT)
  target_compile_definitions(gyrovault PRIVATE GV_TEST_INPUT)
endif()

target_link_libraries(gyrovault
  # ---- UPDATER LIBS (provisional; orchestrator reconciles with the updater agent) ----
  # (libcurl.pc Libs: curl ... ssl crypto z zstd; updater also uses ScePromoterUtil)
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
  # ---- END UPDATER LIBS ----

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
