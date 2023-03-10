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

std::string append_file_line_impl(const std::string &what, const char *file, int line) {
    return what + "\nIn file: " + file + " on line: " + std::to_string(line) + "\n";
}


struct ArrayRefactoringTool : public ClangTool {

    ArrayRefactoringTool(
            const CompilationDatabase &Compilations,
            ArrayRef<std::string> SourcePaths,
            std::shared_ptr<PCHContainerOperations> PCHContainerOps =
            std::make_shared<PCHContainerOperations>()) : ClangTool(Compilations, SourcePaths,
                                                                    std::move(PCHContainerOps)) {}

    /// Return a reference to the current changes
    AtomicChanges &getChanges() { return Changes; }

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
        for (const auto &Change: Changes)
            Files[Change.getFilePath()].push_back(Change);

        //Apply all atomic changes to all files
        for (const auto &[File, FileChanges]: Files) {

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
                throw std::runtime_error(
                        append_file_line("Error generating changes: " + llvm::toString(C.takeError()) + "\n"));
            }
            //Print the metadata of the change
            std::cout << C.get().Metadata << "\n";
            //Save the changes to be handled later
            Changes.insert(Changes.begin(), C.get().Changes.begin(), C.get().Changes.end());


            //Debug info
/*
            std::cout << "Changes:" << std::endl;
            for (auto changes: C.get().Changes) {
                
                std::cout << changes.toYAMLString() << std::endl;
            }
*/

        };
    }

private:
    AtomicChanges &Changes;
};


/// Stencil for retrieving extra information of a node
class NodeOperation {
private:

/// @brief  Factory for creating stencils
/// @tparam T Allows lambda expressions to be used
/// @param name Name of the operation
/// @param lambda The actual implementation of the functionality. Will be called with two parameters: const MatchFinder::MatchResult &Match, std::string *Result
/// @return A stencil that can be used to generate edits or issue warnings
template<typename T>
static transformer::Stencil nodeOperation(
    const std::string& name,
    const T &&lambda) {

    //Internal stencil struct that must be made shared for the stencil API
    struct NodeOp : public transformer::StencilInterface {
        explicit NodeOp(
            const std::string& name, 
            const T & lambda) : 
            Name(name), 
            Lambda(lambda) {}
            //[&lambda](const MatchFinder::MatchResult &Match, std::string *Result){return lambda(Match, Result);}
        
        // Name of the Stencil.
        std::string toString() const override {return Name;}

        //Evaluation method. This will be called by the stencil API.
        Error eval(const MatchFinder::MatchResult &Match, std::string *Result) const override {
            return Lambda(Match, Result);
        }

    private:
        const std::string& Name;
        const std::function<Error(const MatchFinder::MatchResult &, std::string *)> Lambda;
    };

    return std::make_shared<NodeOp>(name, lambda);
}

public:
    /// Matches the ID with a ConstantArrayType and appends the size of the array to Result
    static transformer::Stencil getConstArraySize(StringRef Id) {
        return nodeOperation("getConstArraySize", [=](const MatchFinder::MatchResult &Match, std::string *Result) {
            auto array = Match.Nodes.getNodeAs<ConstantArrayType>(Id);
            if (!array) {
                throw std::invalid_argument(append_file_line("ID not bound or not ConstantArrayType: " + Id.str() + "\n"));
            }
            auto size = array->getSize().getZExtValue();
            std::stringstream ss;
            ss << size;
            *Result += ss.str();
            return Error::success();
        });
    }

    /// Matches the ID with an ArrayType and appends the type of the array to Result
    static transformer::Stencil getArrayElemtType(StringRef Id) {
        return nodeOperation("getArrayElementType", [=](const MatchFinder::MatchResult &Match, std::string *Result) {
            auto array = Match.Nodes.getNodeAs<ArrayType>(Id);
            if (!array) {
                throw std::runtime_error(append_file_line("ID not bound or not ArrayType: " + Id.str()));
            }
            *Result += array->getElementType().getAsString();
            return Error::success();
        });
    }

    /// Matches the ID of a VarDecl and appends the storage class to Result.
    /// Appends nothing if storage class is none.
    static transformer::Stencil getVarStorage(StringRef Id) {
        return nodeOperation("getVarStorage", [=](const MatchFinder::MatchResult &Match, std::string *Result) {
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

            throw std::invalid_argument(append_file_line("ID not bound or not FieldDecl or VarDecl: " + Id.str()));
        });
    }

    /// Matches the ID of a DeclaratorDecl and appends the qualifier in front of the name to the Result.
    /// (In most cases there are no qualifiers)
    static transformer::Stencil getDeclQualifier(StringRef Id) {
        return nodeOperation("getDeclQualifier", [=](const MatchFinder::MatchResult &Match, std::string *Result) {
            auto *D = Match.Nodes.getNodeAs<DeclaratorDecl>(Id);
            if (!D)
                throw std::invalid_argument(append_file_line("ID not bound or not DeclaratorDecl: " + Id.str()));

            /**
             * NOTE: Despite the similar names, `getQualifiedName*` and `getQualifier*` does not have
             * much to do with each other. `getQualifiedName*` refers to the fully qualified name of a node,
             * i.e., a unique identifier for the node.
             * `getQualifier*` refers to a potential qualifier in front of the declaration (as it is written in the
             * source code).
             */
            auto qualifierRange = CharSourceRange::getTokenRange(D->getQualifierLoc().getSourceRange());
            auto qualifierText = getText(qualifierRange, *Match.Context).str();

            *Result += qualifierText;
            return Error::success();
        });
    }

    /// @brief Adds a string reprecentation of the location of the specified Decl to the edit string.
    /// @param Id The Id string of a bound Decl. This method will throw runtime if the node is unbound or not a Decl type node.
    /// @return 
    static transformer::Stencil getLocOfDecl(StringRef Id) {
        return nodeOperation("getLocOfDecl", [=](const MatchFinder::MatchResult &Match, std::string *Result) {
            if (auto decl = Match.Nodes.getNodeAs<Decl>(Id)) {
                *Result += decl->getLocation().printToString(*Match.SourceManager);
                return Error::success();
            }

            throw std::invalid_argument(append_file_line("ID not bound or not Decl: " + Id.str()));
        });
    }
};

/// Matches the ID of a DeclaratorDecl and returns the RangeSelector from storage class to end of the var name.
/// E.g., `static const std::string str = "Hello"` returns RangeSelector with `static const std::string str`
transformer::RangeSelector declaratorDeclStorageToEndName(std::string ID) {
    return [ID](const MatchFinder::MatchResult &Result) -> Expected<CharSourceRange> {
        auto *D = Result.Nodes.getNodeAs<DeclaratorDecl>(ID);
        if (!D)
            throw std::invalid_argument(append_file_line("ID not bound or not DeclaratorDecl: " + ID));

        auto begin = D->getSourceRange().getBegin();
        auto end = D->getTypeSpecEndLoc();
        auto R = CharSourceRange::getTokenRange(begin, end);
        return R;
    };
}

namespace myMatcher {
    /// Matches on expression with the provided originalType.
    /// (E.g. arrays that have been adjusted to pointers)
    /// Found in clang-tools-extra/clang-tidy/cppcoreguidelines/ProTypeVarargCheck.cpp
    AST_MATCHER_P(AdjustedType, hasOriginalType,
                  ast_matchers::internal::Matcher<QualType>, InnerType) {
        return InnerType.matches(Node.getOriginalType(), Finder, Builder);
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
    ArrayRefactoringTool Tool(
            OptionsParser.getCompilations(),
            OptionsParser.getSourcePathList());


    auto ConstArrayFinder = declaratorDecl(
            isExpansionInMainFile(),
            hasType(constantArrayType().bind("array"))
    ).bind("arrayDecl");

    auto FindArrays = makeRule(
            ConstArrayFinder,
            {
                addInclude("array", transformer::IncludeFormat::Angled),
                changeTo(
                    declaratorDeclStorageToEndName("arrayDecl"),
                    cat(
                        NodeOperation::getVarStorage("arrayDecl"),
                        "std::array<",
                        NodeOperation::getArrayElemtType("array"),
                        ", ",
                        NodeOperation::getConstArraySize("array"),
                        "> ",
                        NodeOperation::getDeclQualifier("arrayDecl"),
                        name("arrayDecl")         
            ))},
            cat("Changed CStyle Array: ", NodeOperation::getLocOfDecl("arrayDecl"))
    );

    MatchFinder Finder;
    MyConsumer Consumer(Tool.getChanges());

    Transformer Transf{
            FindArrays,
            Consumer.RefactorConsumer()
    };
    Transf.registerMatchers(&Finder);
    auto ParmConstArrays = parmVarDecl( hasType(
            decayedType(myMatcher::hasOriginalType(constantArrayType().bind("parm")))), isExpansionInMainFile()).bind("parmDecl");

    auto FindCStyleArrayParams = makeRule(
            ParmConstArrays,
            {
                    addInclude("array", transformer::IncludeFormat::Angled),
                    changeTo(
                            declaratorDeclStorageToEndName("parmDecl"),
                            cat(
                                    NodeOperation::getVarStorage("parmDecl"),
                                    "std::array<",
                                    // TODO: Make remove_const unnecessary. We need to make a decision on how to treat const
                                    "std::remove_const_t<",
                                    NodeOperation::getArrayElemtType("parm"),
                                    ">",
                                    ", ",
                                    NodeOperation::getConstArraySize("parm"),
                                    "> ",
                                    NodeOperation::getDeclQualifier("parmDecl"),
                                    name("parmDecl")
                            ))},
            cat("Changed CStyle Array: ", NodeOperation::getLocOfDecl("parmDecl"))
    );

    Transformer WarnCStyleMethod {
        FindCStyleArrayParams,
        Consumer.RefactorConsumer()
    };

    WarnCStyleMethod.registerMatchers(&Finder);

    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    return Tool.runAndSave(newFrontendActionFactory(&Finder).get());
}