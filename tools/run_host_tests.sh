#!/usr/bin/env bash
# Run all host-side unit tests in tests_host/.
#
# Each tests_host/test_<name>.cpp is compiled standalone against
# src/<name>.cpp using a host C++ compiler (no Arduino, no FreeRTOS).
# Used to verify the portable helper modules (track_runtime,
# storage_naming) without flashing firmware.
#
# Exit code: 0 if all pass, non-zero = number of failures.

set -u

cd "$(dirname "$0")/.."

pass=0
fail=0
failed_tests=()

for test_src in tests_host/test_*.cpp; do
    [ -f "$test_src" ] || continue
    name=$(basename "$test_src" .cpp)
    target_src="src/${name#test_}.cpp"

    if [ ! -f "$target_src" ]; then
        echo "==> $name: SKIP (target $target_src not found)"
        continue
    fi

    # Optional dependency manifest.  Some modules rightly depend on
    # a second pure module (e.g. gps_filter uses gps_kalman).  A
    # sidecar file tests_host/test_X.deps with one src-relative path
    # per line (comments with # allowed) tells the runner what else
    # to link.  Keeping it simple: no globbing, no recursion.
    extra_sources=""
    deps_file="tests_host/${name}.deps"
    if [ -f "$deps_file" ]; then
        while IFS= read -r dep; do
            case "$dep" in
                ''|\#*) continue ;;
            esac
            if [ ! -f "$dep" ]; then
                echo "==> $name: SKIP (dep $dep listed in $deps_file not found)"
                continue 2
            fi
            extra_sources="$extra_sources $dep"
        done < "$deps_file"
    fi

    bin="/tmp/host_test_${name}_$$"

    echo "==> $name"
    if c++ -std=c++17 -Wall -Wextra -I src -o "$bin" "$test_src" "$target_src" $extra_sources 2>&1; then
        if "$bin"; then
            echo "    PASS"
            pass=$((pass + 1))
        else
            echo "    FAIL (runtime exit=$?)"
            fail=$((fail + 1))
            failed_tests+=("$name")
        fi
        rm -f "$bin"
    else
        echo "    FAIL (compile)"
        fail=$((fail + 1))
        failed_tests+=("$name")
    fi
done

echo ""
echo "Result: $pass passed, $fail failed"

if [ "$fail" -gt 0 ]; then
    echo "Failed: ${failed_tests[*]}"
    exit "$fail"
fi
