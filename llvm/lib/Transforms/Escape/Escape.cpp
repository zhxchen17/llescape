#include "Node.h"
#include "llvm/IR/Operator.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/MemorySSA.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// #define LLESCAPE_DEBUG

#ifdef LLESCAPE_DEBUG
#define TRACE(x) x
#else
#define TRACE(x)
#endif

using std::cout;
using std::endl;
using std::hex;
using std::map;
using std::set;
using std::string;
using std::to_string;
using std::vector;
using namespace llvm;

static const string GO_HEAP_CALL = "__go_new";
static const string GO_LIB_PREFIX = "__go_";

enum EscapeType { GlobalEscape = 0, LocalEscape = 1, NoEscape = 2 };
struct ConnectionGraph {
  map<NodeId, Node> nodes;
  Node *genMemNode(Type *type, Instruction *inst, string idx = ".0") {
    if (auto structType = dyn_cast<StructType>(type)) {
      Node tmp = Node::makeMemNode(inst, idx);
      auto const &r = nodes.emplace(tmp.id, tmp);
      Node &node = r.first->second;
      int i = 0;
      for (auto it = structType->element_begin(), e = structType->element_end();
           it != e; ++it) {
        Node *child = genMemNode(*it, inst, idx + "." + to_string(i));
        if (isa<PointerType>(*it)) {
          node.fields.insert(child->id);
        } else {
          node.children.insert(child->id);
        }
        i++;
      }
      return &node;
    } else if (auto pointerType = dyn_cast<PointerType>(type)) {
      return genMemNode(pointerType->getElementType(), inst, idx + ".0");
    } else if (auto integerType = dyn_cast<IntegerType>(type)) {
      Node tmp = Node::makeMemNode(inst, idx);
      auto const &r = nodes.emplace(tmp.id, tmp);
      return &r.first->second;
    } else {
      return nullptr;
    }
  }
  Node *getMemNode(Instruction *inst) {
    Type *allocType = nullptr;
    if (auto *call = dyn_cast<CallInst>(inst)) {
      auto func = call->getCalledFunction();
      if (func && func->getName() == GO_HEAP_CALL) {
        allocType = func->getReturnType();
        int cnt = 0;
        for (auto u : call->users()) {
          if (cnt) {
            errs() << "Allocated heap is used more than once!\n";
            assert(false);
          }
          allocType = u->getType();
          cnt += 1;
        }
      }
    } else if (auto *alloca = dyn_cast<AllocaInst>(inst)) {
      allocType = alloca->getAllocatedType();
    }
    if (allocType) {
      auto it = nodes.find(Node::getMemNodeName(getId(inst)));
      if (it == nodes.end()) {
        return genMemNode(allocType, inst);
      } else {
        return &it->second;
      }
    }
    return nullptr;
  }
  Node *getRefNode(Value *inst) {
    Node node = Node::makeRefNode(inst);
    auto const &r = nodes.emplace(node.id, node);
    return &r.first->second;
  }
  void print() {
    for (auto p : nodes) {
      p.second.print();
    }
  }
};

namespace {

struct Escape : public FunctionPass {
  static char ID;
  map<InstId, ConnectionGraph> graphs;

  Escape() : FunctionPass(ID) {}

  EscapeType foundGlobal(Value *val) {
    EscapeType ret = NoEscape;
    if (auto bitcast = dyn_cast<BitCastInst>(val)) {
      ret = foundGlobal(bitcast->getOperand(0));
    } else if (auto gep = dyn_cast<GetElementPtrInst>(val)) {
      ret = foundGlobal(gep->getOperand(0));
    } else if (auto phi = dyn_cast<PHINode>(val)) {
      int n = phi->getNumIncomingValues();
      for (int i = 0; i < n; i++) {
        Value *iv = phi->getIncomingValue(i);
        EscapeType escaping = foundGlobal(iv);
        if (escaping != NoEscape) {
          ret = escaping;
          break;
        }
      }
    } else if (auto bitop = dyn_cast<BitCastOperator>(val)) {
      ret = foundGlobal(bitop->getOperand(0));
    } else if (auto gepop = dyn_cast<GEPOperator>(val)) {
      ret = foundGlobal(gepop->getOperand(0));
    } else if (auto global = dyn_cast<GlobalVariable>(val)) {
      ret = GlobalEscape;
    }
    return ret;
  }

  // Should always begin with store instruction.
  EscapeType backward(MemoryAccess *ma) {
    if (auto ud = dyn_cast<MemoryUseOrDef>(ma)) {
      Value *val = ud->getMemoryInst();
      if (auto call = dyn_cast<CallInst>(val)) {
        return track(call, true);
      } else {
        if (auto store = dyn_cast<StoreInst>(val)) {
          if (foundGlobal(store->getPointerOperand()) == GlobalEscape) {
            return GlobalEscape;
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

  EscapeType forward(MemoryAccess *m, Value *loc) {
    EscapeType escaping = NoEscape;
    for (auto user : m->users()) {
      if (auto mu = dyn_cast<MemoryUse>(user)) {
        Value *val = mu->getMemoryInst();
        if (val && isa<LoadInst>(val)) {
          auto ptr = dyn_cast<LoadInst>(val)->getPointerOperand();
          TRACE(val->print(errs()));
          TRACE(errs() << "\n");
          auto &aa = getAnalysis<AAResultsWrapperPass>().getAAResults();
          Module *mdl = m->getBlock()->getParent()->getParent();
          DataLayout td(mdl);
          PointerType *ptrTy = dyn_cast<PointerType>(ptr->getType());
          PointerType *locTy = dyn_cast<PointerType>(loc->getType());
          auto ar = aa.alias(ptr, td.getTypeAllocSize(ptrTy->getElementType()),
                             loc, td.getTypeAllocSize(locTy->getElementType()));
          if (ar != NoAlias) {
            TRACE(errs() << "load not no alias.\n");
            escaping = track(val);
          } else {
            TRACE(errs() << "load no alias.\n");
          }
          TRACE(errs() << "ptr: ");
          TRACE(ptr->print(errs()));
          TRACE(errs() << " loc: ");
          TRACE(loc->print(errs()));
          TRACE(errs() << "\n");
        } else {
          TRACE(errs() << "ERROR! UNKNOWN MEMORYUSE INST.\n");
          escaping = GlobalEscape;
        }
      } else if (auto phi = dyn_cast<MemoryPhi>(user)) {
        TRACE(errs() << "phi!\n");
        escaping = forward(phi, loc);
      } else if (auto def = dyn_cast<MemoryDef>(user)) {
        Value *val = def->getMemoryInst();
        if (val && isa<StoreInst>(val)) {
          Value *ptr = dyn_cast<StoreInst>(val)->getPointerOperand();
          TRACE(errs() << "def!\n");
          TRACE(val->print(errs()));
          TRACE(errs() << "\n");
          auto &aa = getAnalysis<AAResultsWrapperPass>().getAAResults();
          auto ar = aa.alias(ptr, loc);
          if (ar != MustAlias) {
            escaping = forward(def, loc);
          }
        } else {
          TRACE(errs() << "ERROR! UNKNOWN MEMORYDEF INST.\n");
          escaping = GlobalEscape;
        }
      }
      if (escaping != NoEscape)
        break;
    }
    return escaping;
  }

  set<InstId> trackList;
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
          MemorySSA &mssa = getAnalysis<MemorySSAWrapperPass>().getMSSA();
          MemoryUseOrDef *m = mssa.getMemoryAccess(store);
          escaping = backward(m);
          if (escaping == NoEscape) {
            TRACE(errs() << "USERS:\n");
            escaping = forward(m, store->getPointerOperand());
          }
        }
      } else if (auto load = dyn_cast<LoadInst>(user)) {
        TRACE(errs() << "load " << load->getName() << "\n");
        return NoEscape;
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

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequiredTransitive<AAResultsWrapperPass>();
    AU.addRequiredTransitive<MemorySSAWrapperPass>();
    AU.setPreservesAll();
  }

  bool runOnFunction(Function &F) override {
    if (F.getName().substr(0, GO_LIB_PREFIX.size()) == GO_LIB_PREFIX) {
      return false;
    }

    ConnectionGraph graph;
    errs() << "Escape: ";
    errs().write_escaped(F.getName()) << '\n';
    for (auto bb = F.begin(), e = F.end(); bb != e; ++bb) {
      for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
        InstId id = getId(&*i);
        Instruction *inst = &*i;
        Node *obj = graph.getMemNode(inst);
        if (obj && isa<CallInst>(inst)) {
          trackList.clear();
          i->print(errs());
          errs() << "\n";
          EscapeType res = track(inst, true);
          if (res == NoEscape) {
            errs() << "is local.\n";
          } else if (res == LocalEscape) {
            errs() << "locally escapes.\n";
          } else {
            errs() << "globally escapes.\n";
          }
        }
      }
    }
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
}

char Try::ID = 1;
static RegisterPass<Try> XX("try", "Try pass", false, false);
