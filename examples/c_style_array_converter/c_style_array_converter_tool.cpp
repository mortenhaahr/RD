// Declares clang::SyntaxOnlyAction.
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "clang/Tooling/Transformer/Transformer.h"
#include "clang/ASTMatchers/ASTMatchersMacros.h"
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

    explicit MyConsumer(std::map<std::string, Replacements> &FilesToReplace) : FilesToReplace(FilesToReplace) {}

    auto consumer() {
        return [this](Expected<TransformerResult<std::string>> C) {
            std::cout << "Here\n";
            if (not C) {
                throw "Error generating changes: " + llvm::toString(C.takeError()) + "\n";
            }

            //Print the metadata of the change
            std::cout << "Metadata: " << C.get().Metadata << "\n";

            //Debug info
            std::cout << "Changes:" << std::endl;
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
                // Magic happening on line below that makes changes writable. It seems like we are just adding to a
                // private map but the changes are not written to disk if the line is outcommented.
                // TODO: Explain better
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
    std::map<std::string, Replacements> &FilesToReplace;
};


class MyTransformer : public Transformer {
public:
    using Callback = std::function<void(const MatchFinder::MatchResult &)>;
    //Inherit constructors
    using Transformer::Transformer;

    void run(const MatchFinder::MatchResult &Result) override {
        if (callback_) callback_(Result);
        Transformer::run(Result);
    }

    void set_callback(Callback callback) {
        callback_ = callback;
    }

private:
    Callback callback_ = nullptr;
};

void callback(const MatchFinder::MatchResult &Result) {
    std::cout << "Hello from free method" << std::endl;
}

namespace myMatchers {

AST_MATCHER_P(ConstantArrayType, hasSizeType, clang::ast_matchers::internal::Matcher<Expr>,
              InnerMatcher) {
  return InnerMatcher.matches(*Node.getSizeExpr(), Finder, Builder);
}
}





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

    /*
    auto ConstantArrayMatcher = varDecl(
        isExpansionInMainFile(),
        hasType(constantArrayType(hasElementType(type(anything()).bind("elemType"))).bind("array"))
    ).bind("arrayDecl");
 
    */
    
    auto ConstArrayFinder = varDecl(
            isExpansionInMainFile(),
            hasType(
                constantArrayType(
                    hasElementType(type(anything()).bind("elemType"))
                    //hasInitStatement
                    //hasSizeType(type(integerLiteral).bind("size"))
                    //hasSizeExpr(type(integerLiteral()).bind("size"))
                    //myMatchers::hasSize(integerLiteral().bind("size"))
                    ).bind("array"))
            //hasInitializer(ignoringImpCasts(integerLiteral().bind("size")))
        ).bind("arrayDecl");

    auto FindArrays = makeRule(
        ConstArrayFinder,
        noopEdit(node("arrayDecl")),
        cat("Array")
    );

    MatchFinder Finder;
    MyConsumer Consumer(Tool.getReplacements());
    
    
    
    MyTransformer Transf{
            FindArrays,
            Consumer.consumer()
    };
    Transf.set_callback(callback);

/*
    Transf.registerMatchers(&Finder);
    

    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    Tool.run(newFrontendActionFactory(&Finder).get());
    
    return 0;
*/
    
    
    
    
    ///
    /// ALTERNATIVE WAY:\n\n
    ///
    /// Alternatively if one wishes to have more control-logic inside the `edits` part of a rule
    /// It can be done through the ASTMatchers API.
    /// However, if one wishes to e.g. write changes to disk then
    /// See below:
    class VarCallback : public MatchFinder::MatchCallback {
    public :
        void run(const MatchFinder::MatchResult &Result) override {
            auto context = Result.Context;
            std::cout << "Inside VarCallback" << std::endl;

            if (auto sz = Result.Nodes.getNodeAs<IntegerLiteral>("size")) {
                sz->dump();
                auto x = 42;
            }


            const auto &FS = Result.Nodes.getNodeAs<clang::VarDecl>("arrayDecl");


            if (FS) {
                FS->dump();
                std::cout << "Init style: " << std::hex << static_cast<int>(FS->getInitStyle()) << "\n";
                // //std::cout << "Array type: " << FS->getSizeModifier() << "\n";
                // std::cout << "Size: " << FS->getSize().getZExtValue() << "\n";
                // std::cout << "Printing the canonical array type: " << FS->getCanonicalTypeInternal().getAsString() << "\n";
                // std::cout << "Size expr:\n";
                // (FS->getSizeExpr())->dump();
                // auto type = FS->getPointeeOrArrayElementType();
                // std::cout << "ElemType: " << type->getCanonicalTypeInternal().getAsString() << "\n";
                // //std::cout << "Dumping array type:\n";
                // //type->dump();

                //FS->getDefinition(*context)->dump();

                auto info = FS->getTypeSourceInfo();
                //info->getType().dump();
                auto type = info->getType().getTypePtr();
                //type->dump();
//                auto scalar = type->getScalarTypeKind();
                auto one_layer_off_wrong = type->getLocallyUnqualifiedSingleStepDesugaredType();
                //one_layer_off_wrong->dump();

                auto tmp = type->hasSizedVLAType();
                auto arr = context->getAsConstantArrayType(info->getType());
                auto size = context->getConstantArrayElementCount(arr);



                
            }

            if (auto AS = Result.Nodes.getNodeAs<clang::ConstantArrayType>("array")) {
                auto elem = AS->getElementType()->getCanonicalTypeInternal().getAsString();
                auto size = AS->getSize().getZExtValue();
                //std::cout << "Array type: " << elem << " Array Size: " <<  << "\n";
                std::cout << "std::array<" << elem << ", " << size << ">\n";

            }

            const auto ES = Result.Nodes.getNodeAs<clang::Type>("elemType");
            if (ES) {
                std::cout << "Es found: \n";
                ES->dump();

                auto x = 42;
            }

            const auto SS = Result.Nodes.getNodeAs<clang::IntegerLiteral>("arrSubs");
            if (SS) {
                std::cout << "SS found:\n";
                SS->dump();
            }
//            const auto node = Result.Nodes.getNodeAs<clang::functionDecl>("funMatch");
        }
    };
    auto FunMatcher = constantArrayType(
        hasElementType(type(builtinType()).bind("elemType"))
        //hasSize(integerLiteral(anything()).bind("size"))      
    ).bind("arrType");



    VarCallback Callback;
    // Using same Finder as Transformer example
    Finder.addMatcher(ConstArrayFinder, &Callback);
    // Outcomment line below and comment out the other Tool.runAndSave invocation to try the alternative way
    Tool.run(newFrontendActionFactory(&Finder).get());
}