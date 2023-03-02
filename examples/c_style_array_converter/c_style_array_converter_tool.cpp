// Declares clang::SyntaxOnlyAction.
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "clang/Tooling/Transformer/Transformer.h"
#include "clang/ASTMatchers/ASTMatchersMacros.h"
#include "clang/Tooling/Transformer/SourceCode.h"

// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include <iostream>
#include <sstream>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

using ::clang::transformer::cat;
using ::clang::transformer::node;
using transformer::noopEdit;
using transformer::name;

struct MyConsumer {

    explicit MyConsumer(std::map<std::string, Replacements> &FilesToReplace) : FilesToReplace(FilesToReplace) {}

    auto consumer() {
        return [this](Expected<TransformerResult<std::string>> C) {
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


static llvm::Expected<DynTypedNode> getNode(const BoundNodes &Nodes,
                                            StringRef Id) {
    auto &NodesMap = Nodes.getMap();
    auto It = NodesMap.find(Id);
    if (It == NodesMap.end())
        return llvm::make_error<llvm::StringError>(llvm::errc::invalid_argument,
                                                   "Id not bound: " + Id);
    return It->second;
}

static llvm::Error printNode(StringRef Id, const MatchFinder::MatchResult &Match,
                       std::string *Result) {
    std::string Output;
    llvm::raw_string_ostream Os(Output);
    auto NodeOrErr = getNode(Match.Nodes, Id);
    if (auto Err = NodeOrErr.takeError())
        return Err;
    NodeOrErr->print(Os, PrintingPolicy(Match.Context->getLangOpts()));
    *Result += Os.str();
    return Error::success();
}

enum class UnaryNodeOperator {
    Describe,
    ArraySize,
    ArrayType
};

class UnaryOperationStencil : public transformer::StencilInterface {
    UnaryNodeOperator Op;
    std::string Id;

public:
    UnaryOperationStencil(UnaryNodeOperator Op, std::string Id)
            : Op(Op), Id(std::move(Id)) {}

    std::string toString() const override {
        StringRef OpName;
        switch (Op) {
            case UnaryNodeOperator::Describe:
                OpName = "describe";
                break;
            case UnaryNodeOperator::ArraySize:
                OpName = "arraySize";
            case UnaryNodeOperator::ArrayType:
                OpName = "arrayType";
        }
        return (OpName + "(\"" + Id + "\")").str();
    }

    Error eval(const MatchFinder::MatchResult &Match,
               std::string *Result) const override {
        // The `Describe` operation can be applied to any node, not just
        // expressions, so it is handled here, separately.
        if (Op == UnaryNodeOperator::Describe)
            return printNode(Id, Match, Result);

        switch (Op) {
            case UnaryNodeOperator::ArraySize:
            {
                auto array = Match.Nodes.getNodeAs<ConstantArrayType>(Id);
                if(array){
                    auto size = array->getSize().getZExtValue();
                    std::stringstream ss;
                    ss << size;
                    *Result += ss.str();
                }
            }
                break;
            case UnaryNodeOperator::ArrayType:
            {
                auto array = Match.Nodes.getNodeAs<ConstantArrayType>(Id);
                if(array){
                    auto arr_t = array->getElementType();
                    std::cout << "getElementType: " <<  arr_t.getAsString() << std::endl;
                    *Result += arr_t.getAsString();
                }
            }
                break;
            case UnaryNodeOperator::Describe:
                llvm_unreachable("This case is handled at the start of the function");
        }
        return Error::success();
    }
};


transformer::Stencil my_array_size(StringRef Id) {
    return std::make_shared<UnaryOperationStencil>(UnaryNodeOperator::ArraySize,
                                                   std::string(Id));
}

transformer::Stencil my_array_type(StringRef Id) {
    return std::make_shared<UnaryOperationStencil>(UnaryNodeOperator::ArrayType,
                                                   std::string(Id));
}

transformer::Stencil my_describe(StringRef Id) {
    return std::make_shared<UnaryOperationStencil>(UnaryNodeOperator::Describe,
                                                   std::string(Id));
}


transformer::RangeSelector my_type(std::string ID){
    return [ID](const MatchFinder::MatchResult &Result) -> Expected<CharSourceRange> {
        Expected<DynTypedNode> N = getNode(Result.Nodes, ID);
        if (!N)
            return N.takeError();
        auto &Node = *N;
        if (const auto *D = Node.get<VarDecl>()) {
            auto array = Result.Nodes.getNodeAs<ConstantArrayType>(ID);
            TypeLoc TLoc = D->getTypeSourceInfo()->getTypeLoc();
            SourceLocation L = TLoc.getBeginLoc();
            auto R = CharSourceRange::getTokenRange(L, L);
            // Verify that the range covers exactly the name.
            // FIXME: extend this code to support cases like `operator +` or
            // `foo<int>` for which this range will be too short.  Doing so will
            // require subcasing `NamedDecl`, because it doesn't provide virtual
            // access to the \c DeclarationNameInfo.
//            if (clang::tooling::getText(R, *Result.Context) != D->getName())
//                return CharSourceRange();
            return R;
        }
        return N.takeError();
    };
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


    auto ConstArrayFinder = varDecl(
            isExpansionInMainFile(),
            hasType(
                constantArrayType(
                    //hasInitStatement
                    //hasSizeType(type(integerLiteral).bind("size"))
                    //hasSizeExpr(type(integerLiteral()).bind("size"))
                    //myMatchers::hasSize(integerLiteral().bind("size"))
                    ).bind("array"))
            //hasInitializer(ignoringImpCasts(integerLiteral().bind("size")))
        ).bind("arrayDecl");

    auto FindArrays = makeRule(
        ConstArrayFinder,
        changeTo(my_type("arrayDecl"), cat(
                "std::array<",
                my_array_type("array"),
                ", ",
                my_array_size("array"),
                "> "
            )),
        cat("Array")
    );

    MatchFinder Finder;
    MyConsumer Consumer(Tool.getReplacements());



    MyTransformer Transf{
            FindArrays,
            Consumer.consumer()
    };
    Transf.set_callback(callback);


    Transf.registerMatchers(&Finder);


    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    Tool.runAndSave(newFrontendActionFactory(&Finder).get());

    return 0;


}