// Declares clang::SyntaxOnlyAction.
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "clang/Tooling/Transformer/Transformer.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include <iostream>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

using ::clang::transformer::cat;
using ::clang::transformer::node;
using transformer::noopEdit;

struct MyConsumer {
    // Pass a reference of a map (i.e., one that a Tool uses) and save it in the member
    explicit MyConsumer(std::map<std::string, Replacements> &FilesToReplace) : FilesToReplace(FilesToReplace) {}

    auto consumer() {
        return [this](Expected<TransformerResult<std::string>> C) {
            if (not C) {
                throw "Error generating changes: " + llvm::toString(C.takeError()) + "\n";
            }

            //Print the metadata of the change
            std::cout << "Metadata: " << C.get().Metadata << "\n";

            //Debug info
            std::cout << "Changes:\n";
            for (auto changes: C.get().Changes) {
                std::cout << changes.toYAMLString() << std::endl;
            }

            //Save the changes to be handled later
            convertChangesToFileReplacements(C.get().Changes);
        };
    }

private:

    //Convert atomic changes to file replacements
    //This method is stolen from clang/lib/Tooling/Refactoring/Rename/RenamingAction.cpp:168
    //The goal of this function is to take all the edits in the atomic changes and
    //insert them into a map that maps filepaths to replacements. The replacements
    //can then be applied later. This example uses the functionality of the
    //clang::tooling::RefactoringTool in order to do the actual replacements on disk.
    void convertChangesToFileReplacements(ArrayRef<AtomicChange> AtomicChanges) {
        for (const auto &AtomicChange: AtomicChanges) {
            for (const auto &Replacement: AtomicChange.getReplacements()) {
                // Add the Replacement to the map. (The Tool will use it to write the changes.)
                llvm::Error Err =
                        FilesToReplace[std::string(Replacement.getFilePath())].add(Replacement);
                //TODO: Handle header insertions

                if (Err) {
                    throw "Failed to apply changes in " + Replacement.getFilePath() + "! "
                          + llvm::toString(std::move(Err)) + "\n";
                }
            }
        }
    }
    // Reference to a map that the Tool uses
    std::map<std::string, Replacements> &FilesToReplace;
};

int main(int argc, const char **argv) {
    // Configuring the command-line options
    llvm::cl::OptionCategory MyToolCategory("my-tool options");
    auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
    if (!ExpectedParser) {
        // Fail gracefully for unsupported options.
        llvm::errs() << ExpectedParser.takeError();
        return 1;
    }
    CommonOptionsParser &OptionsParser = ExpectedParser.get();

    // Using refactoring tool since it allows `runAndSave` instead of `run`
    RefactoringTool Tool(
            OptionsParser.getCompilations(),
            OptionsParser.getSourcePathList());

    //Rule to warn with metadata
    auto WarnInvalidFunctionName = makeRule(
            functionDecl(hasName("MkX")).bind("fun"),
            noopEdit(node("fun")),
            cat("The name ``MkX`` is not allowed for functions; please rename")
    );

    //Rule to rename the function rule
    auto RenameInvalidFunctionNameRule = makeRule(
            functionDecl(hasName("MkX")).bind("fun"),
            changeTo(clang::transformer::name("fun"), cat("MakeX")),
            cat("The name ``MkX`` is not allowed for functions; the function has been renamed")
    );

    //Rule to rename all expressions that calls the ``MkX`` function
    auto RenameAllInvocationsOfInvalidFunctionNameRule =
            makeRule(
                    declRefExpr(to(functionDecl(hasName("MkX")))),
                    changeTo(cat("MakeX")), cat("Referencing the invalid function ``MkX`` renaming to ``MakeX``")
            );

    //Combination of rules rename function and rename calls to function.
    auto RenameFunctionAndAllInvocationsOfItRule =
            clang::transformer::applyFirst(
                    {RenameInvalidFunctionNameRule, RenameAllInvocationsOfInvalidFunctionNameRule});
    MatchFinder Finder;
    MyConsumer Consumer(Tool.getReplacements());
    Transformer Transf{
            RenameFunctionAndAllInvocationsOfItRule,
            Consumer.consumer()
    };

    Transf.registerMatchers(&Finder);

    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    Tool.runAndSave(newFrontendActionFactory(&Finder).get());

    ///
    /// ALTERNATIVE WAY:\n\n
    ///
    /// Alternatively if one wishes to have more control-logic inside the `edits` part of a rule
    /// It can be done through the ASTMatchers API.
    /// However, if one wishes to e.g. write changes to disk then
    /// See below:
    class FunCallback : public MatchFinder::MatchCallback {
    public :
        void run(const MatchFinder::MatchResult &Result) override {
            std::cout << "Inside FunCallback" << std::endl;
            // Manipulate the nodes below. Typically, using `Result.Nodes.getNodeAs<T>(ID)`,
            // But with functionDecl this can be tricky. Alternatively, the map can be retrieved like below.
            const auto map = Result.Nodes.getMap();
        }
    };
    auto FunMatcher = functionDecl(hasName("MkX")).bind("funMatch");
    FunCallback Callback;
    // Using same Finder as Transformer example
    Finder.addMatcher(FunMatcher, &Callback);
    // Outcomment line below and comment out the other Tool.runAndSave invocation to try the alternative way
//    Tool.run(newFrontendActionFactory(&Finder).get());
}