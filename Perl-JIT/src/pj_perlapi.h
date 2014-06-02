#ifndef PJ_PERLAPI_H_
#define PJ_PERLAPI_H_

#include "pj_perlapibase.h"

#undef Copy

#include <llvm/ExecutionEngine/ExecutionEngine.h>

namespace PerlJIT {
  class PerlAPI : public PerlAPIBase {
  public:
    PerlAPI(llvm::Module *module, llvm::IRBuilder<> *builder, llvm::ExecutionEngine *ee);

    void alloc_sp();

    llvm::FunctionType *ppaddr_type() const { return pp_type; }

    void emit_call_runloop(OP *op);

    llvm::Value *emit_pad_sv(UV padix);
    llvm::Value *emit_pad_sv_address(UV padix);

    llvm::Constant *IV_constant(IV value);
    llvm::Constant *UV_constant(UV value);
    llvm::Constant *NV_constant(NV value);
  private:
    llvm::Value *interp_value(unsigned int offset, llvm::Type *type, const llvm::Twine &name);
    llvm::FunctionType *function_type(llvm::Type *ret, ...);

    llvm::Type *ptr_type;
    llvm::Type *interpreter_type, *ptr_sv_type, *ptr_ptr_sv_type, *ptr_op_type;
    llvm::FunctionType *pp_type;

    // TODO autogenerate
    llvm::Function *pa_call_runloop;
  };
}

#endif // PJ_PERLAPI_H_
