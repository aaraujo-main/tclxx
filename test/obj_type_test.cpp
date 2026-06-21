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

struct Tracked {
    static int destroyed;
    int value;
    explicit Tracked(int v) : value(v) {}
    Tracked(const Tracked& other) : value(other.value) {}
    ~Tracked() { ++destroyed; }
};

int Tracked::destroyed = 0;

// Simple type that tracks cleanup calls
struct TclObjWrapper {
    static int cleanupCount;
    Tcl_Obj* obj;
    explicit TclObjWrapper(Tcl_Obj* o = nullptr) : obj(o) {}
};

int TclObjWrapper::cleanupCount = 0;

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

// Specialize Cleanup for TclObjWrapper to track when it's called
template <>
void ObjType<::TclObjWrapper>::Cleanup(::TclObjWrapper* value) noexcept {
    try {
        if (value && value->obj) {
            ++::TclObjWrapper::cleanupCount;
            Tcl_DecrRefCount(value->obj);
        }
    } catch (...) {
    }
}

template <>
std::string ObjType<Tracked>::ToString(const Tracked& t) {
    return std::to_string(t.value);
}

template <>
Tracked ObjType<Tracked>::FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
    int v = 0;
    if (Tcl_GetIntFromObj(interp, obj, &v) != TCL_OK) {
        throw std::runtime_error("Tracked requires integer input");
    }
    return Tracked(v);
}

} // namespace tclxx

namespace {

TEST_F(TclInterpFixture, PrimitiveTypeConversionsSetAndReadInternalRep) {
    Tcl_Obj* intObj = Tcl_NewStringObj("42", -1);
    EXPECT_EQ(tclxx::ObjType<int>::SetFromAny(interp_, intObj), TCL_OK);
    EXPECT_EQ(*tclxx::ObjType<int>::GetInternalRep(interp_, intObj), 42);

    Tcl_Obj* dblObj = Tcl_NewStringObj("3.5", -1);
    EXPECT_EQ(tclxx::ObjType<double>::SetFromAny(interp_, dblObj), TCL_OK);
    EXPECT_NEAR(*tclxx::ObjType<double>::GetInternalRep(interp_, dblObj), 3.5, 1e-9);

    Tcl_Obj* boolObj = Tcl_NewStringObj("yes", -1);
    EXPECT_EQ(tclxx::ObjType<bool>::SetFromAny(interp_, boolObj), TCL_OK);
    EXPECT_TRUE(*tclxx::ObjType<bool>::GetInternalRep(interp_, boolObj));

    Tcl_Obj* stringObj = Tcl_NewStringObj("hello", -1);
    EXPECT_EQ(tclxx::ObjType<std::string>::SetFromAny(interp_, stringObj), TCL_OK);
    EXPECT_EQ(*tclxx::ObjType<std::string>::GetInternalRep(interp_, stringObj), "hello");
}

TEST_F(TclInterpFixture, OwningTypeReleasesTrackedObjectOnRefcountDrop) {
    Tracked::destroyed = 0;
    Tcl_Obj* owningObj = tclxx::ObjType<Tracked>::New(new Tracked(7));
    Tcl_IncrRefCount(owningObj);
    Tcl_DecrRefCount(owningObj);
    EXPECT_EQ(Tracked::destroyed, 1);
}

TEST_F(TclInterpFixture, WeakTypeDuplicatePromotesToOwningCopy) {
    Tracked::destroyed = 0;
    Tracked external(11);
    Tcl_Obj* weakObj = Tcl_NewObj();
    Tcl_IncrRefCount(weakObj);
    tclxx::ObjType<Tracked, tclxx::detail::ownership::weak>::Set(weakObj, &external);

    Tcl_Obj* weakDup = Tcl_DuplicateObj(weakObj);
    Tcl_IncrRefCount(weakDup);

    EXPECT_EQ(weakDup->typePtr,
              (tclxx::ObjType<Tracked, tclxx::detail::ownership::owned>::GetType()));

    Tcl_DecrRefCount(weakObj);
    EXPECT_EQ(Tracked::destroyed, 0);

    Tcl_DecrRefCount(weakDup);
    EXPECT_EQ(Tracked::destroyed, 1);
}

TEST_F(TclInterpFixture, ObjCastOwnedAndWeakModesHaveExpectedLifetimeBehavior) {
    Tracked::destroyed = 0;
    Tcl_Obj* ownedFromObjCast = tclxx::obj_cast::from_owned(new Tracked(21));
    Tcl_IncrRefCount(ownedFromObjCast);
    Tcl_DecrRefCount(ownedFromObjCast);
    EXPECT_EQ(Tracked::destroyed, 1);

    Tracked::destroyed = 0;
    Tracked* borrowed = new Tracked(13);
    Tcl_Obj* weakFromObjCast = tclxx::obj_cast::from_weak(borrowed);
    Tcl_IncrRefCount(weakFromObjCast);
    Tcl_DecrRefCount(weakFromObjCast);
    EXPECT_EQ(Tracked::destroyed, 0);
    delete borrowed;
    EXPECT_EQ(Tracked::destroyed, 1);
}

TEST_F(TclInterpFixture, SharedPtrOwnedTypeSetFromAnyAndDupCopyPointeeAllocation) {
    using OwnedTracked = tclxx::ObjType<std::shared_ptr<Tracked>>;
    Tcl_Obj* sharedObj = Tcl_NewStringObj("123", -1);
    Tcl_IncrRefCount(sharedObj);

    EXPECT_EQ(OwnedTracked::SetFromAny(interp_, sharedObj), TCL_OK);
    auto* rep = OwnedTracked::GetInternalRep(interp_, sharedObj);
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(static_cast<bool>(*rep));
    EXPECT_EQ((*rep)->value, 123);

    Tcl_Obj* dup = Tcl_DuplicateObj(sharedObj);
    Tcl_IncrRefCount(dup);
    auto* dupRep = OwnedTracked::GetInternalRep(interp_, dup);
    ASSERT_NE(dupRep, nullptr);
    ASSERT_TRUE(static_cast<bool>(*dupRep));

    // Owned mode duplicates keep owned ObjType and deep-copy pointee.
    EXPECT_EQ(sharedObj->typePtr, OwnedTracked::GetType());
    EXPECT_EQ(dup->typePtr, OwnedTracked::GetType());

    EXPECT_NE(rep->get(), dupRep->get());
    (*dupRep)->value = 456;
    EXPECT_EQ((*rep)->value, 123);
    EXPECT_EQ((*dupRep)->value, 456);

    Tcl_InvalidateStringRep(sharedObj);
    EXPECT_EQ(std::string(Tcl_GetString(sharedObj)), "123");

    Tcl_DecrRefCount(dup);
    Tcl_DecrRefCount(sharedObj);
}

TEST_F(TclInterpFixture, SharedPtrSharedTypeSetFromAnyAndDupShareUnderlyingPointee) {
    Tcl_Obj* sharedObj = Tcl_NewStringObj("123", -1);
    Tcl_IncrRefCount(sharedObj);

    using SharedTracked = tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::shared>;
    EXPECT_EQ(SharedTracked::SetFromAny(interp_, sharedObj), TCL_OK);
    auto* rep = SharedTracked::GetInternalRep(interp_, sharedObj);
    ASSERT_NE(rep, nullptr);
    ASSERT_TRUE(static_cast<bool>(*rep));
    EXPECT_EQ((*rep)->value, 123);

    Tcl_Obj* dup = Tcl_DuplicateObj(sharedObj);
    Tcl_IncrRefCount(dup);
    auto* dupRep = SharedTracked::GetInternalRep(interp_, dup);
    ASSERT_NE(dupRep, nullptr);
    ASSERT_TRUE(static_cast<bool>(*dupRep));

    // Shared mode duplicates keep shared ObjType and share pointee.
    EXPECT_EQ(sharedObj->typePtr, SharedTracked::GetType());
    EXPECT_EQ(dup->typePtr, SharedTracked::GetType());

    EXPECT_EQ(rep->get(), dupRep->get());
    (*dupRep)->value = 456;
    EXPECT_EQ((*rep)->value, 456);

    Tcl_InvalidateStringRep(sharedObj);
    EXPECT_EQ(std::string(Tcl_GetString(sharedObj)), "456");

    Tcl_DecrRefCount(dup);
    Tcl_DecrRefCount(sharedObj);
}

TEST_F(TclInterpFixture, SharedPtrExplicitOwnedTypeDupCopiesContentNotPointee) {
    using ExplicitOwnedTracked =
        tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::owned>;

    Tcl_Obj* original = Tcl_NewStringObj("321", -1);
    Tcl_IncrRefCount(original);

    EXPECT_EQ(ExplicitOwnedTracked::SetFromAny(interp_, original), TCL_OK);
    auto* originalRep = ExplicitOwnedTracked::GetInternalRep(interp_, original);
    ASSERT_NE(originalRep, nullptr);
    ASSERT_TRUE(static_cast<bool>(*originalRep));

    Tcl_Obj* dup = Tcl_DuplicateObj(original);
    Tcl_IncrRefCount(dup);
    auto* dupRep = ExplicitOwnedTracked::GetInternalRep(interp_, dup);
    ASSERT_NE(dupRep, nullptr);
    ASSERT_TRUE(static_cast<bool>(*dupRep));

    EXPECT_EQ(original->typePtr, ExplicitOwnedTracked::GetType());
    EXPECT_EQ(dup->typePtr, ExplicitOwnedTracked::GetType());

    // Owned duplication must deep-copy pointee payload, not alias pointee.
    EXPECT_NE(originalRep->get(), dupRep->get());
    EXPECT_EQ((*originalRep)->value, (*dupRep)->value);

    (*dupRep)->value = 777;
    EXPECT_EQ((*originalRep)->value, 321);
    EXPECT_EQ((*dupRep)->value, 777);

    Tcl_DecrRefCount(dup);
    Tcl_DecrRefCount(original);
}

TEST_F(TclInterpFixture, SharedPtrTypeHandlesNullAndConversionErrors) {
    Tcl_Obj* nullSharedObj =
        tclxx::ObjType<std::shared_ptr<Tracked>>::New(new std::shared_ptr<Tracked>{});
    Tcl_IncrRefCount(nullSharedObj);

    Tcl_InvalidateStringRep(nullSharedObj);
    EXPECT_EQ(std::string(Tcl_GetString(nullSharedObj)), "");

    Tcl_Obj* badSharedObj = Tcl_NewStringObj("not-an-int", -1);
    EXPECT_EQ(tclxx::ObjType<std::shared_ptr<Tracked>>::SetFromAny(interp_, badSharedObj), TCL_ERROR);
    EXPECT_NE(ResultString(interp_).find("Tracked requires integer input"), std::string::npos);

    Tcl_DecrRefCount(nullSharedObj);
}

} // namespace tclxx

namespace {

TEST_F(TclInterpFixture, CleanupIsCalledBeforeDelete) {
    // Create a simple Tcl_Obj to manage
    Tcl_Obj* managedObj = Tcl_NewStringObj("cleanup-test", -1);
    Tcl_IncrRefCount(managedObj);
    Tcl_IncrRefCount(managedObj);
    EXPECT_EQ(managedObj->refCount, 2);

    // Create a wrapper that holds this object
    auto wrapper = new TclObjWrapper(managedObj);
    
    // Reset cleanup counter
    TclObjWrapper::cleanupCount = 0;
    
    // Create a ObjType wrapper and delete it
    Tcl_Obj* typeHandle = tclxx::ObjType<TclObjWrapper>::New(wrapper);
    Tcl_IncrRefCount(typeHandle);
    Tcl_DecrRefCount(typeHandle);
    
    // Verify Cleanup was called (which should have decremented refCount)
    EXPECT_EQ(TclObjWrapper::cleanupCount, 1);
    EXPECT_EQ(managedObj->refCount, 1);
    
    Tcl_DecrRefCount(managedObj);
}

TEST_F(TclInterpFixture, CleanupHandlesNullGracefully) {
    // Create a wrapper with null object
    auto wrapper = new TclObjWrapper(nullptr);
    
    TclObjWrapper::cleanupCount = 0;
    
    // Should not crash
    Tcl_Obj* typeHandle = tclxx::ObjType<TclObjWrapper>::New(wrapper);
    Tcl_IncrRefCount(typeHandle);
    EXPECT_NO_THROW(Tcl_DecrRefCount(typeHandle));
}

TEST_F(TclInterpFixture, StartupAcquireSharedPtrTclObjBalancesCleanupRelease) {
    Tcl_Obj* innerObj = Tcl_NewStringObj("shared-startup-tcl-obj", -1);
    Tcl_IncrRefCount(innerObj);
    const int baseRefCount = innerObj->refCount;

    auto* value = new std::shared_ptr<Tcl_Obj>(innerObj, [](Tcl_Obj*) {});
    Tcl_Obj* typeHandle = Tcl_NewObj();
    Tcl_IncrRefCount(typeHandle);
    tclxx::ObjType<std::shared_ptr<Tcl_Obj>>::Set(typeHandle, value);

    // Startup acquires one Tcl reference.
    EXPECT_EQ(innerObj->refCount, baseRefCount + 1);

    // FreeInternalRep invokes Cleanup, balancing the acquire.
    Tcl_DecrRefCount(typeHandle);
    EXPECT_EQ(innerObj->refCount, baseRefCount);

    Tcl_DecrRefCount(innerObj);
}

TEST_F(TclInterpFixture, StartupAcquireSharedPtrTclObjPointerBalancesCleanupRelease) {
    Tcl_Obj* innerObj = Tcl_NewStringObj("shared-startup-tcl-obj-pointer", -1);
    Tcl_IncrRefCount(innerObj);
    const int baseRefCount = innerObj->refCount;

    auto holder = std::make_shared<Tcl_Obj*>(innerObj);
    auto* value = new std::shared_ptr<Tcl_Obj*>(std::move(holder));
    Tcl_Obj* typeHandle = Tcl_NewObj();
    Tcl_IncrRefCount(typeHandle);
    tclxx::ObjType<std::shared_ptr<Tcl_Obj*>>::Set(typeHandle, value);

    // Startup acquires one Tcl reference.
    EXPECT_EQ(innerObj->refCount, baseRefCount + 1);

    // FreeInternalRep invokes Cleanup, balancing the acquire.
    Tcl_DecrRefCount(typeHandle);
    EXPECT_EQ(innerObj->refCount, baseRefCount);

    Tcl_DecrRefCount(innerObj);
}

TEST_F(TclInterpFixture, SharedPtrTclObjPointerDupDoesNotAcquireExtraTclRef) {
    Tcl_Obj* innerObj = Tcl_NewStringObj("shared-startup-tcl-obj-pointer-dup", -1);
    Tcl_IncrRefCount(innerObj);
    const int baseRefCount = innerObj->refCount;

    auto holder = std::make_shared<Tcl_Obj*>(innerObj);
    using SharedObjPtrType =
        tclxx::ObjType<std::shared_ptr<Tcl_Obj*>, tclxx::detail::ownership::shared>;
    Tcl_Obj* original =
        SharedObjPtrType::New(new std::shared_ptr<Tcl_Obj*>(std::move(holder)));
    Tcl_IncrRefCount(original);
    EXPECT_EQ(innerObj->refCount, baseRefCount + 1);

    Tcl_Obj* dup = Tcl_DuplicateObj(original);
    Tcl_IncrRefCount(dup);
    EXPECT_EQ(innerObj->refCount, baseRefCount + 1);

    Tcl_DecrRefCount(dup);
    EXPECT_EQ(innerObj->refCount, baseRefCount + 1);

    Tcl_DecrRefCount(original);
    EXPECT_EQ(innerObj->refCount, baseRefCount);

    Tcl_DecrRefCount(innerObj);
}


TEST_F(TclInterpFixture, CleanupDoesNothingForNonTclObjTypes) {
    // Cleanup should be a no-op for types that don't manage Tcl_Obj
    Tcl_Obj* trackedObj = tclxx::ObjType<std::shared_ptr<Tracked>>::New(
        new std::shared_ptr<Tracked>(std::make_shared<Tracked>(42)));
    Tcl_IncrRefCount(trackedObj);

    int beforeDestroyed = Tracked::destroyed;
    Tcl_DecrRefCount(trackedObj);
    
    // The Tracked destructor should be called normally during cleanup
    EXPECT_GT(Tracked::destroyed, beforeDestroyed);
}

TEST_F(TclInterpFixture, MakeSharedConvertsOwnedTToSharedPtr) {
    // Create owned Tracked object
    Tracked::destroyed = 0;
    Tcl_Obj* ownedObj = tclxx::ObjType<Tracked>::New(new Tracked(99));
    Tcl_IncrRefCount(ownedObj);

    // Verify initial type is owned
    EXPECT_EQ(ownedObj->typePtr, tclxx::ObjType<Tracked>::GetType());

    // Set it in a variable
    Tcl_Obj* varName = Tcl_NewStringObj("testVar", -1);
    Tcl_IncrRefCount(varName);
    EXPECT_NE(Tcl_ObjSetVar2(interp_, varName, nullptr, ownedObj, 0), nullptr);

    // Call make_shared command
    const Tcl_Obj* objv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result = tclxx::cmd::make_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(objv));
    EXPECT_EQ(result, TCL_OK) << ResultString(interp_);

    // Get modified variable
    Tcl_Obj* modifiedObj = Tcl_ObjGetVar2(interp_, varName, nullptr, 0);
    EXPECT_NE(modifiedObj, nullptr);

    // Verify type changed to shared_ptr<Tracked> with shared ownership
    EXPECT_EQ(modifiedObj->typePtr,
              (tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::shared>::GetType()));

    // Cleanup
    Tcl_DecrRefCount(varName);
    Tcl_DecrRefCount(ownedObj);
}

TEST_F(TclInterpFixture, FromSharedConvertsSharedPtrToOwnedT) {
    // Create shared_ptr<Tracked> object
    Tracked::destroyed = 0;
    auto trackedPtr = std::make_shared<Tracked>(77);
    Tcl_Obj* sharedObj = tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::shared>::New(
        new std::shared_ptr<Tracked>(trackedPtr));
    Tcl_IncrRefCount(sharedObj);

    // Verify initial type is shared_ptr
    EXPECT_EQ(sharedObj->typePtr,
              (tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::shared>::GetType()));

    // Set it in a variable
    Tcl_Obj* varName = Tcl_NewStringObj("testVar2", -1);
    Tcl_IncrRefCount(varName);
    EXPECT_NE(Tcl_ObjSetVar2(interp_, varName, nullptr, sharedObj, 0), nullptr);

    // Call from_shared command
    const Tcl_Obj* objv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result = tclxx::cmd::from_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(objv));
    EXPECT_EQ(result, TCL_OK) << ResultString(interp_);

    // Get modified variable
    Tcl_Obj* modifiedObj = Tcl_ObjGetVar2(interp_, varName, nullptr, 0);
    EXPECT_NE(modifiedObj, nullptr);

    // Verify type changed back to owned Tracked
    EXPECT_EQ(modifiedObj->typePtr, tclxx::ObjType<Tracked>::GetType());

    // Verify value is intact (deep copy happened)
    Tracked* extractedPtr = tclxx::ObjType<Tracked>::GetInternalRep(interp_, modifiedObj);
    EXPECT_NE(extractedPtr, nullptr);
    EXPECT_EQ(extractedPtr->value, 77);

    // Cleanup
    Tcl_DecrRefCount(varName);
    Tcl_DecrRefCount(sharedObj);
}

TEST_F(TclInterpFixture, MakeSharedThenFromSharedRoundTrip) {
    // Create owned Tracked object
    Tracked::destroyed = 0;
    Tcl_Obj* ownedObj = tclxx::ObjType<Tracked>::New(new Tracked(55));
    Tcl_IncrRefCount(ownedObj);

    // Set in variable
    Tcl_Obj* varName = Tcl_NewStringObj("roundTripVar", -1);
    Tcl_IncrRefCount(varName);
    EXPECT_NE(Tcl_ObjSetVar2(interp_, varName, nullptr, ownedObj, 0), nullptr);

    // Convert to shared
    const Tcl_Obj* makeSharedObjv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result1 = tclxx::cmd::make_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(makeSharedObjv));
    EXPECT_EQ(result1, TCL_OK);

    Tcl_Obj* sharedObj = Tcl_ObjGetVar2(interp_, varName, nullptr, 0);
    EXPECT_EQ(sharedObj->typePtr,
              (tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::shared>::GetType()));

    // Convert back to owned
    const Tcl_Obj* fromSharedObjv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result2 = tclxx::cmd::from_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(fromSharedObjv));
    EXPECT_EQ(result2, TCL_OK);

    Tcl_Obj* backToOwnedObj = Tcl_ObjGetVar2(interp_, varName, nullptr, 0);
    EXPECT_EQ(backToOwnedObj->typePtr, tclxx::ObjType<Tracked>::GetType());

    // Verify value preserved
    Tracked* finalPtr = tclxx::ObjType<Tracked>::GetInternalRep(interp_, backToOwnedObj);
    EXPECT_EQ(finalPtr->value, 55);

    // Cleanup
    Tcl_DecrRefCount(varName);
    Tcl_DecrRefCount(ownedObj);
}

TEST_F(TclInterpFixture, FromSharedDeepCopiesValue) {
    // Create shared_ptr<Tracked> with specific value
    Tracked::destroyed = 0;
    auto trackedPtr = std::make_shared<Tracked>(88);
    Tcl_Obj* sharedObj = tclxx::ObjType<std::shared_ptr<Tracked>, tclxx::detail::ownership::shared>::New(
        new std::shared_ptr<Tracked>(trackedPtr));
    Tcl_IncrRefCount(sharedObj);

    // Set in variable
    Tcl_Obj* varName = Tcl_NewStringObj("deepCopyVar", -1);
    Tcl_IncrRefCount(varName);
    EXPECT_NE(Tcl_ObjSetVar2(interp_, varName, nullptr, sharedObj, 0), nullptr);

    // Convert from shared
    const Tcl_Obj* objv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result = tclxx::cmd::from_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(objv));
    EXPECT_EQ(result, TCL_OK);

    Tcl_Obj* ownedObj = Tcl_ObjGetVar2(interp_, varName, nullptr, 0);

    // Verify we have owned copy with correct value
    Tracked* ptr1 = tclxx::ObjType<Tracked>::GetInternalRep(interp_, ownedObj);
    EXPECT_NE(ptr1, nullptr);
    EXPECT_EQ(ptr1->value, 88);

    // Verify type changed from shared_ptr to owned
    EXPECT_EQ(ownedObj->typePtr, tclxx::ObjType<Tracked>::GetType());

    // Cleanup
    Tcl_DecrRefCount(varName);
    Tcl_DecrRefCount(sharedObj);
}

TEST_F(TclInterpFixture, MakeSharedRejectsNonOwnedType) {
    // Create weak Tracked object (non-owned)
    Tracked external(33);
    Tcl_Obj* weakObj = Tcl_NewObj();
    Tcl_IncrRefCount(weakObj);
    using WeakTrackedType = tclxx::ObjType<Tracked, tclxx::detail::ownership::weak>;
    using OwnedTrackedType = tclxx::ObjType<Tracked, tclxx::detail::ownership::owned>;
    WeakTrackedType::Set(weakObj, &external);

    // Assert weak and owned types differ
    ASSERT_NE(WeakTrackedType::GetType(), OwnedTrackedType::GetType());

    // Force internal representation
    EXPECT_EQ(weakObj->typePtr, WeakTrackedType::GetType());

    // Set in variable
    Tcl_Obj* varName = Tcl_NewStringObj("weakVar", -1);
    Tcl_IncrRefCount(varName);
    EXPECT_NE(Tcl_ObjSetVar2(interp_, varName, nullptr, weakObj, 0), nullptr);

    // Try convert to shared
    const Tcl_Obj* objv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result = tclxx::cmd::make_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(objv));
    
    EXPECT_EQ(result, TCL_ERROR);
    EXPECT_NE(ResultString(interp_).find("owned type"), std::string::npos);

    // Cleanup
    Tcl_DecrRefCount(varName);
    Tcl_DecrRefCount(weakObj);
}

TEST_F(TclInterpFixture, FromSharedRejectsNonSharedType) {
    // Create owned Tracked object
    Tcl_Obj* ownedObj = tclxx::ObjType<Tracked>::New(new Tracked(11));
    Tcl_IncrRefCount(ownedObj);

    // Set in variable
    Tcl_Obj* varName = Tcl_NewStringObj("ownedVar", -1);
    Tcl_IncrRefCount(varName);
    EXPECT_NE(Tcl_ObjSetVar2(interp_, varName, nullptr, ownedObj, 0), nullptr);

    // Try convert from shared
    const Tcl_Obj* objv[] = {Tcl_NewStringObj("dummy", -1), varName};
    int result = tclxx::cmd::from_shared<Tracked>(nullptr, interp_, 2, const_cast<Tcl_Obj* const*>(objv));
    EXPECT_EQ(result, TCL_ERROR);
    EXPECT_NE(ResultString(interp_).find("shared_ptr type"), std::string::npos);

    // Cleanup
    Tcl_DecrRefCount(varName);
    Tcl_DecrRefCount(ownedObj);
}

} // namespace
