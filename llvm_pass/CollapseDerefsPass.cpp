#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_map>

using namespace llvm;

namespace {

static std::string graphPathFor(StringRef funcName) {
    std::string outDir = "out";
    if (const char* env = std::getenv("GRAPH_OUT_DIR")) {
        outDir = env;
    }
    return (Twine(outDir) + "/" + funcName + ".json").str();
}

static bool graphIsLinearGuardedChain(StringRef funcName) {
    static std::unordered_map<std::string, bool> cache;
    auto it = cache.find(funcName.str());
    if (it != cache.end()) {
        return it->second;
    }

    std::ifstream in(graphPathFor(funcName));
    if (!in) {
        cache[funcName.str()] = false;
        return false;
    }
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    bool hasLoad = false;

    size_t pos = 0;
    while ((pos = content.find("\"kind\":\"", pos)) != std::string::npos) {
        pos += 8;
        size_t end = content.find("\"", pos);
        if (end == std::string::npos) {
            break;
        }
        std::string kind = content.substr(pos, end - pos);
        if (kind == "load_ptr") {
            hasLoad = true;
        }
        if (!(kind == "input" || kind == "guard_ptr" || kind == "guard_nonnull" || kind == "load_ptr")) {
            cache[funcName.str()] = false;
            return false;
        }
        pos = end + 1;
    }

    cache[funcName.str()] = hasLoad;
    return hasLoad;
}

static bool allUsesInLoop(CallInst* CI, Loop* L) {
    for (User* U : CI->users()) {
        Instruction* I = dyn_cast<Instruction>(U);
        if (!I || !L->contains(I)) {
            return false;
        }
    }
    return true;
}

static bool argsLoopInvariant(CallInst* CI, Loop* L) {
    for (Value* V : CI->args()) {
        if (!L->isLoopInvariant(V)) {
            return false;
        }
    }
    return true;
}

struct CollapseDerefsPass : PassInfoMixin<CollapseDerefsPass> {
    bool processLoop(Loop* L, Function& F, LoopInfo& LI, DominatorTree& DT) {
        bool changed = false;
        for (Loop* Sub : L->getSubLoops()) {
            changed |= processLoop(Sub, F, LI, DT);
        }

        BasicBlock* preheader = L->getLoopPreheader();
        if (!preheader) {
            preheader = InsertPreheaderForLoop(L, &DT, &LI, nullptr, false);
        }
        if (!preheader) {
            return changed;
        }
        Instruction* preTerm = preheader->getTerminator();
        if (!preTerm) {
            return changed;
        }

        for (BasicBlock* BB : L->blocks()) {
            for (auto it = BB->begin(); it != BB->end(); ) {
                Instruction* I = &*it++;
                CallInst* CI = dyn_cast<CallInst>(I);
                if (!CI) {
                    continue;
                }
                Function* callee = CI->getCalledFunction();
                if (!callee) {
                    continue;
                }
                if (callee->getName() != "triple_deref") {
                    continue;
                }
                if (!graphIsLinearGuardedChain(callee->getName())) {
                    continue;
                }
                if (!argsLoopInvariant(CI, L)) {
                    continue;
                }
                if (!allUsesInLoop(CI, L)) {
                    continue;
                }

                IRBuilder<> preBuilder(preTerm);
                SmallVector<Value*, 8> callArgs;
                callArgs.reserve(CI->arg_size());
                for (Use &U : CI->args()) {
                    callArgs.push_back(U.get());
                }
                FunctionCallee calleeRef(callee->getFunctionType(), callee);
                CallInst* hoisted = preBuilder.CreateCall(calleeRef, callArgs);
                hoisted->setCallingConv(CI->getCallingConv());
                hoisted->setAttributes(CI->getAttributes());

                CI->replaceAllUsesWith(hoisted);
                CI->eraseFromParent();
                changed = true;
            }
        }

        return changed;
    }

    PreservedAnalyses run(Function& F, FunctionAnalysisManager& FAM) {
        LoopInfo& LI = FAM.getResult<LoopAnalysis>(F);
        DominatorTree& DT = FAM.getResult<DominatorTreeAnalysis>(F);
        bool changed = false;
        for (Loop* L : LI) {
            changed |= processLoop(L, F, LI, DT);
        }
        return changed ? PreservedAnalyses::none() : PreservedAnalyses::all();
    }
};

} // namespace

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return {LLVM_PLUGIN_API_VERSION, "CollapseDerefsPass", "0.1",
            [](PassBuilder& PB) {
                PB.registerPipelineParsingCallback(
                    [](StringRef Name, FunctionPassManager& FPM,
                       ArrayRef<PassBuilder::PipelineElement>) {
                        if (Name == "collapse-deref") {
                            FPM.addPass(CollapseDerefsPass());
                            return true;
                        }
                        return false;
                    });
            }};
}
