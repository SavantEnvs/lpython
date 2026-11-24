// lpython --show-asr: parse -> python_ast_to_asr (semantics) -> default ASR passes -> ASR pickle.
#include "lpython_fuzz_common.h"

void lpfuzz_run(lpfuzz::Ctx &ctx) {
    LCompilers::ASR::TranslationUnit_t *asr = ctx.source_to_asr();
    if (!asr) return;
    LCompilers::PassManager pass_manager;
    ctx.co.po.always_run = true;
    ctx.co.po.run_fun = "f";
    pass_manager.apply_passes(ctx.al, asr, ctx.co.po, ctx.diagnostics);
    ctx.render();
    std::string s = LCompilers::pickle(*asr, false, false, false);
    (void)s;
}
