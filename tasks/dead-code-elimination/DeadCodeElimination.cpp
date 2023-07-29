#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <llvm/IR/Instructions.h>

#include <iostream>
#include <llvm/IR/Value.h>

#define DEBUG_TYPE "remove-dead-code"

using namespace llvm;

namespace {
struct DeadCodeEliminationPass : PassInfoMixin<DeadCodeEliminationPass> {
	bool is_needed(Instruction& i) const
	{
		return i.isTerminator() || i.getOpcode() == Instruction::Call;
	}

	SmallVector<Instruction*> find_dead_instructions(Function& F)
	{
		SmallVector<Value*> outset;
		SmallVector<Value*> inset;
		SmallVector<Instruction*> dead;

		for (auto& block : F) {
			for (auto i = block.rbegin(); i != std::rend(block); ++i) {
				auto& inst = *i;

				/* Handle branches which are not needed */
				if (isa<BranchInst>(inst)) {
					auto br = dyn_cast<BranchInst>(&inst);

					auto suc1 = br->getSuccessor(0);

					if (br->isConditional() && suc1 == br->getSuccessor(1)) {
						dead.push_back(&inst);
					}

					// continue;
				}

				/* Find out if instruction is used somewhere */
				if (!is_needed(inst) && std::find(inset.begin(), inset.end(), dyn_cast<Value>(&*i)) == inset.end()) {
					dead.push_back(&inst);
					// continue;
				}

				/* Save instruction IN args */
				for (unsigned i = 0; i < inst.getNumOperands(); ++i) {
					auto op = inst.getOperand(i);
					inset.push_back(op);
				}

				/* Save instruction OUT set */
				outset.push_back(dyn_cast<Value>(&inst));
			}
		}

		return std::move(dead);
	}

	SmallVector<BasicBlock*> find_dead_blocks(Function& F)
	{
		SmallVector<BasicBlock*> dead;

		for (auto& block : F) {
			if (block.size() == 1) {
				auto ter = dyn_cast<BranchInst>(block.getTerminator());
				if (!ter)
					continue;

				if (ter->isUnconditional()) {
					dead.push_back(&block);

					/* Change all usage with successor of unconditional branch */
					dyn_cast<Value>(&block)->replaceAllUsesWith(dyn_cast<Value>(ter->getSuccessor(0)));
				}
			} else if (block.size() == 0) {
				dead.push_back(&block);
			}
		}

		return std::move(dead);
	}

	PreservedAnalyses run(Function& F, FunctionAnalysisManager& AM)
	{
		errs() << "Running DeadCodeEliminationPass on function " << F.getName() << "\n";

		bool changed;

		do {
			changed = false;

			auto dead_block = find_dead_blocks(F);

			for (auto& i : dead_block) {
				i->dropAllReferences();
				i->eraseFromParent();
			}

			auto dead_inst = find_dead_instructions(F);

			for (auto inst = dead_inst.rbegin(); inst != std::rend(dead_inst); ++inst) {
				auto& i = *inst;

				if (isa<BranchInst>(i)) {
					auto br = dyn_cast<BranchInst>(i);
					auto bb = br->getParent();
					auto suc = br->getSuccessor(0);

					if (br == bb->getTerminator()) {
						SmallVector<Instruction*> inst;

						for (auto n = bb->rbegin(); n != std::rend(*bb); n++) {
							if (&*n != i)
								inst.push_back(&*n);
						}

						for (auto& n : inst) {
							n->moveBefore(&suc->front());
						}
					}
				}

				i->dropAllReferences();
				i->eraseFromParent();
			}

			SmallVector<BasicBlock *> dead_blocks;

			changed = !!dead_inst.size() || !!dead_block.size();
		} while (changed);

		return PreservedAnalyses::none();
	}
};
} // namespace

/// Registration
PassPluginLibraryInfo getPassPluginInfo()
{
	const auto callback = [](PassBuilder& PB) {
		PB.registerPipelineParsingCallback(
		    [](StringRef Name, FunctionPassManager& FPM, auto) {
			    if (Name == "dead-code-elimination") {
				    FPM.addPass(DeadCodeEliminationPass());
				    return true;
			    }
			    return false;
		    });
	};
	return { LLVM_PLUGIN_API_VERSION, "DeadCodeEliminationPass",
		LLVM_VERSION_STRING, callback };
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
{
	return getPassPluginInfo();
}

#undef DEBUG_TYPE
