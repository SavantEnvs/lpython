// Shared driver for the lpython libFuzzer harnesses.
//
// Each harness mirrors one `lpython --show-*` / `--backend` path from src/bin/lpython.cpp
// (emit_ast, emit_asr, emit_c, emit_wat, compile_to_binary_*), but takes the Python source
// from the fuzzer buffer instead of a file. Errors are reported by the compiler as diagnostics
// or LCompilersException-family exceptions, which the CLI's main() catches and reports; we do
// the same, so only real memory-safety / UB defects (ASan/UBSan) or aborts surface.
//
// State kept across inputs: only the runtime-library directory (read-only, set once from the
// binary's own location). Every input gets a fresh Allocator, Diagnostics, LocationManager and
// CompilerOptions.
#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <limits.h>

#include <lpython/pickle.h>
#include <libasr/pickle.h>
#include <lpython/semantics/python_ast_to_asr.h>
#include <lpython/parser/parser.h>
#include <lpython/utils.h>
#include <libasr/pass/pass_manager.h>
#include <libasr/exception.h>
#include <libasr/diagnostics.h>
#include <libasr/location.h>

namespace lpfuzz {

// Runtime library (.py modules like lpython_builtin.py) ships in the source tree at
// <repo>/src/runtime; the fuzz binaries live in the repo root, so locate it relative to the
// executable (no absolute path baked in).
inline void init_runtime_dir() {
    if (std::getenv("LFORTRAN_RUNTIME_LIBRARY_DIR")) return;
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    std::string exe(buf);
    std::string dir = exe.substr(0, exe.rfind('/'));
    std::string rt = dir + "/src/runtime";
    setenv("LFORTRAN_RUNTIME_LIBRARY_DIR", rt.c_str(), 1);
}

struct Ctx {
    Allocator al{4 * 1024};
    LCompilers::diag::Diagnostics diagnostics;
    LCompilers::LocationManager lm;
    LCompilers::CompilerOptions co;
    std::string input;

    Ctx(const uint8_t *data, size_t size) : input((const char *)data, size) {
        co.use_colors = false;
        co.po.runtime_library_dir = LCompilers::LPython::get_runtime_library_dir();
        LCompilers::LocationManager::FileLocations fl;
        fl.in_filename = "input.py";
        lm.files.push_back(fl);
        lm.init_simple(input);
        lm.file_ends.push_back(input.size());
    }

    // Render (and discard) diagnostics: reaches LocationManager position mapping.
    void render() {
        std::string s = diagnostics.render(lm, co);
        (void)s;
        diagnostics.diagnostics.clear();
    }

    // Source -> AST (lpython --show-ast). nullptr on a parse error.
    LCompilers::LPython::AST::ast_t *parse() {
        LCompilers::Result<LCompilers::LPython::AST::Module_t *> r =
            LCompilers::LPython::parse(al, input, 0, diagnostics);
        render();
        if (!r.ok) return nullptr;
        return (LCompilers::LPython::AST::ast_t *)r.result;
    }

    // AST -> ASR (lpython --show-asr, without passes). nullptr on a semantic error.
    LCompilers::ASR::TranslationUnit_t *to_asr(LCompilers::LPython::AST::ast_t *ast) {
        LCompilers::Result<LCompilers::ASR::TranslationUnit_t *> r =
            LCompilers::LPython::python_ast_to_asr(al, lm, nullptr, *ast, diagnostics, co,
                                                   true, "__main__", "input.py");
        render();
        if (!r.ok) return nullptr;
        return r.result;
    }

    LCompilers::ASR::TranslationUnit_t *source_to_asr() {
        LCompilers::LPython::AST::ast_t *ast = parse();
        if (!ast) return nullptr;
        return to_asr(ast);
    }
};

}  // namespace lpfuzz

// Each harness defines this; it runs one compiler path on the context.
void lpfuzz_run(lpfuzz::Ctx &ctx);

extern "C" int LLVMFuzzerInitialize(int *, char ***) {
    lpfuzz::init_runtime_dir();
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    try {
        lpfuzz::Ctx ctx(data, size);
        lpfuzz_run(ctx);
    } catch (const LCompilers::LCompilersException &) {
        // Internal compiler error reported as an exception: main() prints it and exits 1.
    } catch (const std::runtime_error &) {
    } catch (const std::exception &) {
    } catch (...) {
        // main() also swallows unknown exceptions ("Unknown error").
    }
    return 0;
}
