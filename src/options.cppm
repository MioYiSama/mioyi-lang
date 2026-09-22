module;

#include <filesystem>
#include <string>

#include <CLI/CLI.hpp>

export module mioyi.options;

export enum class Command { Compile, Ast };

export struct CompilerOptions {
  Command command = Command::Compile;
  std::string input;
  std::string output;
  std::string astFormat;
  unsigned optimization = 2;
  bool compileOnly = false;
  bool emitLLVM = false;
  bool exitAfterParsing = false;
};

namespace {

void finalizeOptions(CompilerOptions &options,
                     const std::string &positionalOutput) {
  if (options.command == Command::Ast) {
    if (options.output.empty())
      options.output = "-";
    if (options.astFormat.empty()) {
      const auto extension = std::filesystem::path(options.output).extension();
      options.astFormat =
          extension == ".yaml" || extension == ".yml" ? "yaml" : "json";
    }
    return;
  }
  if (!positionalOutput.empty())
    options.output = positionalOutput;
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
      if (path == std::filesystem::path(options.input))
        path += ".out";
#endif
      options.output = path.string();
    }
  }
}

} // namespace

export int parseOptions(int argc, char **argv, CompilerOptions &options) {
  CLI::App app{"Compile and link a Mioyi program"};
  std::string positionalOutput;
  app.add_option("input", options.input,
                 "Input .mioyi source (stdin if omitted)");
  auto *positionalOutputOption = app.add_option(
      "output", positionalOutput, "Output file (legacy positional form)");
  auto *outputOption = app.add_option("-o", options.output, "Output file");
  outputOption->excludes(positionalOutputOption);
  app.add_flag("-c", options.compileOnly,
               "Emit an object file without linking");
  app.add_flag("-S,--emit-llvm", options.emitLLVM, "Emit LLVM IR");
  app.add_option("-O", options.optimization, "Optimization level")
      ->check(CLI::Range(0u, 3u))
      ->default_str("2");

  auto *ast = app.add_subcommand("ast", "Serialize the parsed AST");
  ast->callback([&options] { options.command = Command::Ast; });
  ast->add_option("input", options.input,
                  "Input .mioyi source (stdin if omitted)");
  ast->add_option("-o,--output", options.output,
                  "Output file (stdout if omitted)");
  ast->add_option("-f,--format", options.astFormat, "Output format")
      ->check(CLI::IsMember({"json", "yaml"}));
  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &error) {
    options.exitAfterParsing = true;
    return app.exit(error);
  }
  finalizeOptions(options, positionalOutput);
  return 0;
}
