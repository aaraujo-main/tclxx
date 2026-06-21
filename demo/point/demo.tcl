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

set p [::CppVooPoint::new 3.0 4.0 "p0" 7 1]
assert_double_eq [::CppVooPoint::get.x $p] 3.0 "getter x"
assert_double_eq [::CppVooPoint::distance $p] 5.0 "getter distance"
assert_eq [::CppVooPoint::get.name $p] "p0" "getter name"

::CppVooPoint::set.x p 6.5
::CppVooPoint::set.name p "alpha"
assert_double_eq [::CppVooPoint::get.x $p] 6.5 "setter x"
assert_eq [::CppVooPoint::get.name $p] "alpha" "setter name"

set tmpX 10.0
set escapedX "sentinel"
::CppVooPoint::update.x p tmpX {
    set escapedX $tmpX
    set tmpX [expr {$tmpX + 2.5}]
}
assert_eq $tmpX "" "updater x temp var cleared"
assert_eq $escapedX "" "updater x escaped alias scrubbed"
assert_double_eq [::CppVooPoint::get.x $p] 9.0 "updater x object"

set tmpName "base"
set escapedName "sentinel"
::CppVooPoint::update.name p tmpName {
    set escapedName $tmpName
    append tmpName "-suffix"
}
assert_eq $tmpName "" "updater name temp var cleared"
assert_eq $escapedName "" "updater name escaped alias scrubbed"
assert_eq [::CppVooPoint::get.name $p] "alpha-suffix" "updater name object"

set p0 [::CppVooPoint::new()]
assert_double_eq [::CppVooPoint::get.x $p0] 0.0 "default ctor"

set origin [::CppVooPoint::origin]
assert_double_eq [::CppVooPoint::distance $origin] 0.0 "static origin"

set ps [::CppVooPoint::origin.shared]
assert_double_eq [::CppVooPoint::get.x $ps] 11.0 "shared_ptr origin"

set pu [::CppVooPoint::origin.unique]
assert_double_eq [::CppVooPoint::get.x $pu] 12.0 "unique_ptr origin"

set pw [::CppVooPoint::origin.weak]
assert_double_eq [::CppVooPoint::get.x $pw] 13.0 "weak_ptr origin"
assert_error_contains {::CppVooPoint::origin.weak.expired} "expired std::weak_ptr" "expired weak_ptr clean error"

set prm [::CppVooPoint::origin.ref.mutable]
::CppVooPoint::set.x prm 31.5
assert_double_eq [::CppVooPoint::get.x $prm] 31.5 "mutable reference handle is writable"

set prc [::CppVooPoint::origin.ref.const]
assert_double_eq [::CppVooPoint::get.x $prc] 22.0 "const reference handle read"
assert_error_contains {::CppVooPoint::set.x prc 99.0} "const object handle" "const reference handle rejects setter"
assert_double_eq [::CppVooPoint::get.x $prc] 22.0 "const reference value unchanged"

set pcw [::CppVooPoint::origin.ptr.const.weak]
assert_double_eq [::CppVooPoint::get.x $pcw] 23.0 "const weak pointer handle read"
assert_error_contains {::CppVooPoint::set.x pcw 99.0} "const object handle" "const weak pointer handle rejects setter"
assert_double_eq [::CppVooPoint::get.x $pcw] 23.0 "const weak pointer value unchanged"

set pco [::CppVooPoint::origin.ptr.const.owned]
assert_double_eq [::CppVooPoint::get.x $pco] 24.0 "const owned pointer handle read"
assert_error_contains {::CppVooPoint::set.x pco 99.0} "const object handle" "const owned pointer handle rejects setter"
assert_double_eq [::CppVooPoint::get.x $pco] 24.0 "const owned pointer value unchanged"

set pnw [::CppVooPoint::origin.null.weak]
assert_eq $pnw "" "null weak pointer handle string form"
assert_error_contains {::CppVooPoint::get.x $pnw} "null object handle" "null weak pointer handle rejects getter"
assert_error_contains {::CppVooPoint::set.x pnw 5.0} "null object handle" "null weak pointer handle rejects setter"

set pno [::CppVooPoint::origin.null.owned]
assert_eq $pno "" "null owned pointer handle string form"
assert_error_contains {::CppVooPoint::get.x $pno} "null object handle" "null owned pointer handle rejects getter"
assert_error_contains {::CppVooPoint::set.x pno 5.0} "null object handle" "null owned pointer handle rejects setter"

assert_double_eq [::CppVooPoint::hypot 3.0 4.0] 5.0 "static hypot"
assert_eq [::CppVooPoint::class.name] "CppVooPoint" "static class.name"
assert_eq [::CppVooPoint::noop] "" "static void result reset"

puts "all demo/point tests passed"
