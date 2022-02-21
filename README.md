A Tmux Fcitx client
====================

Dependenices:
Need fcitx5 to compile.

Compile:
```
mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/usr
cmake --build .
sudo cmake --install .
```

Usage:
Add following content to ~/.tmux.conf
```
run /usr/share/tmux-fcitx5/fcitx5.tmux
```
