// Declares clang::SyntaxOnlyAction.
#include "clang/AST/Type.h"
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
#include <regex>


using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

using ::clang::transformer::cat;
using ::clang::transformer::node;
using transformer::noopEdit;
using transformer::name;

#define in_file_line() in_file_line_impl(__FILE__, __LINE__)

std::string in_file_line_impl(const char* file, int line) {
    return "\nIn file: " + std::string(file) + " on line: " + std::to_string(line) + "\n";
}


#define append_file_line(arg) append_file_line_impl(arg, __FILE__, __LINE__)

std::string append_file_line_impl(const std::string &what, const char *file, int line) {
    return what + in_file_line_impl(file, line);
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
/// @tparam MetadataT the type of result provided by the stencil
/// @param name Name of the operation
/// @param lambda The actual implementation of the functionality. Will be called with two parameters: const MatchFinder::MatchResult &Match, Metadata *Result
/// @return A stencil that can be used to generate edits or issue warnings
template<typename MetadataT>
static auto nodeOperation(
    const std::string& name,
    const std::function<Error(const MatchFinder::MatchResult &, MetadataT*)> &&lambda) {

    //Internal stencil struct that must be made shared for the stencil API
    struct NodeOp : public transformer::MatchComputation<MetadataT> {
        explicit NodeOp(
            const std::string& name, 
            const std::function<Error(const MatchFinder::MatchResult &, MetadataT*)> & lambda) : 
            Name(name), 
            Lambda(lambda) {}
            //[&lambda](const MatchFinder::MatchResult &Match, std::string *Result){return lambda(Match, Result);}
        
        // Name of the Stencil.
        std::string toString() const override {return Name;}

        //Evaluation method. This will be called by the stencil API.
        Error eval(const MatchFinder::MatchResult &Match, MetadataT *Result) const override {
            return Lambda(Match, Result);
        }

        llvm::Expected<MetadataT> eval(const MatchFinder::MatchResult &R) const {
            MetadataT Output;
            if (auto Err = eval(R, &Output))
                return std::move(Err);
            return Output;
        }

    private:
        const std::string& Name;
        const std::function<Error(const MatchFinder::MatchResult &, MetadataT *)> Lambda;
    };

    return std::make_shared<NodeOp>(name, lambda);
}

public:

    /// @brief Get a bound node as the specified type or throw if it is not
    /// @tparam NodeT The type of node to get
    /// @param Id The id of the bound node
    /// @param throw_location The location of the callee (use in_file_line())
    /// @return The node as the given type
    template <typename NodeT>
    static auto getNodeAsOrThrow(StringRef Id, StringRef throw_location) {
        using resType = const NodeT*;
        auto lambda = [=](const MatchFinder::MatchResult &Match, resType *Result) {
            auto node = Match.Nodes.getNodeAs<NodeT>(Id);
            if (!node) {
                std::string throw_msg = "ID not bound";
                auto nodeMap = Match.Nodes.getMap();
                if (nodeMap.find(Id) != nodeMap.end())
                    throw_msg +="expected type";

                throw throw_msg + ": " + Id.str() + "\n" + throw_location;
            }

            *Result = node;
            return Error::success();
        };
        return nodeOperation<resType>("getNodeAsOrThrow", lambda);
    }

    /// @brief Check if a bound node is of a given type
    /// @tparam NodeT The type of the node
    /// @param Id The id of the bound node
    /// @return True if the bound node is of the expected type
    template<typename NodeT>
    static auto nodeIsType(StringRef Id) {
        using resType = bool;
        return nodeOperation<resType>("nodeIsType", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {
                if (auto node = Match.Nodes.getNodeAs<NodeT>(Id)) {
                    *Result = true;
                }
                else {
                    *Result = false;
                }

                return Error::success();
            });
    }

    /// @brief Check if a bound node is not of a given type
    /// @tparam NodeT The type to check
    /// @param Id The id of the bound node
    /// @return False if the bound node is of the specified type
    template<typename NodeT>
    static auto nodeIsNotType(StringRef Id) {
        using resType = bool;
        return nodeOperation<resType>("nodeIsNotType", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {
                *Result = !nodeIsType<NodeT>(Id)->eval(Match).get();
                return Error::success();
            });
    }

    /// @brief Get the size of a ConstArrayType node
    /// @param Id The id of the bound node
    /// @return The size of the array as a uint64_t
    static auto getConstArraySize(StringRef Id) {
        using resType = uint64_t;
        auto lambda = [=](const MatchFinder::MatchResult &Match, resType *Result) {
            auto array = getNodeAsOrThrow<ConstantArrayType>(Id, in_file_line())->eval(Match).get();

            *Result = array->getSize().getZExtValue();
            return Error::success();
        };
        return nodeOperation<resType>("getConstArraySize", lambda);
    }

    /// @brief Get the size of a ConstArrayType node as a string
    /// @param Id The id of the bound node
    /// @return The size of the array as a string
    static transformer::Stencil getConstArraySizeString(StringRef Id) {
        using resType = std::string;
        auto lambda = [=](const MatchFinder::MatchResult &Match, resType *Result) {
            auto size = getConstArraySize(Id)->eval(Match).get();
            std::stringstream ss;
            ss << size;
            *Result += ss.str();
            return Error::success();
        };
        return nodeOperation<resType>("getConstArraySize", lambda);
    }

    /// @brief Get the element type of an ArrayType
    /// @param Id The id of the bound node
    /// @return The QualType of the arrays element.
    static auto getArrayElemtType(StringRef Id) {
        using resType = clang::QualType;
        return nodeOperation<resType>("getArrayElementType", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {
                auto array = getNodeAsOrThrow<ArrayType>(Id, in_file_line())->eval(Match).get();
                
                *Result = array->getElementType();
                return Error::success();
            });
    }


    /// @brief Get the element type of an array as a string
    /// @param Id The id of the bound node
    /// @return The element type of the array as a string
    static transformer::Stencil getArrayElemtTypeString(StringRef Id) {
        using resType = std::string;
        return nodeOperation<resType>("getArrayElementType", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {
                *Result += getArrayElemtType(Id)->eval(Match).get().getAsString();
                return Error::success();
            });
    }
 

    /// @brief Get the storage class of a bound VarDecl node.
    /// @param Id Id of the bound node
    /// @return Returns SC_None if the bound node is not a VarDecl node and the storage class if it is.
    static auto getVarStorage(StringRef Id) {
        using resType = StorageClass;
        return nodeOperation<resType>("getVarStorageClass", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {

                if (nodeIsNotType<VarDecl>(Id)->eval(Match).get()) {
                    *Result = StorageClass::SC_None;
                    return Error::success();
                }

                auto var = getNodeAsOrThrow<VarDecl>(Id, in_file_line())->eval(Match).get();

                *Result = var->getStorageClass();
                return Error::success();
            });
    }

    /// @brief Get the storage class of a VarDecl as a string. \see getVarStorageClass for more info.
    /// @param Id The id of the bound node
    /// @return 
    static transformer::Stencil getVarStorageClassString(StringRef Id) {
        using resType = std::string;
        return nodeOperation<resType>("getVarStorageClassString", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {
                auto storage_class = getVarStorage(Id)->eval(Match).get();

                if (storage_class == StorageClass::SC_None) return Error::success();
                auto duration = VarDecl::getStorageClassSpecifierString(storage_class);
                *Result += duration;
                *Result += " ";
                return Error::success();
            });
    }


    /// @brief Returns the qualifiier location of a bound DeclaratorDecl node
    /// @param Id Id of the bound node.
    /// @return 
    static auto getDeclQualifierLoc(StringRef Id) {
        using resType = NestedNameSpecifierLoc;
        return nodeOperation<resType>("getDeclQualifierLoc", 
            [=](const MatchFinder::MatchResult &Match, resType *Result) {
                auto *D = getNodeAsOrThrow<DeclaratorDecl>(Id, in_file_line())->eval(Match).get();

                /**
                 * NOTE: Despite the similar names, `getQualifiedName*` and `getQualifier*` does not have
                 * much to do with each other. `getQualifiedName*` refers to the fully qualified name of a node,
                 * i.e., a unique identifier for the node.
                 * `getQualifier*` refers to a potential qualifier in front of the declaration (as it is written in the
                 * source code).
                 */
                *Result = D->getQualifierLoc();
                return Error::success();
            });
    }
    
    /// @brief Gets the qualifier location of a bound DeclaratorDecl node as a string. \see getDeclQualifierLoc.
    /// @param Id Id of the bound node
    /// @return 
    static transformer::Stencil getDeclQualifierLocString(StringRef Id) {
        using resType = std::string;
        return nodeOperation<resType>("getDeclQualifierLocString", 
            [=](const MatchFinder::MatchResult &Match, std::string *Result) {
                auto qualifierLoc = getDeclQualifierLoc(Id)->eval(Match).get();
                auto qualifierRange = CharSourceRange::getTokenRange(qualifierLoc.getSourceRange());
                auto qualifierText = getText(qualifierRange, *Match.Context).str();

                *Result += qualifierText;
                return Error::success();
            });
    }

    /// @brief Get the location of a bound Decl node
    /// @param Id Id of the bound node
    /// @return 
    static auto getLocOfDecl(StringRef Id) {
        using resType = SourceLocation;
        return nodeOperation<resType>("getLocOfDecl", 
            [=](const MatchFinder::MatchResult &Match, SourceLocation *Result) {
                auto decl = getNodeAsOrThrow<Decl>(Id, in_file_line())->eval(Match).get();

                *Result = decl->getLocation();
                return Error::success();
        });
    }

    /// @brief Adds a string reprecentation of the location of the specified Decl to the edit string.
    /// @param Id The Id string of a bound Decl. This method will throw runtime if the node is unbound or not a Decl type node.
    /// @return 
    static transformer::Stencil getLocOfDeclString(StringRef Id) {
        using resType = std::string;
        return nodeOperation<resType>("getLocOfDeclString", 
            [=](const MatchFinder::MatchResult &Match, std::string *Result) -> Error {
                SourceLocation location = getLocOfDecl(Id)->eval(Match).get();
                *Result += location.printToString(*Match.SourceManager);
                return Error::success();
            });
    }

    /// @brief This method will throw a warning about the usage of a constant sized CStyle array as a mehtod parameter. 
    /// @tparam N The amount of chars in the format specifier. This should be autodeduced, so ignore it.
    /// @param Id The Id of the bound ParmVarDecl
    /// @param format The format of the warning to be printed. See DiagnosticEngine::getCustomDiagID for details.
    /// @return 
    template<unsigned N>
    static auto warnAboutCStyleArrayParameter(StringRef Id, const char (&format)[N]) {
        using resType = std::string;
        return nodeOperation<resType>("warnAboutCStyleArrayParameters", 
            [Id, &format](const MatchFinder::MatchResult &Match, resType *Result) {

                auto method = getNodeAsOrThrow<ParmVarDecl>(Id, in_file_line())->eval(Match).get();

                auto charRange = Match.SourceManager->getExpansionRange(method->getSourceRange());
                auto text = getText(charRange, *Match.Context);

                //Check the source text for "identifyer + any amount of whitespaces ' ' + '[' + at least one number + ']'"
                std::regex CStyleParameterMatcher("\\w+\\s*\\[(\\d+)\\]");
                auto found = std::regex_search(text.str(), CStyleParameterMatcher);
                if (found) {
                    // Get diagnostic builder and place the ^ at the parameter name
                    auto &DE = Match.SourceManager->getDiagnostics(); 
                    auto ID = DE.getCustomDiagID(DiagnosticsEngine::Warning, format);
                    DiagnosticBuilder DB = DE.Report(method->getLocation(), ID);

                    // Add the ~ squiggles under the parameter in the output window.
                    DB.AddSourceRange(charRange);
                }
                
                return Error::success();
            });
    }





/*
    /// @brief issue a warning theough the Diagnostic engine wit the specified message from the format.
    /// @tparam F Functor to get the SourceLocation to issue the warning. Will be called with argument: const MatchFinder::MatchResult &
    /// @tparam N The amount of chars in the format string. This will be auto deduced.
    /// @param format The format to issue in the warning. See DiagnosticEngine::getCustomDiagID for details. 
    /// @param getLocation A functor that returns the SourceLocation to issue the warning at. Will be called with argument: const MatchFinder::MatchResult &
    /// @return 
    template<unsigned N, typename F>
    static auto issueWarning(const char (&format)[N], F &&getLocation) {
        return nodeOperation("issueWarning", [&](const MatchFinder::MatchResult &Match, std::string *Result) {
            auto &DE = Match.SourceManager->getDiagnostics(); 
            auto ID = DE.getCustomDiagID(DiagnosticsEngine::Warning, format);
            DiagnosticBuilder Report = DE.Report(getLocation(Match), ID);
        });
    }
*/
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
            isExpansionInMainFile(),
            hasType(constantArrayType().bind("array"))
    ).bind("arrayDecl");

    //auto MethodFinder = parmVarDecl(myMatcher::isNotInStdNamespace()).bind("method");

    auto FindArrays = makeRule(
            ConstArrayFinder,
            {
                addInclude("array", transformer::IncludeFormat::Angled),
                changeTo(
                    declaratorDeclStorageToEndName("arrayDecl"),
                    cat(
                        NodeOperation::getVarStorageClassString("arrayDecl"),
                        "std::array<",
                        NodeOperation::getArrayElemtTypeString("array"),
                        ", ",
                        NodeOperation::getConstArraySizeString("array"),
                        "> ",
                        NodeOperation::getDeclQualifierLocString("arrayDecl"),
                        name("arrayDecl")         
            ))},
            cat("Changed CStyle Array: ", NodeOperation::getLocOfDeclString("arrayDecl"))
    );

    MatchFinder Finder;
    MyConsumer Consumer(Tool.getChanges());

    Transformer Transf{
            FindArrays,
            Consumer.RefactorConsumer()
    };
    Transf.registerMatchers(&Finder);

    auto MethodParams = parmVarDecl(isExpansionInMainFile()).bind("method");

    auto FindCStyleArrayParams = makeRule(
        MethodParams,
        note(
            node("method"),
            cat(NodeOperation::warnAboutCStyleArrayParameter("method", "CStyleArray used as method parameter. This is actually just an unbounded pointer.")))
    );

    auto consumer = [](Expected<TransformerResult<void>> C) {};

    Transformer WarnCStyleMethod {
        FindCStyleArrayParams,
        consumer
    };

    WarnCStyleMethod.registerMatchers(&Finder);

    //Run the tool and save the changes on disk immediately.
    //See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
    return Tool.runAndSave(newFrontendActionFactory(&Finder).get());
}