// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "clang/Tooling/Transformer/RangeSelector.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "clang/Tooling/Transformer/Transformer.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"

#include "clang/AST/ASTContext.h"

#include <iostream>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

using ::clang::transformer::cat;
using ::clang::transformer::node;
using transformer::noopEdit;


// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

struct MyConsumer {

  MyConsumer(std::map<std::string, Replacements> &FilesToReplace) : FilesToReplace(FilesToReplace) {}

  auto consumer() {
    return [this](Expected<TransformerResult<std::string>> C) {
      if (not C) {
        // FIXME: stash this error rather than printing.
        llvm::errs() << "Error generating changes: "
                      << llvm::toString(C.takeError()) << "\n";
        return;
      }

      //Print the metadata of the change
      std::cout << "Metadata: " << C.get().Metadata << "\n";

      //Debug info
      std::cout << "Changes:\n";
      for ( auto changes : C.get().Changes) {
        std::cout << changes.toYAMLString() << std::endl;
      }

      //Save the changes to be handeled later
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
    for (const auto &AtomicChange : AtomicChanges) {
      for (const auto &Replacement : AtomicChange.getReplacements()) {
        llvm::Error Err =
          FilesToReplace[std::string(Replacement.getFilePath())].add(Replacement);
        //TODO: Handle header insertions

        if (Err) {
          llvm::errs() << "Failed to apply changes in " << Replacement.getFilePath() << "! "
                       << llvm::toString(std::move(Err)) << "\n";
        }
      }
    }
  }

  std::map<std::string, Replacements> &FilesToReplace;

};

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
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

  //Rule to rename all expresions that calls the ``MkX`` function
  auto RenameAllInvocationsOfInvalidFunctionNameRule = 
      makeRule(
        declRefExpr(to(functionDecl(hasName("MkX")))),
        changeTo(cat("MakeX"))
        ,cat("Referencing the invalid function ``MkX`` renaming to ``MakeX``")
      );

  //Combination of rules rename function and rename calls to function.
  auto RenameFunctionAndAllInvocationsOfItRule = 
    clang::transformer::applyFirst({
        RenameInvalidFunctionNameRule, 
        RenameAllInvocationsOfInvalidFunctionNameRule
      });

  MatchFinder Finder;
  MyConsumer Consumer(Tool.getReplacements());
  
  Transformer Trans{
    RenameFunctionAndAllInvocationsOfItRule, 
    Consumer.consumer()
  };

  Trans.registerMatchers(&Finder);
  
  
  //Run the tool and save the changes on disk immediatly.
  //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
  return Tool.runAndSave(newFrontendActionFactory(&Finder).get());

}