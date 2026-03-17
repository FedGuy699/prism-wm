#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <vector>
#include <algorithm>
#include <unistd.h>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <sstream>

Display* dpy;
Window root;

struct Client {
    Window win;
    bool floating = false;
};

std::vector<Client> clients;

struct Keybind {
    unsigned int mod;
    KeySym key;
    std::string action;
    std::string argument;
};

std::vector<Keybind> keybinds;
std::vector<std::string> startup_cmds;

bool resize_mode = false;

std::string config_path() {
    const char* home = getenv("HOME");
    return std::string(home) + "/.config/prismwm/config";
}

unsigned int mod_from_token(const std::string& tok) {
    if (tok == "Super") return Mod4Mask;
    if (tok == "Shift") return ShiftMask;
    if (tok == "Ctrl") return ControlMask;
    if (tok == "Alt") return Mod1Mask;
    return 0;
}

void parse_key_combo(const std::string& combo, unsigned int& mod, KeySym& key) {
    std::stringstream ss(combo);
    std::string token;
    mod = 0;

    while (std::getline(ss, token, '+')) {
        unsigned int m = mod_from_token(token);
        if (m) mod |= m;
        else key = XStringToKeysym(token.c_str());
    }
}

void ensure_default_config() {
    std::string path = config_path();
    std::string dir = path.substr(0, path.find_last_of('/'));
    system(("mkdir -p " + dir).c_str());

    std::ifstream check(path);
    if (check.good()) return;

    std::ofstream out(path);
    out << "# keybinds\n";
    out << "Super+Return exec alacritty\n";
    out << "Super+Shift+q kill\n";
    out << "Super+f toggle_float\n";
    out << "Super+r resize_mode\n";
    out << "\n# startup\n";
    out << "exec picom\n";
}

void load_config() {
    ensure_default_config();

    std::ifstream file(config_path());
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string first;
        iss >> first;

        if (first == "exec") {
            std::string cmd;
            std::getline(iss, cmd);
            if (!cmd.empty() && cmd[0] == ' ') cmd.erase(0, 1);
            startup_cmds.push_back(cmd);
            continue;
        }

        std::string action;
        iss >> action;

        Keybind kb{};
        parse_key_combo(first, kb.mod, kb.key);
        kb.action = action;

        if (action == "exec") {
            std::getline(iss, kb.argument);
            if (!kb.argument.empty() && kb.argument[0] == ' ')
                kb.argument.erase(0, 1);
        }

        keybinds.push_back(kb);
    }
}

int xerror(Display*, XErrorEvent*) { return 0; }

Client* get_client(Window w) {
    for (auto& c : clients)
        if (c.win == w) return &c;
    return nullptr;
}

void spawn(const std::string& cmd) {
    if (fork() == 0) {
        setsid();
        execl("/bin/sh", "sh", "-c", cmd.c_str(), NULL);
        exit(0);
    }
}

void run_startup() {
    for (auto& cmd : startup_cmds)
        spawn(cmd);
}

void kill_window(Window w) {
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    Atom wm_protocols = XInternAtom(dpy, "WM_PROTOCOLS", False);

    XEvent ev{};
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = wm_protocols;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = wm_delete;
    ev.xclient.data.l[1] = CurrentTime;

    XSendEvent(dpy, w, False, NoEventMask, &ev);
}

void tile_windows() {
    std::vector<Window> tiled;
    for (auto& c : clients)
        if (!c.floating)
            tiled.push_back(c.win);

    if (tiled.empty()) return;

    int screen = DefaultScreen(dpy);
    int sw = DisplayWidth(dpy, screen);
    int sh = DisplayHeight(dpy, screen);

    int n = tiled.size();

    for (int i = 0; i < n; ++i) {
        int x = (i * sw) / n;
        int w = sw / n;
        XMoveResizeWindow(dpy, tiled[i], x, 0, w, sh);
    }
}

void toggle_float(Window w) {
    Client* c = get_client(w);
    if (!c) return;

    c->floating = !c->floating;
    XRaiseWindow(dpy, w);
    tile_windows();
}

void resize_window(Window w, int dx, int dy) {
    XWindowAttributes wa;
    XGetWindowAttributes(dpy, w, &wa);

    int new_w = std::max(50, wa.width + dx);
    int new_h = std::max(50, wa.height + dy);

    XResizeWindow(dpy, w, new_w, new_h);
}

int main() {
    dpy = XOpenDisplay(nullptr);
    if (!dpy) return 1;

    XSetErrorHandler(xerror);
    load_config();

    root = DefaultRootWindow(dpy);

    XSelectInput(dpy, root,
        SubstructureRedirectMask |
        SubstructureNotifyMask |
        KeyPressMask |
        EnterWindowMask |
        ButtonPressMask |
        ButtonMotionMask
    );

    for (auto& kb : keybinds) {
        KeyCode code = XKeysymToKeycode(dpy, kb.key);
        XGrabKey(dpy, code, kb.mod, root, True, GrabModeAsync, GrabModeAsync);
    }

    run_startup();

    XEvent ev;
    Window drag_win = 0;

    while (true) {
        XNextEvent(dpy, &ev);

        switch (ev.type) {
            case MapRequest: {
                auto* e = &ev.xmaprequest;
                clients.push_back({e->window, false});
                XSelectInput(dpy, e->window, EnterWindowMask);
                XMapWindow(dpy, e->window);
                tile_windows();
                break;
            }

            case DestroyNotify:
            case UnmapNotify: {
                Window w = (ev.type == DestroyNotify) ? ev.xdestroywindow.window : ev.xunmap.window;
                clients.erase(std::remove_if(clients.begin(), clients.end(), [&](Client& c){ return c.win == w; }), clients.end());
                tile_windows();
                break;
            }

            case EnterNotify:
                XSetInputFocus(dpy, ev.xcrossing.window, RevertToPointerRoot, CurrentTime);
                break;

            case ButtonPress:
                if (ev.xbutton.state & Mod4Mask) {
                    drag_win = ev.xbutton.subwindow;
                }
                break;

            case MotionNotify:
                if (drag_win) {
                    XMoveWindow(dpy, drag_win, ev.xmotion.x_root, ev.xmotion.y_root);
                }
                break;

            case ButtonRelease:
                drag_win = 0;
                break;

            case KeyPress: {
                auto* e = &ev.xkey;
                Window target = e->subwindow ? e->subwindow : e->window;

                for (auto& kb : keybinds) {
                    KeyCode code = XKeysymToKeycode(dpy, kb.key);

                    if (e->keycode == code && (e->state & kb.mod) == kb.mod) {
                        if (kb.action == "exec") spawn(kb.argument);
                        else if (kb.action == "kill") kill_window(target);
                        else if (kb.action == "toggle_float") toggle_float(target);
                        else if (kb.action == "resize_mode") resize_mode = !resize_mode;
                    }
                }

                if (resize_mode) {
                    if (e->keycode == XKeysymToKeycode(dpy, XK_Left)) resize_window(target, -20, 0);
                    if (e->keycode == XKeysymToKeycode(dpy, XK_Right)) resize_window(target, 20, 0);
                    if (e->keycode == XKeysymToKeycode(dpy, XK_Up)) resize_window(target, 0, -20);
                    if (e->keycode == XKeysymToKeycode(dpy, XK_Down)) resize_window(target, 0, 20);
                }
                break;
            }
        }
    }
}
