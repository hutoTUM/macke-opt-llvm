#include <string>
#include <vector>
#include "FunctionDeclarations.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

namespace {

static llvm::cl::opt<std::string> EncapsulatedFunction(
    "encapsulatedfunction",
    llvm::cl::desc("Name of the function to be encapsulated"));


struct EncapsulateSymbolic : public llvm::ModulePass {
  static char ID;  // uninitialized ID is needed for pass registration

  EncapsulateSymbolic() : llvm::ModulePass(ID) {
    /* empty constructor, just call the parent's one */
  }

  bool runOnModule(llvm::Module& M) {
    if (EncapsulatedFunction.empty()) {
      llvm::errs() << "Error: -encapsulatedfunction paramter is needed!"
                   << '\n';
      return false;
    }

    // Look for the function to be encapsulated
    llvm::Function* toencapsulate = M.getFunction(EncapsulatedFunction);

    // Check if the function given by the user really exists
    if (toencapsulate == nullptr) {
      llvm::errs() << "Error: " << EncapsulatedFunction
                   << " is no function inside the module. " << '\n'
                   << "Encapsulation is not possible!" << '\n';
      return false;
    }

    // Look for an old main method
    llvm::Function* oldmain = M.getFunction("main");

    if (oldmain != nullptr) {
      // If an old main exist, rename it ...
      oldmain->setName("__main_old");
    }

    // Get builder for the whole module
    llvm::IRBuilder<> modulebuilder(M.getContext());
    // Datalayout for size calculation
    llvm::DataLayout datalayout(&M);

    // Register relevant functions
    llvm::Function* kleemakesym = declare_klee_make_symbolic(&M);
    llvm::Function* mymalloc = declare_malloc(&M);
    llvm::Function* myfree = declare_free(&M);

    // Create the encapsulation

    // The capsule is "void main()"
    llvm::Constant* cm = M.getOrInsertFunction(
        "main", llvm::FunctionType::get(modulebuilder.getVoidTy(), false));
    llvm::Function* newmain = llvm::cast<llvm::Function>(cm);
    newmain->setCallingConv(llvm::CallingConv::C);

    // Create two new basic blocks inside the new main
    llvm::BasicBlock* block =
        llvm::BasicBlock::Create(M.getContext(), "", newmain);
    llvm::IRBuilder<> builder(block);

    std::vector<llvm::Value*> newargs = {};
    std::vector<llvm::Instruction*> mallocs = {};

    // For all arguments of the function we want to encapsulate
    for (auto& argument : toencapsulate->getArgumentList()) {
      if (argument.getType()->isPointerTy()) {
        // Allocate new storage
        llvm::Instruction* malloc = builder.CreateCall(
            mymalloc, (llvm::Triple(M.getTargetTriple()).isArch64Bit())
                          ? builder.getInt64(datalayout.getTypeAllocSize(
                                argument.getType()->getPointerElementType()))
                          : builder.getInt32(datalayout.getTypeAllocSize(
                                argument.getType()->getPointerElementType())));

        // Register the malloc for later call to free
        mallocs.push_back(malloc);

        // Add a call to make_klee_symbolic for the new variable
        builder.CreateCall(
            kleemakesym,
            llvm::ArrayRef<llvm::Value*>{std::vector<llvm::Value*>{
                malloc, (llvm::Triple(M.getTargetTriple()).isArch64Bit())
                            ? builder.getInt64(datalayout.getTypeAllocSize(
                                  argument.getType()->getPointerElementType()))
                            : builder.getInt32(datalayout.getTypeAllocSize(
                                  argument.getType()->getPointerElementType())),
                builder.CreateGlobalStringPtr(
                    argument.getValueName()->first())}});

        newargs.push_back(builder.CreateBitCast(malloc, argument.getType()));

      } else {
        // Allocate new storage
        llvm::AllocaInst* alloc = builder.CreateAlloca(
            argument.getType(), 0, argument.getValueName()->first());

        // Add a call to make_klee_symbolic for the new variable
        builder.CreateCall(
            kleemakesym, llvm::ArrayRef<llvm::Value*>{std::vector<llvm::Value*>{
                             builder.CreateBitCast(
                                 alloc, builder.getInt8Ty()->getPointerTo()),
                             (llvm::Triple(M.getTargetTriple()).isArch64Bit())
                                 ? builder.getInt64(datalayout.getTypeAllocSize(
                                       argument.getType()))
                                 : builder.getInt32(datalayout.getTypeAllocSize(
                                       argument.getType())),
                             builder.CreateGlobalStringPtr(
                                 argument.getValueName()->first())}});

        // And store a load of the variable for latter call
        newargs.push_back(builder.CreateLoad(alloc));
      }
    }

    // Call our encapsulated function with the newly generated arguments
    builder.CreateCall(toencapsulate, llvm::ArrayRef<llvm::Value*>(newargs));

    // Free all previous malloc
    for (auto& malloc : mallocs) {
      builder.CreateCall(myfree, malloc);
    }

    // The new main has void return type
    builder.CreateRetVoid();

    return true;  // This module was modified
  }
};

}  // namespace

// Finally register the new pass
char EncapsulateSymbolic::ID = 0;
static llvm::RegisterPass<EncapsulateSymbolic> X(
    "encapsulatesymbolic",
    "Add a new main that calls a function with symbolic arguments",
    true,  // walks CFG without modifying it
    true   // is only analysis
    );
