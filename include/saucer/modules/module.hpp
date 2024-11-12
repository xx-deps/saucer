#pragma once

#include <type_traits>
#include <unordered_map>

#include <memory>
#include <string>
#include <optional>

namespace saucer
{
    template <typename T>
    struct stable_natives;

    template <typename T, bool Stable>
    using natives = std::conditional_t<Stable, stable_natives<T>, typename T::impl *>;

    template <typename S, typename T, typename... Ts>
    concept Module = std::constructible_from<S, T *, Ts...>;

    class erased_module
    {
        struct base;

      private:
        std::size_t m_id;
        std::unique_ptr<base> m_value;

      private:
        erased_module() = default;

      public:
        [[nodiscard]] base *get() const;

      public:
        template <typename T>
        [[nodiscard]] std::optional<T *> get() const;

      public:
        template <typename T>
        [[nodiscard]] static std::size_t id_of();

      public:
        template <typename T, typename... Ts>
        [[nodiscard]] static erased_module from(Ts &&...args);
    };

    template <typename T>
    class extensible
    {
        friend T;

      private:
        using module_map = std::unordered_map<std::size_t, erased_module>;

      private:
        T *m_parent;
        module_map m_modules;

      public:
        extensible(T *parent);

      protected:
        bool on_message(const std::string &message);

      public:
        template <typename M, typename... Ts>
            requires Module<M, T, Ts...>
        M &add_module(Ts &&...);

      public:
        template <typename M>
        [[nodiscard]] std::optional<M *> module();
    };
} // namespace saucer

#include "module.inl"
