#include "llvm/IR/Instructions.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Value.h"
#include <string>
#include <set>
#include <sstream>

using std::string;
using std::set;
using std::stringstream;
using std::hex;
using namespace llvm;

struct ConnectionGraph;

using NodeId = string;
using InstId = string;

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
          isa<AllocaInst>(inst) || isa<BitCastInst>(inst))
        ) {
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
  int getIndex() {
    int n = id.rfind(".");
    return atoi(id.substr(n + 1).c_str());
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
    node.id = getRefNodeName(inst->getName());
    node.inst = dyn_cast<Instruction>(inst);
    return node;
  }
};
