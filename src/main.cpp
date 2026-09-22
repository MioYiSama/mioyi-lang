import mioyi.ast;
import mioyi.compiler;
import mioyi.options;

int main(int argc, char **argv) {
  CompilerOptions options;
  const int result = parseOptions(argc, argv, options);
  if (result != 0 || options.exitAfterParsing)
    return result;
  if (options.command == Command::Ast) return dumpAst(options);
  return compile(options);
}
