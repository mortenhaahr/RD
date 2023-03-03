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
#include <stdexcept>


using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

using ::clang::transformer::cat;
using ::clang::transformer::node;
using transformer::noopEdit;
using transformer::name;

#define append_file_line(arg) append_file_line_impl(arg, __FILE__, __LINE__)
std::string append_file_line_impl(const std::string& what, const char *file, int line){
    return what + "\nIn file: " + file + " on line: " + std::to_string(line) + "\n";
}

struct MyConsumer {

    explicit MyConsumer(std::map<std::string, Replacements> &FilesToReplace) : FilesToReplace(FilesToReplace) {}

    auto RefactorConsumer() {
        return [this](Expected<TransformerResult<std::string>> C) {
            if (not C) {
                throw std::runtime_error(append_file_line("Error generating changes: " + llvm::toString(C.takeError()) + "\n"));
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
                    throw std::runtime_error(append_file_line("Failed to apply changes in " + Replacement.getFilePath().str() + "! " +
                                             llvm::toString(std::move(Err)) + "\n"));
                }
            }
        }
    }

    std::map<std::string, Replacements> &FilesToReplace;
};


/// Enum of different NodeOperations (used by the Stencil)
enum class NodeOperator {
    ArraySize,
    ArrayType,
    VarStorage
};

/// Stencil for retrieving extra information of a node
class NodeOperatorStencil : public transformer::StencilInterface {
    NodeOperator Op;
    std::string Id;


public:
    NodeOperatorStencil(NodeOperator Op, std::string Id)
            : Op(Op), Id(std::move(Id)) {}

    /// Required by StencilInterface
    std::string toString() const override {
        StringRef OpName;
        switch (Op) {
            case NodeOperator::ArraySize:
                OpName = "arraySize";
                break;
            case NodeOperator::ArrayType:
                OpName = "arrayType";
                break;
            case NodeOperator::VarStorage:
                OpName = "varStorage";
                break;

        }
        return (OpName + "(\"" + Id + "\")").str();
    }

    /// Switch that decides which type of operation to use
    Error eval(const MatchFinder::MatchResult &Match, std::string *Result) const override {
        switch (Op) {
            case NodeOperator::ArraySize:
                return getConstArraySize(Id, Match, Result);
            case NodeOperator::ArrayType:
                return getArrayElemtType(Id, Match, Result);
            case NodeOperator::VarStorage:
                return getVarStorage(Id, Match, Result);
            default:
                llvm_unreachable("Unsupported NodeOperator");
        }
    }

private:

    /// Matches the ID with a ConstantArrayType and appends the size of the array to Result
    static Error getConstArraySize(StringRef Id, const MatchFinder::MatchResult &Match, std::string *Result) {
        auto array = Match.Nodes.getNodeAs<ConstantArrayType>(Id);
        if (!array) {
            throw std::invalid_argument(append_file_line("ID not bound or not ConstantArrayType: " + Id.str() + "\n"));
        }
        auto size = array->getSize().getZExtValue();
        std::stringstream ss;
        ss << size;
        *Result += ss.str();
        return Error::success();
    }

    /// Matches the ID with an ArrayType and appends the type of the array to Result
    static Error getArrayElemtType(StringRef Id, const MatchFinder::MatchResult &Match, std::string *Result) {
        auto array = Match.Nodes.getNodeAs<ArrayType>(Id);
        if (!array) {
            throw std::runtime_error(append_file_line("ID not bound or not ArrayType: " + Id.str()));
        }
        *Result += array->getElementType().getAsString();
        return Error::success();
    }

    /// Matches the ID of a VarDecl and appends the storage class to Result.
    /// Appends nothing if storage class is none.
    static Error getVarStorage(StringRef Id, const MatchFinder::MatchResult &Match,
                               std::string *Result) {
        auto var = Match.Nodes.getNodeAs<VarDecl>(Id);
        if (!var) {
            throw std::invalid_argument(append_file_line("ID not bound or not ArrayType: " + Id.str()));
        }
        auto storage_class = var->getStorageClass();
        if (storage_class == StorageClass::SC_None) return Error::success();
        auto duration = VarDecl::getStorageClassSpecifierString(storage_class);
        *Result += duration;
        *Result += " ";
        return Error::success();
    }
};

/// Factory for NodeOperatorStencil
transformer::Stencil nodeOperation(NodeOperator Op, StringRef ID) {
    return std::make_shared<NodeOperatorStencil>(Op, std::string(ID));
}

/// Matches the ID of a VarDecl and returns the RangeSelector from storage class to end of the var name.
/// E.g., `static const std::string str = "Hello"` returns RangeSelector with `static const std::string str`
transformer::RangeSelector varDeclStorageToEndName(std::string ID) {
    return [ID](const MatchFinder::MatchResult &Result) -> Expected<CharSourceRange> {
        auto *D = Result.Nodes.getNodeAs<VarDecl>(ID);
        if (!D)
            throw std::invalid_argument(append_file_line("ID not bound or not ArrayType: " + ID));

        auto begin = D->getSourceRange().getBegin();
        auto end = D->getTypeSpecEndLoc();
        auto R = CharSourceRange::getTokenRange(begin, end);
        return R;
    };
}

namespace myMatcher {
    /// Matches all that is not in the `std` namespace
    /// Inverse of a native Clang matcher: see line 7810 in ASTMatchers.h (isInStdNamespace)
    AST_MATCHER(Decl, isNotInStdNamespace) { return !Node.isInStdNamespace(); }
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
            myMatcher::isNotInStdNamespace(),
            hasType(constantArrayType().bind("array"))
    ).bind("arrayDecl");

    auto FindArrays = makeRule(
            ConstArrayFinder,
            changeTo(
                    varDeclStorageToEndName("arrayDecl"),
                    cat(
                            nodeOperation(NodeOperator::VarStorage, "arrayDecl"),
                            "std::array<",
                            nodeOperation(NodeOperator::ArrayType, ("array")),
                            ", ",
                            nodeOperation(NodeOperator::ArraySize, "array"),
                            "> ",
                            name("arrayDecl")
                    )),
            cat("Array")
    );

    MatchFinder Finder;
    MyConsumer Consumer(Tool.getReplacements());

    Transformer Transf{
            FindArrays,
            Consumer.RefactorConsumer()
    };
    Transf.registerMatchers(&Finder);


    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    Tool.runAndSave(newFrontendActionFactory(&Finder).get());

    return 0;


}