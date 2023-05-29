- Write somewhere (not necessarily in this file) other attempts of using LibTooling for source-code generation
- Write something about how our techniques easily can be applied for static analysis as well
  - Instead of generating code simply report it e.g. as a warning
  - Write it somewhere as a sidecomment
- Write about these deterministic tools compared to probabilistic tools (e.g. LLM)
- Perhaps mention in discussion/future work that we could investigate the third way of implementing the enum2string tool
- We forgot to consider following scenario - discussion enum-to-string:

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
- Consistency with subsection, subsection*, etc.
- Consistency with cppinline, ``code'', etc.
- Enum-to-string tool cannot handle when definition is seperated from declaration - discussion/future work
- Consistency with how we refer to LibTooling classes. E.g., `parmVarDecl` is a matcher, `ParmVarDecl` is a class.
  - We should also have a description of this
- In the parmVar discussion: Talk about how the rewriting of functions where CStyle array was previously used is quite skethcy. Especially, if the function is implemented in a seperate compilation unit. A better approach would've been to change to `func(arr.data())` and then one could write a seperate tool if function refactoring was needed.
- In parmVarDecl discussion how we break when function takes incomplete array types (e.g. variable array sizes).
- In parmVarDecl discussion write about the annoying example arr[4][5] and how C++ is broken since [5] is enforced but [4] is not, since [4] decays to ptr but [5] doesn't. We made it so that both are enforced.
- In parmVarDecl discussion write about how we broke function calls with ConstantArrayTypes where the passed array is bigger than the function indicates.
- More citations
- (Perhaps) write something about: Purpose of the refactoring tool is to strengthen the safety guarantees the structure provides.