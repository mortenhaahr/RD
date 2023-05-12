// Declares clang::SyntaxOnlyAction.
#include "clang/ASTMatchers/ASTMatchersMacros.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Transformer/RewriteRule.h"
#include "clang/Tooling/Transformer/SourceCode.h"
#include "clang/Tooling/Transformer/Stencil.h"
#include "clang/Tooling/Transformer/Transformer.h"

// Declares llvm::cl::extrahelp.
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "llvm/Support/CommandLine.h"

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::tooling;
using namespace llvm;

using ::clang::transformer::cat;
using ::clang::transformer::node;
using transformer::name;
using transformer::noopEdit;

#define append_file_line(arg) append_file_line_impl(arg, __FILE__, __LINE__)

std::string append_file_line_impl(const std::string &what, const char *file,
                                  int line) {
	return what + "\nIn file: " + file + " on line: " + std::to_string(line) +
	       "\n";
}

struct ArrayRefactoringTool : public ClangTool {
	ArrayRefactoringTool(
	    const CompilationDatabase &Compilations,
	    ArrayRef<std::string> SourcePaths,
	    std::shared_ptr<PCHContainerOperations> PCHContainerOps =
	        std::make_shared<PCHContainerOperations>())
	    : ClangTool(Compilations, SourcePaths, std::move(PCHContainerOps)) {}

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
		IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts =
		    new DiagnosticOptions();
		TextDiagnosticPrinter DiagnosticPrinter(llvm::errs(), &*DiagOpts);
		DiagnosticsEngine Diagnostics(
		    IntrusiveRefCntPtr<DiagnosticIDs>(new DiagnosticIDs()), &*DiagOpts,
		    &DiagnosticPrinter, false);

		SourceManager Sources(Diagnostics, getFiles());
		Rewriter Rewrite(Sources, DefaultLangOptions);

		return applyAllChanges(Rewrite) ? 0 : 1;
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
		std::unordered_map<std::string, AtomicChanges> Files;
		for (const auto &Change : Changes)
			Files[Change.getFilePath()].push_back(Change);

		// Apply all atomic changes to all files
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
			auto fileRange = SourceRange(sm.getLocForStartOfFile(id),
			                             sm.getLocForEndOfFile(id));
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
				    append_file_line("Error generating changes: " +
				                     llvm::toString(C.takeError()) + "\n"));
			}
			// Print the metadata of the change
			std::cout << C.get().Metadata << "\n";
			// Save the changes to be handled later
			Changes.reserve(Changes.size() + C.get().Changes.size());
			std::move(C.get().Changes.begin(), C.get().Changes.end(),
			          std::back_inserter(Changes));

			// Debug info
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
namespace NodeOps {

using resType = transformer::MatchConsumer<std::string>;

/// Matches the ID with a ConstantArrayType and appends the size of the array to
/// Result
resType getConstArraySize(StringRef Id) {
	return [=](const MatchFinder::MatchResult &Match) -> Expected<std::string> {
		auto array = Match.Nodes.getNodeAs<ConstantArrayType>(Id);
		if (!array) {
			throw std::invalid_argument(append_file_line(
			    "ID not bound or not ConstantArrayType: " + Id.str() + "\n"));
		}
		auto size = array->getSize().getZExtValue();
		std::stringstream ss;
		ss << size;
		return ss.str();
	};
}

/// Matches the ID with an ArrayType and appends the type of the array to Result
resType getArrayElemtType(StringRef Id) {
	return [=](const MatchFinder::MatchResult &Match) -> Expected<std::string> {
		auto array = Match.Nodes.getNodeAs<ArrayType>(Id);
		if (!array) {
			throw std::runtime_error(
			    append_file_line("ID not bound or not ArrayType: " + Id.str()));
		}
		return array->getElementType().getAsString();
	};
}

/// Matches the ID of a VarDecl and appends the storage class to Result.
/// Appends nothing if storage class is none.
resType getVarStorage(StringRef Id) {
	return [=](const MatchFinder::MatchResult &Match) -> Expected<std::string> {
		if (auto field = Match.Nodes.getNodeAs<FieldDecl>(Id)) {
			// Ignore
			return "";
		}
		if (auto var = Match.Nodes.getNodeAs<VarDecl>(Id)) {
			auto storage_class = var->getStorageClass();
			if (storage_class == StorageClass::SC_None) return "";
			auto duration =
			    VarDecl::getStorageClassSpecifierString(storage_class);
			return std::string(duration) + " ";
		}

		throw std::invalid_argument(append_file_line(
		    "ID not bound or not FieldDecl or VarDecl: " + Id.str()));
	};
}

/// Matches the ID of a DeclaratorDecl and appends the qualifier in front of the
/// name to the Result. (In most cases there are no qualifiers)
resType getDeclQualifier(StringRef Id) {
	return [=](const MatchFinder::MatchResult &Match) -> Expected<std::string> {
		auto *D = Match.Nodes.getNodeAs<DeclaratorDecl>(Id);
		if (!D)
			throw std::invalid_argument(append_file_line(
			    "ID not bound or not DeclaratorDecl: " + Id.str()));

		/**
		 * NOTE: Despite the similar names, `getQualifiedName*` and
		 * `getQualifier*` does not have much to do with each other.
		 * `getQualifiedName*` refers to the fully qualified name of a node,
		 * i.e., a unique identifier for the node.
		 * `getQualifier*` refers to a potential qualifier in front of the
		 * declaration (as it is written in the source code).
		 */
		auto qualifierRange = CharSourceRange::getTokenRange(
		    D->getQualifierLoc().getSourceRange());
		auto qualifierText = getText(qualifierRange, *Match.Context).str();

		return qualifierText;
	};
}

/// @brief Adds a string reprecentation of the location of the specified Decl to
/// the edit string.
/// @param Id The Id string of a bound Decl. This method will throw runtime if
/// the node is unbound or not a Decl type node.
/// @return
resType getLocOfDecl(StringRef Id) {
	return [=](const MatchFinder::MatchResult &Match) -> Expected<std::string> {
		if (auto decl = Match.Nodes.getNodeAs<Decl>(Id)) {
			return decl->getLocation().printToString(*Match.SourceManager);
		}

		throw std::invalid_argument(
		    append_file_line("ID not bound or not Decl: " + Id.str()));
	};
}

}  // end namespace NodeOps

namespace myMatcher {
/// Matches on expression with the provided originalType.
/// (E.g. arrays that have been adjusted to pointers)
/// Found in
/// clang-tools-extra/clang-tidy/cppcoreguidelines/ProTypeVarargCheck.cpp
AST_MATCHER_P(AdjustedType, hasOriginalType,
              ast_matchers::internal::Matcher<QualType>, InnerType) {
	return InnerType.matches(Node.getOriginalType(), Finder, Builder);
}
}  // namespace myMatcher

int main(int argc, const char **argv) {
	// Configuring the command-line options
	llvm::cl::OptionCategory MyToolCategory("my-tool options");
	auto ExpectedParser =
	    CommonOptionsParser::create(argc, argv, MyToolCategory);
	if (!ExpectedParser) {
		// Fail gracefully for unsupported options.
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}
	CommonOptionsParser &OptionsParser = ExpectedParser.get();

	// Using refactoring tool since it allows `runAndSave` instead of `run`
	ArrayRefactoringTool Tool(OptionsParser.getCompilations(),
	                          OptionsParser.getSourcePathList());

	auto ConstArrayFinder =
	    declaratorDecl(isExpansionInMainFile(),
	                   hasType(constantArrayType().bind("array")),
	                   hasTypeLoc(typeLoc().bind("arrayLoc")))
	        .bind("arrayDecl");

	auto FindArrays = makeRule(
	    ConstArrayFinder,
	    {addInclude("array", transformer::IncludeFormat::Angled),
	     changeTo(
	         // The RangeSelector that we are going to replace. From start of
	         // parmDecl to end of parmLoc in case of constant array this is
	         // until end bracket of arr declaration
	         transformer::encloseNodes("arrayDecl", "arrayLoc"),
	         cat(transformer::run(NodeOps::getVarStorage("arrayDecl")),
	             "std::array<",
	             transformer::run(NodeOps::getArrayElemtType("array")), ", ",
	             transformer::run(NodeOps::getConstArraySize("array")), "> ",
	             transformer::run(NodeOps::getDeclQualifier("arrayDecl")),
	             name("arrayDecl")))},
	    cat("Changed CStyle Array: ",
	        transformer::run(NodeOps::getLocOfDecl("arrayDecl"))));

	MatchFinder Finder;
	MyConsumer Consumer(Tool.getChanges());

	Transformer Transf{FindArrays, Consumer.RefactorConsumer()};
	Transf.registerMatchers(&Finder);
	auto ParmConstArrays =
	    parmVarDecl(isExpansionInMainFile(),
	                hasType(decayedType(myMatcher::hasOriginalType(
	                    constantArrayType().bind("parm")))),
	                hasTypeLoc(typeLoc().bind("parmLoc")))
	        .bind("parmDecl");

	auto FindCStyleArrayParams = makeRule(
	    ParmConstArrays,
	    {addInclude("array", transformer::IncludeFormat::Angled),
	     changeTo(
	         // The RangeSelector that we are going to replace. From start of
	         // parmDecl to end of parmLoc in case of constant array this is
	         // until end bracket of arr declaration
	         transformer::encloseNodes("parmDecl", "parmLoc"),
	         cat(transformer::run(NodeOps::getVarStorage("parmDecl")),
	             "std::array<",
	             transformer::run(NodeOps::getArrayElemtType("parm")), ", ",
	             transformer::run(NodeOps::getConstArraySize("parm")), "> ",
	             transformer::run(NodeOps::getDeclQualifier("parmDecl")),
	             name("parmDecl")))},
	    cat("Changed CStyle Array: ",
	        transformer::run(NodeOps::getLocOfDecl("parmDecl"))));

	Transformer WarnCStyleMethod{FindCStyleArrayParams,
	                             Consumer.RefactorConsumer()};

	WarnCStyleMethod.registerMatchers(&Finder);

	// Run the tool and save the changes on disk immediately.
	// See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
	return Tool.runAndSave(newFrontendActionFactory(&Finder).get());
}