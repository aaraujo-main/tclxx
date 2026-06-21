#include <tcl.h>
#include <cmath>
#include <string>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "tclxx.hpp"

class VooPoint {
public:
    VooPoint(double x, double y, std::string name, int id, bool active)
        : m_x(x), m_y(y), m_name(std::move(name)), m_id(id), m_active(active) {}

    VooPoint()
        : m_x(0.0), m_y(0.0), m_name("point"), m_id(0), m_active(true) {}

    double getX() const { return m_x; }
    double getY() const { return m_y; }
    std::string getName() const { return m_name; }
    int getId() const { return m_id; }
    bool getActive() const { return m_active; }

    void setX(double v) { m_x = v; }
    void setY(double v) { m_y = v; }
    void setName(std::string v) { m_name = std::move(v); }
    void setId(int v) { m_id = v; }
    void setActive(bool v) { m_active = v; }

    double& refX() { return m_x; }
    std::string& refName() { return m_name; }

    double distance() const { return std::sqrt(m_x * m_x + m_y * m_y); }

    static VooPoint* origin() { return new VooPoint(); }
    static std::shared_ptr<VooPoint> originShared() {
        return std::make_shared<VooPoint>(11.0, 0.0, "shared", 101, true);
    }
    static std::unique_ptr<VooPoint> originUnique() {
        return std::make_unique<VooPoint>(12.0, 0.0, "unique", 102, true);
    }
    static std::shared_ptr<VooPoint>& weakStore() {
        static std::shared_ptr<VooPoint> store =
            std::make_shared<VooPoint>(13.0, 0.0, "weak", 103, true);
        return store;
    }
    static std::weak_ptr<VooPoint> originWeak() { return weakStore(); }
    static std::weak_ptr<VooPoint> originWeakExpired() { return std::weak_ptr<VooPoint>{}; }
    static VooPoint& originRefMutable() {
        static VooPoint ref(21.0, 0.0, "ref-mutable", 201, true);
        return ref;
    }
    static const VooPoint& originRefConst() {
        static const VooPoint ref(22.0, 0.0, "ref-const", 202, true);
        return ref;
    }
    static const VooPoint* originConstPtrWeak() {
        static const VooPoint ptr(23.0, 0.0, "ptr-const-weak", 203, true);
        return &ptr;
    }
    static const VooPoint* originConstPtrOwned() {
        return new const VooPoint(24.0, 0.0, "ptr-const-owned", 204, true);
    }
    static VooPoint* originNullWeak() { return nullptr; }
    static VooPoint* originNullOwned() { return nullptr; }
    static double hypot(double x, double y) { return std::sqrt(x * x + y * y); }
    static std::string className() { return "CppVooPoint"; }
    static void noop() {}

private:
    double m_x;
    double m_y;
    std::string m_name;
    int m_id;
    bool m_active;
};

namespace tclxx {

template <>
std::string ObjType<VooPoint>::ToString(const VooPoint& p) {
    std::ostringstream oss;
    oss << p.getX() << ' ' << p.getY() << ' ' << p.getName() << ' ' << p.getId() << ' ' << p.getActive();
    return oss.str();
}

template <>
VooPoint ObjType<VooPoint>::FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
    int objc = 0;
    Tcl_Obj** objv = nullptr;
    if (Tcl_ListObjGetElements(interp, obj, &objc, &objv) != TCL_OK || objc != 5) {
        throw std::runtime_error("Expected list of 5 elements: x y name id active");
    }

    return VooPoint(
        obj_cast::to<double>(interp, objv[0]),
        obj_cast::to<double>(interp, objv[1]),
        obj_cast::to<std::string>(interp, objv[2]),
        obj_cast::to<int>(interp, objv[3]),
        obj_cast::to<bool>(interp, objv[4])
    );
}

} // namespace tclxx

extern "C" int Point_Init(Tcl_Interp* interp) {
    if (!interp) return TCL_ERROR;

    Tcl_CreateNamespace(interp, "::CppVooPoint", nullptr, nullptr);

    TCLXX_CMD_NEW(interp, "::CppVooPoint::new", VooPoint,
                     double, double, std::string, int, bool);
    TCLXX_CMD_NEW0(interp, "::CppVooPoint::new()", VooPoint);

    TCLXX_CMD_STATIC_OWNED(interp, "::CppVooPoint::origin", &VooPoint::origin);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.shared", &VooPoint::originShared);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.unique", &VooPoint::originUnique);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.weak", &VooPoint::originWeak);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.weak.expired", &VooPoint::originWeakExpired);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.ref.mutable", &VooPoint::originRefMutable);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.ref.const", &VooPoint::originRefConst);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.ptr.const.weak", &VooPoint::originConstPtrWeak);
    TCLXX_CMD_STATIC_OWNED(interp, "::CppVooPoint::origin.ptr.const.owned", &VooPoint::originConstPtrOwned);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::origin.null.weak", &VooPoint::originNullWeak);
    TCLXX_CMD_STATIC_OWNED(interp, "::CppVooPoint::origin.null.owned", &VooPoint::originNullOwned);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::hypot", &VooPoint::hypot);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::class.name", &VooPoint::className);
    TCLXX_CMD_STATIC(interp, "::CppVooPoint::noop", &VooPoint::noop);

    TCLXX_CMD_GETTER_METHOD(interp, "::CppVooPoint::get.x", &VooPoint::getX);
    TCLXX_CMD_GETTER_METHOD(interp, "::CppVooPoint::get.y", &VooPoint::getY);
    TCLXX_CMD_GETTER_METHOD(interp, "::CppVooPoint::get.name", &VooPoint::getName);
    TCLXX_CMD_GETTER_METHOD(interp, "::CppVooPoint::get.id", &VooPoint::getId);
    TCLXX_CMD_GETTER_METHOD(interp, "::CppVooPoint::get.active", &VooPoint::getActive);
    TCLXX_CMD_GETTER_METHOD(interp, "::CppVooPoint::distance", &VooPoint::distance);

    TCLXX_CMD_SETTER_METHOD(interp, "::CppVooPoint::set.x", &VooPoint::setX);
    TCLXX_CMD_SETTER_METHOD(interp, "::CppVooPoint::set.y", &VooPoint::setY);
    TCLXX_CMD_SETTER_METHOD(interp, "::CppVooPoint::set.name", &VooPoint::setName);
    TCLXX_CMD_SETTER_METHOD(interp, "::CppVooPoint::set.id", &VooPoint::setId);
    TCLXX_CMD_SETTER_METHOD(interp, "::CppVooPoint::set.active", &VooPoint::setActive);

    TCLXX_CMD_UPDATER_METHOD(interp, "::CppVooPoint::update.x", &VooPoint::refX);
    TCLXX_CMD_UPDATER_METHOD(interp, "::CppVooPoint::update.name", &VooPoint::refName);

    return Tcl_PkgProvideEx(interp, "VooPointCpp", "1.0.0", nullptr);
}

static int AppInit(Tcl_Interp* interp) {
    if (Tcl_Init(interp) != TCL_OK) {
        return TCL_ERROR;
    }
    return Point_Init(interp);
}

int main(int argc, char** argv) {
    Tcl_Main(argc, argv, AppInit);
    return 0;
}
