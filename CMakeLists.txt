cmake_minimum_required(VERSION 3.21)

project(GPSSim VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Network Widgets OpenGL OpenGLWidgets)

# Everything that does not need a window. Generating samples needs optimising even in a Debug build.
add_library(gpssimcore STATIC
    src/sim/Gps.h          src/sim/Gps.cpp
    src/sim/Almanac.h      src/sim/Almanac.cpp
    src/sim/GpsTime.h      src/sim/GpsTime.cpp
    src/sim/Location.h     src/sim/Location.cpp
    src/sim/Lnav.h         src/sim/Lnav.cpp
    src/sim/Simulator.h    src/sim/Simulator.cpp
    src/sim/IqOutput.h     src/sim/IqOutput.cpp
    src/sim/UsrpOutput.h   src/sim/UsrpOutput.cpp
    src/sim/HackRfOutput.h src/sim/HackRfOutput.cpp
)
target_include_directories(gpssimcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(gpssimcore PUBLIC Qt6::Core Qt6::Concurrent Qt6::Network)
target_compile_options(gpssimcore PRIVATE -O3)

qt_add_executable(GPSSim
    MANUAL_FINALIZATION
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    src/gui/SkyView.h
    src/gui/SkyView.cpp
    src/gui/GlobeView.h
    src/gui/GlobeView.cpp
    src/gui/TileStore.h
    src/gui/TileStore.cpp
    src/gui/BorderStore.h
    src/gui/BorderStore.cpp
    src/gui/CityStore.h
    src/gui/CityStore.cpp
    src/gui/GlobeDialog.h
    src/gui/GlobeDialog.cpp
    src/gui/GlobeDialog.ui
    src/sim/EthernetShare.h src/sim/UDP_rx_commands.h
)
qt_add_resources(GPSSim "icon"
    PREFIX "/"
    FILES resources/GPSSim.ico
)
if(WIN32)
    target_sources(GPSSim PRIVATE resources/GPSSim.rc)    # icon of the .exe in Explorer
endif()
target_link_libraries(GPSSim PRIVATE gpssimcore Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets)
set_target_properties(GPSSim PROPERTIES WIN32_EXECUTABLE TRUE)

qt_add_executable(gpssim_cli src/cli/main_cli.cpp)
target_link_libraries(gpssim_cli PRIVATE gpssimcore)

# The HackRF driver, found next to the executables. It is built from the HackRF sources (see the README), so it is
# optional here. It is loaded at run time, so nothing links to it.
set(GPSSIM_HACKRF_DIR ${CMAKE_SOURCE_DIR}/third_party/hackrf/windows_x64)
if(WIN32 AND EXISTS ${GPSSIM_HACKRF_DIR}/hackrf.dll)
    add_custom_target(hackrf_dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GPSSIM_HACKRF_DIR}/hackrf.dll ${GPSSIM_HACKRF_DIR}/libusb-1.0.dll
                $<TARGET_FILE_DIR:GPSSim>
        VERBATIM)
    add_dependencies(GPSSim hackrf_dll)
    add_dependencies(gpssim_cli hackrf_dll)
endif()

# Stands in for the UHD driver, to test the USRP output without a radio (see fake_uhd.cpp)
option(GPSSIM_BUILD_TESTS "Build the test helpers" OFF)
if(GPSSIM_BUILD_TESTS AND WIN32)
    add_library(fake_uhd SHARED tests/fake_uhd/fake_uhd.cpp)
    set_target_properties(fake_uhd PROPERTIES OUTPUT_NAME uhd PREFIX "" RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fake_uhd
                          AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    if(MINGW)
        target_link_options(fake_uhd PRIVATE -static)      # loads into a program that may not have the MinGW runtime beside it
    endif()
endif()
if(GPSSIM_BUILD_TESTS)
    # Transmits and receives back on one USRP, for a cable from TX/RX to RX2 (see usrp_loopback.cpp)
    qt_add_executable(usrp_loopback tests/usrp_loopback/usrp_loopback.cpp)
    target_link_libraries(usrp_loopback PRIVATE gpssimcore)
endif()

include(GNUInstallDirs)
install(TARGETS GPSSim gpssim_cli
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(GPSSim)
