#include <tcl.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

#include "tclxx.hpp"

namespace {

std::string ResultString(Tcl_Interp* interp) {
    const char* result = Tcl_GetStringResult(interp);
    return result == nullptr ? std::string() : std::string(result);
}

struct Sample {
    int id;
    explicit Sample(int v) : id(v) {}
    Sample(const Sample& other) : id(other.id) {}
};

class TclInterpFixture : public ::testing::Test {
protected:
    void SetUp() override {
        interp_ = Tcl_CreateInterp();
        ASSERT_NE(interp_, nullptr);
        ASSERT_EQ(Tcl_Init(interp_), TCL_OK) << ResultString(interp_);
    }

    void TearDown() override {
        if (interp_ != nullptr) {
            Tcl_DeleteInterp(interp_);
            interp_ = nullptr;
        }
    }

    Tcl_Interp* interp_ = nullptr;
};

} // namespace

namespace tclxx {

template <>
std::string ObjType<Sample>::ToString(const Sample& s) {
    return std::to_string(s.id);
}

template <>
Sample ObjType<Sample>::FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
    int v = 0;
    if (Tcl_GetIntFromObj(interp, obj, &v) != TCL_OK) {
        throw std::runtime_error("Sample requires integer input");
    }
    return Sample(v);
}

} // namespace tclxx

namespace {

TEST_F(TclInterpFixture, PrimitiveAndTclObjRoundTrips) {
    Tcl_Obj* intObj = tclxx::obj_cast::from<int>(12);
    EXPECT_EQ(tclxx::obj_cast::to<int>(interp_, intObj), 12);

    Tcl_Obj* doubleObj = tclxx::obj_cast::from<double>(2.75);
    EXPECT_NEAR(tclxx::obj_cast::to<double>(interp_, doubleObj), 2.75, 1e-9);

    Tcl_Obj* stringObj = tclxx::obj_cast::from<std::string>("abc");
    EXPECT_EQ(tclxx::obj_cast::to<std::string>(interp_, stringObj), "abc");

    Tcl_Obj* boolObj = tclxx::obj_cast::from<bool>(true);
    EXPECT_TRUE(tclxx::obj_cast::to<bool>(interp_, boolObj));

    Tcl_Obj* rawObj = Tcl_NewStringObj("raw", -1);
    EXPECT_EQ(tclxx::obj_cast::from<Tcl_Obj*>(rawObj), rawObj);
    EXPECT_EQ(tclxx::obj_cast::to<Tcl_Obj*>(interp_, rawObj), rawObj);
}

TEST_F(TclInterpFixture, PointerAndValueConversionsUseSpecializedTypeHooks) {
    Sample* sample = new Sample(99);
    Tcl_Obj* sampleObj = tclxx::obj_cast::from_owned(sample);
    Tcl_IncrRefCount(sampleObj);

    Sample* samplePtr = tclxx::obj_cast::to<Sample*>(interp_, sampleObj);
    EXPECT_EQ(samplePtr, sample);
    EXPECT_EQ(samplePtr->id, 99);

    Sample sampleValue = tclxx::obj_cast::to<Sample>(interp_, Tcl_NewStringObj("123", -1));
    EXPECT_EQ(sampleValue.id, 123);

    Tcl_DecrRefCount(sampleObj);
}

TEST_F(TclInterpFixture, ConstHandlesRejectMutableExtraction) {
    const Sample* constSample = new const Sample(77);
    Tcl_Obj* constWeakObj = tclxx::obj_cast::from_weak(constSample);
    Tcl_IncrRefCount(constWeakObj);
    const Sample* constExtracted = tclxx::obj_cast::to<const Sample*>(interp_, constWeakObj);
    EXPECT_EQ(constExtracted, constSample);
    EXPECT_EQ(constExtracted->id, 77);

    try {
        (void)tclxx::obj_cast::to<Sample*>(interp_, constWeakObj);
        FAIL() << "Expected mutable extraction from const handle to throw";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("const object handle"), std::string::npos);
    }

    Tcl_DecrRefCount(constWeakObj);
    delete constSample;
}

TEST_F(TclInterpFixture, InvalidScalarConversionsIncludeTargetTypeInError) {
    try {
        (void)tclxx::obj_cast::to<int>(interp_, Tcl_NewStringObj("not-an-int", -1));
        FAIL() << "Expected invalid int conversion to throw";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("int"), std::string::npos);
    }

    try {
        (void)tclxx::obj_cast::to<bool>(interp_, Tcl_NewStringObj("not-a-bool", -1));
        FAIL() << "Expected invalid bool conversion to throw";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("bool"), std::string::npos);
    }
}

TEST_F(TclInterpFixture, SharedPtrOwnedRoundTripSupportsPointerExtraction) {
    auto shared = std::make_shared<Sample>(314);
    Tcl_Obj* sharedObj = tclxx::obj_cast::from_shared(shared);
    Tcl_IncrRefCount(sharedObj);

    Sample* raw = tclxx::obj_cast::to<Sample*>(interp_, sharedObj);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw, shared.get());
    EXPECT_EQ(raw->id, 314);

    const Sample* constRaw = tclxx::obj_cast::to<const Sample*>(interp_, sharedObj);
    EXPECT_EQ(constRaw, shared.get());

    Tcl_Obj* dup = Tcl_DuplicateObj(sharedObj);
    Tcl_IncrRefCount(dup);
    Sample* dupRaw = tclxx::obj_cast::to<Sample*>(interp_, dup);
    ASSERT_NE(dupRaw, nullptr);
    EXPECT_NE(dupRaw, shared.get());

    raw->id = 500;
    EXPECT_EQ(dupRaw->id, 314);

    Tcl_DecrRefCount(dup);
    Tcl_DecrRefCount(sharedObj);
}

TEST_F(TclInterpFixture, SharedPtrSharedRoundTripSupportsSharedExtraction) {
    auto shared = std::make_shared<Sample>(2718);
    Tcl_Obj* sharedObj = tclxx::obj_cast::from_shared<tclxx::detail::ownership::shared>(shared);
    Tcl_IncrRefCount(sharedObj);

    Sample* raw = tclxx::obj_cast::to<Sample*>(interp_, sharedObj);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(raw, shared.get());
    EXPECT_EQ(raw->id, 2718);

    Tcl_Obj* dup = Tcl_DuplicateObj(sharedObj);
    Tcl_IncrRefCount(dup);
    Sample* dupRaw = tclxx::obj_cast::to<Sample*>(interp_, dup);
    ASSERT_NE(dupRaw, nullptr);
    EXPECT_EQ(dupRaw, shared.get());

    raw->id = 777;
    EXPECT_EQ(dupRaw->id, 777);

    Tcl_DecrRefCount(dup);
    Tcl_DecrRefCount(sharedObj);
}

TEST_F(TclInterpFixture, SharedPtrConstPointeeRejectsMutableExtraction) {
    auto sharedConst = std::make_shared<const Sample>(777);
    Tcl_Obj* obj = tclxx::obj_cast::from_shared(sharedConst);
    Tcl_IncrRefCount(obj);

    const Sample* constRaw = tclxx::obj_cast::to<const Sample*>(interp_, obj);
    ASSERT_NE(constRaw, nullptr);
    EXPECT_EQ(constRaw, sharedConst.get());
    EXPECT_EQ(constRaw->id, 777);

    try {
        (void)tclxx::obj_cast::to<Sample*>(interp_, obj);
        FAIL() << "Expected mutable extraction from const shared handle to throw";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find("const shared object handle"), std::string::npos);
    }

    Tcl_DecrRefCount(obj);
}

TEST_F(TclInterpFixture, SharedPtrNullPointeeConvertsToNullRawPointer) {
    std::shared_ptr<Sample> empty;
    Tcl_Obj* obj = tclxx::obj_cast::from_shared(empty);
    Tcl_IncrRefCount(obj);

    EXPECT_EQ(tclxx::obj_cast::to<Sample*>(interp_, obj), nullptr);
    EXPECT_EQ(tclxx::obj_cast::to<const Sample*>(interp_, obj), nullptr);

    Tcl_DecrRefCount(obj);
}

} // namespace
