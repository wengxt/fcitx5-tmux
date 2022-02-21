
add_executable(fcitx5-tmux main.cpp)
target_link_libraries(fcitx5-tmux Fcitx5::Utils)

install(TARGETS fcitx5-tmux RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR})
