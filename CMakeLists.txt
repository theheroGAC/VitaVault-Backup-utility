cmake_minimum_required(VERSION 3.10)

# VitaSDK defines
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK environment variable pointing to your SDK path!")
  endif()
endif()

project(VitaVault VERSION 1.0 LANGUAGES C)

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaVault")
set(VITA_TITLEID  "VTBK00001")
set(VITA_VERSION  "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -fno-lto -Wl,-z,noexecstack")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -s CATEGORY=gdb")
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2808000000000000")

include_directories(
  ${PROJECT_SOURCE_DIR}
)

link_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(${PROJECT_NAME}
  main.c
  backup.c
  backup_logic.c
  ui.c
  ftp.c
  hash.c
  zip_utils.c
  usb.c
  language.c
  game_backup.c
)

target_link_libraries(${PROJECT_NAME}
  ftpvita
  vita2d
  zip
  bz2
  lzma
  zstd
  ssl
  crypto
  pthread
  SceIme_stub
  z
  c
  m
  png
  jpeg
  SceGxm_stub
  SceSysmodule_stub
  ScePgf_stub
  ScePvf_stub
  SceCtrl_stub
  SceCommonDialog_stub
  SceDisplay_stub
  SceRtc_stub
  SceNet_stub
  SceNetCtl_stub
  ScePower_stub
  SceLibKernel_stub
  SceIofilemgr_stub
  SceAppMgr_stub
  SceShellSvc_stub
  SceMtpIfDriver_stub
  SceUsbstorVStorDriver_stub
  SceUdcd_stub
  taihen_stub
  SceVshBridge_stub
)

vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)

vita_create_vpk(${PROJECT_NAME}.vpk
  ${VITA_TITLEID}
  eboot.bin
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}

  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml

  # Language files
  FILE lang/en.txt lang/en.txt
  FILE lang/it.txt lang/it.txt
  FILE lang/jpn.txt lang/jpn.txt
  FILE lang/es.txt lang/es.txt
  FILE lang/fr.txt lang/fr.txt
  FILE lang/de.txt lang/de.txt

  # Pre-compiled USB modules
  FILE module/kernel.skprx module/kernel.skprx
  FILE module/patch.skprx module/patch.skprx
  FILE module/usbdevice.skprx module/usbdevice.skprx
  FILE module/user.suprx module/user.suprx
  FILE module/umass.skprx module/umass.skprx
)
