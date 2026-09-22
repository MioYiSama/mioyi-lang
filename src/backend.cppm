module;

#include <memory>
#include <optional>
#include <string>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

export module mioyi.compiler.backend;

namespace {

llvm::OptimizationLevel optimizationLevel(unsigned level) {
  switch (level) {
  case 0: return llvm::OptimizationLevel::O0;
  case 1: return llvm::OptimizationLevel::O1;
  case 2: return llvm::OptimizationLevel::O2;
  default: return llvm::OptimizationLevel::O3;
  }
}

std::unique_ptr<llvm::TargetMachine> createNativeTargetMachine(unsigned level) {
  if (llvm::InitializeNativeTarget() ||
      llvm::InitializeNativeTargetAsmPrinter()) {
    llvm::errs() << "error: failed to initialize the native LLVM target\n";
    return nullptr;
  }
  llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
  std::string targetError;
  const llvm::Target *target =
      llvm::TargetRegistry::lookupTarget(triple, targetError);
  if (!target) {
    llvm::errs() << "error: " << targetError << '\n';
    return nullptr;
  }
  const auto codegenLevel = static_cast<llvm::CodeGenOptLevel>(level);
  const std::string cpu = llvm::sys::getHostCPUName().str();
  auto machine = std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(
      triple, cpu, "", llvm::TargetOptions{}, std::nullopt, std::nullopt,
      codegenLevel));
  if (!machine)
    llvm::errs() << "error: failed to create target machine for '"
                 << triple.str() << "'\n";
  return machine;
}

void optimizeModule(llvm::Module &module, llvm::TargetMachine &machine,
                    unsigned level) {
  llvm::LoopAnalysisManager loops;
  llvm::FunctionAnalysisManager functions;
  llvm::CGSCCAnalysisManager cgscc;
  llvm::ModuleAnalysisManager modules;
  llvm::PassBuilder passes(&machine);
  passes.registerModuleAnalyses(modules);
  passes.registerCGSCCAnalyses(cgscc);
  passes.registerFunctionAnalyses(functions);
  passes.registerLoopAnalyses(loops);
  passes.crossRegisterProxies(loops, functions, cgscc, modules);
  const auto optimization = optimizationLevel(level);
  llvm::ModulePassManager pipeline = optimization == llvm::OptimizationLevel::O0
      ? passes.buildO0DefaultPipeline(optimization)
      : passes.buildPerModuleDefaultPipeline(optimization);
  pipeline.run(module, modules);
}

std::unique_ptr<llvm::TargetMachine>
prepareModule(llvm::Module &module, unsigned level) {
  auto machine = createNativeTargetMachine(level);
  if (!machine) return nullptr;
  module.setTargetTriple(machine->getTargetTriple());
  module.setDataLayout(machine->createDataLayout());
  if (llvm::verifyModule(module, &llvm::errs())) return nullptr;
  optimizeModule(module, *machine, level);
  if (llvm::verifyModule(module, &llvm::errs())) return nullptr;
  return machine;
}

} // namespace

export bool prepareNativeIR(llvm::Module &module, unsigned optimization) {
  return prepareModule(module, optimization) != nullptr;
}

export bool emitNativeObject(llvm::Module &module, unsigned optimization,
                             llvm::raw_pwrite_stream &output) {
  auto machine = prepareModule(module, optimization);
  if (!machine) return false;
  llvm::legacy::PassManager passes;
  if (machine->addPassesToEmitFile(passes, output, nullptr,
                                   llvm::CodeGenFileType::ObjectFile)) {
    llvm::errs() << "error: target does not support object-file emission\n";
    return false;
  }
  passes.run(module);
  return true;
}
