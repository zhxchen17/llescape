#include "llvm/Pass.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
  struct Escape : public FunctionPass {
    static char ID;
    Escape() : FunctionPass(ID) {}
    bool runOnFunction(Function &F) override {
      errs() << "Escape: ";
      errs().write_escaped(F.getName()) << '\n';
      return false;
    }
  };
}

char Escape::ID = 0;
static RegisterPass<Escape> X("escape", "Escape pass",
                              false,
                              false);
