#include "wv2.webview.impl.hpp"

#include "instantiate.hpp"
#include "win32.utils.hpp"

#include "win32.app.impl.hpp"
#include "win32.icon.impl.hpp"
#include "win32.window.impl.hpp"
#include "wv2.navigation.impl.hpp"

#include <ranges>
#include <cassert>
#include <filesystem>

#include <fmt/core.h>
#include <fmt/xchar.h>

#include <shlobj.h>
#include <WebView2EnvironmentOptions.h>

namespace saucer
{
    webview::webview(const preferences &prefs) : window(prefs), extensible(this), m_impl(std::make_unique<impl>())
    {
        static std::once_flag flag;
        std::call_once(flag, [] { register_scheme("saucer"); });

        m_impl->o_wnd_proc = utils::overwrite_wndproc(window::m_impl->hwnd.get(), impl::wnd_proc);
        m_impl->create_webview(m_parent, window::m_impl->hwnd.get(), prefs);

        m_impl->web_view->get_Settings(&m_impl->settings);
        m_impl->settings->put_IsStatusBarEnabled(false);

        auto resource_requested = [this](auto, auto *args)
        {
            m_impl->scheme_handler(args, this);
            return S_OK;
        };

        m_impl->web_view->add_WebResourceRequested(Callback<ResourceRequested>(resource_requested).Get(), nullptr);

        auto receive_message = [this](auto, auto *args)
        {
            utils::string_handle raw;
            args->TryGetWebMessageAsString(&raw.reset());

            auto message = utils::narrow(raw.get());
            m_parent->post([this, message = std::move(message)] { on_message(message); });

            return S_OK;
        };

        m_impl->web_view->add_WebMessageReceived(Callback<WebMessageHandler>(receive_message).Get(), nullptr);

        auto new_window = [this](auto, ICoreWebView2NewWindowRequestedEventArgs *args)
        {
            args->put_Handled(true);

            ComPtr<ICoreWebView2Deferral> deferral;
            args->GetDeferral(&deferral);

            auto func = [this, args, deferral]
            {
                auto request = navigation{{args}};

                m_events.at<web_event::navigate>().until(policy::block, request);
                deferral->Complete();
            };

            m_parent->post(func);

            return S_OK;
        };

        m_impl->web_view->add_NewWindowRequested(Callback<NewWindowRequest>(new_window).Get(), nullptr);

        auto navigation_starting = [this](auto, ICoreWebView2NavigationStartingEventArgs *args)
        {
            m_impl->dom_loaded = false;
            m_parent->post([this] { m_events.at<web_event::load>().fire(state::started); });

            auto request = navigation{{args}};

            if (m_events.at<web_event::navigate>().until(policy::block, request))
            {
                args->put_Cancel(true);
            }

            return S_OK;
        };

        m_impl->web_view->add_NavigationStarting(Callback<NavigationStarting>(navigation_starting).Get(), nullptr);

        auto on_loaded = [this](auto...)
        {
            m_impl->dom_loaded = true;

            for (const auto &[script, id] : m_impl->scripts)
            {
                if (script.time != load_time::ready)
                {
                    continue;
                }

                execute(script.code);
            }

            for (const auto &pending : m_impl->pending)
            {
                execute(pending);
            }

            m_impl->pending.clear();
            m_parent->post([this] { m_events.at<web_event::dom_ready>().fire(); });

            return S_OK;
        };

        m_impl->web_view->add_DOMContentLoaded(Callback<DOMLoaded>(on_loaded).Get(), nullptr);

        if (ComPtr<ICoreWebView2_15> webview; SUCCEEDED(m_impl->web_view.As(&webview)))
        {
            auto icon_received = [this](auto, auto *stream)
            {
                m_impl->favicon = icon{{std::shared_ptr<Gdiplus::Bitmap>(Gdiplus::Bitmap::FromStream(stream))}};
                m_events.at<web_event::favicon>().fire(m_impl->favicon);

                return S_OK;
            };

            auto icon_changed = [webview, icon_received](auto...)
            {
                webview->GetFavicon(COREWEBVIEW2_FAVICON_IMAGE_FORMAT_PNG, Callback<GetFavicon>(icon_received).Get());
                return S_OK;
            };

            webview->add_FaviconChanged(Callback<FaviconChanged>(icon_changed).Get(), nullptr);
        }

        set_dev_tools(false);

        if (ComPtr<ICoreWebView2Settings2> settings; !prefs.user_agent.empty() && SUCCEEDED(m_impl->settings.As(&settings)))
        {
            settings->put_UserAgent(utils::widen(prefs.user_agent).c_str());
        }

        if (ComPtr<ICoreWebView2Settings3> settings; SUCCEEDED(m_impl->settings.As(&settings)))
        {
            settings->put_AreBrowserAcceleratorKeysEnabled(false);
        }

        inject({.code = impl::inject_script(), .time = load_time::creation, .permanent = true});
    }

    webview::~webview()
    {
        std::ignore = utils::overwrite_wndproc(window::m_impl->hwnd.get(), m_impl->o_wnd_proc);
        m_impl->controller->Close();

        if (!m_impl->temp_path)
        {
            return;
        }

        // ICorWebView5s `add_BrowserProcessExited` fires rather unreliably and requries us to run the main-loop.
        // Thus, we wait for the webview process to exit instead.

        utils::handle<HANDLE, CloseHandle> handle = OpenProcess(SYNCHRONIZE, false, m_impl->browser_pid);
        WaitForSingleObject(handle.get(), 3000);

        std::error_code ec{};
        fs::remove_all(m_impl->temp_path.value(), ec);
    }

    icon webview::favicon() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return favicon(); });
        }

        return m_impl->favicon;
    }

    std::string webview::page_title() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return page_title(); });
        }

        utils::string_handle title;
        m_impl->web_view->get_DocumentTitle(&title.reset());

        return utils::narrow(title.get());
    }

    bool webview::dev_tools() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return dev_tools(); });
        }

        BOOL rtn{false};
        m_impl->settings->get_AreDevToolsEnabled(&rtn);

        return static_cast<bool>(rtn);
    }

    std::string webview::url() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return url(); });
        }

        utils::string_handle url;
        m_impl->web_view->get_Source(&url.reset());

        return utils::narrow(url.get());
    }

    bool webview::context_menu() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return context_menu(); });
        }

        BOOL rtn{false};
        m_impl->settings->get_AreDefaultContextMenusEnabled(&rtn);

        return static_cast<bool>(rtn);
    }

    color webview::background() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return background(); });
        }

        ComPtr<ICoreWebView2Controller2> controller;

        if (!SUCCEEDED(m_impl->controller.As(&controller)))
        {
            return {};
        }

        COREWEBVIEW2_COLOR color;
        controller->get_DefaultBackgroundColor(&color);

        return {color.R, color.G, color.B, color.A};
    }

    bool webview::force_dark_mode() const
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return force_dark_mode(); });
        }

        ComPtr<ICoreWebView2_13> webview;

        if (!SUCCEEDED(m_impl->web_view.As(&webview)))
        {
            return {};
        }

        ComPtr<ICoreWebView2Profile> profile;

        if (!SUCCEEDED(webview->get_Profile(&profile)))
        {
            return {};
        }

        COREWEBVIEW2_PREFERRED_COLOR_SCHEME scheme{};

        if (!SUCCEEDED(profile->get_PreferredColorScheme(&scheme)))
        {
            return {};
        }

        return scheme == COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK;
    }

    void webview::set_dev_tools(bool enabled)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, enabled] { return set_dev_tools(enabled); });
        }

        m_impl->settings->put_AreDevToolsEnabled(enabled);

        if (!enabled)
        {
            return;
        }

        m_impl->web_view->OpenDevToolsWindow();
    }

    void webview::set_context_menu(bool enabled)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, enabled] { return set_context_menu(enabled); });
        }

        m_impl->settings->put_AreDefaultContextMenusEnabled(enabled);
    }

    void webview::set_force_dark_mode(bool enabled)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, enabled] { return set_force_dark_mode(enabled); });
        }

        utils::set_immersive_dark(window::m_impl->hwnd.get(), enabled);

        ComPtr<ICoreWebView2_13> webview;

        if (!SUCCEEDED(m_impl->web_view.As(&webview)))
        {
            return;
        }

        ComPtr<ICoreWebView2Profile> profile;

        if (!SUCCEEDED(webview->get_Profile(&profile)))
        {
            return;
        }

        profile->put_PreferredColorScheme(enabled ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK
                                                  : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO);
    }

    void webview::set_background(const color &color)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, color] { return set_background(color); });
        }

        ComPtr<ICoreWebView2Controller2> controller;

        if (!SUCCEEDED(m_impl->controller.As(&controller)))
        {
            return;
        }

        const auto [r, g, b, a] = color;
        controller->put_DefaultBackgroundColor({.A = a, .R = r, .G = g, .B = b});
    }

    void webview::set_file(const fs::path &file)
    {
        auto path = fmt::format("file://{}", fs::canonical(file).string());
        set_url(path);
    }

    void webview::set_url(const std::string &url)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, url] { return set_url(url); });
        }

        m_impl->web_view->Navigate(utils::widen(url).c_str());
    }

    void webview::back()
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return back(); });
        }

        m_impl->web_view->GoBack();
    }

    void webview::forward()
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return forward(); });
        }

        m_impl->web_view->GoForward();
    }

    void webview::reload()
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return reload(); });
        }

        m_impl->web_view->Reload();
    }

    void webview::clear_scripts()
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this] { return clear_scripts(); });
        }

        for (auto it = m_impl->scripts.begin(); it != m_impl->scripts.end();)
        {
            const auto &[script, id] = *it;

            if (script.permanent)
            {
                ++it;
                continue;
            }

            if (!id.empty())
            {
                m_impl->web_view->RemoveScriptToExecuteOnDocumentCreated(id.c_str());
            }

            it = m_impl->scripts.erase(it);
        }
    }

    void webview::inject(const script &script)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, script] { return inject(script); });
        }

        if (script.time == load_time::ready)
        {
            m_impl->scripts.emplace_back(script, L"");
            return;
        }

        auto callback = [this, script](auto, LPCWSTR id)
        {
            m_impl->scripts.emplace_back(script, id);
            return S_OK;
        };

        auto source = script.code;

        if (script.frame == web_frame::top)
        {
            source = fmt::format(R"js(
            if (self === top)
            {{
                {}
            }}
            )js",
                                 source);
        }

        m_impl->web_view->AddScriptToExecuteOnDocumentCreated(utils::widen(source).c_str(),
                                                              Callback<ScriptInjected>(callback).Get());
    }

    void webview::execute(const std::string &code)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, code] { return execute(code); });
        }

        if (!m_impl->dom_loaded)
        {
            m_impl->pending.emplace_back(code);
            return;
        }

        m_impl->web_view->ExecuteScript(utils::widen(code).c_str(), nullptr);
    }

    void webview::handle_scheme(const std::string &name, scheme::resolver &&resolver, launch policy)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, name, resolver = std::move(resolver), policy]() mutable
                                      { return handle_scheme(name, std::move(resolver), policy); });
        }

        ComPtr<ICoreWebView2_22> webview;

        if (!SUCCEEDED(m_impl->web_view.As(&webview)))
        {
            return;
        }

        if (m_impl->schemes.contains(name))
        {
            return;
        }

        m_impl->schemes.emplace(name, std::make_pair(std::move(resolver), policy));

        const auto pattern = utils::widen(fmt::format("{}*", name));

        webview->AddWebResourceRequestedFilterWithRequestSourceKinds(pattern.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL,
                                                                     COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL);
    }

    void webview::remove_scheme(const std::string &name)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, name] { return remove_scheme(name); });
        }

        ComPtr<ICoreWebView2_22> webview;

        if (!SUCCEEDED(m_impl->web_view.As(&webview)))
        {
            return;
        }

        auto it = m_impl->schemes.find(name);

        if (it == m_impl->schemes.end())
        {
            return;
        }

        const auto pattern = utils::widen(fmt::format("{}*", name));

        webview->RemoveWebResourceRequestedFilterWithRequestSourceKinds(
            pattern.c_str(), COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL, COREWEBVIEW2_WEB_RESOURCE_REQUEST_SOURCE_KINDS_ALL);

        m_impl->schemes.erase(it);
    }

    void webview::clear(web_event event)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, event] { return clear(event); });
        }

        m_events.clear(event);
    }

    void webview::remove(web_event event, std::uint64_t id)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, event, id] { return remove(event, id); });
        }

        m_events.remove(event, id);
    }

    template <web_event Event>
    void webview::once(events::type<Event> callback)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, callback = std::move(callback)]() mutable
                                      { return once<Event>(std::move(callback)); });
        }

        m_impl->setup<Event>(this);
        m_events.at<Event>().once(std::move(callback));
    }

    template <web_event Event>
    std::uint64_t webview::on(events::type<Event> callback)
    {
        if (!m_parent->thread_safe())
        {
            return m_parent->dispatch([this, callback = std::move(callback)]() mutable
                                      { return on<Event>(std::move(callback)); });
        }

        m_impl->setup<Event>(this);
        return m_events.at<Event>().add(std::move(callback));
    }

    void webview::register_scheme(const std::string &name)
    {
        static std::unordered_map<std::string, ComPtr<ICoreWebView2CustomSchemeRegistration>> schemes;

        ComPtr<ICoreWebView2EnvironmentOptions4> options;

        if (!SUCCEEDED(impl::env_options().As(&options)))
        {
            assert(false && "Failed to query ICoreWebView2EnvironmentOptions4");
        }

        static LPCWSTR allowed_origins = L"*";
        auto scheme                    = Make<CoreWebView2CustomSchemeRegistration>(utils::widen(name).c_str());

        scheme->put_TreatAsSecure(true);
        scheme->put_HasAuthorityComponent(true); // Required to make JS-Fetch work
        scheme->SetAllowedOrigins(1, &allowed_origins);

        schemes.emplace(name, std::move(scheme));

        auto mapped = std::views::transform(schemes, [](const auto &item) { return item.second.Get(); });
        std::vector<ICoreWebView2CustomSchemeRegistration *> raw{mapped.begin(), mapped.end()};

        if (SUCCEEDED(options->SetCustomSchemeRegistrations(static_cast<UINT32>(schemes.size()), raw.data())))
        {
            return;
        }

        assert(false && "Failed to register scheme(s)");
    }

    SAUCER_INSTANTIATE_EVENTS(6, webview, web_event);
} // namespace saucer
