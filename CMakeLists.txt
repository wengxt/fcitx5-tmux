cmake_minimum_required(VERSION 3.0)

project(fcitx5-tmux)

add_executable(fcitx5-tmux main.cpp)

install(TARGETS fcitx5-tmux RUNTIME DESTINATION bin)
