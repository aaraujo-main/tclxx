#pragma once
#include <tcl.h>
#include <type_traits>
#include <tuple>
#include <array>
#include <utility>
#include <string>
#include <memory>
#include "tclxx/obj_cast.hpp"

namespace tclxx { namespace cmd {

namespace detail {

    enum class pointer_return_mode {
        weak,
        owned,
        shared
    };

    template <typename>
    struct is_shared_ptr : std::false_type {};

    template <typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

    template <typename>
    struct is_weak_ptr : std::false_type {};

    template <typename T>
    struct is_weak_ptr<std::weak_ptr<T>> : std::true_type {};

    template <typename>
    struct is_unique_ptr : std::false_type {};

    template <typename T, typename D>
    struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};

    // ── function traits ──────────────────────────────────────────

    template <typename T>
    struct function_traits;

    template <typename R, typename... Args>
    struct function_traits<R(*)(Args...)> {
        using return_type = R;
        using args = std::tuple<Args...>;
        static constexpr std::size_t arity = sizeof...(Args);
    };

    template <typename T>
    struct member_function_traits;

    template <typename C, typename R, typename... Args>
    struct member_function_traits<R(C::*)(Args...)> {
        using class_type = C;
        using return_type = R;
        using args = std::tuple<Args...>;
        static constexpr std::size_t arity = sizeof...(Args);
    };

    template <typename C, typename R, typename... Args>
    struct member_function_traits<R(C::*)(Args...) const> {
        using class_type = const C;
        using return_type = R;
        using args = std::tuple<Args...>;
        static constexpr std::size_t arity = sizeof...(Args);
    };

    // ── invoke: call Func with args converted from Tcl_Obj array ─

    template <auto Func, typename ArgsTuple, std::size_t... Is>
    decltype(auto) invoke([[maybe_unused]] Tcl_Interp* interp,
                          [[maybe_unused]] Tcl_Obj* const* objs,
                          std::index_sequence<Is...>) {
        return Func(
            obj_cast::to<std::tuple_element_t<Is, ArgsTuple>>(interp, objs[Is])...);
    }

    template <typename C, typename... Args, std::size_t... Is>
    C* construct(Tcl_Interp* interp, Tcl_Obj* const* objs, std::index_sequence<Is...>) {
        return new C(obj_cast::to<Args>(interp, objs[Is])...);
    }

    template <typename C, typename... Args, std::size_t... Is>
    std::shared_ptr<C> construct_shared(
        Tcl_Interp* interp,
        Tcl_Obj* const* objs,
        std::index_sequence<Is...>) {
        return std::make_shared<C>(obj_cast::to<Args>(interp, objs[Is])...);
    }

    template <auto Func, typename ArgsTuple, std::size_t... Is>
    decltype(auto) invoke_with_object(Tcl_Interp* interp,
                                      Tcl_Obj* objectObj,
                                      Tcl_Obj* const* objs,
                                      std::index_sequence<Is...>) {
        using object_arg_t = std::tuple_element_t<0, ArgsTuple>;
        object_arg_t objectArg = obj_cast::to<object_arg_t>(interp, objectObj);
        
        if constexpr (std::is_pointer_v<object_arg_t> &&
                      !std::is_same_v<std::remove_cv_t<std::remove_pointer_t<object_arg_t>>, Tcl_Obj>) {
            if (!objectArg) {
                throw std::runtime_error("Cannot invoke function on null object handle");
            }
        }
        return Func(
            objectArg,
            obj_cast::to<std::tuple_element_t<Is + 1, ArgsTuple>>(interp, objs[Is])...);
    }

    template <auto Method, typename Traits, std::size_t... Is>
    decltype(auto) invoke_member_with_object(Tcl_Interp* interp,
                            Tcl_Obj* objectObj,
                            Tcl_Obj* const* objs,
                            std::index_sequence<Is...>) {
    using C = typename Traits::class_type;
    C* self = obj_cast::to<C*>(interp, objectObj);
    if (!self) {
        throw std::runtime_error("Cannot invoke member method on null object handle");
    }
    return (self->*Method)(
        obj_cast::to<std::tuple_element_t<Is, typename Traits::args>>(interp, objs[Is])...);
    }

    // ── variable helpers (for setter / updater) ──────────────────

    inline Tcl_Obj* var_resolve(Tcl_Interp* interp, Tcl_Obj* nameObj) {
        return Tcl_ObjGetVar2(interp, nameObj, nullptr, TCL_LEAVE_ERR_MSG);
    }

    inline Tcl_Obj* var_ensure_unshared(Tcl_Interp* interp, Tcl_Obj* nameObj) {
        Tcl_Obj* obj = var_resolve(interp, nameObj);
        if (!obj) return nullptr;
        if (Tcl_IsShared(obj)) {
            obj = Tcl_DuplicateObj(obj);
            if (!Tcl_ObjSetVar2(interp, nameObj, nullptr, obj, TCL_LEAVE_ERR_MSG)) {
                return nullptr;
            }
        }
        return obj;
    }

    inline void maybe_invalidate_string_rep(Tcl_Obj* obj) {
        if (!obj || !obj->typePtr) return;
        if (!obj->typePtr->updateStringProc) return;
        Tcl_InvalidateStringRep(obj);
    }

    // ── usage string builder (cold path only) ────────────────────

    inline std::string make_usage(std::initializer_list<const char*> named,
                                  std::size_t extra) {
        std::string s;
        for (auto n : named) {
            if (!s.empty()) s += ' ';
            s += n;
        }
        for (std::size_t i = 0; i < extra; ++i) {
            if (!s.empty()) s += ' ';
            s += "arg";
            if (extra > 1) s += std::to_string(i + 1);
        }
        return s;
    }

    template <pointer_return_mode PointerMode, typename R>
    Tcl_Obj* result_to_obj(R&& result) {
        using decayed_t = std::decay_t<R>;
        if constexpr (is_unique_ptr<decayed_t>::value) {
            return obj_cast::from_owned(result.release());
        } else if constexpr (is_shared_ptr<decayed_t>::value) {
            if constexpr (PointerMode == pointer_return_mode::shared) {
                return obj_cast::from_shared<tclxx::detail::ownership::shared>(result);
            } else {
                return obj_cast::from_shared<tclxx::detail::ownership::owned>(result);
            }
        } else if constexpr (is_weak_ptr<decayed_t>::value) {
            auto shared = result.lock();
            if (!shared) {
                throw std::runtime_error("Cannot convert expired std::weak_ptr return value");
            }
            if constexpr (PointerMode == pointer_return_mode::shared) {
                return obj_cast::from_shared<tclxx::detail::ownership::shared>(shared);
            } else {
                return obj_cast::from_shared<tclxx::detail::ownership::owned>(shared);
            }
        } else if constexpr (std::is_pointer_v<decayed_t> &&
                             std::is_same_v<std::remove_cv_t<std::remove_pointer_t<decayed_t>>, Tcl_Obj>) {
            return const_cast<Tcl_Obj*>(result);
        } else if constexpr (std::is_pointer_v<decayed_t> &&
                             !std::is_same_v<std::remove_cv_t<std::remove_pointer_t<decayed_t>>, Tcl_Obj>) {
            if constexpr (PointerMode == pointer_return_mode::owned) {
                return obj_cast::from_owned(result);
            } else {
                return obj_cast::from_weak(result);
            }
        } else if constexpr (std::is_lvalue_reference_v<R> &&
                             !std::is_arithmetic_v<decayed_t> &&
                             !std::is_same_v<decayed_t, std::string> &&
                             !std::is_same_v<decayed_t, bool>) {
            return obj_cast::from_weak(std::addressof(result));
        } else {
            return obj_cast::from(std::forward<R>(result));
        }
    }

} // namespace detail

template <detail::pointer_return_mode PointerMode, typename C, typename... Args>
int create_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    constexpr int expected = static_cast<int>(sizeof...(Args)) + 1;
    if (objc != expected) {
        if constexpr (sizeof...(Args) == 0) {
            Tcl_WrongNumArgs(interp, 1, objv, nullptr);
        } else {
            auto u = detail::make_usage({}, sizeof...(Args));
            Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        }
        return TCL_ERROR;
    }

    try {
        if constexpr (PointerMode == detail::pointer_return_mode::shared) {
            auto created = detail::construct_shared<C, Args...>(
                interp,
                objv + 1,
                std::make_index_sequence<sizeof...(Args)>{});
            Tcl_SetObjResult(
                interp,
                obj_cast::from_shared<tclxx::detail::ownership::shared>(created));
        } else {
            C* created = detail::construct<C, Args...>(
                interp,
                objv + 1,
                std::make_index_sequence<sizeof...(Args)>{});
            Tcl_SetObjResult(interp, obj_cast::from_owned(created));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <typename C, typename... Args>
int create(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return create_impl<detail::pointer_return_mode::owned, C, Args...>(cd, interp, objc, objv);
}

template <typename C, typename... Args>
int create_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return create_impl<detail::pointer_return_mode::shared, C, Args...>(cd, interp, objc, objv);
}

template <detail::pointer_return_mode PointerMode, typename C>
int create_args_impl(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "?arg1 arg2 ...?");
        return TCL_ERROR;
    }
    try {
        if constexpr (PointerMode == detail::pointer_return_mode::shared) {
            auto c = std::make_shared<C>(cd, interp, objc, objv);
            Tcl_SetObjResult(
                interp,
                obj_cast::from_shared<tclxx::detail::ownership::shared>(c));
        } else {
            std::unique_ptr<C> c = std::make_unique<C>(cd, interp, objc, objv);
            Tcl_SetObjResult(interp, obj_cast::from_owned(c.release()));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    }
}

template <typename C>
int create_args(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return create_args_impl<detail::pointer_return_mode::owned, C>(cd, interp, objc, objv);
}

template <typename C>
int create_args_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return create_args_impl<detail::pointer_return_mode::shared, C>(cd, interp, objc, objv);
}

// ─── getter ──────────────────────────────────────────────────────────
//
// Wraps: ReturnType func(ThisType, ExtraArgs...)
// Tcl:   cmdName $this ?arg ...?
//
// objv[1] is the object passed by value.
// objv[2..] are additional parameters.

template <detail::pointer_return_mode PointerMode, auto Func>
int getter_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    using T = detail::function_traits<decltype(Func)>;
    static_assert(T::arity >= 1,
        "getter function must accept at least one parameter (the object)");
    constexpr int expected = static_cast<int>(T::arity) + 1;

    if (objc != expected) {
        auto u = detail::make_usage({"object"}, T::arity - 1);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    try {
        if constexpr (std::is_void_v<typename T::return_type>) {
            detail::invoke<Func, typename T::args>(
                interp, objv + 1, std::make_index_sequence<T::arity>{});
            Tcl_ResetResult(interp);
        } else {
            decltype(auto) result = detail::invoke<Func, typename T::args>(
                interp, objv + 1, std::make_index_sequence<T::arity>{});
            Tcl_SetObjResult(interp, detail::result_to_obj<PointerMode>(result));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <auto Func>
int getter(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_impl<detail::pointer_return_mode::weak, Func>(cd, interp, objc, objv);
}

template <auto Func>
int getter_weak(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_impl<detail::pointer_return_mode::weak, Func>(cd, interp, objc, objv);
}

template <auto Func>
int getter_owned(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_impl<detail::pointer_return_mode::owned, Func>(cd, interp, objc, objv);
}

template <auto Func>
int getter_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_impl<detail::pointer_return_mode::shared, Func>(cd, interp, objc, objv);
}

template <detail::pointer_return_mode PointerMode, auto Method>
int getter_member_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                  "getter_member expects a member function pointer");
    using T = detail::member_function_traits<decltype(Method)>;
    constexpr int expected = static_cast<int>(T::arity) + 2;

    if (objc != expected) {
        auto u = detail::make_usage({"object"}, T::arity);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    try {
        if constexpr (std::is_void_v<typename T::return_type>) {
            detail::invoke_member_with_object<Method, T>(
                interp, objv[1], objv + 2, std::make_index_sequence<T::arity>{});
            Tcl_ResetResult(interp);
        } else {
            decltype(auto) result = detail::invoke_member_with_object<Method, T>(
                interp, objv[1], objv + 2, std::make_index_sequence<T::arity>{});
            Tcl_SetObjResult(interp, detail::result_to_obj<PointerMode>(result));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <auto Method>
int getter_member(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_member_impl<detail::pointer_return_mode::weak, Method>(cd, interp, objc, objv);
}

template <auto Method>
int getter_member_weak(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_member_impl<detail::pointer_return_mode::weak, Method>(cd, interp, objc, objv);
}

template <auto Method>
int getter_member_owned(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_member_impl<detail::pointer_return_mode::owned, Method>(cd, interp, objc, objv);
}

template <auto Method>
int getter_member_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return getter_member_impl<detail::pointer_return_mode::shared, Method>(cd, interp, objc, objv);
}

// ─── setter ──────────────────────────────────────────────────────────
//
// Wraps: ReturnType func(ThisType, ExtraArgs...)
// Tcl:   cmdName thisVarName ?arg ...?
//
// objv[1] is a variable name; resolved and ensured unshared (COW).
// objv[2..] are additional parameters passed by value.
// The object's string representation is invalidated after invocation.

template <detail::pointer_return_mode PointerMode, auto Func>
int setter_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    using T = detail::function_traits<decltype(Func)>;
    static_assert(T::arity >= 1,
        "setter function must accept at least one parameter (the object)");
    constexpr int expected = static_cast<int>(T::arity) + 1;

    if (objc != expected) {
        auto u = detail::make_usage({"varName"}, T::arity - 1);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    Tcl_Obj* thisObj = detail::var_ensure_unshared(interp, objv[1]);
    if (!thisObj) return TCL_ERROR;

    try {
        if constexpr (std::is_void_v<typename T::return_type>) {
            detail::invoke_with_object<Func, typename T::args>(
                interp, thisObj, objv + 2, std::make_index_sequence<T::arity - 1>{});
            detail::maybe_invalidate_string_rep(thisObj);
            Tcl_ResetResult(interp);
        } else {
            decltype(auto) result = detail::invoke_with_object<Func, typename T::args>(
                interp, thisObj, objv + 2, std::make_index_sequence<T::arity - 1>{});
            detail::maybe_invalidate_string_rep(thisObj);
            Tcl_SetObjResult(interp, detail::result_to_obj<PointerMode>(result));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <auto Func>
int setter(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_impl<detail::pointer_return_mode::weak, Func>(cd, interp, objc, objv);
}

template <auto Func>
int setter_weak(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_impl<detail::pointer_return_mode::weak, Func>(cd, interp, objc, objv);
}

template <auto Func>
int setter_owned(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_impl<detail::pointer_return_mode::owned, Func>(cd, interp, objc, objv);
}

template <auto Func>
int setter_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_impl<detail::pointer_return_mode::shared, Func>(cd, interp, objc, objv);
}

template <detail::pointer_return_mode PointerMode, auto Method>
int setter_member_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                  "setter_member expects a member function pointer");
    using T = detail::member_function_traits<decltype(Method)>;
    constexpr int expected = static_cast<int>(T::arity) + 2;

    if (objc != expected) {
        auto u = detail::make_usage({"varName"}, T::arity);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    Tcl_Obj* thisObj = detail::var_ensure_unshared(interp, objv[1]);
    if (!thisObj) return TCL_ERROR;

    try {
        if constexpr (std::is_void_v<typename T::return_type>) {
            detail::invoke_member_with_object<Method, T>(
                interp, thisObj, objv + 2, std::make_index_sequence<T::arity>{});
            detail::maybe_invalidate_string_rep(thisObj);
            Tcl_ResetResult(interp);
        } else {
            decltype(auto) result = detail::invoke_member_with_object<Method, T>(
                interp, thisObj, objv + 2, std::make_index_sequence<T::arity>{});
            detail::maybe_invalidate_string_rep(thisObj);
            Tcl_SetObjResult(interp, detail::result_to_obj<PointerMode>(result));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <auto Method>
int setter_member(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_member_impl<detail::pointer_return_mode::weak, Method>(cd, interp, objc, objv);
}

template <auto Method>
int setter_member_weak(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_member_impl<detail::pointer_return_mode::weak, Method>(cd, interp, objc, objv);
}

template <auto Method>
int setter_member_owned(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_member_impl<detail::pointer_return_mode::owned, Method>(cd, interp, objc, objv);
}

template <auto Method>
int setter_member_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return setter_member_impl<detail::pointer_return_mode::shared, Method>(cd, interp, objc, objv);
}

// ─── updater_member ──────────────────────────────────────────────────
//
// Wraps: FieldType& method()
// Tcl:   cmdName objVarName fieldVarName body
//
// fieldVarName is bound in caller scope to the field Tcl_Obj and body is
// evaluated in that same scope. The final value of fieldVarName is written
// back to the C++ field.

template <auto Method>
int updater_member(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    static_assert(std::is_member_function_pointer_v<decltype(Method)>,
                  "updater_member expects a member function pointer");
    using T = detail::member_function_traits<decltype(Method)>;
    static_assert(T::arity == 0,
                  "updater_member method must take no arguments");
    static_assert(std::is_lvalue_reference_v<typename T::return_type>,
                  "updater_member method must return an lvalue reference to the field");

    constexpr int expected = 4;

    if (objc != expected) {
        auto u = detail::make_usage({"objVar", "fieldVar", "body"}, 0);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    Tcl_Obj* thisObj = detail::var_ensure_unshared(interp, objv[1]);
    if (!thisObj) return TCL_ERROR;

    bool fieldVarBound = false;

    auto clear_field_var = [&]() {
        Tcl_Obj* emptyObj = Tcl_NewStringObj("", 0);
        Tcl_ObjSetVar2(interp, objv[2], nullptr, emptyObj, 0);
    };

    try {
        using C = typename T::class_type;
        using field_type = std::remove_cv_t<std::remove_reference_t<typename T::return_type>>;

        C* self = obj_cast::to<C*>(interp, thisObj);
        if (!self) {
            throw std::runtime_error("Cannot invoke updater_member on null object handle");
        }
        auto&& fieldRef = (self->*Method)();

        Tcl_Obj* fieldVarObj = obj_cast::from_weak(std::addressof(fieldRef));
        if (!Tcl_ObjSetVar2(interp, objv[2], nullptr, fieldVarObj, TCL_LEAVE_ERR_MSG)) {
            return TCL_ERROR;
        }
        fieldVarBound = true;

        int evalCode = Tcl_EvalObjEx(interp, objv[3], 0);

        Tcl_Obj* updatedFieldObj = detail::var_resolve(interp, objv[2]);
        if (!updatedFieldObj && evalCode == TCL_OK) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("fieldVar must remain defined during updater body", -1));
            clear_field_var();
            return TCL_ERROR;
        }

        if (updatedFieldObj) {
            fieldRef = obj_cast::to<field_type>(interp, updatedFieldObj);
        }

        detail::maybe_invalidate_string_rep(thisObj);

        clear_field_var();
        return evalCode;
    } catch (const std::exception& e) {
        detail::maybe_invalidate_string_rep(thisObj);
        if (fieldVarBound) {
            clear_field_var();
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        detail::maybe_invalidate_string_rep(thisObj);
        if (fieldVarBound) {
            clear_field_var();
        }
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

// ─── static_method ───────────────────────────────────────────────────
//
// Wraps: ReturnType func(Args...)
// Tcl:   cmdName ?arg ...?
//
// No special first parameter. All objv[1..] are regular parameters.

template <detail::pointer_return_mode PointerMode, auto Func>
int static_method_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    using T = detail::function_traits<decltype(Func)>;
    constexpr int expected = static_cast<int>(T::arity) + 1;

    if (objc != expected) {
        if constexpr (T::arity == 0) {
            Tcl_WrongNumArgs(interp, 1, objv, nullptr);
        } else {
            auto u = detail::make_usage({}, T::arity);
            Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        }
        return TCL_ERROR;
    }

    try {
        if constexpr (std::is_void_v<typename T::return_type>) {
            detail::invoke<Func, typename T::args>(
                interp, objv + 1, std::make_index_sequence<T::arity>{});
            Tcl_ResetResult(interp);
        } else {
            decltype(auto) result = detail::invoke<Func, typename T::args>(
                interp, objv + 1, std::make_index_sequence<T::arity>{});
            Tcl_SetObjResult(interp, detail::result_to_obj<PointerMode>(result));
        }
        return TCL_OK;
    } catch (const std::exception& e) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <auto Func>
int static_method(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_method_impl<detail::pointer_return_mode::weak, Func>(cd, interp, objc, objv);
}

template <auto Func>
int static_method_weak(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_method_impl<detail::pointer_return_mode::weak, Func>(cd, interp, objc, objv);
}

template <auto Func>
int static_method_owned(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_method_impl<detail::pointer_return_mode::owned, Func>(cd, interp, objc, objv);
}

template <auto Func>
int static_method_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return static_method_impl<detail::pointer_return_mode::shared, Func>(cd, interp, objc, objv);
}

// ─── make_shared ─────────────────────────────────────────────────────
//
// Converts ObjType<T, owned> to ObjType<shared_ptr<T>, shared> in-place.
// Tcl:   cmdName varName
//
// objv[1] is a variable name. Existing T* pointer is wrapped in shared_ptr,
// typePtr changes, internalRep reinterpreted.

template <typename T>
int make_shared_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    constexpr int expected = 2;

    if (objc != expected) {
        auto u = detail::make_usage({"varName"}, 0);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    Tcl_Obj* thisObj = detail::var_resolve(interp, objv[1]);
    if (!thisObj) return TCL_ERROR;

    try {
        // Verify thisObj has owned T type
        if (thisObj->typePtr != tclxx::ObjType<T>::GetType()) {
            throw std::runtime_error(
                "Object does not have owned type; cannot convert to shared");
        }

        if (Tcl_IsShared(thisObj)) {
            thisObj = Tcl_DuplicateObj(thisObj);
            if (!Tcl_ObjSetVar2(interp, objv[1], nullptr, thisObj, TCL_LEAVE_ERR_MSG)) {
                return TCL_ERROR;
            }
        }

        // Extract T* from internalRep
        T* ptr = static_cast<T*>(thisObj->internalRep.otherValuePtr);
        if (!ptr) {
            throw std::runtime_error("Cannot convert null object to shared_ptr");
        }

        // Wrap existing pointer in shared_ptr
        auto shared_ptr_wrapper = new std::shared_ptr<T>(ptr);

        // Change typePtr to shared_ptr variant (no freeIntRepProc call)
        thisObj->typePtr = tclxx::ObjType<std::shared_ptr<T>, tclxx::detail::ownership::shared>::GetType();
        thisObj->internalRep.otherValuePtr = static_cast<void*>(shared_ptr_wrapper);
        Tcl_InvalidateStringRep(thisObj);

        return TCL_OK;
    } catch (const std::exception& e) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <typename T>
int make_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return make_shared_impl<T>(cd, interp, objc, objv);
}

// ─── from_shared ─────────────────────────────────────────────────────
//
// Converts ObjType<shared_ptr<T>, shared> to ObjType<T, owned> in-place.
// Tcl:   cmdName varName
//
// objv[1] is a variable name. Existing shared_ptr is duplicated if needed,
// dereferenced to get T*, typePtr changes, internalRep reinterpreted.

template <typename T>
int from_shared_impl(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    constexpr int expected = 2;

    if (objc != expected) {
        auto u = detail::make_usage({"varName"}, 0);
        Tcl_WrongNumArgs(interp, 1, objv, u.c_str());
        return TCL_ERROR;
    }

    Tcl_Obj* thisObj = detail::var_resolve(interp, objv[1]);
    if (!thisObj) return TCL_ERROR;

    try {
        // Verify thisObj has shared_ptr<T> shared type
        if (thisObj->typePtr != tclxx::ObjType<std::shared_ptr<T>, tclxx::detail::ownership::shared>::GetType()) {
            throw std::runtime_error(
                "Object does not have shared_ptr type; cannot convert from shared");
        }

        if (Tcl_IsShared(thisObj)) {
            thisObj = Tcl_DuplicateObj(thisObj);
            if (!Tcl_ObjSetVar2(interp, objv[1], nullptr, thisObj, TCL_LEAVE_ERR_MSG)) {
                return TCL_ERROR;
            }
        }

        // Extract shared_ptr<T>* from internalRep
        auto shared_ptr_wrapper = static_cast<std::shared_ptr<T>*>(thisObj->internalRep.otherValuePtr);
        if (!shared_ptr_wrapper || !shared_ptr_wrapper->get()) {
            throw std::runtime_error("Cannot convert null shared_ptr to owned type");
        }

        // Since object is unshared (var_ensure_unshared), create new owned T
        T* new_ptr = new T(**shared_ptr_wrapper);

        // Free old shared_ptr wrapper
        delete shared_ptr_wrapper;

        // Change typePtr to owned T variant (no freeIntRepProc call)
        thisObj->typePtr = tclxx::ObjType<T>::GetType();
        thisObj->internalRep.otherValuePtr = static_cast<void*>(new_ptr);
        Tcl_InvalidateStringRep(thisObj);

        return TCL_OK;
    } catch (const std::exception& e) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        detail::maybe_invalidate_string_rep(thisObj);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

template <typename T>
int from_shared(ClientData cd, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    return from_shared_impl<T>(cd, interp, objc, objv);
}

}} // namespace tclxx::cmd