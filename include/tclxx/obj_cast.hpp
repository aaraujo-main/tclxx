#pragma once
#include <tcl.h>
#include <string>
#include <stdexcept>
#include <type_traits>
#include <memory>
#include "tclxx/obj_type.hpp"

namespace tclxx { namespace obj_cast {

    namespace detail {

    template <typename>
    struct dependent_false : std::false_type {};

    template <typename>
    struct is_shared_ptr : std::false_type {};

    template <typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

    inline std::string conversion_error(Tcl_Interp* interp, const char* target) {
        if (interp) {
            const char* msg = Tcl_GetStringResult(interp);
            if (msg && *msg) {
                return std::string("Failed to convert Tcl_Obj to ") + target + ": " + msg;
            }
        }
        return std::string("Failed to convert Tcl_Obj to ") + target;
    }

    } // namespace detail

    template <typename T, std::enable_if_t<!std::is_pointer_v<T>, int> = 0>
    Tcl_Obj* from(const T&) {
        static_assert(!std::is_same_v<T, T>,
            "No tclxx::obj_cast::from<T> specialization for this type.");
        return nullptr;
    }

    template <typename T>
    Tcl_Obj* from_owned(T* v) {
        static_assert(!std::is_void_v<T>,
            "No tclxx::obj_cast::from_owned<void*> conversion.");
        using pointee_t = std::remove_cv_t<T>;
        if constexpr (std::is_const_v<T>) {
            return tclxx::ObjType<const pointee_t>::New(v);
        } else {
            return tclxx::ObjType<pointee_t>::New(v);
        }
    }

    template <typename T>
    Tcl_Obj* from_weak(T* v) {
        static_assert(!std::is_void_v<T>,
            "No tclxx::obj_cast::from_weak<void*> conversion.");
        using pointee_t = std::remove_cv_t<T>;
        Tcl_Obj* obj = Tcl_NewObj();
        if (!obj) throw std::runtime_error("Failed to allocate Tcl object");
        if constexpr (std::is_const_v<T>) {
            return tclxx::ObjType<const pointee_t, tclxx::detail::ownership::weak>::Set(obj, v);
        } else {
            return tclxx::ObjType<pointee_t, tclxx::detail::ownership::weak>::Set(obj, v);
        }
    }

    template <tclxx::detail::ownership Ownership = tclxx::detail::ownership::owned, typename T>
    Tcl_Obj* from_shared(const std::shared_ptr<T>& v) {
        static_assert(!std::is_void_v<T>,
            "No tclxx::obj_cast::from_shared<std::shared_ptr<void>> conversion.");
        static_assert(
            Ownership == tclxx::detail::ownership::owned ||
                Ownership == tclxx::detail::ownership::shared,
            "obj_cast::from_shared supports ownership::owned or ownership::shared");
        return tclxx::ObjType<std::shared_ptr<T>, Ownership>::New(new std::shared_ptr<T>(v));
    }

    template <typename T, std::enable_if_t<std::is_pointer_v<T>, int> = 0>
    Tcl_Obj* from(const T& v) {
        using pointee_t = std::remove_pointer_t<T>;
        if constexpr (std::is_same_v<std::remove_cv_t<pointee_t>, Tcl_Obj>) {
            return const_cast<Tcl_Obj*>(v);
        } else {
            // Default model: raw pointers are treated as non-owning references.
            return from_weak(v);
        }
    }

    template <typename T>
    T to(Tcl_Interp* interp, Tcl_Obj* const obj) {
        if constexpr (std::is_pointer_v<T>) {
            using pointee_t = std::remove_cv_t<std::remove_pointer_t<T>>;
            constexpr bool wants_const =
                std::is_const_v<std::remove_pointer_t<T>>;
            // Hot path: mutable raw-pointer object reps first.
            if (obj && obj->typePtr == tclxx::ObjType<pointee_t>::GetType()) {
                return tclxx::ObjType<pointee_t>::GetInternalRep(interp, obj);
            }
            if (obj && obj->typePtr ==
                           tclxx::ObjType<std::shared_ptr<pointee_t>,
                                       tclxx::detail::ownership::shared>::GetType()) {
                auto* sharedRep =
                    tclxx::ObjType<std::shared_ptr<pointee_t>,
                                tclxx::detail::ownership::shared>::GetInternalRep(interp, obj);
                return sharedRep ? sharedRep->get() : nullptr;
            }
            if (obj && obj->typePtr == tclxx::ObjType<std::shared_ptr<pointee_t>>::GetType()) {
                auto* sharedRep = tclxx::ObjType<std::shared_ptr<pointee_t>>::GetInternalRep(interp, obj);
                return sharedRep ? sharedRep->get() : nullptr;
            }
            if (obj && obj->typePtr ==
                           tclxx::ObjType<pointee_t, tclxx::detail::ownership::weak>::GetType()) {
                return tclxx::ObjType<pointee_t, tclxx::detail::ownership::weak>::GetInternalRep(
                    interp, obj);
            }
            if (obj && obj->typePtr ==
                           tclxx::ObjType<const pointee_t, tclxx::detail::ownership::weak>::GetType()) {
                auto ptr = tclxx::ObjType<const pointee_t, tclxx::detail::ownership::weak>::GetInternalRep(
                    interp, obj);
                if constexpr (wants_const) {
                    return ptr;
                } else {
                    throw std::runtime_error("Cannot cast const object handle to mutable pointer");
                }
            }
            if (obj && obj->typePtr == tclxx::ObjType<const pointee_t>::GetType()) {
                auto ptr = tclxx::ObjType<const pointee_t>::GetInternalRep(interp, obj);
                if constexpr (wants_const) {
                    return ptr;
                } else {
                    throw std::runtime_error("Cannot cast const object handle to mutable pointer");
                }
            }
            if (obj && obj->typePtr ==
                           tclxx::ObjType<std::shared_ptr<const pointee_t>,
                                       tclxx::detail::ownership::shared>::GetType()) {
                auto* sharedRep =
                    tclxx::ObjType<std::shared_ptr<const pointee_t>,
                                tclxx::detail::ownership::shared>::GetInternalRep(interp, obj);
                auto* raw = sharedRep ? sharedRep->get() : nullptr;
                if constexpr (wants_const) {
                    return raw;
                } else {
                    throw std::runtime_error("Cannot cast const shared object handle to mutable pointer");
                }
            }
            if (obj && obj->typePtr == tclxx::ObjType<std::shared_ptr<const pointee_t>>::GetType()) {
                auto* sharedRep = tclxx::ObjType<std::shared_ptr<const pointee_t>>::GetInternalRep(interp, obj);
                auto* raw = sharedRep ? sharedRep->get() : nullptr;
                if constexpr (wants_const) {
                    return raw;
                } else {
                    throw std::runtime_error("Cannot cast const shared object handle to mutable pointer");
                }
            }
            return tclxx::ObjType<pointee_t>::GetInternalRep(interp, obj);
        } else {
            return *tclxx::ObjType<T>::GetInternalRep(interp, obj);
        }
    }

    // int
    template <> inline Tcl_Obj* from<int>(const int& v) {
        return Tcl_NewIntObj(v);
    }

    template <> inline int to<int>(Tcl_Interp* i, Tcl_Obj* const o) {
        int v = 0;
        if (Tcl_GetIntFromObj(i, o, &v) != TCL_OK) {
            throw std::runtime_error(detail::conversion_error(i, "int"));
        }
        return v;
    }

    // double
    template <> inline Tcl_Obj* from<double>(const double& v) {
        return Tcl_NewDoubleObj(v);
    }

    template <> inline double to<double>(Tcl_Interp* i, Tcl_Obj* const o) {
        double v = 0.0;
        if (Tcl_GetDoubleFromObj(i, o, &v) != TCL_OK) {
            throw std::runtime_error(detail::conversion_error(i, "double"));
        }
        return v;
    }

    // std::string
    template <> inline Tcl_Obj* from<std::string>(const std::string& v) {
        return Tcl_NewStringObj(v.c_str(), -1);
    }

    template <> inline std::string to<std::string>(Tcl_Interp*, Tcl_Obj* const o) {
        int len;
        const char* s = Tcl_GetStringFromObj(o, &len);
        return std::string(s, len);
    }

    // bool
    template <> inline Tcl_Obj* from<bool>(const bool& v) {
        return Tcl_NewBooleanObj(v);
    }

    template <> inline bool to<bool>(Tcl_Interp* i, Tcl_Obj* const o) {
        int v = 0;
        if (Tcl_GetBooleanFromObj(i, o, &v) != TCL_OK) {
            throw std::runtime_error(detail::conversion_error(i, "bool"));
        }
        return static_cast<bool>(v);
    }

    template <> inline Tcl_Obj* to<Tcl_Obj*>(Tcl_Interp*, Tcl_Obj* const o) {
        return o;
    }

}} // namespace tclxx::obj_cast
