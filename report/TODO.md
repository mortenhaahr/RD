- Write somewhere (not necessarily in this file) other attempts of using LibTooling for source-code generation
- Write something about the issues with C-style to std::array:
  - Since the functionality of std::array is a subset of the functionality of C-style arrays there are cases where transformation is impossible.
- Provide text for LLVM, Clang and LibTooling figures in project description
- Write something about how our techniques easily can be applied for static analysis as well
  - Instead of generating code simply report it e.g. as a warning
- Write about the Clang AST and how it is different since it closely ressembles C++ code - see https://clang.llvm.org/docs/IntroductionToTheClangAST.html
- Rename Tests to Results and provide results on running tools on existing code bases
- Write about these deterministic tools compared to probabilistic tools (e.g. LLM)
- Perhaps mention in discussion/future work that we could investigate the third way of implementing the enum2string tool
- CStyle == C-style
- We forgot to consider following scenario:

```cpp
enum class Desserts {
  Cake
};
void to_string(Desserts e){

}
namespace ns {
  void to_string(Desserts e){

  }
}
```
  - Mikkel's tool will overwrite both methods
  - Morten's tool will overwrite the first occasion (global namespace to_string)
- Write about how we wrote our own `ClangTool` implementations
- List of abbreviations
- Consistency with subsection, subsection*, etc.
- Consistency with cppinline, ``code'', etc.
- Enum-to-string tool cannot handle when definition is seperated from declaration
- Consistency with how we refer to LibTooling classes. E.g., `parmVarDecl` is a matcher, `ParmVarDecl` is a class.
  - We should also have a description of this
- In the parmVar discussion: Talk about how the rewriting of functions where CStyle array was previously used is quite skethcy. Especially, if the function is implemented in a seperate compilation unit. A better approach would've been to change to `func(arr.data())` and then one could write a seperate tool if function refactoring was needed.
- In parmVarDecl discussion how we break when function takes incomplete array types (e.g. variable array sizes).
- In parmVarDecl discussion write about the annoying example arr[4][5] and how C++ is broken since [5] is enforced but [4] is not, since [4] decays to ptr but [5] doesn't. We made it so that both are enforced.
- In parmVarDecl discussion write about how we broke function calls with ConstantArrayTypes where the passed array is bigger than the function indicates.
- More citations
- (Perhaps) write something about: Purpose of the refactoring tool is to strengthen the safety guarantees the structure provides.