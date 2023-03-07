// Declares clang::SyntaxOnlyAction.
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "clang/Tooling/Transformer/Transformer.h"
#include "clang/ASTMatchers/ASTMatchersMacros.h"
#include "clang/Tooling/Transformer/SourceCode.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"

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


struct ArrayRefactoringTool : public ClangTool {

    ArrayRefactoringTool(
        const CompilationDatabase& Compilations,
        ArrayRef<std::string> SourcePaths,
        std::shared_ptr<PCHContainerOperations> PCHContainerOps = 
            std::make_shared<PCHContainerOperations>()) : ClangTool(Compilations, SourcePaths, std::move(PCHContainerOps)) 
        {}

    /// Return a reference to the current changes
    AtomicChanges& getChanges() {return Changes;}

    /// Call run(), apply all generated replacements, and immediately save
    /// the results to disk.
    ///
    /// \returns 0 upon success. Non-zero upon failure.
    int runAndSave(FrontendActionFactory *ActionFactory) {
        if (int Result = run(ActionFactory)) {
            return Result;
        }

        LangOptions DefaultLangOptions;
        IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
        TextDiagnosticPrinter DiagnosticPrinter(llvm::errs(), &*DiagOpts);
        DiagnosticsEngine Diagnostics(
            IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()),
            &*DiagOpts, &DiagnosticPrinter, false);
        SourceManager Sources(Diagnostics, getFiles());
        Rewriter Rewrite(Sources, DefaultLangOptions);

        return applyAllChanges(Rewrite) != false;
    }

    /// @brief Apply all the saved changes to the rewriter
    /// @param Rewrite - the rewriter to use
    /// @return true if sucessfull
    bool applyAllChanges(Rewriter &Rewrite) {

        auto &sm = Rewrite.getSourceMgr();
        auto &fm = sm.getFileManager();

        // FIXME: Add automatic formatting support as well.
        tooling::ApplyChangesSpec Spec;

        // FIXME: We should probably cleanup the result by default as well.
        Spec.Cleanup = false;

        // Split the changes according to filename
        std::map<std::string, AtomicChanges> Files;
        for (const auto &Change : Changes)
            Files[Change.getFilePath()].push_back(Change);

        //Apply all atomic changes to all files
        for (const auto &[File, FileChanges] : Files) {
            
            // Get File from file manager
            auto Entry = fm.getFileRef(File);
            if (!Entry) {
                llvm::errs() << Entry.takeError();
                return false;
            }

            // Get the id for the file
            auto id = sm.getOrCreateFileID(Entry.get(), SrcMgr::C_User);

            // Read the current code and apply all the changes that to that file
            auto code = sm.getBufferData(id);
            auto new_code = applyAtomicChanges(File, code, FileChanges, Spec);
            
            if (!new_code) {
                llvm::errs() << new_code.takeError();
                return false;
            }

            // Replace the entire file with the new code
            auto fileRange = SourceRange(sm.getLocForStartOfFile(id), sm.getLocForEndOfFile(id));
            Rewrite.ReplaceText(fileRange, new_code.get());
        }

        // Apply all changes to disk
        return Rewrite.overwriteChangedFiles();

    }

private:
    AtomicChanges Changes{};
};

struct MyConsumer {

    explicit MyConsumer(AtomicChanges &Changes) : Changes(Changes) {}

    auto RefactorConsumer() {
        return [this](Expected<TransformerResult<std::string>> C) {
            if (not C) {
                throw std::runtime_error(append_file_line("Error generating changes: " + llvm::toString(C.takeError()) + "\n"));
            }
            //Print the metadata of the change
            std::cout << "Metadata: " << C.get().Metadata << "\n";
            //Save the changes to be handled later
            Changes.insert(Changes.begin(), C.get().Changes.begin(), C.get().Changes.end());

            //Debug info
            std::cout << "Changes:" << std::endl;
            for (auto changes: C.get().Changes) {
                std::cout << changes.toYAMLString() << std::endl;
            }
            
        };
    }

private:
    AtomicChanges &Changes;
};


/// Enum of different NodeOperations (used by the Stencil)
enum class NodeOperator {
    ArraySize,
    ArrayType,
    VarStorage,
    NamedDeclQualified
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
            case NodeOperator::NamedDeclQualified:
                OpName = "qualifiedDeclName";
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
            case NodeOperator::NamedDeclQualified:
                return getQualifiedName(Id, Match, Result);
            default:
                throw std::invalid_argument(append_file_line("Unsuported Node operator!\n"));
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

    static Error getQualifiedName(StringRef Id, const MatchFinder::MatchResult &Match, 
                                  std::string *Result) {
        if (auto named = Match.Nodes.getNodeAs<NamedDecl>(Id)) {
            *Result += named->getQualifiedNameAsString();
            return Error::success();
        }

        throw std::invalid_argument(append_file_line("ID not bound or not NamedDecl: " + Id.str())); 

    }

    /// Matches the ID of a VarDecl and appends the storage class to Result.
    /// Appends nothing if storage class is none.
    static Error getVarStorage(StringRef Id, const MatchFinder::MatchResult &Match,
                               std::string *Result) {
        

        if (auto field = Match.Nodes.getNodeAs<FieldDecl>(Id)) {
            //Ignore
            return Error::success();
        }
        if (auto var = Match.Nodes.getNodeAs<VarDecl>(Id)) {
            auto storage_class = var->getStorageClass();
            if (storage_class == StorageClass::SC_None) return Error::success();
            auto duration = VarDecl::getStorageClassSpecifierString(storage_class);
            *Result += duration;
            *Result += " ";
            return Error::success();
        }

        throw std::invalid_argument(append_file_line("ID not bound or not ArrayType: " + Id.str()));
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
        auto *D = Result.Nodes.getNodeAs<DeclaratorDecl>(ID);
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
    ArrayRefactoringTool Tool(
            OptionsParser.getCompilations(),
            OptionsParser.getSourcePathList());


    auto ConstArrayFinder = declaratorDecl(
            myMatcher::isNotInStdNamespace(),
            hasType(constantArrayType().bind("array"))
    ).bind("arrayDecl");

    auto FindArrays = makeRule(
            ConstArrayFinder,
            {
                addInclude("array", transformer::IncludeFormat::Angled),
                changeTo(
                        varDeclStorageToEndName("arrayDecl"),
                        cat(
                                nodeOperation(NodeOperator::VarStorage, "arrayDecl"),
                                "std::array<",
                                nodeOperation(NodeOperator::ArrayType, ("array")),
                                ", ",
                                nodeOperation(NodeOperator::ArraySize, "array"),
                                "> ",
                                nodeOperation(NodeOperator::NamedDeclQualified, "arrayDecl")
            ))},
            cat("Array")
    );

    MatchFinder Finder;
    MyConsumer Consumer(Tool.getChanges());

    Transformer Transf{
            FindArrays,
            Consumer.RefactorConsumer()
    };
    Transf.registerMatchers(&Finder);


    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    return Tool.runAndSave(newFrontendActionFactory(&Finder).get());
}