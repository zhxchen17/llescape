#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <deque>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// #define LLESCAPE_DEBUG

#ifdef LLESCAPE_DEBUG
#define TRACE(x) x
#else
#define TRACE(x)
#endif

using std::cout;
using std::deque;
using std::endl;
using std::hex;
using std::map;
using std::max;
using std::set;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;
using InstId = string;
using namespace llvm;

static const string GO_HEAP_CALL = "__go_new";
static const string GO_LIB_PREFIX = "__go_";

InstId getId(Value *val) {
  uintptr_t id = reinterpret_cast<uintptr_t>(val) & 0xffff;
  stringstream stream;
  stream << hex << id;
  if (auto inst = dyn_cast<Instruction>(val)) {
    return inst->getName().str() + "_" + stream.str();
  } else {
    return "_" + stream.str();
  }
}

enum EscapeType { GlobalEscape = 0, LocalEscape = 1, NoEscape = 2 };

struct Context {
  Function *f;
  Context(Function *_f) : f(_f) {}
  bool operator<(const Context &other) const {
    return reinterpret_cast<uintptr_t>(f) <
           reinterpret_cast<uintptr_t>(other.f);
  }
};

struct EscapeLattice {
  static EscapeType getBottom() { return GlobalEscape; }
};

struct Summary {
  vector<EscapeType> args;
  Summary(vector<EscapeType> &_a) : args(_a) {}
  Summary(int n) : args(n, EscapeLattice::getBottom()) {}
  EscapeType get(int i) { return args[i]; }
};

struct EscapeCache {
  map<Context, Summary> cache;
  Summary *getSummary(const Context &ctx) { return nullptr; }
  void putSummary(const Context &ctx, const Summary &summary) {}
};

struct EscapeAnalysis {
  // AAResultsWrapperPass &aa;
  // MemorySSAWrapperPass &mssa;
  Pass &pass;
  EscapeCache *cache;
  Function *current;
  AAResults &aa() {
    if (cache) {
      return pass.getAnalysis<AAResultsWrapperPass>(*current).getAAResults();
    } else {
      return pass.getAnalysis<AAResultsWrapperPass>().getAAResults();
    }
  }
  MemorySSA &mssa() {
    if (cache) {
      return pass.getAnalysis<MemorySSAWrapperPass>(*current).getMSSA();
    } else {
      return pass.getAnalysis<MemorySSAWrapperPass>().getMSSA();
    }
  }
  EscapeAnalysis(Pass &_pass, EscapeCache *_cache = nullptr)
      : pass(_pass), cache(_cache) {}
  EscapeType foundEscape(Value *val) {
    EscapeType ret = NoEscape;
    if (auto bitcast = dyn_cast<BitCastInst>(val)) {
      ret = foundEscape(bitcast->getOperand(0));
    } else if (auto gep = dyn_cast<GetElementPtrInst>(val)) {
      ret = foundEscape(gep->getOperand(0));
    } else if (auto phi = dyn_cast<PHINode>(val)) {
      int n = phi->getNumIncomingValues();
      for (int i = 0; i < n; i++) {
        Value *iv = phi->getIncomingValue(i);
        EscapeType escaping = foundEscape(iv);
        if (escaping != NoEscape) {
          ret = escaping;
          break;
        }
      }
    } else if (auto bitop = dyn_cast<BitCastOperator>(val)) {
      ret = foundEscape(bitop->getOperand(0));
    } else if (auto gepop = dyn_cast<GEPOperator>(val)) {
      ret = foundEscape(gepop->getOperand(0));
    } else if (auto global = dyn_cast<GlobalVariable>(val)) {
      ret = GlobalEscape;
    } else if (isa<Argument>(val)) {
      ret = LocalEscape;
    }
    return ret;
  }

  // Should always begin with store instruction.
  EscapeType backward(MemoryAccess *ma) {
    if (auto ud = dyn_cast<MemoryUseOrDef>(ma)) {
      Value *val = ud->getMemoryInst();
      if (!val)
        return NoEscape;
      if (auto call = dyn_cast<CallInst>(val)) {
        return track(call, true);
      } else {
        if (auto store = dyn_cast<StoreInst>(val)) {
          EscapeType escaping = foundEscape(store->getPointerOperand());
          if (escaping == GlobalEscape) {
            return GlobalEscape;
          } else if (escaping == LocalEscape) {
            return LocalEscape;
          }
        }
        return backward(ud->getDefiningAccess());
      }
    } else if (auto phi = dyn_cast<MemoryPhi>(ma)) {
      // MemoryPhi
      uint32_t n = phi->getNumIncomingValues();
      for (uint32_t i = 0; i < n; i++) {
        EscapeType escaping = backward(phi->getIncomingValue(i));
        if (escaping != NoEscape)
          return escaping;
      }
      return NoEscape;
    } else {
      return GlobalEscape;
    }
  }

  EscapeType resultFor(CallInst *call, Value *inst) {
    EscapeType escaping = NoEscape;
    auto func = call->getCalledFunction();
    if (func) {
      if (func->isDeclaration()) {
        escaping = NoEscape; // TODO
      } else if (!cache) {
        escaping = GlobalEscape;
      } else {
        Context ctx(func);
        if (analyzing.find(ctx) != analyzing.end()) {
          escaping = GlobalEscape;
        } else {
          if (!cache->getSummary(ctx)) {
            EscapeAnalysis analysis(pass, cache);
            analysis.transform(func);
          }
          auto summary = cache->getSummary(ctx);
          int i = 0;
          for (auto &arg : func->args()) {
            if (&arg == inst && summary->get(i) == GlobalEscape) {
              escaping = GlobalEscape;
              break;
            }
            i++;
          }
        }
      }
    } else {
      escaping = GlobalEscape;
    }
    return escaping;
  }

  EscapeType forward(MemoryAccess *m, Value *loc) {
    EscapeType escaping = NoEscape;
    Module *mdl = m->getBlock()->getParent()->getParent();
    DataLayout td(mdl);
    PointerType *locTy = dyn_cast<PointerType>(loc->getType());

    for (auto user : m->users()) {
      if (auto mu = dyn_cast<MemoryUse>(user)) {
        Value *val = mu->getMemoryInst();
        if (val && isa<LoadInst>(val)) {
          auto ptr = dyn_cast<LoadInst>(val)->getPointerOperand();
          TRACE(val->print(errs()));
          TRACE(errs() << "\n");
          PointerType *ptrTy = dyn_cast<PointerType>(ptr->getType());
          auto ar =
              aa().alias(ptr, td.getTypeAllocSize(ptrTy->getElementType()), loc,
                         td.getTypeAllocSize(locTy->getElementType()));
          if (ar != NoAlias) {
            TRACE(errs() << "load not no alias.\n");
            escaping = track(val);
          } else {
            TRACE(errs() << "load no alias.\n");
          }
        } else {
          TRACE(errs() << "ERROR! UNKNOWN MEMORYUSE INST.\n");
          escaping = GlobalEscape;
        }
      } else if (auto phi = dyn_cast<MemoryPhi>(user)) {
        TRACE(errs() << "phi!\n");
        escaping = forward(phi, loc);
      } else if (auto def = dyn_cast<MemoryDef>(user)) {
        Value *val = def->getMemoryInst();
        if (!val) {
          escaping = NoEscape;
        } else if (isa<StoreInst>(val)) {
          Value *ptr = dyn_cast<StoreInst>(val)->getPointerOperand();
          TRACE(errs() << "def!\n");
          TRACE(val->print(errs()));
          TRACE(errs() << "\n");
          PointerType *ptrTy = dyn_cast<PointerType>(ptr->getType());
          auto ar =
              aa().alias(ptr, td.getTypeAllocSize(ptrTy->getElementType()), loc,
                         td.getTypeAllocSize(locTy->getElementType()));
          if (ar != MustAlias) {
            escaping = forward(def, loc);
          }
        } else if (auto call = dyn_cast<CallInst>(val)) {
          auto func = call->getCalledFunction();
          if (func) {
            if (func->isDeclaration()) {
              escaping = NoEscape; // TODO
            } else {
              for (auto &arg : func->args()) {
                if (auto ptr = dyn_cast<Argument>(&arg)) {
                  PointerType *ptrTy = dyn_cast<PointerType>(ptr->getType());
                  auto ar = aa().alias(
                      ptr, td.getTypeAllocSize(ptrTy->getElementType()), loc,
                      td.getTypeAllocSize(locTy->getElementType()));
                  if (ar != NoAlias) {
                    escaping = resultFor(call, ptr);
                  }
                }
              }
            }
          } else {
            escaping = GlobalEscape;
          }
        } else {
          TRACE(errs() << "ERROR! UNKNOWN MEMORYDEF INST.\n");
          TRACE(val->print(errs()));
          TRACE(errs() << "\n");
          escaping = GlobalEscape;
        }
      }
      if (escaping != NoEscape)
        break;
    }
    return escaping;
  }

  set<InstId> trackList;
  static set<Context> analyzing;
  EscapeType track(Value *inst, bool isRoot = false) {
    EscapeType escaping = NoEscape;
    if (isRoot) {
      const auto &r = trackList.emplace(getId(inst));
      // already exists
      if (!r.second) {
        TRACE(errs() << "LOOP BACK\n");
        return NoEscape;
      }
    }
    for (auto user : inst->users()) {
      if (auto bitcast = dyn_cast<BitCastInst>(user)) {
        TRACE(errs() << "bitcast " << bitcast->getName() << "\n");
        escaping = track(bitcast);
      } else if (auto gep = dyn_cast<GetElementPtrInst>(user)) {
        TRACE(errs() << "gep " << gep->getName() << "\n");
        escaping = track(gep);
      } else if (auto phi = dyn_cast<PHINode>(inst)) {
        escaping = track(phi);
      } else if (auto store = dyn_cast<StoreInst>(user)) {
        TRACE(store->print(errs()));
        TRACE(errs() << "\n");
        if (getId(store->getValueOperand()) == getId(inst)) {
          MemoryUseOrDef *m = mssa().getMemoryAccess(store);
          escaping = backward(m);
          if (escaping == NoEscape) {
            TRACE(errs() << "USERS:\n");
            escaping = forward(m, store->getPointerOperand());
          }
        }
      } else if (auto load = dyn_cast<LoadInst>(user)) {
        TRACE(errs() << "load " << load->getName() << "\n");
        return NoEscape;
      } else if (auto iv = dyn_cast<InsertValueInst>(user)) {
        TRACE(errs() << "insertvalue " << iv->getName() << "\n");
        escaping = track(iv);
      } else if (auto ev = dyn_cast<ExtractValueInst>(user)) {
        TRACE(errs() << "extractvalue " << ev->getName() << "\n");
        escaping = track(ev);
      } else if (auto op = dyn_cast<ICmpInst>(user)) {
        escaping = NoEscape;
      } else if (auto call = dyn_cast<CallInst>(user)) {
        TRACE(call->print(errs()));
        escaping = resultFor(call, inst);
      } else {
        escaping = LocalEscape;
      }
      if (escaping != NoEscape)
        break;
    }
    if (isRoot) {
      trackList.erase(getId(inst));
    }
    return escaping;
  }

  void transform(Function *F) {
    current = F;
    analyzing.emplace(F);
    Summary summary(F->arg_size());
    int i = 0;
    for (auto &arg : F->args()) {
      summary.args[i++] = track(&arg, true);
    }
    for (auto bb = F->begin(), e = F->end(); bb != e; ++bb) {
      for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
        InstId id = getId(&*i);
        Instruction *inst = &*i;
        if (auto call = dyn_cast<CallInst>(inst)) {
          Function *func = call->getCalledFunction();
          if (func && func->getName() == GO_HEAP_CALL) {
            trackList.clear();
            string var;
            for (auto user : i->users()) {
              var = user->getName();
              break;
            }
            EscapeType res = track(inst, true);
            errs() << "%" << i->getName() << "(" << var << ")";
            if (res == NoEscape) {
              errs() << " is local.\n";
            } else if (res == LocalEscape) {
              errs() << " locally escapes.\n";
            } else {
              errs() << " globally escapes.\n";
            }
          }
        }
      }
    }
    cache->putSummary(Context(F), summary);
    analyzing.erase(F);
  }
};

set<Context> EscapeAnalysis::analyzing;

namespace {

struct Escape : public FunctionPass {
  static char ID;

  Escape() : FunctionPass(ID) {}

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredTransitive<AAResultsWrapperPass>();
    AU.addRequiredTransitive<MemorySSAWrapperPass>();
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    if (F.getName().substr(0, GO_LIB_PREFIX.size()) == GO_LIB_PREFIX) {
      return false;
    }
    errs() << "Escape: ";
    errs().write_escaped(F.getName()) << '\n';
    EscapeAnalysis analysis(*this);
    analysis.transform(&F);
    return false;
  }
};
} // namespace

char Escape::ID = 0;
static RegisterPass<Escape> X("escape", "Escape pass", false, false);

namespace {
struct Try : public FunctionPass {
  static char ID;

  Try() : FunctionPass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredTransitive<AAResultsWrapperPass>();
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    if (F.getName() != "main.fooo") {
      return false;
    }
    errs() << "Escape: ";
    errs().write_escaped(F.getName()) << '\n';
    auto &aa = getAnalysis<AAResultsWrapperPass>().getAAResults();
    for (auto bb = F.begin(), e = F.end(); bb != e; ++bb) {
      for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
        for (auto j = bb->begin(), ej = bb->end(); j != ej; ++j) {
          errs() << i->getName() << " " << j->getName() << ": ";
          auto ar = aa.alias(&*i, 8, &*j, 8);
          if (ar == NoAlias) {
            errs() << "NoAlias";
          } else if (ar == MayAlias) {
            errs() << "MayAlias";
          } else if (ar == PartialAlias) {
            errs() << "PartialAlias";
          } else if (ar == MustAlias) {
            errs() << "MustAlias";
          }
          errs() << "\n";
        }
      }
    }
    return false;
  }
};
} // namespace

char Try::ID = 1;
static RegisterPass<Try> XX("try", "Try pass", false, false);

namespace {

struct EscapeModule : public ModulePass {
  static char ID;

  EscapeModule() : ModulePass(ID) {}
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredTransitive<AAResultsWrapperPass>();
    AU.addRequiredTransitive<MemorySSAWrapperPass>();
    AU.setPreservesAll();
  }

  deque<Context> workList;
  bool runOnModule(Module &M) override {
    EscapeCache cache;
    EscapeAnalysis analysis(*this, &cache);
    static const string PREFIX = "main.";
    for (auto it = M.begin(), e = M.end(); it != e; ++it) {
      if (it->isDeclaration())
        continue;
      if (it->getName().substr(0, PREFIX.size()) != PREFIX)
        continue;
      analysis.transform(&*it);
    }

    return false;
  }
};
} // namespace

char EscapeModule::ID = 2;
static RegisterPass<EscapeModule> XXX("escape-module", "Escape module pass",
                                      false, false);
