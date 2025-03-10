#include "gtk.app.impl.hpp"

#include <fmt/format.h>

namespace saucer
{
    template void application::run<true>() const;
    template void application::run<false>() const;

    application::application(const options &opts) : extensible(this), m_pool(opts.threads), m_impl(std::make_unique<impl>())
    {
        const auto id = g_application_id_is_valid(opts.id.value().c_str())
                            ? opts.id.value()
                            : fmt::format("app.saucer.{}", impl::fix_id(opts.id.value()));

        m_impl->thread      = std::this_thread::get_id();
        m_impl->application = adw_application_new(id.c_str(), G_APPLICATION_DEFAULT_FLAGS);

        auto callback = [](GtkApplication *, application *self)
        {
            self->quit();
        };
        g_signal_connect(m_impl->application, "activate", G_CALLBACK(+callback), this);

        run<true>();
    }

    application::~application()
    {
        auto fut = dispatch<false>([this] { g_application_quit(G_APPLICATION(m_impl->application)); });
        {
            g_application_run(G_APPLICATION(m_impl->application), 0, nullptr);
        }
        fut.get();
    }

    bool application::thread_safe() const
    {
        return m_impl->thread == std::this_thread::get_id();
    }

    std::vector<screen> application::screens() const
    {
        if (!thread_safe())
        {
            return dispatch([this] { return screens(); });
        }

        auto *const display  = gdk_display_get_default();
        auto *const monitors = gdk_display_get_monitors(display);
        const auto size      = g_list_model_get_n_items(monitors);

        std::vector<screen> rtn{};
        rtn.reserve(size);

        for (auto i = 0uz; size > i; ++i)
        {
            auto *const current = reinterpret_cast<GdkMonitor *>(g_list_model_get_item(monitors, i));
            rtn.emplace_back(impl::convert(current));
        }

        return rtn;
    }

    void application::post(callback_t callback) const // NOLINT(*-static)
    {
        auto once = [](callback_t *data)
        {
            auto callback = std::unique_ptr<callback_t>{data};
            std::invoke(*callback);
        };

        g_idle_add_once(reinterpret_cast<GSourceOnceFunc>(+once), new callback_t{std::move(callback)});
    }

    template <bool Blocking>
    void application::run() const
    {
        // https://github.com/GNOME/glib/blob/ce5e11aef4be46594941662a521c7f5e026cfce9/gio/gapplication.c#L2591

        m_impl->should_quit = false;
        auto *context       = g_main_context_default();

        if (!g_main_context_acquire(context))
        {
            return;
        }

        if (!m_impl->initialized) [[unlikely]]
        {
            g_application_register(G_APPLICATION(m_impl->application), nullptr, nullptr);
            g_application_activate(G_APPLICATION(m_impl->application));
            m_impl->initialized = true;
        }

        if constexpr (Blocking)
        {
            while (!m_impl->should_quit)
            {
                g_main_context_iteration(context, true);
            }
        }
        else
        {
            g_main_context_iteration(context, false);
        }

        g_main_context_release(context);
    }

    void application::quit()
    {
        if (!thread_safe())
        {
            return dispatch([this] { return quit(); });
        }

        m_impl->should_quit = true;
    }
} // namespace saucer
