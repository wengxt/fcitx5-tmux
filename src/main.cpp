/*
 * SPDX-FileCopyrightText: 2022~2022 CSSlayer <wengxt@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */
#include <unistd.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/dbus/bus.h>
#include <fcitx-utils/dbus/matchrule.h>
#include <fcitx-utils/dbus/servicewatcher.h>
#include <sys/syscall.h>

using namespace fcitx;

std::string toTmuxKey(const Key &key) {
    std::string result;
    if (key.states() & KeyState::Alt) {
        result.append("M-");
    }
    if (key.states() & KeyState::Ctrl) {
        result.append("C-");
    }

    const static std::unordered_map<KeySym, std::string> tmuxKeymap = {
        {FcitxKey_F1, "F1"},
        {FcitxKey_F2, "F2"},
        {FcitxKey_F3, "F3"},
        {FcitxKey_F4, "F4"},
        {FcitxKey_F5, "F5"},
        {FcitxKey_F6, "F6"},
        {FcitxKey_F7, "F7"},
        {FcitxKey_F8, "F8"},
        {FcitxKey_F9, "F9"},
        {FcitxKey_F10, "F10"},
        {FcitxKey_F11, "F11"},
        {FcitxKey_F12, "F12"},
        {FcitxKey_Insert, "Insert"},
        {FcitxKey_Delete, "Delete"},
        {FcitxKey_Home, "Home"},
        {FcitxKey_End, "End"},
        {FcitxKey_Page_Down, "PageDown"},
        {FcitxKey_Page_Up, "PageUp"},
        {FcitxKey_Tab, "Tab"},
        // {FcitxKey_BTab, ""},
        {FcitxKey_space, "Space"},
        {FcitxKey_BackSpace, "BSpace"},
        {FcitxKey_Return, "Enter"},
        {FcitxKey_Escape, "Escape"},
        {FcitxKey_Up, "Up"},
        {FcitxKey_Down, "Down"},
        {FcitxKey_Left, "Left"},
        {FcitxKey_Right, "Right"},
        {FcitxKey_KP_Divide, "KP/"},
        {FcitxKey_KP_Multiply, "KP*"},
        {FcitxKey_KP_Subtract, "KP-"},
        {FcitxKey_KP_7, "KP7"},
        {FcitxKey_KP_8, "KP8"},
        {FcitxKey_KP_9, "KP9"},
        {FcitxKey_KP_Add, "KP+"},
        {FcitxKey_KP_4, "KP4"},
        {FcitxKey_KP_5, "KP5"},
        {FcitxKey_KP_6, "KP6"},
        {FcitxKey_KP_1, "KP1"},
        {FcitxKey_KP_2, "KP2"},
        {FcitxKey_KP_3, "KP3"},
        {FcitxKey_KP_Enter, "KPEnter"},
        {FcitxKey_KP_0, "KP0"},
        {FcitxKey_KP_Decimal, "KP."},
    };

    if (auto iter = tmuxKeymap.find(key.sym()); iter != tmuxKeymap.end()) {
        result.append(iter->second);
    } else if (key.sym() >= FcitxKey_space &&
               key.sym() <= FcitxKey_asciitilde) {
        result.push_back(static_cast<char>(key.sym()));
    } else {
        return "";
    }
    return result;
}

class InputContextProxy : public dbus::ObjectVTable<InputContextProxy> {
public:
    InputContextProxy(dbus::Bus &bus) : bus_(bus), watcher_(bus) {
        handler_ = watcher_.watchService("org.freedesktop.portal.Fcitx",
                                         [this](const std::string &,
                                                const std::string &oldName,
                                                const std::string &newName) {
                                             if (!oldName.empty()) {
                                                 reset();
                                             }
                                             if (!newName.empty()) {
                                                 init();
                                             }
                                         });
        bus_.addObjectVTable("/tmux", "org.fcitx.Fcitx.TmuxProxy", *this);
    }

    void reset() {
        path_ = dbus::ObjectPath();
        createIC_.reset();
    }
    void init() {
        auto call = bus_.createMethodCall("org.freedesktop.portal.Fcitx",
                                          "/org/freedesktop/portal/inputmethod",
                                          "org.fcitx.Fcitx.InputMethod1",
                                          "CreateInputContext");
        std::vector<dbus::DBusStruct<std::string, std::string>> params;
        params.emplace_back("program", "tmux");
        params.emplace_back("display", "tmux");
        call << params;
        createIC_ = call.callAsync(0, [this](dbus::Message &reply) {
            if (reply.isError()) {
                return true;
            }
            // Skip UUID;
            reply >> path_;
            bus_.createMethodCall("org.freedesktop.portal.Fcitx",
                                  path_.path().c_str(),
                                  "org.fcitx.Fcitx.InputContext1", "FocusIn")
                .send();
            auto cap = bus_.createMethodCall(
                "org.freedesktop.portal.Fcitx", path_.path().c_str(),
                "org.fcitx.Fcitx.InputContext1", "SetCapability");
            cap << static_cast<uint64_t>(
                CapabilityFlags(CapabilityFlag::ClientSideInputPanel));
            cap.send();
            signals_.push_back(bus_.addMatch(
                dbus::MatchRule("org.freedesktop.portal.Fcitx", path_.path(),
                                "org.fcitx.Fcitx.InputContext1",
                                "CommitString"),
                [](dbus::Message &msg) {
                    if (msg.isError()) {
                        return true;
                    }
                    std::string text;
                    msg >> text;
                    startProcess({"tmux", "send-key", "-l", text});
                    return true;
                }));
            signals_.push_back(bus_.addMatch(
                dbus::MatchRule("org.freedesktop.portal.Fcitx", path_.path(),
                                "org.fcitx.Fcitx.InputContext1",
                                "UpdateClientSideUI"),
                [](dbus::Message &msg) {
                    // a(si)ia(si)a(si)a(ss)iibb
                    if (msg.isError()) {
                        return true;
                    }
                    std::vector<dbus::DBusStruct<std::string, int>>
                        preeditStrings, auxUpStrings, auxDownStrings;
                    std::vector<dbus::DBusStruct<std::string, std::string>>
                        candidates;
                    int cursorIndex;
                    int cursor;
                    msg >> preeditStrings >> cursor >> auxUpStrings >>
                        auxDownStrings >> candidates >> cursorIndex;
                    std::string result;
                    for (auto &item : auxUpStrings) {
                        result.append(std::get<std::string>(item));
                    }
                    if (cursor >= 0) {
                        cursor += result.size();
                    }
                    for (auto &item : preeditStrings) {
                        result.append(std::get<std::string>(item));
                    }
                    if (cursor >= 0) {
                        result.insert(cursor, "|");
                    }
                    result.append(" ");
                    int idx = 0;
                    for (auto &item : candidates) {
                        if (idx == cursorIndex) {
                            result.append("*");
                        }
                        result.append(std::get<0>(item));
                        result.append(std::get<1>(item));
                        result.append(" ");
                        idx += 1;
                    }
                    startProcess(
                        {"tmux", "set-option", "-gq", "@fcitx5", result});
                    return true;
                }));
            signals_.push_back(bus_.addMatch(
                dbus::MatchRule("org.freedesktop.portal.Fcitx", path_.path(),
                                "org.fcitx.Fcitx.InputContext1", "ForwardKey"),
                [](dbus::Message &msg) {
                    if (msg.isError()) {
                        return true;
                    }
                    uint32_t sym, states;
                    bool isRelease;
                    msg >> sym >> states >> isRelease;
                    if (!isRelease) {
                        Key key(static_cast<KeySym>(sym), KeyStates(states));
                        auto keyStr = toTmuxKey(key);
                        if (!keyStr.empty()) {
                            startProcess({"tmux", "send-key", keyStr});
                        } else {
                            if (!key.states().test(KeyState::Ctrl)) {
                                if (auto str = Key::keySymToUTF8(key.sym());
                                    !str.empty()) {
                                    startProcess(
                                        {"tmux", "send-key", "-l", str});
                                }
                            }
                        }
                    }
                    return true;
                }));
            createIC_.reset();
            return true;
        });
    }

    void processKeyEvent(uint32_t sym, uint32_t state) {
        auto call = bus_.createMethodCall(
            "org.freedesktop.portal.Fcitx", path_.path().c_str(),
            "org.fcitx.Fcitx.InputContext1", "ProcessKeyEvent");
        call << sym << 0u << state << false << 0u;
        auto reply = call.call(0);
        Key key{KeySym(sym), KeyStates(state)};
        bool handled = false;
        if (!reply.isError()) {
            reply >> handled;
        }

        if (!handled) {
            auto keyStr = toTmuxKey(key);
            if (!keyStr.empty()) {
                startProcess({"tmux", "send-key", keyStr});
            }
        }
    }

private:
    FCITX_OBJECT_VTABLE_METHOD(processKeyEvent, "ProcessKeyEvent", "uu", "");

    dbus::Bus &bus_;
    dbus::ObjectPath path_;
    dbus::ServiceWatcher watcher_;
    std::unique_ptr<HandlerTableEntry<dbus::ServiceWatcherCallback>> handler_;
    std::unique_ptr<dbus::Slot> createIC_;
    std::vector<std::unique_ptr<dbus::Slot>> signals_;
};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        return 1;
    }

    UnixFD fd;
    pid_t tmuxPid = 0;
    try {
        tmuxPid = std::stoi(argv[1]);
        fd = UnixFD::own(syscall(SYS_pidfd_open, tmuxPid, 0));
    } catch (...) {
        return 1;
    }

    if (!fd.isValid()) {
        return 1;
    }

    EventLoop event;
    dbus::Bus bus{dbus::BusType::Session};
    bus.attachEventLoop(&event);

    if (!bus.requestName("org.fcitx.Fcitx5.Tmux-" + std::to_string(tmuxPid),
                         Flags<dbus::RequestNameFlag>{})) {
        return 1;
    }

    auto ioEvent =
        event.addIOEvent(fd.fd(), {IOEventFlag::In, IOEventFlag::Err},
                         [&event](EventSource *, int, IOEventFlags) {
                             event.exit();
                             return true;
                         });

    InputContextProxy proxy(bus);

    event.exec();
    return 0;
}
