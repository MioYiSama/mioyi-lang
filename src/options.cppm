module;

#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

export module mioyi.options;

export struct CompilerOptions {
  std::string input;
  std::string output;
  unsigned optimization = 2;
  bool compileOnly = false;
  bool emitLLVM = false;
  bool exitAfterParsing = false;
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
    } else if (options.compileOnly) {
      if (options.input.empty()) {
        options.output = "a.o";
      } else {
        auto path = std::filesystem::path(options.input);
        path.replace_extension(".o");
        options.output = path.string();
      }
    } else if (options.input.empty()) {
      options.output =
#ifdef _WIN32
          "a.exe";
#else
          "a.out";
#endif
    } else {
      auto path = std::filesystem::path(options.input);
#ifdef _WIN32
      path.replace_extension(".exe");
#else
      path.replace_extension();
      if (path == std::filesystem::path(options.input)) path += ".out";
#endif
      options.output = path.string();
    }
  }
}

} // namespace

export int parseOptions(int argc, char **argv, CompilerOptions &options) {
  CLI::App app{"Compile and link a SysY program"};
  std::string positionalOutput;
  app.add_option("input", options.input, "Input SysY source (stdin if omitted)");
  auto *positionalOutputOption = app.add_option(
      "output", positionalOutput, "Output file (legacy positional form)");
  auto *outputOption = app.add_option("-o", options.output, "Output file");
  outputOption->excludes(positionalOutputOption);
  app.add_flag("-c", options.compileOnly, "Emit an object file without linking");
  app.add_flag("-S,--emit-llvm", options.emitLLVM, "Emit LLVM IR");
  app.add_option("-O", options.optimization, "Optimization level")
      ->check(CLI::Range(0u, 3u))
      ->default_str("2");
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    options.exitAfterParsing = true;
    return app.exit(error);
  }
  finalizeOptions(options, positionalOutput);
  return 0;
}
