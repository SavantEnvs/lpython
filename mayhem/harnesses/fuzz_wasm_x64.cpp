// lpython --backend wasm_x64 / wasm_x86: parse -> ASR -> wasm bytes -> wasm_to_x64 or wasm_to_x86
// (x86 assembler + ELF writer). The first input byte picks the backend (odd = x86); the rest is
// the Python source. The ELF goes to a private temp file that is removed; it is never executed.
#include "lpython_fuzz_common.h"
#include <libasr/codegen/asr_to_wasm.h>
#include <libasr/codegen/wasm_to_x64.h>
#include <libasr/codegen/wasm_to_x86.h>
#include <stdio.h>

void lpfuzz_run(lpfuzz::Ctx &ctx) {
    if (ctx.input.empty()) return;
    bool x86 = ctx.input[0] & 1;
    // Re-seat the context on the source without the selector byte.
    std::string src = ctx.input.substr(1);
    ctx.input = src;
    ctx.lm = LCompilers::LocationManager();
    LCompilers::LocationManager::FileLocations fl;
    fl.in_filename = "input.py";
    ctx.lm.files.push_back(fl);
    ctx.lm.init_simple(ctx.input);
    ctx.lm.file_ends.push_back(ctx.input.size());

    LCompilers::ASR::TranslationUnit_t *asr = ctx.source_to_asr();
    if (!asr) return;
    LCompilers::Result<LCompilers::Vec<uint8_t>> wasm =
        LCompilers::asr_to_wasm_bytes_stream(*asr, ctx.al, ctx.diagnostics, ctx.co);
    ctx.render();
    if (!wasm.ok) return;

    const char *tmpdir = getenv("TMPDIR");
    std::string tmpl = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/lpfuzz_XXXXXX";
    int fd = mkstemp(&tmpl[0]);
    if (fd < 0) return;
    close(fd);
    if (x86) {
        auto r = LCompilers::wasm_to_x86(wasm.result, ctx.al, tmpl, false, ctx.diagnostics);
        (void)r;
    } else {
        auto r = LCompilers::wasm_to_x64(wasm.result, ctx.al, tmpl, false, ctx.diagnostics);
        (void)r;
    }
    unlink(tmpl.c_str());
    ctx.render();
}
