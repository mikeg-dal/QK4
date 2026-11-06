// SPDX-License-Identifier: GPL-3.0-or-later
//
// FunctionRef<R(Args...)>: a non-owning reference to a callable, the shape of C++26's
// std::function_ref. Two pointers, copied by value; it never allocates.
//
// WHY: std::function stores a callable that does not fit its small buffer on the heap, and that
// buffer's size is the library's choice (libstdc++: 16 bytes; a lambda capturing three references
// does not fit). On a real-time thread that is an allocation per call site (CONVENTIONS Rule 16).
// Use FunctionRef for a callable passed down a call and used before the call returns. It does not
// own what it refers to: never store one beyond the lifetime of the callable it was made from.

#ifndef FUNCTIONREF_H
#define FUNCTIONREF_H

#include <memory>
#include <type_traits>
#include <utility>

template <typename Signature> class FunctionRef;

template <typename R, typename... Args> class FunctionRef<R(Args...)> {
public:
    template <typename F, typename = std::enable_if_t<!std::is_same_v<std::decay_t<F>, FunctionRef> &&
                                                      std::is_invocable_r_v<R, F &, Args...>>>
    FunctionRef(F &&f) noexcept // NOLINT(google-explicit-constructor): converts like std::function
        : m_object(const_cast<void *>(static_cast<const void *>(std::addressof(f)))),
          m_call([](void *object, Args... args) -> R {
              return (*static_cast<std::remove_reference_t<F> *>(object))(std::forward<Args>(args)...);
          }) {}

    R operator()(Args... args) const { return m_call(m_object, std::forward<Args>(args)...); }

private:
    void *m_object;
    R (*m_call)(void *, Args...);
};

#endif // FUNCTIONREF_H
