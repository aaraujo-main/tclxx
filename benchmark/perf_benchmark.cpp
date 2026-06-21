#include <tcl.h>

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "tclxx.hpp"

namespace {

struct BenchPoint {
    double x = 0.0;
    BenchPoint() = default;
    explicit BenchPoint(double v) : x(v) {}
    double& getX() { return x; }
};

volatile int g_sink_int = 0;
volatile double g_sink_double = 0.0;
volatile std::uintptr_t g_sink_ptr = 0;

struct BenchResult {
    std::string name;
    std::size_t iterations = 0;
    double ns_per_op = 0.0;
    double ops_per_sec = 0.0;
};

template <typename F>
BenchResult run_benchmark(const std::string& name,
                          std::size_t iterations,
                          std::size_t warmup,
                          F&& fn) {
    for (std::size_t i = 0; i < warmup; ++i) {
        fn();
    }

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        fn();
    }
    const auto end = std::chrono::steady_clock::now();

    const auto elapsed_ns =
        static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());

    BenchResult r;
    r.name = name;
    r.iterations = iterations;
    r.ns_per_op = elapsed_ns / static_cast<double>(iterations);
    r.ops_per_sec = 1e9 / r.ns_per_op;
    return r;
}

double bench_get_x(BenchPoint* p) {
    return p->x;
}

void bench_set_x(BenchPoint* p, double v) {
    p->x = v;
}

// Profile from_owned: creates new owned pointer
Tcl_Obj* create_owned_point(BenchPoint* p) {
    return tclxx::obj_cast::from_owned(p);
}

// Profile from_shared: wraps shared_ptr
Tcl_Obj* create_shared_point(const std::shared_ptr<BenchPoint>& p) {
    return tclxx::obj_cast::from_shared(p);
}

} // namespace

namespace tclxx {

template <>
std::string ObjType<BenchPoint>::ToString(const BenchPoint& p) {
    return std::to_string(p.x);
}

template <>
BenchPoint ObjType<BenchPoint>::FromAny(Tcl_Interp* interp, Tcl_Obj* const obj) {
    BenchPoint p;
    p.x = obj_cast::to<double>(interp, obj);
    return p;
}

} // namespace tclxx

int main() {
    Tcl_Interp* interp = Tcl_CreateInterp();
    if (!interp) {
        std::cerr << "Failed to create Tcl interpreter\n";
        return 1;
    }
    if (Tcl_Init(interp) != TCL_OK) {
        std::cerr << "Failed to initialize Tcl: " << Tcl_GetStringResult(interp) << "\n";
        Tcl_DeleteInterp(interp);
        return 1;
    }

    // Register cmd::create and cmd::create_shared for BenchPoint with double arg
    Tcl_CreateObjCommand(interp, "BenchCreate", tclxx::cmd::create<BenchPoint, double>, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "BenchCreateShared", tclxx::cmd::create_shared<BenchPoint, double>, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "BenchUpdate", tclxx::cmd::updater_member<&BenchPoint::getX>, nullptr, nullptr);

    BenchPoint point{3.5};
    const BenchPoint const_point{4.5};

    Tcl_Obj* int_obj = Tcl_NewIntObj(123);
    Tcl_IncrRefCount(int_obj);

    Tcl_Obj* weak_obj = tclxx::obj_cast::from_weak(&point);
    Tcl_IncrRefCount(weak_obj);

    Tcl_Obj* const_weak_obj = tclxx::obj_cast::from_weak(&const_point);
    Tcl_IncrRefCount(const_weak_obj);

    // Create owned and shared variants for profiling
    Tcl_Obj* owned_obj = tclxx::obj_cast::from_owned(new BenchPoint{5.5});
    Tcl_IncrRefCount(owned_obj);

    auto shared_point = std::make_shared<BenchPoint>(BenchPoint{6.5});
    Tcl_Obj* shared_obj = tclxx::obj_cast::from_shared(shared_point);
    Tcl_IncrRefCount(shared_obj);

    Tcl_Obj* cmd_get = Tcl_NewStringObj("::Bench::get.x", -1);
    Tcl_IncrRefCount(cmd_get);
    Tcl_Obj* get_objv[] = {cmd_get, weak_obj};

    Tcl_Obj* cmd_set = Tcl_NewStringObj("::Bench::set.x", -1);
    Tcl_IncrRefCount(cmd_set);
    Tcl_Obj* var_name = Tcl_NewStringObj("bench_point_var", -1);
    Tcl_IncrRefCount(var_name);
    Tcl_Obj* value_obj = Tcl_NewDoubleObj(9.25);
    Tcl_IncrRefCount(value_obj);

    Tcl_Obj* initial_var_obj = tclxx::obj_cast::from_weak(&point);
    if (!Tcl_ObjSetVar2(interp, var_name, nullptr, initial_var_obj, TCL_LEAVE_ERR_MSG)) {
        std::cerr << "Failed to initialize benchmark Tcl variable: " << Tcl_GetStringResult(interp) << "\n";
        Tcl_DecrRefCount(value_obj);
        Tcl_DecrRefCount(var_name);
        Tcl_DecrRefCount(cmd_set);
        Tcl_DecrRefCount(cmd_get);
        Tcl_DecrRefCount(const_weak_obj);
        Tcl_DecrRefCount(weak_obj);
        Tcl_DecrRefCount(int_obj);
        Tcl_DeleteInterp(interp);
        return 1;
    }

    Tcl_Obj* set_objv[] = {cmd_set, var_name, value_obj};

    Tcl_Obj* const_var_name = Tcl_NewStringObj("bench_const_point_var", -1);
    Tcl_IncrRefCount(const_var_name);
    Tcl_Obj* const_initial_var_obj = tclxx::obj_cast::from_weak(&const_point);
    if (!Tcl_ObjSetVar2(interp, const_var_name, nullptr, const_initial_var_obj, TCL_LEAVE_ERR_MSG)) {
        std::cerr << "Failed to initialize const benchmark Tcl variable: " << Tcl_GetStringResult(interp) << "\n";
        Tcl_DecrRefCount(const_var_name);
        Tcl_DecrRefCount(value_obj);
        Tcl_DecrRefCount(var_name);
        Tcl_DecrRefCount(cmd_set);
        Tcl_DecrRefCount(cmd_get);
        Tcl_DecrRefCount(const_weak_obj);
        Tcl_DecrRefCount(weak_obj);
        Tcl_DecrRefCount(int_obj);
        Tcl_DeleteInterp(interp);
        return 1;
    }

    Tcl_Obj* set_const_objv[] = {cmd_set, const_var_name, value_obj};

    // Setup getter/setter with owned variant
    Tcl_Obj* cmd_get_owned = Tcl_NewStringObj("::Bench::get.x", -1);
    Tcl_IncrRefCount(cmd_get_owned);
    Tcl_Obj* get_owned_objv[] = {cmd_get_owned, owned_obj};

    Tcl_Obj* cmd_set_owned = Tcl_NewStringObj("::Bench::set.x", -1);
    Tcl_IncrRefCount(cmd_set_owned);
    Tcl_Obj* var_owned_name = Tcl_NewStringObj("bench_owned_var", -1);
    Tcl_IncrRefCount(var_owned_name);
    Tcl_Obj* initial_owned_var_obj = tclxx::obj_cast::from_owned(new BenchPoint{7.5});
    if (!Tcl_ObjSetVar2(interp, var_owned_name, nullptr, initial_owned_var_obj, TCL_LEAVE_ERR_MSG)) {
        std::cerr << "Failed to init owned var: " << Tcl_GetStringResult(interp) << "\n";
        return 1;
    }
    Tcl_Obj* set_owned_objv[] = {cmd_set_owned, var_owned_name, value_obj};

    // Setup getter/setter with shared variant
    Tcl_Obj* cmd_get_shared = Tcl_NewStringObj("::Bench::get.x", -1);
    Tcl_IncrRefCount(cmd_get_shared);
    Tcl_Obj* get_shared_objv[] = {cmd_get_shared, shared_obj};

    Tcl_Obj* cmd_set_shared = Tcl_NewStringObj("::Bench::set.x", -1);
    Tcl_IncrRefCount(cmd_set_shared);
    Tcl_Obj* var_shared_name = Tcl_NewStringObj("bench_shared_var", -1);
    Tcl_IncrRefCount(var_shared_name);
    Tcl_Obj* initial_shared_var_obj = tclxx::obj_cast::from_shared(std::make_shared<BenchPoint>(BenchPoint{8.5}));
    if (!Tcl_ObjSetVar2(interp, var_shared_name, nullptr, initial_shared_var_obj, TCL_LEAVE_ERR_MSG)) {
        std::cerr << "Failed to init shared var: " << Tcl_GetStringResult(interp) << "\n";
        return 1;
    }
    Tcl_Obj* set_shared_objv[] = {cmd_set_shared, var_shared_name, value_obj};

    // Setup cmd::create benchmark
    Tcl_Obj* cmd_create = Tcl_NewStringObj("BenchCreate", -1);
    Tcl_IncrRefCount(cmd_create);
    Tcl_Obj* create_arg = Tcl_NewDoubleObj(10.5);
    Tcl_IncrRefCount(create_arg);
    Tcl_Obj* create_objv[] = {cmd_create, create_arg};

    // Setup cmd::create_shared benchmark
    Tcl_Obj* cmd_create_shared = Tcl_NewStringObj("BenchCreateShared", -1);
    Tcl_IncrRefCount(cmd_create_shared);
    Tcl_Obj* create_shared_arg = Tcl_NewDoubleObj(11.5);
    Tcl_IncrRefCount(create_shared_arg);
    Tcl_Obj* create_shared_objv[] = {cmd_create_shared, create_shared_arg};

    // Setup cmd::updater_member benchmark
    Tcl_Obj* update_obj_var = Tcl_NewStringObj("bench_update_var", -1);
    Tcl_IncrRefCount(update_obj_var);
    Tcl_Obj* initial_update_obj = tclxx::obj_cast::from_weak(&point);
    if (!Tcl_ObjSetVar2(interp, update_obj_var, nullptr, initial_update_obj, TCL_LEAVE_ERR_MSG)) {
        std::cerr << "Failed to init updater var: " << Tcl_GetStringResult(interp) << "\n";
        return 1;
    }
    Tcl_Obj* update_field_var = Tcl_NewStringObj("bench_field_var", -1);
    Tcl_IncrRefCount(update_field_var);
    Tcl_Obj* update_body = Tcl_NewStringObj("", 0);
    Tcl_IncrRefCount(update_body);
    Tcl_Obj* cmd_update = Tcl_NewStringObj("BenchUpdate", -1);
    Tcl_IncrRefCount(cmd_update);
    Tcl_Obj* update_objv[] = {cmd_update, update_obj_var, update_field_var, update_body};

    std::vector<BenchResult> results;
    results.reserve(8);

    results.push_back(run_benchmark("obj_cast::from<int> alloc/free", 400000, 10000, [&]() {
        Tcl_Obj* o = tclxx::obj_cast::from<int>(123);
        Tcl_IncrRefCount(o);
        Tcl_DecrRefCount(o);
    }));

    results.push_back(run_benchmark("obj_cast::from_weak alloc/free", 200000, 5000, [&]() {
        Tcl_Obj* o = tclxx::obj_cast::from_weak(&const_point);
        Tcl_IncrRefCount(o);
        Tcl_DecrRefCount(o);
    }));

    results.push_back(run_benchmark("obj_cast::from_owned alloc/free", 200000, 5000, [&]() {
        Tcl_Obj* o = tclxx::obj_cast::from_owned(new BenchPoint{7.5});
        Tcl_IncrRefCount(o);
        Tcl_DecrRefCount(o);
    }));

    results.push_back(run_benchmark("obj_cast::from_shared alloc/free", 200000, 5000, [&]() {
        auto sp = std::make_shared<BenchPoint>(BenchPoint{8.5});
        Tcl_Obj* o = tclxx::obj_cast::from_shared(sp);
        Tcl_IncrRefCount(o);
        Tcl_DecrRefCount(o);
    }));

    results.push_back(run_benchmark("obj_cast::to<int>", 2500000, 10000, [&]() {
        g_sink_int += tclxx::obj_cast::to<int>(interp, int_obj);
    }));

    results.push_back(run_benchmark("obj_cast::to<BenchPoint*> weak", 2500000, 10000, [&]() {
        BenchPoint* p = tclxx::obj_cast::to<BenchPoint*>(interp, weak_obj);
        g_sink_ptr ^= reinterpret_cast<std::uintptr_t>(p);
    }));

    results.push_back(run_benchmark("obj_cast::to<BenchPoint*> owned", 2500000, 10000, [&]() {
        BenchPoint* p = tclxx::obj_cast::to<BenchPoint*>(interp, owned_obj);
        g_sink_ptr ^= reinterpret_cast<std::uintptr_t>(p);
    }));

    results.push_back(run_benchmark("obj_cast::to<BenchPoint*> shared", 2500000, 10000, [&]() {
        BenchPoint* p = tclxx::obj_cast::to<BenchPoint*>(interp, shared_obj);
        g_sink_ptr ^= reinterpret_cast<std::uintptr_t>(p);
    }));

    results.push_back(run_benchmark("obj_cast::to<const BenchPoint*> weak", 2500000, 10000, [&]() {
        const BenchPoint* p = tclxx::obj_cast::to<const BenchPoint*>(interp, const_weak_obj);
        g_sink_ptr ^= reinterpret_cast<std::uintptr_t>(p);
    }));

    results.push_back(run_benchmark("cmd::getter direct-call", 600000, 5000, [&]() {
        int rc = tclxx::cmd::getter<&bench_get_x>(nullptr, interp, 2, get_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
        g_sink_double += tclxx::obj_cast::to<double>(interp, Tcl_GetObjResult(interp));
    }));

    results.push_back(run_benchmark("cmd::setter direct-call", 600000, 5000, [&]() {
        int rc = tclxx::cmd::setter<&bench_set_x>(nullptr, interp, 3, set_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
        g_sink_double += point.x;
    }));

    results.push_back(run_benchmark("cmd::setter const-handle rejection", 300000, 2000, [&]() {
        int rc = tclxx::cmd::setter<&bench_set_x>(nullptr, interp, 3, set_const_objv);
        if (rc != TCL_ERROR) {
            throw std::runtime_error("Expected const-handle setter rejection");
        }
    }));

    results.push_back(run_benchmark("cmd::getter from_owned", 600000, 5000, [&]() {
        int rc = tclxx::cmd::getter<&bench_get_x>(nullptr, interp, 2, get_owned_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
        g_sink_double += tclxx::obj_cast::to<double>(interp, Tcl_GetObjResult(interp));
    }));

    results.push_back(run_benchmark("cmd::setter from_owned", 600000, 5000, [&]() {
        int rc = tclxx::cmd::setter<&bench_set_x>(nullptr, interp, 3, set_owned_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
    }));

    results.push_back(run_benchmark("cmd::getter from_shared", 600000, 5000, [&]() {
        int rc = tclxx::cmd::getter<&bench_get_x>(nullptr, interp, 2, get_shared_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
        g_sink_double += tclxx::obj_cast::to<double>(interp, Tcl_GetObjResult(interp));
    }));

    results.push_back(run_benchmark("cmd::setter from_shared", 600000, 5000, [&]() {
        int rc = tclxx::cmd::setter<&bench_set_x>(nullptr, interp, 3, set_shared_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
    }));

    results.push_back(run_benchmark("cmd::create alloc/free", 150000, 3000, [&]() {
        int rc = tclxx::cmd::create<BenchPoint, double>(nullptr, interp, 2, create_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
        Tcl_Obj* result = Tcl_GetObjResult(interp);
        g_sink_ptr ^= reinterpret_cast<std::uintptr_t>(result);
    }));

    results.push_back(run_benchmark("cmd::create_shared alloc/free", 150000, 3000, [&]() {
        int rc = tclxx::cmd::create_shared<BenchPoint, double>(nullptr, interp, 2, create_shared_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
        Tcl_Obj* result = Tcl_GetObjResult(interp);
        g_sink_ptr ^= reinterpret_cast<std::uintptr_t>(result);
    }));

    results.push_back(run_benchmark("cmd::updater_member field bind", 600000, 5000, [&]() {
        int rc = tclxx::cmd::updater_member<&BenchPoint::getX>(nullptr, interp, 4, update_objv);
        if (rc != TCL_OK) {
            throw std::runtime_error(Tcl_GetStringResult(interp));
        }
    }));

    std::cout << "tclxx microbenchmarks (lower ns/op is better)\n";
    std::cout << std::left << std::setw(40) << "Benchmark"
              << std::right << std::setw(14) << "Iterations"
              << std::setw(14) << "ns/op"
              << std::setw(16) << "ops/sec" << "\n";
    std::cout << std::string(84, '-') << "\n";

    for (const auto& r : results) {
        std::cout << std::left << std::setw(40) << r.name
                  << std::right << std::setw(14) << r.iterations
                  << std::setw(14) << std::fixed << std::setprecision(2) << r.ns_per_op
                  << std::setw(16) << std::fixed << std::setprecision(0) << r.ops_per_sec
                  << "\n";
    }

    std::cout << "\n"
              << "sinks: int=" << g_sink_int
              << " double=" << g_sink_double
              << " ptr=" << g_sink_ptr << "\n";

    Tcl_DecrRefCount(const_var_name);
    Tcl_DecrRefCount(var_shared_name);
    Tcl_DecrRefCount(var_owned_name);
    Tcl_DecrRefCount(value_obj);
    Tcl_DecrRefCount(var_name);
    Tcl_DecrRefCount(cmd_set);
    Tcl_DecrRefCount(cmd_get);
    Tcl_DecrRefCount(cmd_set_owned);
    Tcl_DecrRefCount(cmd_get_owned);
    Tcl_DecrRefCount(cmd_set_shared);
    Tcl_DecrRefCount(cmd_get_shared);
    Tcl_DecrRefCount(cmd_create);
    Tcl_DecrRefCount(create_arg);
    Tcl_DecrRefCount(cmd_create_shared);
    Tcl_DecrRefCount(create_shared_arg);
    Tcl_DecrRefCount(cmd_update);
    Tcl_DecrRefCount(update_body);
    Tcl_DecrRefCount(update_field_var);
    Tcl_DecrRefCount(update_obj_var);
    Tcl_DecrRefCount(const_weak_obj);
    Tcl_DecrRefCount(weak_obj);
    Tcl_DecrRefCount(owned_obj);
    Tcl_DecrRefCount(shared_obj);
    Tcl_DecrRefCount(int_obj);

    Tcl_DeleteInterp(interp);
    return 0;
}
