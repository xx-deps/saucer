#pragma once

#include "smartview.hpp"

namespace saucer
{
    template <Serializer Serializer, Module... Modules>
    saucer::natives smartview<Serializer, Modules...>::natives() const
    {
        return {
            reinterpret_cast<natives::window_impl *>(window::m_impl.get()),
            reinterpret_cast<natives::webview_impl *>(webview::m_impl.get()),
        };
    }

    template <Serializer Serializer, Module... Modules>
    smartview<Serializer, Modules...>::smartview(const options &options)
        : smartview_core(std::make_unique<Serializer>(), options), m_modules(Modules{this, natives()}...)
    {
    }

    template <Serializer Serializer, Module... Modules>
    template <typename Return, typename... Params>
    std::future<Return> smartview<Serializer, Modules...>::evaluate(const std::string &code, Params &&...params)
    {
        auto promise = std::make_shared<std::promise<Return>>();
        auto rtn     = promise->get_future();

        auto args    = Serializer::serialize_args(std::forward<Params>(params)...);
        auto resolve = Serializer::resolve(promise);

        add_evaluation(std::move(resolve), fmt::vformat(code, args));

        return rtn;
    }

    template <Serializer Serializer, Module... Modules>
    template <typename Function>
    void smartview<Serializer, Modules...>::expose(std::string name, Function &&func, launch policy)
    {
        auto resolve = Serializer::serialize(std::forward<Function>(func));
        add_function(std::move(name), std::move(resolve), policy);
    }

    template <Serializer Serializer, Module... Modules>
    template <Module T>
    auto &smartview<Serializer, Modules...>::module()
    {
        return std::get<T>(m_modules);
    }
} // namespace saucer
