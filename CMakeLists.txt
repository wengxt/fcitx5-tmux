cmake_minimum_required(VERSION 3.0)

project(fcitx5-tmux)

include(FeatureSummary)
include(GNUInstallDirs)

find_package(Fcitx5Utils REQUIRED)

include("${FCITX_INSTALL_CMAKECONFIG_DIR}/Fcitx5Utils/Fcitx5CompilerSettings.cmake")

add_subdirectory(src)

configure_file(fcitx5.tmux.in fcitx5.tmux @ONLY)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/fcitx5.tmux DESTINATION ${CMAKE_INSTALL_DATADIR}/tmux-fcitx5)
