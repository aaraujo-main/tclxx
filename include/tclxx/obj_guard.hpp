#pragma once
#include <tcl.h>

namespace tclxx {
class ObjGuard {
private:
    Tcl_Obj* objPtr;

public:
    // Takes ownership of a Tcl_Obj (already has a refCount)
    explicit ObjGuard(Tcl_Obj* obj = nullptr) noexcept : objPtr(obj) {}

    // Releases the object when guard goes out of scope
    ~ObjGuard() noexcept {
        if (objPtr) {
            Tcl_DecrRefCount(objPtr);
        }
    }

    // Disable copying to prevent double-free errors
    ObjGuard(const ObjGuard&) = delete;
    ObjGuard& operator=(const ObjGuard&) = delete;

    // Allow moving to transfer ownership
    ObjGuard(ObjGuard&& other) noexcept : objPtr(other.objPtr) {
        other.objPtr = nullptr;
    }

    ObjGuard& operator=(ObjGuard&& other) noexcept {
        if (this != &other) {
            if (objPtr) {
                Tcl_DecrRefCount(objPtr);
            }
            objPtr = other.objPtr;
            other.objPtr = nullptr;
        }
        return *this;
    }

    // Access the raw Tcl_Obj pointer
    Tcl_Obj* get() const noexcept {
        return objPtr;
    }

    // Detach the object so it is NOT freed when the guard dies
    Tcl_Obj* release() noexcept {
        Tcl_Obj* temp = objPtr;
        objPtr = nullptr;
        return temp;
    }

    // Reset to a new object, decrementing the refCount of the old one
    void reset(Tcl_Obj* obj = nullptr) noexcept {
        if (objPtr) {
            Tcl_DecrRefCount(objPtr);
        }
        objPtr = obj;
    }
};
}
