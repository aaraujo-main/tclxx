#include <tcl.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "tclxx.hpp"

namespace {

std::string ResultString(Tcl_Interp* interp) {
    const char* result = Tcl_GetStringResult(interp);
    return result == nullptr ? std::string() : std::string(result);
}

testing::AssertionResult EvalOk(Tcl_Interp* interp, const char* script) {
    int rc = Tcl_Eval(interp, script);
    if (rc != TCL_OK) {
        return testing::AssertionFailure()
               << "script failed: " << script << "\nerror: " << ResultString(interp);
    }
    return testing::AssertionSuccess();
}

testing::AssertionResult EvalError(Tcl_Interp* interp, const char* script) {
    int rc = Tcl_Eval(interp, script);
    if (rc == TCL_OK) {
        return testing::AssertionFailure()
               << "script unexpectedly succeeded: " << script;
    }
    return testing::AssertionSuccess();
}

testing::AssertionResult ResultListHasSize(
    Tcl_Interp* interp,
    int expected_count,
    Tcl_Obj*** outv,
    const char* context) {
    Tcl_Obj* result_obj = Tcl_GetObjResult(interp);
    int objc = 0;
    Tcl_Obj** objv = nullptr;
    if (Tcl_ListObjGetElements(interp, result_obj, &objc, &objv) != TCL_OK) {
        return testing::AssertionFailure()
               << context << ": result is not a list: " << ResultString(interp);
    }
    if (objc != expected_count) {
        return testing::AssertionFailure()
               << context << ": expected " << expected_count
               << " elements, got " << objc;
    }

    *outv = objv;
    return testing::AssertionSuccess();
}

struct ApiPoint {
    ApiPoint(double x, double y, std::string name, int id, bool active)
        : x_(x), y_(y), name_(std::move(name)), id_(id), active_(active) {}

    ApiPoint() : x_(0.0), y_(0.0), name_("point"), id_(0), active_(true) {}

    double getX() const { return x_; }
    std::string getName() const { return name_; }
    int getId() const { return id_; }
    bool getActive() const { return active_; }

    void setX(double v) { x_ = v; }
    void setName(std::string v) { name_ = std::move(v); }
    ApiPoint* setXAndClone(double v) {
        x_ = v;
        return new ApiPoint(*this);
    }
    std::shared_ptr<ApiPoint> setXAndCloneShared(double v) {
        x_ = v;
        return std::make_shared<ApiPoint>(*this);
    }

    double& refX() { return x_; }
    std::string& refName() { return name_; }
    ApiPoint* cloneOwned() const { return new ApiPoint(*this); }
    std::shared_ptr<ApiPoint> cloneShared() const {
        return std::make_shared<ApiPoint>(*this);
    }

    static double hypot(double x, double y) { return std::sqrt(x * x + y * y); }
    static ApiPoint* origin() { return new ApiPoint(); }
    static std::shared_ptr<ApiPoint> originShared() {
        return std::make_shared<ApiPoint>(20.0, 0.0, "shared", 1, true);
    }
    static std::unique_ptr<ApiPoint> originUnique() {
        return std::make_unique<ApiPoint>(30.0, 0.0, "unique", 2, true);
    }
    static std::shared_ptr<ApiPoint>& weakStore() {
        static std::shared_ptr<ApiPoint> store =
            std::make_shared<ApiPoint>(40.0, 0.0, "weak", 3, true);
        return store;
    }
    static std::weak_ptr<ApiPoint> originWeak() { return weakStore(); }
    static std::weak_ptr<ApiPoint> originWeakExpired() { return std::weak_ptr<ApiPoint>{}; }
    static ApiPoint& originRef() {
        static ApiPoint ref(50.0, 0.0, "ref", 4, true);
        return ref;
    }
    static const ApiPoint& originConstRef() {
        static const ApiPoint ref(60.0, 0.0, "const-ref", 5, true);
        return ref;
    }
    static const ApiPoint* originConstPtrWeak() {
        static const ApiPoint ptr(70.0, 0.0, "const-ptr-weak", 6, true);
        return &ptr;
    }
    static const ApiPoint* originConstPtrOwned() {
        return new const ApiPoint(80.0, 0.0, "const-ptr-owned", 7, true);
    }
    static ApiPoint* originNullWeak() { return nullptr; }
    static ApiPoint* originNullOwned() { return nullptr; }
    static std::string className() { return "ApiPoint"; }
    static void noop() {}

private:
    double x_;
    double y_;
    std::string name_;
    int id_;
    bool active_;
};

struct ArgCapture {
    ArgCapture(ClientData, Tcl_Interp*, int objc, Tcl_Obj* const objv[]) : objc_(objc) {
        if (objc_ > 1) {
            arg1_ = Tcl_GetString(objv[1]);
        }
        if (objc_ > 2) {
            arg2_ = Tcl_GetString(objv[2]);
        }
        label_ = arg1_ + ":" + arg2_;
    }

    std::string describe() const {
        return std::to_string(objc_) + " " + arg1_ + " " + arg2_ + " " + label_;
    }

    void setLabel(std::string value) { label_ = std::move(value); }

private:
    int objc_ = 0;
    std::string arg1_;
    std::string arg2_;
    std::string label_;
};

double point_get_x(ApiPoint* point) {
    return point->getX();
}

Tcl_Obj* point_get_obj(ApiPoint*) {
    return Tcl_NewIntObj(42);
}

void point_set_x(ApiPoint* point, double value) {
    point->setX(value);
}

std::string object_arg_text(Tcl_Obj* object) {
    return Tcl_GetString(object);
}

ApiPoint* point_clone_owned(ApiPoint* point) {
    return new ApiPoint(*point);
}

std::shared_ptr<ApiPoint> point_clone_shared(ApiPoint* point) {
    return std::make_shared<ApiPoint>(*point);
}

ApiPoint* point_set_x_clone_owned(ApiPoint* point, double value) {
    point->setX(value);
    return new ApiPoint(*point);
}

std::shared_ptr<ApiPoint> point_set_x_clone_shared(ApiPoint* point, double value) {
    point->setX(value);
    return std::make_shared<ApiPoint>(*point);
}

class CmdWrapperFixture : public ::testing::Test {
protected:
    void SetUp() override {
        interp_ = Tcl_CreateInterp();
        ASSERT_NE(interp_, nullptr);
        ASSERT_EQ(Tcl_Init(interp_), TCL_OK) << ResultString(interp_);
        RegisterCommands();
    }

    void TearDown() override {
        if (interp_ != nullptr) {
            Tcl_DeleteInterp(interp_);
            interp_ = nullptr;
        }
    }

    void RegisterCommands() {
        TCLXX_CMD_NEW(interp_, "::ApiPoint::new", ApiPoint,
                      double, double, std::string, int, bool);
        TCLXX_CMD_NEW0(interp_, "::ApiPoint::new()", ApiPoint);
        TCLXX_CMD_NEW0_SHARED(interp_, "::ApiPoint::new().shared", ApiPoint);
        TCLXX_CMD_NEWARGS(interp_, "::ArgCapture::newargs", ArgCapture);
        TCLXX_CMD_NEWARGS_SHARED(interp_, "::ArgCapture::newargs.shared", ArgCapture);

        TCLXX_CMD_GETTER_METHOD(interp_, "::ApiPoint::get.x", &ApiPoint::getX);
        TCLXX_CMD_GETTER_METHOD(interp_, "::ApiPoint::get.name", &ApiPoint::getName);
        TCLXX_CMD_GETTER(interp_, "::ApiPoint::get.x.free", &point_get_x);
        TCLXX_CMD_GETTER(interp_, "::ApiPoint::get.obj", &point_get_obj);
        TCLXX_CMD_GETTER_METHOD(interp_, "::ArgCapture::get.desc", &ArgCapture::describe);
        TCLXX_CMD_SETTER_METHOD(interp_, "::ArgCapture::set.label", &ArgCapture::setLabel);
        TCLXX_CMD_GETTER_OWNED(interp_, "::ApiPoint::clone.free.owned", &point_clone_owned);
        TCLXX_CMD_GETTER_SHARED(interp_, "::ApiPoint::clone.free.shared", &point_clone_shared);
        TCLXX_CMD_GETTER_METHOD_OWNED(interp_, "::ApiPoint::clone.member.owned", &ApiPoint::cloneOwned);
        TCLXX_CMD_GETTER_METHOD_SHARED(interp_, "::ApiPoint::clone.member.shared", &ApiPoint::cloneShared);
        TCLXX_CMD_SETTER_METHOD(interp_, "::ApiPoint::set.x", &ApiPoint::setX);
        TCLXX_CMD_SETTER_METHOD(interp_, "::ApiPoint::set.name", &ApiPoint::setName);
        TCLXX_CMD_SETTER(interp_, "::ApiPoint::set.x.free", &point_set_x);
        TCLXX_CMD_SETTER_OWNED(interp_, "::ApiPoint::set.x.clone.free.owned", &point_set_x_clone_owned);
        TCLXX_CMD_SETTER_SHARED(interp_, "::ApiPoint::set.x.clone.free.shared", &point_set_x_clone_shared);
        TCLXX_CMD_SETTER_METHOD_OWNED(interp_, "::ApiPoint::set.x.clone.member.owned", &ApiPoint::setXAndClone);
        TCLXX_CMD_SETTER_METHOD_SHARED(interp_, "::ApiPoint::set.x.clone.member.shared", &ApiPoint::setXAndCloneShared);
        TCLXX_CMD_UPDATER_METHOD(interp_, "::ApiPoint::update.x", &ApiPoint::refX);
        TCLXX_CMD_UPDATER_METHOD(interp_, "::ApiPoint::update.name", &ApiPoint::refName);

        TCLXX_CMD_STATIC_OWNED(interp_, "::ApiPoint::origin", &ApiPoint::origin);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.shared", &ApiPoint::originShared);
        TCLXX_CMD_STATIC_SHARED(interp_, "::ApiPoint::origin.shared.shared", &ApiPoint::originShared);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.unique", &ApiPoint::originUnique);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.weak", &ApiPoint::originWeak);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.weak.expired", &ApiPoint::originWeakExpired);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.ref", &ApiPoint::originRef);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.const.ref", &ApiPoint::originConstRef);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.const.ptr.weak", &ApiPoint::originConstPtrWeak);
        TCLXX_CMD_STATIC_OWNED(interp_, "::ApiPoint::origin.const.ptr.owned", &ApiPoint::originConstPtrOwned);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::origin.null.weak", &ApiPoint::originNullWeak);
        TCLXX_CMD_STATIC_OWNED(interp_, "::ApiPoint::origin.null.owned", &ApiPoint::originNullOwned);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::hypot", &ApiPoint::hypot);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::object.arg", &object_arg_text);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::class", &ApiPoint::className);
        TCLXX_CMD_STATIC(interp_, "::ApiPoint::noop", &ApiPoint::noop);
    }

    Tcl_Interp* interp_ = nullptr;
};

} // namespace

namespace tclxx {

template <>
std::string ObjType<ArgCapture>::ToString(const ArgCapture& capture) {
    return capture.describe();
}

template <>
std::string ObjType<ApiPoint>::ToString(const ApiPoint& p) {
    std::ostringstream oss;
    oss << p.getX() << " 0 " << p.getName() << " " << p.getId() << " " << p.getActive();
    return oss.str();
}

template <>
ApiPoint ObjType<ApiPoint>::FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
    int objc = 0;
    Tcl_Obj** objv = nullptr;
    if (Tcl_ListObjGetElements(interp, obj, &objc, &objv) != TCL_OK || objc != 5) {
        throw std::runtime_error("Expected list of 5 elements: x y name id active");
    }

    return ApiPoint(
        obj_cast::to<double>(interp, objv[0]),
        obj_cast::to<double>(interp, objv[1]),
        obj_cast::to<std::string>(interp, objv[2]),
        obj_cast::to<int>(interp, objv[3]),
        obj_cast::to<bool>(interp, objv[4])
    );
}

} // namespace tclxx
namespace {

TEST_F(CmdWrapperFixture, ConstructorGetterAndSetterCommandsWork) {
    ASSERT_TRUE(EvalOk(interp_, "set p [::ApiPoint::new 3.0 4.0 p0 10 1]"));

    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p] == 3.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x p 9.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p] == 9.5}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x.free $p] == 9.5}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x.free p 12.25"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p] == 12.25}"));
    EXPECT_EQ(ResultString(interp_), "1");
}

TEST_F(CmdWrapperFixture, GetterPreservesRawTclObjectResult) {
    ASSERT_TRUE(EvalOk(interp_, "set p [::ApiPoint::new()]"));
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::get.obj $p"));

    EXPECT_EQ(ResultString(interp_), "42");
    int value = 0;
    ASSERT_EQ(Tcl_GetIntFromObj(interp_, Tcl_GetObjResult(interp_), &value), TCL_OK);
    EXPECT_EQ(value, 42);
}

TEST_F(CmdWrapperFixture, CommandConvertsTclObjectParameter) {
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::object.arg {typed Tcl object}"));
    EXPECT_EQ(ResultString(interp_), "typed Tcl object");
}

TEST_F(CmdWrapperFixture, CreateArgsCommandsForwardRawInvocationTuple) {
    ASSERT_TRUE(EvalOk(interp_, "set ao [::ArgCapture::newargs alpha beta]"));
    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::get.desc $ao"));
    EXPECT_EQ(ResultString(interp_), "3 alpha beta alpha:beta");

    ASSERT_TRUE(EvalOk(interp_, "set as [::ArgCapture::newargs.shared gamma delta]"));
    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::get.desc $as"));
    EXPECT_EQ(ResultString(interp_), "3 gamma delta gamma:delta");
}

TEST_F(CmdWrapperFixture, CreateArgsOwnedAndSharedFollowDuplicationSemantics) {
    ASSERT_TRUE(EvalOk(interp_, "set ao [::ArgCapture::newargs alpha beta]"));
    Tcl_Obj* aoObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("ao", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(aoObj, nullptr);
    Tcl_Obj* aoDup = Tcl_DuplicateObj(aoObj);
    Tcl_IncrRefCount(aoDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("aoDup", -1), nullptr, aoDup, TCL_LEAVE_ERR_MSG));

    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::set.label aoDup owned-mut"));
    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::get.desc $ao"));
    EXPECT_EQ(ResultString(interp_), "3 alpha beta alpha:beta");
    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::get.desc $aoDup"));
    EXPECT_EQ(ResultString(interp_), "3 alpha beta owned-mut");
    Tcl_DecrRefCount(aoDup);

    ASSERT_TRUE(EvalOk(interp_, "set as [::ArgCapture::newargs.shared gamma delta]"));
    Tcl_Obj* asObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("as", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(asObj, nullptr);
    Tcl_Obj* asDup = Tcl_DuplicateObj(asObj);
    Tcl_IncrRefCount(asDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("asDup", -1), nullptr, asDup, TCL_LEAVE_ERR_MSG));

    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::set.label asDup shared-mut"));
    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::get.desc $as"));
    EXPECT_EQ(ResultString(interp_), "3 gamma delta shared-mut");
    ASSERT_TRUE(EvalOk(interp_, "::ArgCapture::get.desc $asDup"));
    EXPECT_EQ(ResultString(interp_), "3 gamma delta shared-mut");
    Tcl_DecrRefCount(asDup);
}

TEST_F(CmdWrapperFixture, UpdaterSetTempVarToEmptyAfterFinishingAndPersistChanges) {
    ASSERT_TRUE(EvalOk(interp_, "set p [::ApiPoint::new 12.25 2.0 p0 10 1]"));

    ASSERT_TRUE(EvalOk(interp_,
        "set tmpX 1.5; set escapedX sentinel; ::ApiPoint::update.x p tmpX {"
        "set escapedX $tmpX; set tmpX [expr {$tmpX + 2.0}]}; list $tmpX $escapedX [::ApiPoint::get.x $p]"));
    {
        Tcl_Obj** objv = nullptr;
        ASSERT_TRUE(ResultListHasSize(interp_, 3, &objv, "updater x result list shape"));
        // list = tmpX escapedX currentX
        EXPECT_TRUE(std::string(Tcl_GetString(objv[0])).empty());
        EXPECT_EQ(std::string(Tcl_GetString(objv[1])), "12.250000");
        EXPECT_NEAR(tclxx::obj_cast::to<double>(interp_, objv[2]), 14.25, 1e-9);
    }
    ASSERT_TRUE(EvalOk(interp_, "string length $tmpX"));
    EXPECT_EQ(ResultString(interp_), "0");

    ASSERT_TRUE(EvalOk(interp_,
        "set tmpN base; set escapedN sentinel; ::ApiPoint::update.name p tmpN {"
        "set escapedN $tmpN; append tmpN _tail}; list $tmpN $escapedN [::ApiPoint::get.name $p]"));
    {
        Tcl_Obj** objv = nullptr;
        ASSERT_TRUE(ResultListHasSize(interp_, 3, &objv, "updater name result list shape"));
        // list = tmpN escapedN currentName
        EXPECT_TRUE(std::string(Tcl_GetString(objv[0])).empty());
        EXPECT_EQ(std::string(Tcl_GetString(objv[2])), "p0_tail");
    }
}

TEST_F(CmdWrapperFixture, StaticCommandsExposeFactoryModesAndPrimitives) {
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::hypot 3.0 4.0] == 5.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::class"));
    EXPECT_EQ(ResultString(interp_), "ApiPoint");

    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::noop"));
    EXPECT_TRUE(ResultString(interp_).empty());

    ASSERT_TRUE(EvalOk(interp_, "set p0 [::ApiPoint::new()]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p0] == 0.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set p0s [::ApiPoint::new().shared]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p0s] == 0.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set po [::ApiPoint::origin]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $po] == 0.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set ps [::ApiPoint::origin.shared]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $ps] == 20.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pu [::ApiPoint::origin.unique]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pu] == 30.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pw [::ApiPoint::origin.weak]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pw] == 40.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pr [::ApiPoint::origin.ref]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pr] == 50.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x pr 51.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pr] == 51.5}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pcr [::ApiPoint::origin.const.ref]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pcr] == 60.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pcpw [::ApiPoint::origin.const.ptr.weak]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pcpw] == 70.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pcpo [::ApiPoint::origin.const.ptr.owned]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pcpo] == 80.0}"));
    EXPECT_EQ(ResultString(interp_), "1");
}

TEST_F(CmdWrapperFixture, ConstBackedHandlesRejectMutableMemberCommands) {
    ASSERT_TRUE(EvalOk(interp_, "set pcr [::ApiPoint::origin.const.ref]"));
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::set.x pcr 61.5"));
    EXPECT_NE(ResultString(interp_).find("const object handle"), std::string::npos);
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pcr] == 60.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pcpw [::ApiPoint::origin.const.ptr.weak]"));
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::set.x pcpw 71.5"));
    EXPECT_NE(ResultString(interp_).find("const object handle"), std::string::npos);
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pcpw] == 70.0}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set pcpo [::ApiPoint::origin.const.ptr.owned]"));
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::set.x pcpo 81.5"));
    EXPECT_NE(ResultString(interp_).find("const object handle"), std::string::npos);
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $pcpo] == 80.0}"));
    EXPECT_EQ(ResultString(interp_), "1");
}

TEST_F(CmdWrapperFixture, NullHandlesReturnEmptyAndGuardMemberAccess) {
    ASSERT_TRUE(EvalOk(interp_, "set pnullw [::ApiPoint::origin.null.weak]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[string length $pnullw] == 0}"));
    EXPECT_EQ(ResultString(interp_), "1");
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::get.x $pnullw"));
    EXPECT_NE(ResultString(interp_).find("null object handle"), std::string::npos);
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::set.x pnullw 1.0"));
    EXPECT_NE(ResultString(interp_).find("null object handle"), std::string::npos);

    ASSERT_TRUE(EvalOk(interp_, "set pnullo [::ApiPoint::origin.null.owned]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[string length $pnullo] == 0}"));
    EXPECT_EQ(ResultString(interp_), "1");
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::get.x $pnullo"));
    EXPECT_NE(ResultString(interp_).find("null object handle"), std::string::npos);
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::set.x pnullo 1.0"));
    EXPECT_NE(ResultString(interp_).find("null object handle"), std::string::npos);
}

TEST_F(CmdWrapperFixture, OwnedGetterAndSetterVariantsReturnClones) {
    ASSERT_TRUE(EvalOk(interp_, "set p [::ApiPoint::new 3.0 4.0 p0 10 1]"));

    ASSERT_TRUE(EvalOk(interp_, "set cgf [::ApiPoint::clone.free.owned $p]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $cgf] == [::ApiPoint::get.x $p]}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set cgm [::ApiPoint::clone.member.owned $p]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $cgm] == [::ApiPoint::get.x $p]}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set csf [::ApiPoint::set.x.clone.free.owned p 16.5]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p] == 16.5}"));
    EXPECT_EQ(ResultString(interp_), "1");
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $csf] == 16.5}"));
    EXPECT_EQ(ResultString(interp_), "1");

    ASSERT_TRUE(EvalOk(interp_, "set csm [::ApiPoint::set.x.clone.member.owned p 17.75]"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $p] == 17.75}"));
    EXPECT_EQ(ResultString(interp_), "1");
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $csm] == 17.75}"));
    EXPECT_EQ(ResultString(interp_), "1");
}

TEST_F(CmdWrapperFixture, SharedVariantsPreserveSharedOwnershipAcrossDuplication) {
    ASSERT_TRUE(EvalOk(interp_, "set p [::ApiPoint::new 3.0 4.0 p0 10 1]"));

    ASSERT_TRUE(EvalOk(interp_, "set gfs [::ApiPoint::clone.free.shared $p]"));
    Tcl_Obj* gfsObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("gfs", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(gfsObj, nullptr);
    Tcl_Obj* gfsDup = Tcl_DuplicateObj(gfsObj);
    Tcl_IncrRefCount(gfsDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("gfsDup", -1), nullptr, gfsDup, TCL_LEAVE_ERR_MSG));
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x gfsDup 42.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $gfs] == 42.5}"));
    EXPECT_EQ(ResultString(interp_), "1");
    Tcl_DecrRefCount(gfsDup);

    ASSERT_TRUE(EvalOk(interp_, "set gms [::ApiPoint::clone.member.shared $p]"));
    Tcl_Obj* gmsObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("gms", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(gmsObj, nullptr);
    Tcl_Obj* gmsDup = Tcl_DuplicateObj(gmsObj);
    Tcl_IncrRefCount(gmsDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("gmsDup", -1), nullptr, gmsDup, TCL_LEAVE_ERR_MSG));
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x gmsDup 52.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $gms] == 52.5}"));
    EXPECT_EQ(ResultString(interp_), "1");
    Tcl_DecrRefCount(gmsDup);

    ASSERT_TRUE(EvalOk(interp_, "set sfs [::ApiPoint::set.x.clone.free.shared p 62.5]"));
    Tcl_Obj* sfsObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("sfs", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(sfsObj, nullptr);
    Tcl_Obj* sfsDup = Tcl_DuplicateObj(sfsObj);
    Tcl_IncrRefCount(sfsDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("sfsDup", -1), nullptr, sfsDup, TCL_LEAVE_ERR_MSG));
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x sfsDup 72.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $sfs] == 72.5}"));
    EXPECT_EQ(ResultString(interp_), "1");
    Tcl_DecrRefCount(sfsDup);

    ASSERT_TRUE(EvalOk(interp_, "set sms [::ApiPoint::set.x.clone.member.shared p 82.5]"));
    Tcl_Obj* smsObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("sms", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(smsObj, nullptr);
    Tcl_Obj* smsDup = Tcl_DuplicateObj(smsObj);
    Tcl_IncrRefCount(smsDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("smsDup", -1), nullptr, smsDup, TCL_LEAVE_ERR_MSG));
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x smsDup 92.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $sms] == 92.5}"));
    EXPECT_EQ(ResultString(interp_), "1");
    Tcl_DecrRefCount(smsDup);

    ASSERT_TRUE(EvalOk(interp_, "set oss [::ApiPoint::origin.shared.shared]"));
    Tcl_Obj* ossObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("oss", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(ossObj, nullptr);
    Tcl_Obj* ossDup = Tcl_DuplicateObj(ossObj);
    Tcl_IncrRefCount(ossDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("ossDup", -1), nullptr, ossDup, TCL_LEAVE_ERR_MSG));
    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x ossDup 112.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $oss] == 112.5}"));
    EXPECT_EQ(ResultString(interp_), "1");
    Tcl_DecrRefCount(ossDup);
}

TEST_F(CmdWrapperFixture, DefaultSharedPtrReturnModeUsesOwnedSemantics) {
    ASSERT_TRUE(EvalOk(interp_, "set ps [::ApiPoint::origin.shared]"));
    Tcl_Obj* psObj = Tcl_ObjGetVar2(interp_, Tcl_NewStringObj("ps", -1), nullptr, TCL_LEAVE_ERR_MSG);
    ASSERT_NE(psObj, nullptr);
    Tcl_Obj* psDup = Tcl_DuplicateObj(psObj);
    Tcl_IncrRefCount(psDup);
    ASSERT_TRUE(Tcl_ObjSetVar2(interp_, Tcl_NewStringObj("psDup", -1), nullptr, psDup, TCL_LEAVE_ERR_MSG));

    ASSERT_TRUE(EvalOk(interp_, "::ApiPoint::set.x psDup 212.5"));
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $ps] == 20.0}"));
    EXPECT_EQ(ResultString(interp_), "1");
    ASSERT_TRUE(EvalOk(interp_, "expr {[::ApiPoint::get.x $psDup] == 212.5}"));
    EXPECT_EQ(ResultString(interp_), "1");

    Tcl_DecrRefCount(psDup);
}

TEST_F(CmdWrapperFixture, InvalidInvocationPathsProduceActionableErrors) {
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::new 1.0 2.0"));

    ASSERT_TRUE(EvalOk(interp_, "set p [::ApiPoint::new 3.0 4.0 p0 10 1]"));
    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::set.x p not-a-double"));
    EXPECT_NE(ResultString(interp_).find("double"), std::string::npos);

    ASSERT_TRUE(EvalError(interp_, "::ApiPoint::origin.weak.expired"));
    EXPECT_NE(ResultString(interp_).find("expired std::weak_ptr"), std::string::npos);
}

} // namespace
