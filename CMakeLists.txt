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

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Network Widgets)

# Everything that does not need a window. Generating samples needs optimising even in a Debug build.
add_library(gpssimcore STATIC
    src/sim/Gps.h          src/sim/Gps.cpp
    src/sim/Lnav.h         src/sim/Lnav.cpp
    src/sim/Simulator.h    src/sim/Simulator.cpp
    src/sim/IqOutput.h     src/sim/IqOutput.cpp
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
)
qt_add_resources(GPSSim "icon"
    PREFIX "/"
    FILES resources/GPSSim.ico
)
if(WIN32)
    target_sources(GPSSim PRIVATE resources/GPSSim.rc)    # icon of the .exe in Explorer
endif()
target_link_libraries(GPSSim PRIVATE gpssimcore Qt6::Widgets)
set_target_properties(GPSSim PROPERTIES WIN32_EXECUTABLE TRUE)

qt_add_executable(gpssim_cli src/cli/main_cli.cpp)
target_link_libraries(gpssim_cli PRIVATE gpssimcore)

include(GNUInstallDirs)
install(TARGETS GPSSim gpssim_cli
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(GPSSim)
