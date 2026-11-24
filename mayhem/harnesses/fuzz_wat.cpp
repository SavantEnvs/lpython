// lpython --show-wat: parse -> ASR -> asr_to_wasm_bytes_stream -> wasm_to_wat (wasm decoder).
#include "lpython_fuzz_common.h"
#include <libasr/codegen/asr_to_wasm.h>
#include <libasr/codegen/wasm_to_wat.h>

void lpfuzz_run(lpfuzz::Ctx &ctx) {
    LCompilers::ASR::TranslationUnit_t *asr = ctx.source_to_asr();
    if (!asr) return;
    LCompilers::Result<LCompilers::Vec<uint8_t>> wasm =
        LCompilers::asr_to_wasm_bytes_stream(*asr, ctx.al, ctx.diagnostics, ctx.co);
    ctx.render();
    if (!wasm.ok) return;
    LCompilers::Result<std::string> wat = LCompilers::wasm_to_wat(wasm.result, ctx.al, ctx.diagnostics);
    ctx.render();
    (void)wat;
}
