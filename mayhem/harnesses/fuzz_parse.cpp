// lpython --show-ast: tokenizer (re2c) + parser (bison) + AST pickling + diagnostics rendering.
#include "lpython_fuzz_common.h"

void lpfuzz_run(lpfuzz::Ctx &ctx) {
    LCompilers::LPython::AST::ast_t *ast = ctx.parse();
    if (!ast) return;
    std::string s = LCompilers::LPython::pickle_python(*ast, false, false);
    (void)s;
}
