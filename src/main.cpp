import mioyi.compiler;
import mioyi.options;

int main(int argc, char **argv) {
  CompilerOptions options;
  if (const int result = parseOptions(argc, argv, options); result != 0)
    return result;
  return compile(options);
}
