#pragma once
#include <tcl.h>
#include <exception>
#include <string>
#include <sstream>
#include <cstring>
#include <type_traits>
#include <typeinfo>
#include <string_view>
#include <limits>
#include <memory>
#include <utility>

namespace tclxx {

namespace detail {

template <typename>
struct is_shared_ptr : std::false_type {};

template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

enum class ownership {
    owned,
    weak,
    shared
};

} // namespace detail

template <typename T, detail::ownership Ownership = detail::ownership::owned>
class ObjType {
public:
    static_assert(
        Ownership != detail::ownership::shared || detail::is_shared_ptr<T>::value,
        "ObjType<..., detail::ownership::shared> requires T = std::shared_ptr<U>");

    static Tcl_ObjType* GetType() {
        static std::string nameStorage(TypeName());
        static Tcl_ObjType type = {
            nameStorage.c_str(),
            FreeInternalRep,
            DupInternalRep,
            UpdateString,
            SetFromAny
        };
        static const bool registered = []() {
            Tcl_RegisterObjType(&type);
            return true;
        }();
        (void)registered;
        return &type;
    }

    static Tcl_Obj* New(T* value) {
        Tcl_Obj* obj = Tcl_NewObj();
        if (!obj) throw std::runtime_error("Failed to allocate Tcl object");
        return Set(obj, value);
    }

    static Tcl_Obj* Set(Tcl_Obj* obj, T* value) {
        Startup(value);
        obj->bytes = NULL;
        obj->typePtr = GetType();
        obj->internalRep.otherValuePtr = PtrToVoid(value);
        return obj;
    }

    // Startup hook: specialize for types that acquire Tcl-managed resources.
    // By default, no startup is needed. Startup is intentionally invoked only
    // by Set() so all acquire behavior has one entry point.
    static void Startup(T* value) noexcept {
        try {
            if constexpr (detail::is_shared_ptr<T>::value) {
                using element_type = typename T::element_type;
                // For shared_ptr<Tcl_Obj>, acquire by incrementing Tcl refcount.
                if constexpr (std::is_same_v<element_type, Tcl_Obj>) {
                    if (value && value->get() && value->use_count() == 1) {
                        Tcl_IncrRefCount(value->get());
                    }
                }
                // For shared_ptr<Tcl_Obj*>, acquire pointed Tcl_Obj.
                else if constexpr (std::is_pointer_v<element_type> &&
                                   std::is_same_v<std::remove_pointer_t<element_type>, Tcl_Obj>) {
                    if (value && value->get() && *value->get() && value->use_count() == 1) {
                        Tcl_IncrRefCount(*value->get());
                    }
                }
            }
        } catch (...) {
            // Tcl callback hooks must never throw.
        }
    }

    // Cleanup hook: specialize for types that manage Tcl_Obj references.
    // By default, no cleanup is needed. Specializations can handle custom logic
    // before object deletion (e.g., decrementing Tcl_Obj refcounts for shared_ptr<Tcl_Obj>).
    static void Cleanup(T* value) noexcept {
        try {
            if constexpr (detail::is_shared_ptr<T>::value) {
                using element_type = typename T::element_type;
                // For shared_ptr<Tcl_Obj>, release only when this is the last
                // shared_ptr owner so the Tcl refcount is balanced exactly once.
                if constexpr (std::is_same_v<element_type, Tcl_Obj>) {
                    if (value && value->get() && value->use_count() == 1) {
                        Tcl_DecrRefCount(value->get());
                    }
                }
                // For shared_ptr<Tcl_Obj*>, release only on last shared owner.
                else if constexpr (std::is_pointer_v<element_type> &&
                                   std::is_same_v<std::remove_pointer_t<element_type>, Tcl_Obj>) {
                    if (value && value->get() && *value->get() && value->use_count() == 1) {
                        Tcl_DecrRefCount(*value->get());
                    }
                }
            }
        } catch (...) {
            // Tcl callback hooks must never throw.
        }
    }

    static void FreeInternalRep(Tcl_Obj* obj) noexcept {
        try {
            if constexpr (Ownership != detail::ownership::weak) {
                T* ptr = static_cast<T*>(obj->internalRep.otherValuePtr);
                Cleanup(ptr);
                delete ptr;
            }
        } catch (...) {
            // Tcl callback hooks must never throw.
        }
        obj->internalRep.otherValuePtr = nullptr;
    }

    static void DupInternalRep(Tcl_Obj* src, Tcl_Obj* dup) noexcept {
        try {
            T* srcPtr = static_cast<T*>(src->internalRep.otherValuePtr);
            std::unique_ptr<T> copied;
            if (srcPtr) {
                if constexpr (Ownership == detail::ownership::shared) {
                    copied = std::make_unique<T>(*srcPtr);
                } else if constexpr (detail::is_shared_ptr<T>::value) {
                    using element_type = typename T::element_type;
                    if (*srcPtr) {
                        copied = std::make_unique<T>(
                            std::make_shared<element_type>(*srcPtr->get()));
                    } else {
                        copied = std::make_unique<T>();
                    }
                } else {
                    copied = std::make_unique<T>(*srcPtr);
                }
            }

            Tcl_ObjType* targetType = nullptr;
            if constexpr (Ownership == detail::ownership::weak) {
                targetType = ObjType<T, detail::ownership::owned>::GetType();
            } else {
                targetType = GetType();
            }

            dup->internalRep.otherValuePtr = PtrToVoid(copied.release());
            dup->typePtr = targetType;
        } catch (...) {
            // Keep dup without an internal rep when duplication fails.
            dup->internalRep.otherValuePtr = nullptr;
            dup->typePtr = nullptr;
        }
    }

    static void UpdateString(Tcl_Obj* obj) noexcept {
        T* value = static_cast<T*>(obj->internalRep.otherValuePtr);
        std::string str;
        try {
            if (!value) {
                // Null object handles stringify to an empty Tcl string.
                str.clear();
            } else {
                str = ToString(*value);
            }
        } catch (...) {
            // Never propagate C++ exceptions through Tcl callbacks.
            // Fall back to empty string on conversion errors.
            str.clear();
        }

        SetStringRep(obj, str);
    }

    static int SetFromAny(Tcl_Interp* interp, Tcl_Obj* obj) noexcept {
        std::unique_ptr<T> converted;
        try {
            T value = FromAny(interp, obj);

            converted = std::make_unique<T>(std::move(value));

            if (obj->typePtr && obj->typePtr->freeIntRepProc) {
                obj->typePtr->freeIntRepProc(obj);
            }

            if constexpr (Ownership == detail::ownership::weak) {
                ObjType<T, detail::ownership::owned>::Set(obj, converted.release());
            } else {
                Set(obj, converted.release());
            }
            Tcl_InvalidateStringRep(obj);
            return TCL_OK;
        } catch (const std::exception& e) {
            if (interp) Tcl_SetObjResult(interp, Tcl_NewStringObj(e.what(), -1));
            return TCL_ERROR;
        } catch (...) {
            if (interp) {
                Tcl_SetObjResult(
                    interp,
                    Tcl_NewStringObj("Failed to convert Tcl object to target type", -1));
            }
            return TCL_ERROR;
        }
    }

    static T* GetInternalRep(Tcl_Interp* interp, Tcl_Obj* obj) {
        const bool ownsCompatibleRep =
            obj->typePtr == GetType() ||
            (Ownership == detail::ownership::weak &&
             obj->typePtr == ObjType<T, detail::ownership::owned>::GetType());
        if (!ownsCompatibleRep) {
            if (SetFromAny(interp, obj) != TCL_OK) {
                std::ostringstream oss;
                oss << "Failed to convert to \"" << TypeName() << "\"";
                throw std::runtime_error(oss.str());
            }
        }
        return static_cast<T*>(obj->internalRep.otherValuePtr);
    }

    static std::string ToString(const T& value) {
        if constexpr (std::is_const_v<T>) {
            using base_t = std::remove_const_t<T>;
            return ObjType<base_t>::ToString(value);
        } else if constexpr (detail::is_shared_ptr<T>::value) {
            using element_type = typename T::element_type;
            if (!value) return "";
            return ObjType<element_type>::ToString(*value);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return value;
        } else if constexpr (std::is_same_v<T, bool>) {
            return value ? "1" : "0";
        } else if constexpr (std::is_arithmetic_v<T>) {
            return std::to_string(value);
        } else {
            std::ostringstream oss;
            oss << "ObjType \"" << TypeName() << "\" can't be cast to string";
            throw std::runtime_error(oss.str());
        }
    }

    // Default FromAny falls back to string conversion
    static T FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
        if constexpr (std::is_const_v<T>) {
            using base_t = std::remove_const_t<T>;
            return T(ObjType<base_t>::FromAny(interp, obj));
        } else if constexpr (detail::is_shared_ptr<T>::value) {
            using element_type = typename T::element_type;
            return std::make_shared<element_type>(ObjType<element_type>::FromAny(interp, obj));
        } else {
            return FromString(Tcl_GetString(obj));
        }
    }

    static T FromString(const std::string& s) {
        if constexpr (std::is_const_v<T>) {
            using base_t = std::remove_const_t<T>;
            return T(ObjType<base_t>::FromString(s));
        } else if constexpr (std::is_same_v<T, std::string>) {
            return s;
        } else if constexpr (std::is_same_v<T, bool>) {
            if (s == "1" || s == "true" || s == "on" || s == "yes") return true;
            if (s == "0" || s == "false" || s == "off" || s == "no") return false;
            throw std::runtime_error("Invalid boolean string");
        } else if constexpr (std::is_integral_v<T>) {
            std::size_t pos = 0;
            long long v = std::stoll(s, &pos, 10);
            if (pos != s.size()) throw std::runtime_error("Invalid integral string");
            return static_cast<T>(v);
        } else if constexpr (std::is_floating_point_v<T>) {
            std::size_t pos = 0;
            double v = std::stod(s, &pos);
            if (pos != s.size()) throw std::runtime_error("Invalid floating-point string");
            return static_cast<T>(v);
        } else {
            std::ostringstream oss;
            oss << "ObjType \"" << TypeName() << "\" can't be cast from string";
            throw std::runtime_error(oss.str());
        }
    }

    // Optional customization point. The default implementation derives a
    // stable, compiler-provided type spelling when available.
    static const char* TypeName() {
        static std::string generated = AutoTypeName();
        return generated.c_str();
    }

private:
    static void SetStringRep(Tcl_Obj* obj, const std::string& str) noexcept {
        int len = 0;
        const char* bytes = nullptr;

        if (str.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            len = static_cast<int>(str.size());
            bytes = str.data();
        }

        obj->bytes = Tcl_Alloc(len + 1);
        if (len > 0 && bytes != nullptr) {
            std::memcpy(obj->bytes, bytes, static_cast<std::size_t>(len));
        }
        obj->bytes[len] = '\0';
        obj->length = len;
    }

    static void* PtrToVoid(T* p) {
        return const_cast<void*>(static_cast<const void*>(p));
    }

    template <typename U>
    static std::string CompilerTypeName() {
#if defined(__clang__) || defined(__GNUC__)
        std::string_view sig = __PRETTY_FUNCTION__;
        const std::string_view marker = "U = ";
        const auto start = sig.find(marker);
        if (start != std::string_view::npos) {
            const auto typeStart = start + marker.size();
            const auto typeEnd = sig.find_first_of(";]", typeStart);
            if (typeEnd != std::string_view::npos && typeEnd > typeStart) {
                return std::string(sig.substr(typeStart, typeEnd - typeStart));
            }
        }
#elif defined(_MSC_VER)
        std::string_view sig = __FUNCSIG__;
        const std::string_view marker = "CompilerTypeName<";
        const auto start = sig.find(marker);
        if (start != std::string_view::npos) {
            const auto typeStart = start + marker.size();
            const auto typeEnd = sig.find(">(void)", typeStart);
            if (typeEnd != std::string_view::npos && typeEnd > typeStart) {
                return std::string(sig.substr(typeStart, typeEnd - typeStart));
            }
        }
#endif
        return typeid(U).name();
    }

    static std::string AutoTypeName() {
        return CompilerTypeName<T>();
    }
};

} // namespace tclxx