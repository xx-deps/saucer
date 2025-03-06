#pragma once

#include "window.hpp"

#include "win32.utils.hpp"

#include <utility>
#include <optional>

#include <windows.h>

namespace saucer
{
    struct window::impl
    {
        utils::window_handle hwnd;

      public:
        UINT prev_state;
        std::optional<window_decoration> prev_decoration;

      public:
        long styles{};
        bool titlebar{true};

      public:
        utils::handle<HICON, DestroyIcon> icon;
        std::optional<std::pair<int, int>> max_size, min_size;

      public:
        WNDPROC o_wnd_proc;

      public:
        static void set_style(HWND, long);
        static LRESULT CALLBACK wnd_proc(HWND, UINT, WPARAM, LPARAM);
        static void OnPaint(HWND hwnd, HDC hdc);
    };
} // namespace saucer
