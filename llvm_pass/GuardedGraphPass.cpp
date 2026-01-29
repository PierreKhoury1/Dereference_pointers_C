#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Path.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace llvm;

namespace {

struct Node {
    int id = 0;
    std::string kind;
    std::string name;
    int x = 0;
    int y = 0;
    int field = 0;
    int value = 0;
    int cond = 0;
    int then_id = 0;
    int else_id = 0;
};

struct GraphBuilder {
    int nextId = 1;
    std::vector<Node> nodes;
    std::map<std::string, int> inputNodes;

    int addNode(const Node& n) {
        Node node = n;
        node.id = nextId++;
        nodes.push_back(node);
        return node.id;
    }

    int getOrAddInput(const std::string& name) {
        auto it = inputNodes.find(name);
        if (it != inputNodes.end()) {
            return it->second;
        }
        Node n;
        n.kind = "input";
        n.name = name;
        int id = addNode(n);
        inputNodes[name] = id;
        return id;
    }
};

static bool isKernelName(StringRef name) {
    return name == "triple_deref" || name == "graph_walk" || name == "field_chain" ||
           name == "guarded_chain" || name == "alias_branch" || name == "mixed_fields" ||
           name == "add_two";
}

static Value* stripCasts(Value* v) {
    while (true) {
        if (auto* bc = dyn_cast<BitCastInst>(v)) {
            v = bc->getOperand(0);
            continue;
        }
        if (auto* ce = dyn_cast<ConstantExpr>(v)) {
            if (ce->getOpcode() == Instruction::BitCast) {
                v = ce->getOperand(0);
                continue;
            }
        }
        break;
    }
    return v;
}

static Value* stripGEP(Value* v) {
    v = stripCasts(v);
    if (auto* gep = dyn_cast<GetElementPtrInst>(v)) {
        return stripCasts(gep->getPointerOperand());
    }
    if (auto* ce = dyn_cast<ConstantExpr>(v)) {
        if (ce->getOpcode() == Instruction::GetElementPtr) {
            return stripCasts(ce->getOperand(0));
        }
    }
    return v;
}

static AllocaInst* getAlloca(Value* v) {
    v = stripGEP(v);
    if (auto* ai = dyn_cast<AllocaInst>(v)) {
        return ai;
    }
    return nullptr;
}

static std::string getConstString(Value* v) {
    v = stripCasts(v);

    if (auto* gep = dyn_cast<GetElementPtrInst>(v)) {
        v = gep->getPointerOperand();
    }

    if (auto* ce = dyn_cast<ConstantExpr>(v)) {
        if (ce->getOpcode() == Instruction::GetElementPtr) {
            v = ce->getOperand(0);
        }
    }

    if (auto* gv = dyn_cast<GlobalVariable>(v)) {
        if (auto* cda = dyn_cast<ConstantDataArray>(gv->getInitializer())) {
            if (cda->isString()) {
                std::string s = cda->getAsString().str();
                if (!s.empty() && s.back() == '\0') {
                    s.pop_back();
                }
                return s;
            }
        }
    }
    return "input";
}

static bool getConstInt(Value* v, int& out) {
    v = stripCasts(v);
    if (auto* ci = dyn_cast<ConstantInt>(v)) {
        out = (int)ci->getSExtValue();
        return true;
    }
    return false;
}

struct GuardedGraphPass : PassInfoMixin<GuardedGraphPass> {
    PreservedAnalyses run(Function& F, FunctionAnalysisManager&) {
        if (!isKernelName(F.getName())) {
            return PreservedAnalyses::all();
        }

        GraphBuilder builder;
        DenseMap<const Value*, int> valueToNode;
        DenseMap<const AllocaInst*, int> allocaToNode;

        auto resolveNode = [&](Value* v) -> int {
            v = stripCasts(v);
            if (auto* li = dyn_cast<LoadInst>(v)) {
                if (AllocaInst* ai = getAlloca(li->getPointerOperand())) {
                    auto it = allocaToNode.find(ai);
                    if (it != allocaToNode.end()) {
                        return it->second;
                    }
                }
            }
            auto it = valueToNode.find(v);
            if (it != valueToNode.end()) {
                return it->second;
            }
            return 0;
        };

        auto resolveEvalArg = [&](CallInst* CI, unsigned idx) -> int {
            if (idx >= CI->arg_size()) {
                return 0;
            }
            return resolveNode(CI->getArgOperand(idx));
        };

        auto addGuardedPtr = [&](int ptrNodeId) -> int {
            Node guardPtr;
            guardPtr.kind = "guard_ptr";
            guardPtr.x = ptrNodeId;
            int guardPtrId = builder.addNode(guardPtr);

            Node guardNonNull;
            guardNonNull.kind = "guard_nonnull";
            guardNonNull.x = guardPtrId;
            int guardNonNullId = builder.addNode(guardNonNull);

            return guardNonNullId;
        };

        for (BasicBlock& BB : F) {
            for (Instruction& I : BB) {
                if (auto* CI = dyn_cast<CallInst>(&I)) {
                    Function* callee = CI->getCalledFunction();
                    if (!callee) {
                        continue;
                    }
                    StringRef name = callee->getName();
                    Node n;
                    bool makeNode = true;

                    if (name == "ck_input") {
                        std::string inputName = getConstString(CI->getArgOperand(0));
                        int id = builder.getOrAddInput(inputName);
                        valueToNode[CI] = id;
                        continue;
                    } else if (name == "ck_const_int") {
                        int val = 0;
                        getConstInt(CI->getArgOperand(0), val);
                        n.kind = "const_int";
                        n.value = val;
                    } else if (name == "ck_const_null") {
                        n.kind = "const_null";
                    } else if (name == "ck_guard_nonnull") {
                        n.kind = "is_nonnull";
                        n.x = resolveEvalArg(CI, 0);
                    } else if (name == "ck_guard_eq") {
                        n.kind = "guard_eq";
                        n.x = resolveEvalArg(CI, 0);
                        n.y = resolveEvalArg(CI, 2);
                    } else if (name == "ck_load_ptr") {
                        n.kind = "load_ptr";
                        n.x = addGuardedPtr(resolveEvalArg(CI, 1));
                        n.field = 0;
                    } else if (name == "ck_load_int") {
                        n.kind = "load_int";
                        n.x = addGuardedPtr(resolveEvalArg(CI, 1));
                        n.field = 0;
                    } else if (name == "ck_getfield") {
                        int field = 0;
                        n.kind = "getfield";
                        n.x = addGuardedPtr(resolveEvalArg(CI, 1));
                        getConstInt(CI->getArgOperand(3), field);
                        n.field = field;
                    } else if (name == "ck_getfield_int") {
                        int field = 0;
                        n.kind = "getfield_int";
                        n.x = addGuardedPtr(resolveEvalArg(CI, 1));
                        getConstInt(CI->getArgOperand(3), field);
                        n.field = field;
                    } else if (name == "ck_select") {
                        n.kind = "select";
                        n.cond = resolveEvalArg(CI, 0);
                        n.then_id = resolveEvalArg(CI, 2);
                        n.else_id = resolveEvalArg(CI, 4);
                    } else if (name == "ck_add") {
                        n.kind = "add";
                        n.x = resolveEvalArg(CI, 0);
                        n.y = resolveEvalArg(CI, 2);
                    } else if (name.startswith("llvm.memcpy")) {
                        AllocaInst* dst = getAlloca(CI->getArgOperand(0));
                        AllocaInst* src = getAlloca(CI->getArgOperand(1));
                        if (dst && src) {
                            auto it = allocaToNode.find(src);
                            if (it != allocaToNode.end()) {
                                allocaToNode[dst] = it->second;
                            }
                        }
                        makeNode = false;
                    } else {
                        makeNode = false;
                    }

                    if (makeNode) {
                        int id = builder.addNode(n);
                        valueToNode[CI] = id;
                    }
                }

                if (auto* SI = dyn_cast<StoreInst>(&I)) {
                    Value* val = SI->getValueOperand();
                    Value* ptr = stripCasts(SI->getPointerOperand());
                    if (auto* AI = dyn_cast<AllocaInst>(ptr)) {
                        int id = resolveNode(val);
                        if (id) {
                            allocaToNode[AI] = id;
                        }
                    }
                }
            }
        }

        int outputId = 0;
        for (BasicBlock& BB : F) {
            if (auto* RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
                if (Value* rv = RI->getReturnValue()) {
                    outputId = resolveNode(rv);
                }
            }
        }

        std::string outDir = "out";
        if (const char* env = std::getenv("GRAPH_OUT_DIR")) {
            outDir = env;
        }
        sys::fs::create_directories(outDir);
        std::string path = (Twine(outDir) + "/" + F.getName() + ".json").str();
        std::error_code ec;
        raw_fd_ostream os(path, ec, sys::fs::OF_Text);
        if (ec) {
            errs() << "Failed to open output: " << ec.message() << "\n";
            return PreservedAnalyses::all();
        }

        std::vector<std::pair<int, int>> edges;
        for (const Node& n : builder.nodes) {
            if (n.kind == "guard_ptr" || n.kind == "guard_nonnull" || n.kind == "is_nonnull") {
                edges.emplace_back(n.x, n.id);
            } else if (n.kind == "guard_eq") {
                edges.emplace_back(n.x, n.id);
                edges.emplace_back(n.y, n.id);
            } else if (n.kind == "load_ptr" || n.kind == "load_int" ||
                       n.kind == "getfield" || n.kind == "getfield_int") {
                edges.emplace_back(n.x, n.id);
            } else if (n.kind == "select") {
                edges.emplace_back(n.cond, n.id);
                edges.emplace_back(n.then_id, n.id);
                edges.emplace_back(n.else_id, n.id);
            } else if (n.kind == "add") {
                edges.emplace_back(n.x, n.id);
                edges.emplace_back(n.y, n.id);
            }
        }

        os << "{\n";
        os << "  \"function\": \"" << F.getName() << "\",\n";
        os << "  \"nodes\": [\n";
        for (size_t i = 0; i < builder.nodes.size(); ++i) {
            const Node& n = builder.nodes[i];
            os << "    {\"id\":" << n.id << ",\"kind\":\"" << n.kind << "\"";
            if (!n.name.empty()) {
                os << ",\"name\":\"" << n.name << "\"";
            }
            if (n.x) {
                os << ",\"x\":" << n.x;
            }
            if (n.y) {
                os << ",\"y\":" << n.y;
            }
            if (n.field) {
                os << ",\"field\":" << n.field;
            }
            if (n.value || n.kind == "const_int") {
                os << ",\"value\":" << n.value;
            }
            if (n.cond) {
                os << ",\"cond\":" << n.cond;
            }
            if (n.then_id) {
                os << ",\"then\":" << n.then_id;
            }
            if (n.else_id) {
                os << ",\"else\":" << n.else_id;
            }
            os << "}";
            if (i + 1 < builder.nodes.size()) {
                os << ",";
            }
            os << "\n";
        }
        os << "  ],\n";
        os << "  \"edges\": [";
        for (size_t i = 0; i < edges.size(); ++i) {
            os << "[" << edges[i].first << "," << edges[i].second << "]";
            if (i + 1 < edges.size()) {
                os << ",";
            }
        }
        os << "],\n";
        os << "  \"output\": " << outputId << "\n";
        os << "}\n";

        return PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "GuardedGraphPass", "0.1",
            [](PassBuilder& PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager& FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "guarded-graph") {
                            FPM.addPass(GuardedGraphPass());
                            return true;
                        }
                        return false;
                    });
            }};
}
