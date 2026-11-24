// lpython --show-cpp / --show-python: parse -> ASR -> asr_to_cpp or asr_to_python. The first input
// byte picks the backend (odd = python); the rest is the Python source.
#include "lpython_fuzz_common.h"
#include <libasr/codegen/asr_to_cpp.h>
#include <libasr/codegen/asr_to_python.h>

void lpfuzz_run(lpfuzz::Ctx &ctx) {
    if (ctx.input.empty()) return;
    bool python = ctx.input[0] & 1;
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
    if (python) {
        auto r = LCompilers::asr_to_python(ctx.al, *asr, ctx.diagnostics, ctx.co, false, 0);
        (void)r;
    } else {
        auto r = LCompilers::asr_to_cpp(ctx.al, *asr, ctx.diagnostics, ctx.co, 0);
        (void)r;
    }
    ctx.render();
}
