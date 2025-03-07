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

      public:
        void on_paint(HWND hwnd, HDC hdc);
        void on_mouse_down_left(HWND hwnd, POINT pt);
        void on_mouse_button_left(HWND hwnd, POINT pt);
        void on_mouse_move(HWND hwnd, POINT pt, bool pressed);
        void on_mouse_enter(HWND hwnd, POINT pt);
        void on_mouse_leave(HWND hwnd, POINT pt);

      private:
        bool isDragging        = false;
        POINT cursorPrevious   = {0, 0};
        POINT cursorNow        = {0, 0};
        POINT windowPostionNow = {0, 0};
    };
} // namespace saucer
