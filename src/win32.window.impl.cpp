#include "win32.window.impl.hpp"

#include "win32.app.impl.hpp"
#include <iostream>
#include <windowsx.h>
#include <winuser.h>

namespace saucer
{
    void window::impl::set_style(HWND hwnd, long style)
    {
        auto current = GetWindowLongPtr(hwnd, GWL_STYLE);

        if (current & WS_VISIBLE)
        {
            style |= WS_VISIBLE;
        }

        SetWindowLongPtr(hwnd, GWL_STYLE, style);
    }

    LRESULT CALLBACK window::impl::wnd_proc(HWND hwnd, UINT msg, WPARAM w_param, LPARAM l_param)
    {
        auto userdata = GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        auto *window  = reinterpret_cast<saucer::window *>(userdata);

        if (!window)
        {
            return DefWindowProcW(hwnd, msg, w_param, l_param);
        }

        const auto &impl = window->m_impl;

        switch (msg)
        {
        case WM_STYLECHANGED: {
            auto &prev         = window->m_impl->prev_decoration;
            const auto current = window->decoration();

            if (prev.has_value() && prev.value() == current)
            {
                break;
            }

            prev.emplace(current);
            window->m_events.at<window_event::decorated>().fire(current);

            break;
        }
        case WM_NCCALCSIZE:
            if (!window->m_impl->titlebar)
            {
                auto *const rect = w_param ? std::addressof(reinterpret_cast<NCCALCSIZE_PARAMS *>(l_param)->rgrc[0])
                                           : reinterpret_cast<RECT *>(l_param);

                const auto maximized = window->maximized();
                const auto keep      = !maximized || rect->top >= 0;

                WINDOWINFO info{};
                GetWindowInfo(hwnd, &info);

                rect->top += keep ? maximized ? 0 : 1 : static_cast<LONG>(info.cxWindowBorders);
                rect->bottom -= static_cast<LONG>(info.cyWindowBorders);

                rect->left += static_cast<LONG>(info.cxWindowBorders);
                rect->right -= static_cast<LONG>(info.cxWindowBorders);

                return 0;
            }
            break;
        case WM_GETMINMAXINFO: {
            auto *info = reinterpret_cast<MINMAXINFO *>(l_param);

            if (auto min_size = window->m_impl->min_size; min_size)
            {
                auto [min_x, min_y]  = min_size.value();
                info->ptMinTrackSize = {.x = min_x, .y = min_y};
            }

            if (auto max_size = window->m_impl->max_size; max_size)
            {
                auto [max_x, max_y]  = max_size.value();
                info->ptMaxTrackSize = {.x = max_x, .y = max_y};
            }

            break;
        }
        case WM_NCACTIVATE:
            window->m_events.at<window_event::focus>().fire(w_param);
            break;
        case WM_SIZE: {
            switch (w_param)
            {
            case SIZE_MAXIMIZED:
                window->m_impl->prev_state = SIZE_MAXIMIZED;
                window->m_events.at<window_event::maximize>().fire(true);
                break;
            case SIZE_MINIMIZED:
                window->m_impl->prev_state = SIZE_MINIMIZED;
                window->m_events.at<window_event::minimize>().fire(true);
                break;
            case SIZE_RESTORED:
                switch (window->m_impl->prev_state)
                {
                case SIZE_MAXIMIZED:
                    window->m_events.at<window_event::maximize>().fire(false);
                    break;
                case SIZE_MINIMIZED:
                    window->m_events.at<window_event::minimize>().fire(false);
                    break;
                }

                window->m_impl->prev_state = SIZE_RESTORED;
                break;
            }

            auto [width, height] = window->size();
            window->m_events.at<window_event::resize>().fire(width, height);

            break;
        }
        case WM_CLOSE: {
            if (window->m_events.at<window_event::close>().until(policy::block))
            {
                return 0;
            }

            auto parent = window->m_parent;

            window->hide();
            window->m_events.at<window_event::closed>().fire();

            auto &instances = parent->native<false>()->instances;
            instances.erase(hwnd);

            if (!std::ranges::any_of(instances | std::views::values, std::identity{}))
            {
                parent->quit();
            }

            return 0;
        }
        case WM_NCHITTEST: {

            // POINT pt = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            // ScreenToClient(hwnd, &pt);

            // // Check if the point is over a non-transparent element in WebView2
            // BOOL isTransparent = TRUE;
            POINT pt;
            pt.x = GET_X_LPARAM(l_param);
            pt.y = GET_Y_LPARAM(l_param);

            std::cout << "WM_NCHITTEST" << std::endl;
        }
        case WM_PAINT: {

            // POINT pt = {GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
            // ScreenToClient(hwnd, &pt);

            // // Check if the point is over a non-transparent element in WebView2
            // BOOL isTransparent = TRUE;
            std::cout << "WM_PAINT" << std::endl;
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            impl->on_paint(hwnd, hdc);

            EndPaint(hwnd, &ps);
        }
        case WM_LBUTTONDOWN: {
            POINT pt;
            pt.x = GET_X_LPARAM(l_param);
            pt.y = GET_Y_LPARAM(l_param);
            std::cout << "WM_LBUTTONDOWN" << pt.x << "," << pt.y << std::endl;
            impl->on_mouse_down_left(hwnd, pt);
        }
        case WM_LBUTTONUP: {
            POINT pt;
            pt.x = GET_X_LPARAM(l_param);
            pt.y = GET_Y_LPARAM(l_param);
            std::cout << "WM_LBUTTONUP" << pt.x << "," << pt.y << std::endl;
            impl->on_mouse_up_left(hwnd, pt);
        }
        case WM_MOUSELEAVE: {
            POINT pt;
            pt.x = GET_X_LPARAM(l_param);
            pt.y = GET_Y_LPARAM(l_param);
            std::cout << "WM_MOUSELEAVE" << pt.x << "," << pt.y << std::endl;
            impl->on_mouse_leave(hwnd, pt);
        }
        case WM_MOUSEMOVE: {
            POINT pt;
            pt.x                = GET_X_LPARAM(l_param);
            pt.y                = GET_Y_LPARAM(l_param);
            bool isMousePressed = l_param & MK_LBUTTON;
            std::cout << "WM_MOUSEMOVE" << pt.x << "," << pt.y << std::endl;
            impl->on_mouse_move(hwnd, pt, isMousePressed);
        }
        }

        return CallWindowProcW(impl->o_wnd_proc, hwnd, msg, w_param, l_param);
    }
    void window::impl::on_paint(HWND hwnd, HDC hdc)
    {
        // create mem dc
        RECT rect;
        GetClientRect(hwnd, &rect);
        int w              = rect.right - rect.left;
        int h              = rect.bottom - rect.top;
        HDC hMemDC         = CreateCompatibleDC(hdc);
        HBITMAP hMemBitmap = CreateCompatibleBitmap(hdc, w, h);
        HBITMAP oldBM      = (HBITMAP)SelectObject(hMemDC, hMemBitmap);

        // draw
        Gdiplus::Graphics graphics(hMemDC);
        auto color = Gdiplus::Color(0xffff0000);
        if (isDragging)
        {
            color = Gdiplus::Color(0xff0000ff);
        }
        Gdiplus::SolidBrush brush(color);
        graphics.FillRectangle(&brush, w / 3, 0, w / 3, h);
        // alpha
        POINT ptSrc    = {0, 0};
        SIZE szLayered = {w, h};
        BLENDFUNCTION bf;
        bf.AlphaFormat         = AC_SRC_ALPHA;
        bf.BlendFlags          = 0;
        bf.BlendOp             = AC_SRC_OVER;
        bf.SourceConstantAlpha = 255;
        ::UpdateLayeredWindow(hwnd, hdc, nullptr, &szLayered, hMemDC, &ptSrc, 0, &bf, ULW_ALPHA);

        // clear
        SelectObject(hMemDC, oldBM);
        DeleteObject(hMemBitmap);
        DeleteDC(hMemDC);
    }
    void window::impl::on_mouse_down_left(HWND hwnd, POINT pt)
    {
        RECT rect;
        GetClientRect(hwnd, &rect);
        int w = rect.right - rect.left;
        // int h = rect.bottom - rect.top;
        if (pt.x < w / 2)
        {
        }

        isDragging = true;
        SetCapture(hwnd);
    }
    void window::impl::on_mouse_up_left(HWND, POINT)
    {
        isDragging = false;
    }
    void window::impl::on_mouse_move(HWND hwnd, POINT, bool)
    {
        // TRACKMOUSEEVENT tme;
        // tme.cbSize    = sizeof(TRACKMOUSEEVENT);
        // tme.dwFlags   = TME_LEAVE;
        // tme.hwndTrack = hwnd;
        // _TrackMouseEvent(&tme);

        if (isDragging)
        {
            GetCursorPos(&cursorNow);
            int dx         = (cursorNow.x - cursorPrevious.x);
            int dy         = (cursorNow.y - cursorPrevious.y);
            cursorPrevious = cursorNow;
            RECT rect;
            GetWindowRect(hwnd, &rect);

            int xNext = rect.left += dx;
            int yNext = rect.top += dy;
            SetWindowPos(hwnd, nullptr, xNext, yNext, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
            return;
        }
    }

    void window::impl::on_mouse_enter(HWND, POINT)
    {
        // isDragging = false;
    }

    void window::impl::on_mouse_leave(HWND, POINT)
    {
        // isDragging = false;
    }
} // namespace saucer
