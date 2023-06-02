
- Consistency with subsection, subsection*, etc.

- In the parmVar discussion: Talk about how the rewriting of functions where CStyle array was previously used is quite skethcy. Especially, if the function is implemented in a seperate compilation unit. A better approach would've been to change to `func(arr.data())` and then one could write a seperate tool if function refactoring was needed.
- In parmVarDecl discussion how we break when function takes incomplete array types (e.g. variable array sizes).
- In parmVarDecl discussion write about the annoying example arr[4][5] and how C++ is broken since [5] is enforced but [4] is not, since [4] decays to ptr but [5] doesn't. We made it so that both are enforced.
- In parmVarDecl discussion write about how we broke function calls with ConstantArrayTypes where the passed array is bigger than the function indicates.

- (Perhaps) write something about: Purpose of the refactoring tool is to strengthen the safety guarantees the structure provides.

- Check for ref in std::array parms

- Introduction
- Abstract
- Conclusion
