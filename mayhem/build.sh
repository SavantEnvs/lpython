#!/usr/bin/env bash
#
# mayhem/build.sh — build lpython's fuzz harnesses (ASan+UBSan) and a normal-flags lpython for
# the reference test suite (mayhem/test.sh). Runs in /mayhem as `mayhem`; re-runnable and offline.
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
# Default parallelism is bounded by memory as well as cores: sanitized compiles of the large
# semantics/codegen units take ~2.5 GB each, and 16 at once OOM a 12 GB container.
if [ -z "${MAYHEM_JOBS:-}" ]; then
  mem=$(cat /sys/fs/cgroup/memory.max 2>/dev/null || echo max)
  [ "$mem" = max ] && mem=$(( $(awk '/^MemTotal/{print $2}' /proc/meminfo) * 1024 ))
  MAYHEM_JOBS=$(( mem / (2560 * 1024 * 1024) ))
  [ "$MAYHEM_JOBS" -ge 1 ] || MAYHEM_JOBS=1
  [ "$MAYHEM_JOBS" -le "$(nproc)" ] || MAYHEM_JOBS=$(nproc)
fi
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS
cd "$SRC"

# 0) Generated sources (what upstream's build0.sh does). Always regenerate so an edit to the
#    grammar / ASDL is picked up. The version comes from `git describe`; fall back when no tag
#    is reachable (shallow or tagless checkout).
# Keep the old mtime of every generated file whose content did not change, so a re-run is
# incremental instead of a full rebuild (the generators rewrite all their outputs every time).
PREV=mayhem/build-fuzz/.gen-prev
rm -rf "$PREV"; mkdir -p "$PREV"
gen_candidates() {
  ls lp_version src/runtime/python_ast.py src/lpython/*.h src/lpython/parser/* \
     libasr/src/libasr/*.h libasr/src/libasr/pass/*.h 2>/dev/null || true
}
gen_candidates | while read -r f; do if [ -f "$f" ]; then cp -p --parents "$f" "$PREV/"; fi; done
ci/version.sh 2>/dev/null || echo "0.0.0-mayhem" > lp_version
python grammar/asdl_py.py
python libasr/src/libasr/asdl_cpp.py grammar/Python.asdl src/lpython/python_ast.h
python libasr/src/libasr/asdl_cpp.py libasr/src/libasr/ASR.asdl libasr/src/libasr/asr.h
python libasr/src/libasr/wasm_instructions_visitor.py
python libasr/src/libasr/intrinsic_func_registry_util_gen.py
(cd src/lpython/parser && re2c -W -b tokenizer.re -o tokenizer.cpp)
(cd src/lpython/parser && bison -Wall -d -r all parser.yy)
python - <<'PY'
file = 'src/lpython/parser/parser.tab.cc'
text = open(file).read()
open(file, 'w').write('[[maybe_unused]] int yynerrs'.join(text.split('int yynerrs')))
PY
gen_candidates | while read -r f; do
  if [ -f "$PREV/$f" ] && cmp -s "$f" "$PREV/$f"; then touch -r "$PREV/$f" "$f"; fi
done
rm -rf "$PREV"
common_cmake=(-DCMAKE_BUILD_TYPE=Release -DWITH_LLVM=no -DWITH_STACKTRACE=no
              -DWITH_RUNTIME_STACKTRACE=no -DWITH_LCOMPILERS_FAST_ALLOC=no)

# 1) Sanitized library build (the fuzzed code is instrumented).
#    UBSan's `bool` check is relaxed: every generated AST/ASR visitor ends with
#    `if ((bool&)x) { }` ("suppress unused warning"), which loads a struct byte as bool and fires
#    on the first input. ASan and the rest of UBSan stay on and halting.
UBSAN_RELAX="-fno-sanitize=bool"
FUZZ_FLAGS="$SANITIZER_FLAGS $UBSAN_RELAX $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link"
cmake -S . -B mayhem/build-fuzz "${common_cmake[@]}" \
  -DCMAKE_C_FLAGS="$FUZZ_FLAGS" -DCMAKE_CXX_FLAGS="$FUZZ_FLAGS" \
  -DCMAKE_C_FLAGS_RELEASE="" -DCMAKE_CXX_FLAGS_RELEASE=""
cmake --build mayhem/build-fuzz -j"$MAYHEM_JOBS" --target lpython_lib asr lpython_runtime_static

B=mayhem/build-fuzz
LIBS=("$B/src/lpython/liblpython_lib.a" "$B/libasr/libasr.a"
      "$(find "$B/src/runtime/legacy" -name 'liblpython_runtime_static.a' | head -1)")
ZLIB=/usr/lib/x86_64-linux-gnu/libz.a
INC=(-Isrc -I"$B/src" -Ilibasr/src -I"$B/libasr/..")
CXXH=(-std=c++17 -DHAVE_WHEREAMI=1 -DHAVE_ZLIB=1)

$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o $B/lsan_off.o
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o $B/standalone_main.o

for h in parse asr c wat wasm_x64 cpp_python; do
  $CXX $SANITIZER_FLAGS $UBSAN_RELAX $DEBUG_FLAGS -O1 "${CXXH[@]}" "${INC[@]}" \
    -c "mayhem/harnesses/fuzz_$h.cpp" -o "$B/fuzz_$h.o"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$B/fuzz_$h.o" $B/lsan_off.o \
    "${LIBS[@]}" "$ZLIB" -ldl -lpthread -o "$SRC/fuzz_$h"
  $CXX $SANITIZER_FLAGS $DEBUG_FLAGS "$B/fuzz_$h.o" $B/standalone_main.o $B/lsan_off.o \
    "${LIBS[@]}" "$ZLIB" -ldl -lpthread -o "$SRC/fuzz_$h-standalone"
done

# 2) Normal-flags build of the lpython CLI for the reference test suite (run_tests.py).
cmake -S . -B mayhem/build-test "${common_cmake[@]}" \
  -DCMAKE_C_FLAGS="$COVERAGE_FLAGS" -DCMAKE_CXX_FLAGS="$COVERAGE_FLAGS" \
  -DCMAKE_C_FLAGS_RELEASE="-O1 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O1 -DNDEBUG"
cmake --build mayhem/build-test -j"$MAYHEM_JOBS" --target lpython
