module;

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/TargetParser/Host.h>

export module mioyi.optimizer;

namespace {

llvm::OptimizationLevel optimizationLevel(unsigned level) {
  switch (level) {
  case 0:
    return llvm::OptimizationLevel::O0;
  case 1:
    return llvm::OptimizationLevel::O1;
  case 2:
    return llvm::OptimizationLevel::O2;
  default:
    return llvm::OptimizationLevel::O3;
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
  auto machine = std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, cpu, "", llvm::TargetOptions{},
                                  std::nullopt, std::nullopt, codegenLevel));
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
  llvm::ModulePassManager pipeline =
      optimization == llvm::OptimizationLevel::O0
          ? passes.buildO0DefaultPipeline(optimization)
          : passes.buildPerModuleDefaultPipeline(optimization);
  pipeline.run(module, modules);
}

std::unique_ptr<llvm::TargetMachine> prepareModule(llvm::Module &module,
                                                   unsigned level) {
  auto machine = createNativeTargetMachine(level);
  if (!machine)
    return nullptr;
  module.setTargetTriple(machine->getTargetTriple());
  module.setDataLayout(machine->createDataLayout());
  if (llvm::verifyModule(module, &llvm::errs()))
    return nullptr;
  optimizeModule(module, *machine, level);
  if (llvm::verifyModule(module, &llvm::errs()))
    return nullptr;
  return machine;
}

} // namespace

export namespace mioyi::optimizer {

class Options {
public:
  std::string input;
  std::string output;
  unsigned optimization = 2;
};

using Result = std::expected<void, std::string>;

bool prepareNativeIR(llvm::Module &module, unsigned optimization) {
  return prepareModule(module, optimization) != nullptr;
}

Result optimize(Options options) {
  llvm::LLVMContext context;
  llvm::SMDiagnostic diagnostic;
  const std::string input = options.input.empty() ? "-" : options.input;
  auto module = llvm::parseIRFile(input, diagnostic, context);
  if (!module) {
    diagnostic.print("mioyi-lang optimize", llvm::errs());
    return std::unexpected("failed to read LLVM IR");
  }
  if (!prepareNativeIR(*module, options.optimization))
    return std::unexpected("failed to optimize LLVM IR");

  if (options.output == "-") {
    module->print(llvm::outs(), nullptr);
    return {};
  }
  std::error_code error;
  llvm::raw_fd_ostream output(options.output, error, llvm::sys::fs::OF_None);
  if (error)
    return std::unexpected("cannot open output file '" + options.output +
                           "': " + error.message());
  module->print(output, nullptr);
  return {};
}

} // namespace mioyi::optimizer
