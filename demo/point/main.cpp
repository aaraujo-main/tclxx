#include <tcl.h>
#include <cmath>
#include <string>
#include <sstream>
#include <stdexcept>
#include <memory>

#include "tclxx.hpp"

class Point {
public:
    Point(double x, double y, std::string name, int id, bool active)
        : m_x(x), m_y(y), m_name(std::move(name)), m_id(id), m_active(active) {}

    Point()
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

    void move(std::function<void(double, double)> move_func) {
        move_func(m_x, m_y);
    }

    static Point* origin() { return new Point(); }
    static std::shared_ptr<Point> originShared() {
        return std::make_shared<Point>(11.0, 0.0, "shared", 101, true);
    }
    static std::unique_ptr<Point> originUnique() {
        return std::make_unique<Point>(12.0, 0.0, "unique", 102, true);
    }
    static std::shared_ptr<Point>& weakStore() {
        static std::shared_ptr<Point> store =
            std::make_shared<Point>(13.0, 0.0, "weak", 103, true);
        return store;
    }
    static std::weak_ptr<Point> originWeak() { return weakStore(); }
    static std::weak_ptr<Point> originWeakExpired() { return std::weak_ptr<Point>{}; }
    static Point& originRefMutable() {
        static Point ref(21.0, 0.0, "ref-mutable", 201, true);
        return ref;
    }
    static const Point& originRefConst() {
        static const Point ref(22.0, 0.0, "ref-const", 202, true);
        return ref;
    }
    static const Point* originConstPtrWeak() {
        static const Point ptr(23.0, 0.0, "ptr-const-weak", 203, true);
        return &ptr;
    }
    static const Point* originConstPtrOwned() {
        return new const Point(24.0, 0.0, "ptr-const-owned", 204, true);
    }
    static Point* originNullWeak() { return nullptr; }
    static Point* originNullOwned() { return nullptr; }
    static double hypot(double x, double y) { return std::sqrt(x * x + y * y); }
    static std::string className() { return "Point"; }
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
std::string ObjType<Point>::ToString(const Point& p) {
    std::ostringstream oss;
    oss << p.getX() << ' ' << p.getY() << ' ' << p.getName() << ' ' << p.getId() << ' ' << p.getActive();
    return oss.str();
}

template <>
Point ObjType<Point>::FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
    int objc = 0;
    Tcl_Obj** objv = nullptr;
    if (Tcl_ListObjGetElements(interp, obj, &objc, &objv) != TCL_OK || objc != 5) {
        throw std::runtime_error("Expected list of 5 elements: x y name id active");
    }

    return Point(
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

    Tcl_CreateNamespace(interp, "::Point", nullptr, nullptr);

    TCLXX_CMD_NEW(interp, "::Point::new", Point,
                     double, double, std::string, int, bool);
    TCLXX_CMD_NEW0(interp, "::Point::new()", Point);

    TCLXX_CMD_STATIC_OWNED(interp, "::Point::origin", &Point::origin);
    TCLXX_CMD_STATIC(interp, "::Point::origin.shared", &Point::originShared);
    TCLXX_CMD_STATIC(interp, "::Point::origin.unique", &Point::originUnique);
    TCLXX_CMD_STATIC(interp, "::Point::origin.weak", &Point::originWeak);
    TCLXX_CMD_STATIC(interp, "::Point::origin.weak.expired", &Point::originWeakExpired);
    TCLXX_CMD_STATIC(interp, "::Point::origin.ref.mutable", &Point::originRefMutable);
    TCLXX_CMD_STATIC(interp, "::Point::origin.ref.const", &Point::originRefConst);
    TCLXX_CMD_STATIC(interp, "::Point::origin.ptr.const.weak", &Point::originConstPtrWeak);
    TCLXX_CMD_STATIC_OWNED(interp, "::Point::origin.ptr.const.owned", &Point::originConstPtrOwned);
    TCLXX_CMD_STATIC(interp, "::Point::origin.null.weak", &Point::originNullWeak);
    TCLXX_CMD_STATIC_OWNED(interp, "::Point::origin.null.owned", &Point::originNullOwned);
    TCLXX_CMD_STATIC(interp, "::Point::hypot", &Point::hypot);
    TCLXX_CMD_STATIC(interp, "::Point::class.name", &Point::className);
    TCLXX_CMD_STATIC(interp, "::Point::noop", &Point::noop);

    TCLXX_CMD_GETTER_METHOD(interp, "::Point::get.x", &Point::getX);
    TCLXX_CMD_GETTER_METHOD(interp, "::Point::get.y", &Point::getY);
    TCLXX_CMD_GETTER_METHOD(interp, "::Point::get.name", &Point::getName);
    TCLXX_CMD_GETTER_METHOD(interp, "::Point::get.id", &Point::getId);
    TCLXX_CMD_GETTER_METHOD(interp, "::Point::get.active", &Point::getActive);
    TCLXX_CMD_GETTER_METHOD(interp, "::Point::distance", &Point::distance);

    TCLXX_CMD_SETTER_METHOD(interp, "::Point::set.x", &Point::setX);
    TCLXX_CMD_SETTER_METHOD(interp, "::Point::set.y", &Point::setY);
    TCLXX_CMD_SETTER_METHOD(interp, "::Point::set.name", &Point::setName);
    TCLXX_CMD_SETTER_METHOD(interp, "::Point::set.id", &Point::setId);
    TCLXX_CMD_SETTER_METHOD(interp, "::Point::set.active", &Point::setActive);

    TCLXX_CMD_UPDATER_METHOD(interp, "::Point::update.x", &Point::refX);
    TCLXX_CMD_UPDATER_METHOD(interp, "::Point::update.name", &Point::refName);

    TCLXX_CMD_GETTER_METHOD(interp, "::Point::move_getter", &Point::move);
    TCLXX_CMD_SETTER_METHOD(interp, "::Point::move_setter", &Point::move);

    return Tcl_PkgProvideEx(interp, "PointCpp", "1.0.0", nullptr);
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
