# CsvTC — Total Commander WLX plugin for viewing and editing CSV files.
#
# Copyright (c) 2026 LordDog
# Open source — feel free to use, modify, and distribute.
# Credits are appreciated but not required.

cmake_minimum_required(VERSION 3.25)
project(CsvTC VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statische CRT: keine Abhängigkeit vom VC++-Redistributable beim Anwender.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WLX_SUFFIX ".wlx64")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(WLX_SUFFIX ".wlx")
else()
  message(FATAL_ERROR "Nur x64 und x86 werden unterstützt.")
endif()

set(DIST_DIR "${CMAKE_SOURCE_DIR}/dist")
set(COMMON_DEFS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
set(COMMON_OPTS /W4 /WX- /MP /utf-8 /permissive-)

# ── Datenmodell (ohne UI, von Plugin und Tests genutzt) ──────────────────────
add_library(csvtc_core STATIC src/CsvDocument.cpp src/Lang.cpp)
target_include_directories(csvtc_core PUBLIC src)
target_compile_definitions(csvtc_core PUBLIC ${COMMON_DEFS})
target_compile_options(csvtc_core PRIVATE ${COMMON_OPTS})

# ── Lister-Plugin (DLL) ──────────────────────────────────────────────────────
add_library(CsvTC SHARED
  src/plugin.cpp
  src/GridView.cpp
  src/Theme.cpp
  src/Settings.cpp
  src/csvtc.def
)
target_include_directories(CsvTC PRIVATE include src)
target_link_libraries(CsvTC PRIVATE csvtc_core user32 gdi32 comctl32 uxtheme advapi32)
target_compile_definitions(CsvTC PRIVATE CSVTC_VERSION="${PROJECT_VERSION}")
target_compile_options(CsvTC PRIVATE ${COMMON_OPTS} /Zi)
# Debug-Symbole auch im Release (PDB bleibt im Build-Ordner, nicht in dist/).
target_link_options(CsvTC PRIVATE /DEBUG /OPT:REF /OPT:ICF)
set_target_properties(CsvTC PROPERTIES
  OUTPUT_NAME "csvtc"
  SUFFIX      "${WLX_SUFFIX}"
  PREFIX      ""
  PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pdb"
  RUNTIME_OUTPUT_DIRECTORY         "${DIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${DIST_DIR}"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${DIST_DIR}"
)

add_custom_command(TARGET CsvTC POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/pluginst.inf" "${DIST_DIR}/pluginst.inf"
  VERBATIM
)

# ── Tests für das Datenmodell ────────────────────────────────────────────────
add_executable(csvtest tests/csvtest.cpp)
target_link_libraries(csvtest PRIVATE csvtc_core)
target_compile_options(csvtest PRIVATE ${COMMON_OPTS})
set_target_properties(csvtest PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
)
enable_testing()
add_test(NAME csvtest COMMAND csvtest)

# ── Test-Host (lädt das Plugin ohne Total Commander) ─────────────────────────
add_executable(harness WIN32 harness/harness.cpp)
target_include_directories(harness PRIVATE include)
target_link_libraries(harness PRIVATE user32 gdi32 gdiplus shell32 ole32)
target_compile_definitions(harness PRIVATE ${COMMON_DEFS})
target_compile_options(harness PRIVATE /W4 /utf-8)
set_target_properties(harness PROPERTIES
  OUTPUT_NAME "harness${WLX_SUFFIX}"
  SUFFIX      ".exe"
  RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin"
)
