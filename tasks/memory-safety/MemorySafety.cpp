#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/PassManager.h>
#include <llvm/IR/Value.h>
#include <llvm/Transforms/Utils/LoopUtils.h>

#include <vector>

#define DEBUG_TYPE "memory-safety"

using namespace llvm;

namespace {
	struct MemorySafetyPass : PassInfoMixin<MemorySafetyPass> {
		PreservedAnalyses run(Module& F, ModuleAnalysisManager& AM)
		{
			IRBuilder<> builder(&(*F.begin()).front());
			const DataLayout &data = (*F.begin()).getParent()->getDataLayout();
			auto init = F.getOrInsertFunction("__runtime_init",
					builder.getVoidTy()
					);
			auto Fun = dyn_cast<Function>(init.getCallee());

			appendToGlobalCtors(F, Fun, 0);

			for (auto &i: F)
				run_on_function(i, builder, data, F);

			return PreservedAnalyses::all();
		}

		template<typename T>
			void insert_callback_store_load(T &target, const DataLayout &data, IRBuilder<> &builder, FunctionCallee &check, bool is_store)
			{
				std::vector<Value *> args;
				uint64_t size = data.getTypeStoreSize(getLoadStoreType(&target));

				/* ptr */
				args.push_back(target.getPointerOperand());

				/* size */
				args.push_back(builder.getInt32(size));

				/* load */
				args.push_back(builder.getInt8(is_store));

				Value *ret = builder.CreateCall(check, args);
				dyn_cast<Instruction>(ret)->moveBefore(&target);
			}

		void insert_redzone(AllocaInst &alloca, const DataLayout &data, IRBuilder<> &builder, FunctionCallee &poison)
		{
			size_t size_of_alloc = alloca.getAllocationSize(data).value();
			std::vector<Value *> args;

			// Create 32-byte poison right above allocation
			auto high_redzone = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(32));
			dyn_cast<Instruction>(high_redzone)->moveBefore(&alloca);

			high_redzone->setAlignment(Align(32));

			// Create at least 32-byte poison right under allocation. Align to 32
			auto low_redzone = builder.CreateAlloca(builder.getInt8Ty(), builder.getInt32(32 - size_of_alloc % 32 + 32));
			dyn_cast<Instruction>(low_redzone)->moveAfter(&alloca);

			// Unpoison allocated space
			args.push_back(&alloca);
			args.push_back(builder.getInt32(size_of_alloc));

			Value *ret = builder.CreateCall(poison, args);
			dyn_cast<Instruction>(ret)->moveAfter(dyn_cast<Instruction>(low_redzone));

			// Set align to 32
			alloca.setAlignment(Align(32));
		}

		void run_on_function(Function& F, IRBuilder<> &builder, const DataLayout &data, Module &mod)
		{
			/* Do not call on ourself */
			if (F.getName().find("__runtime") != std::string::npos)
				return;

			errs() << "Running MemorySafetyPass on function " << F.getName() << "\n";

			auto check = mod.getOrInsertFunction("__runtime_check_addr",
					builder.getVoidTy(),
					builder.getPtrTy(),
					builder.getInt32Ty(),
					builder.getInt8Ty());

			auto stack_poison = mod.getOrInsertFunction("__runtime_poison",
					builder.getVoidTy(),
					builder.getPtrTy(),
					builder.getInt32Ty());

			auto malloc_wrapper = mod.getOrInsertFunction("__runtime_malloc",
					builder.getPtrTy(),
					builder.getInt32Ty());

			auto free_wrapper = mod.getOrInsertFunction("__runtime_free",
					builder.getVoidTy(),
					builder.getPtrTy());

			auto malloc = mod.getFunction("malloc");
			if (malloc) {
				malloc->replaceAllUsesWith(mod.getFunction("__runtime_malloc"));
			}

			auto free = mod.getFunction("free");
			if (free) {
				free->replaceAllUsesWith(mod.getFunction("__runtime_free"));
			}

			for (auto &bb: F) {
				for (auto i = bb.begin(); i != bb.end(); ++i) {
					auto &inst = *i;

					if (inst.getOpcode() == Instruction::Store) {
						auto store = dyn_cast<StoreInst>(&inst);
						insert_callback_store_load(*store, data, builder, check, 1);
					} else if (inst.getOpcode() == Instruction::Load) {
						auto load = dyn_cast<LoadInst>(&inst);
						insert_callback_store_load(*load, data, builder, check, 0);
					} else if (inst.getOpcode() == Instruction::Alloca) {
						auto alloca = dyn_cast<AllocaInst>(&inst);
						insert_redzone(*alloca, data, builder, stack_poison);

						// skip redzone
						i++; i++;
					}
				}
			}

			errs() << F << "\n";
		}

		// do not skip this pass for functions annotated with optnone
		static bool isRequired() { return true; }
	};
} // namespace

/// Registration
PassPluginLibraryInfo getPassPluginInfo()
{
	const auto callback = [](PassBuilder& PB) {
		PB.registerPipelineParsingCallback(
				[](StringRef Name, ModulePassManager& FPM, auto) {
				if (Name == "memory-safety") {
				FPM.addPass(MemorySafetyPass());
				return true;
				}
				return false;
				});
	};
	return { LLVM_PLUGIN_API_VERSION, "MemorySafetyPass",
		LLVM_VERSION_STRING, callback };
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
{
	return getPassPluginInfo();
}

#undef DEBUG_TYPE
