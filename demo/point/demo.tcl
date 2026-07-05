proc assert_eq {actual expected label} {
    if {$actual ne $expected} {
        error "$label: expected '$expected' but got '$actual'"
    }
    puts "ok: $label"
}

proc assert_double_eq {actual expected label} {
    if {[expr {abs($actual - $expected)}] > 1e-9} {
        error "$label: expected $expected but got $actual"
    }
    puts "ok: $label"
}

proc assert_error_contains {script needle label} {
    if {[catch {uplevel 1 $script} msg] == 0} {
        error "$label: expected error, command succeeded"
    }
    if {[string first $needle $msg] < 0} {
        error "$label: expected error containing '$needle' but got '$msg'"
    }
    puts "ok: $label"
}

set p [::Point::new 3.0 4.0 "p0" 7 1]
assert_double_eq [::Point::get.x $p] 3.0 "getter x"
assert_double_eq [::Point::distance $p] 5.0 "getter distance"
assert_eq [::Point::get.name $p] "p0" "getter name"

::Point::set.x p 6.5
::Point::set.name p "alpha"
assert_double_eq [::Point::get.x $p] 6.5 "setter x"
assert_eq [::Point::get.name $p] "alpha" "setter name"

set tmpX 10.0
set escapedX "sentinel"
::Point::update.x p tmpX {
    set escapedX $tmpX
    set tmpX [expr {$tmpX + 2.5}]
}
assert_eq $tmpX "" "updater x temp var cleared"
assert_double_eq $escapedX 6.5 "updater x escaped alias remains"
assert_double_eq [::Point::get.x $p] 9.0 "updater x object"

set tmpName "base"
set escapedName "sentinel"
::Point::update.name p tmpName {
    set escapedName $tmpName
    append tmpName "-suffix"
}
assert_eq $tmpName "" "updater name temp var cleared"
# this behavior is specific for C++ implementation of update methods since it uses weak_ptr for efficiency
assert_eq $escapedName "alpha-suffix" "updater name escaped alias remains syncrhonized with tmpName reference"
assert_eq [::Point::get.name $p] "alpha-suffix" "updater name object"

set p0 [::Point::new()]
assert_double_eq [::Point::get.x $p0] 0.0 "default ctor"

set origin [::Point::origin]
assert_double_eq [::Point::distance $origin] 0.0 "static origin"

set ps [::Point::origin.shared]
assert_double_eq [::Point::get.x $ps] 11.0 "shared_ptr origin"

set pu [::Point::origin.unique]
assert_double_eq [::Point::get.x $pu] 12.0 "unique_ptr origin"

set pw [::Point::origin.weak]
assert_double_eq [::Point::get.x $pw] 13.0 "weak_ptr origin"
assert_error_contains {::Point::origin.weak.expired} "expired std::weak_ptr" "expired weak_ptr clean error"

set prm [::Point::origin.ref.mutable]
::Point::set.x prm 31.5
assert_double_eq [::Point::get.x $prm] 31.5 "mutable reference handle is writable"

set prc [::Point::origin.ref.const]
assert_double_eq [::Point::get.x $prc] 22.0 "const reference handle read"
assert_error_contains {::Point::set.x prc 99.0} "const object handle" "const reference handle rejects setter"
assert_double_eq [::Point::get.x $prc] 22.0 "const reference value unchanged"

set pcw [::Point::origin.ptr.const.weak]
assert_double_eq [::Point::get.x $pcw] 23.0 "const weak pointer handle read"
assert_error_contains {::Point::set.x pcw 99.0} "const object handle" "const weak pointer handle rejects setter"
assert_double_eq [::Point::get.x $pcw] 23.0 "const weak pointer value unchanged"

set pco [::Point::origin.ptr.const.owned]
assert_double_eq [::Point::get.x $pco] 24.0 "const owned pointer handle read"
assert_error_contains {::Point::set.x pco 99.0} "const object handle" "const owned pointer handle rejects setter"
assert_double_eq [::Point::get.x $pco] 24.0 "const owned pointer value unchanged"

set pnw [::Point::origin.null.weak]
assert_eq $pnw "" "null weak pointer handle string form"
assert_error_contains {::Point::get.x $pnw} "null object handle" "null weak pointer handle rejects getter"
assert_error_contains {::Point::set.x pnw 5.0} "null object handle" "null weak pointer handle rejects setter"

set pno [::Point::origin.null.owned]
assert_eq $pno "" "null owned pointer handle string form"
assert_error_contains {::Point::get.x $pno} "null object handle" "null owned pointer handle rejects getter"
assert_error_contains {::Point::set.x pno 5.0} "null object handle" "null owned pointer handle rejects setter"

assert_double_eq [::Point::hypot 3.0 4.0] 5.0 "static hypot"
assert_eq [::Point::class.name] "Point" "static class.name"
assert_eq [::Point::noop] "" "static void result reset"

set move_callback_result ""
proc move_callback {x y} {
    global move_callback_result
    set move_callback_result "$x,$y"
}

set p [::Point::new 10.0 20.0 "move_test" 0 1]

::Point::move_getter $p move_callback
assert_eq $move_callback_result "10.0,20.0" "move (getter) callback receives args"

set move_callback_result ""
::Point::move_setter p move_callback
assert_eq $move_callback_result "10.0,20.0" "move (setter) callback receives args"

puts "all demo/point tests passed"
