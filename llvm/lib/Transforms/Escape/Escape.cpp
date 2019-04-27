#include "llvm/Analysis/CaptureTracking.h"
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
#include <sstream>
#include <string>
#include <vector>

using std::cout;
using std::endl;
using std::hex;
using std::map;
using std::set;
using std::string;
using std::stringstream;
using std::to_string;
using std::vector;
using namespace llvm;

namespace {
using NodeId = string;
using InstId = string;
static const string GO_HEAP_CALL = "__go_new";
static const string GO_LIB_PREFIX = "__go_";
NodeId getId(Instruction *inst) {
  uintptr_t id = reinterpret_cast<uintptr_t>(inst);
  stringstream stream;
  stream << hex << id;
  return stream.str();
}
struct ConnectionGraph;
struct Node {
  bool isMem;
  string id;
  ConnectionGraph *graph;
  Instruction *inst;
  // For RefNode
  set<NodeId> indirects;
  set<NodeId> directs;
  // For MemNode
  set<NodeId> children;
  set<NodeId> fields;
  void setGraph(ConnectionGraph *g) { graph = g; }
  bool operator==(const Node &other) const {
    if (id != other.id) {
      return false;
    }
    auto cmp = [&](const set<NodeId> &a, const set<NodeId> &b) {
      if (a.size() != b.size()) {
        return false;
      }
      int n = a.size();
      auto ita = a.begin();
      auto itb = b.begin();
      for (int i = 0; i < n; i++) {
        if (*ita != *itb) {
          return false;
        }
        ita++;
        itb++;
      }
      return true;
    };
    return cmp(indirects, other.indirects) && cmp(directs, other.directs) &&
           cmp(fields, other.fields);
  }
  void print() {
    if (!(isa<CallInst>(inst) || isa<StoreInst>(inst) ||
          isa<AllocaInst>(inst))) {
      return;
    }
    errs() << id << ": ";
    if (isMem) {
      errs() << "fields ";
      for (auto &s : fields)
        errs() << s << ", ";
      errs() << "\nchildren ";
      for (auto &s : children)
        errs() << s << ", ";
      errs() << "\n";
    } else {
      errs() << "directs ";
      for (auto &s : directs)
        errs() << s << ", ";
      errs() << "\nindirects ";
      for (auto &s : indirects)
        errs() << s << ", ";
      errs() << "\n";
    }
  }
  static string getMemNodeName(const string &reg, const string &suffix = ".0") {
    return "@" + reg + suffix;
  }
  static Node makeMemNode(Instruction *inst, const string &suffix) {
    Node node;
    node.isMem = true;
    node.id = getMemNodeName(getId(inst), suffix);
    node.inst = inst;
    return node;
  }
  static string getRefNodeName(const string &reg) { return "&" + reg; }
  static Node makeRefNode(Value *inst) {
    Node node;
    node.isMem = false;
    node.id = inst->getName();
    node.inst = dyn_cast<Instruction>(inst);
    return node;
  }
};
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
  void cloneTo(ConnectionGraph &ret) {
    ret.nodes = nodes;
    for (auto p : nodes) {
      p.second.setGraph(&ret);
    }
  }
  void print() {
    for (auto p : nodes) {
      p.second.print();
    }
  }
};
struct Escape : public FunctionPass {
  static char ID;
  map<InstId, ConnectionGraph> graphs;
  Escape() : FunctionPass(ID) {}
  void flowThrough(Instruction *inst, ConnectionGraph &next) {
    if (auto call = dyn_cast<CallInst>(inst)) {
      Node *obj = next.getMemNode(inst);
      if (obj) {
        errs() << "\t" << obj->id << "\n";
        errs() << "\t"
               << "captured: "
               << llvm::PointerMayBeCaptured(obj->inst, true, true) << "\n";
        Node *root = next.getRefNode(inst);
        root->directs.emplace(obj->id);
      }
    } else if (auto alloca = dyn_cast<AllocaInst>(inst)) {
      Node *obj = next.getMemNode(inst);
      Node *root = next.getRefNode(inst);
      root->directs.emplace(obj->id);
    } else if (auto bitcast = dyn_cast<BitCastInst>(inst)) {
      Node *reg = next.getRefNode(inst);
      Value *src = bitcast->getOperand(0);
      reg->indirects.emplace(next.getRefNode(src)->id);
    }
  }
  void propagate(Instruction *inst, ConnectionGraph &graph) {
    Instruction *next = nullptr;
    switch (inst->getOpcode()) {
    default:
      next = inst->getNextNode();
      if (next) {
        graph.cloneTo(graphs[getId(next)]);
      }
    }
  }
  bool runOnFunction(Function &F) override {
    if (F.getName().substr(0, GO_LIB_PREFIX.size()) == GO_LIB_PREFIX) {
      return false;
    }
    for (auto bb = F.begin(), e = F.end(); bb != e; ++bb) {
      for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
        graphs.emplace(getId(&*i), ConnectionGraph());
      }
    }
    errs() << "Escape: ";
    errs().write_escaped(F.getName()) << '\n';
    for (auto bb = F.begin(), e = F.end(); bb != e; ++bb) {
      for (auto i = bb->begin(), e = bb->end(); i != e; ++i) {
        ConnectionGraph next;
        InstId id = getId(&*i);
        graphs[id].cloneTo(next);
        errs() << "Instruction: ";
        i->print(errs());
        errs() << "\n";
        errs() << "Current graph:\n";
        next.print();
        flowThrough(&*i, next);
        errs() << "Next graph:\n";
        next.print();
        propagate(&*i, next);
      }
    }
    return false;
  }
};
} // namespace

char Escape::ID = 0;
static RegisterPass<Escape> X("escape", "Escape pass", false, false);
