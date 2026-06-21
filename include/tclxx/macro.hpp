#pragma once

#include "tclxx/cmd.hpp"

#define TCLXX_CMD_NEW(interp, cmdName, classType, ...) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::create<classType, __VA_ARGS__>, nullptr, nullptr)

#define TCLXX_CMD_NEW0(interp, cmdName, classType) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::create<classType>, nullptr, nullptr)

#define TCLXX_CMD_NEW_SHARED(interp, cmdName, classType, ...) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::create_shared<classType, __VA_ARGS__>, nullptr, nullptr)

#define TCLXX_CMD_NEWARGS(interp, cmdName, classType) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::create_args<classType>, nullptr, nullptr)

#define TCLXX_CMD_NEWARGS_SHARED(interp, cmdName, classType) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::create_args_shared<classType>, nullptr, nullptr)

#define TCLXX_CMD_NEW0_SHARED(interp, cmdName, classType) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::create_shared<classType>, nullptr, nullptr)

#define TCLXX_CMD_GETTER(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_weak<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_WEAK(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_weak<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_OWNED(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_owned<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_SHARED(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_shared<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_METHOD(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_member_weak<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_METHOD_WEAK(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_member_weak<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_METHOD_OWNED(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_member_owned<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_GETTER_METHOD_SHARED(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::getter_member_shared<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_weak<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_WEAK(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_weak<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_OWNED(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_owned<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_SHARED(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_shared<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_METHOD(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_member_weak<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_METHOD_WEAK(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_member_weak<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_METHOD_OWNED(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_member_owned<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_SETTER_METHOD_SHARED(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::setter_member_shared<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_UPDATER_METHOD(interp, cmdName, methodPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::updater_member<methodPtr>, nullptr, nullptr)

#define TCLXX_CMD_STATIC(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::static_method_weak<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_STATIC_WEAK(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::static_method_weak<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_STATIC_OWNED(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::static_method_owned<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_STATIC_SHARED(interp, cmdName, funcPtr) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::static_method_shared<funcPtr>, nullptr, nullptr)

#define TCLXX_CMD_NEW_MAKE_SHARED(interp, cmdName, classType) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::make_shared<classType>, nullptr, nullptr)

#define TCLXX_CMD_NEW_FROM_SHARED(interp, cmdName, classType) Tcl_CreateObjCommand((interp), (cmdName), ::tclxx::cmd::from_shared<classType>, nullptr, nullptr)
