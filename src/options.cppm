module;

#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

export module mioyi.options;

export struct CompilerOptions {
  std::string input;
  std::string output;
  std::string target = "native";
  unsigned optimization = 2;
  bool emitLLVM = false;
};

namespace {

void finalizeOptions(CompilerOptions &options,
                     const std::string &positionalOutput) {
  if (!positionalOutput.empty()) options.output = positionalOutput;
  if (!options.output.empty() &&
      std::filesystem::path(options.output).extension() == ".ll")
    options.emitLLVM = true;
  if (options.output.empty()) {
    if (options.emitLLVM) {
      options.output = "-";
    } else if (options.input.empty()) {
      options.output = "a.o";
    } else {
      auto path = std::filesystem::path(options.input);
      path.replace_extension(".o");
      options.output = path.string();
    }
  }
}

} // namespace

export int parseOptions(int argc, char **argv, CompilerOptions &options) {
  CLI::App app{"Compile SysY source to LLVM IR or an object file"};
  std::string positionalOutput;
  app.add_option("input", options.input, "Input SysY source (stdin if omitted)");
  auto *positionalOutputOption = app.add_option(
      "output", positionalOutput, "Output file (legacy positional form)");
  auto *outputOption = app.add_option("-o", options.output, "Output file");
  outputOption->excludes(positionalOutputOption);
  app.add_flag("-c", "Emit an object file");
  app.add_flag("-S,--emit-llvm", options.emitLLVM, "Emit LLVM IR");
  app.add_option("-O", options.optimization, "Optimization level")
      ->check(CLI::Range(0u, 3u))
      ->default_str("2");
  app.add_option("--target", options.target, "Target architecture")
      ->check(CLI::IsMember({"native", "x86_64"}))
      ->default_str("native");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    return app.exit(error);
  }
  finalizeOptions(options, positionalOutput);
  return 0;
}
