// lpython --show-c: parse -> ASR -> C-backend pass pipeline -> asr_to_c.
#include "lpython_fuzz_common.h"
#include <libasr/codegen/asr_to_c.h>

void lpfuzz_run(lpfuzz::Ctx &ctx) {
    LCompilers::ASR::TranslationUnit_t *asr = ctx.source_to_asr();
    if (!asr) return;
    LCompilers::PassManager pass_manager;
    pass_manager.use_default_passes(true);
    ctx.co.po.always_run = true;
    ctx.co.po.run_fun = "f";
    ctx.co.po.c_skip_bindpy_pass = true;
    pass_manager.apply_passes(ctx.al, asr, ctx.co.po, ctx.diagnostics);
    ctx.diagnostics.diagnostics.clear();
    auto res = LCompilers::asr_to_c(ctx.al, *asr, ctx.diagnostics, ctx.co, 0);
    ctx.render();
    (void)res;
}
