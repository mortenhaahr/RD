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
#include <optional>
#include <sstream>

#include "llvm/Support/CommandLine.h"

using namespace clang;

#define append_file_line(arg) append_file_line_impl(arg, __FILE__, __LINE__)

std::string append_file_line_impl(const std::string &what, const char *file,
                                  int line) {
	return what + "\nIn file: " + file + " on line: " + std::to_string(line) +
	       "\n";
}

struct EnumStringGeneratorTool : public tooling::ClangTool {
	EnumStringGeneratorTool(
	    const tooling::CompilationDatabase &Compilations,
	    ArrayRef<std::string> SourcePaths,
	    std::shared_ptr<PCHContainerOperations> PCHContainerOps =
	        std::make_shared<PCHContainerOperations>())
	    : ClangTool(Compilations, SourcePaths, std::move(PCHContainerOps)) {}

	/// Return a reference to the current changes
	tooling::AtomicChanges &getChanges() { return Changes; }

	/// Call run(), apply all generated replacements, and immediately save
	/// the results to disk.
	///
	/// \returns 0 upon success. Non-zero upon failure.
	int runAndSave(tooling::FrontendActionFactory *ActionFactory) {
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

		return applyAllChanges(Rewrite);
	}

	/// @brief Apply all the saved changes to the rewriter
	/// @param Rewrite - the rewriter to use
	/// @return true if sucessfull
	bool applyAllChanges(Rewriter &Rewrite) {
		auto &sm = Rewrite.getSourceMgr();
		auto &fm = sm.getFileManager();

		// FIXME: Add automatic formatting support as well.
		tooling::ApplyChangesSpec Spec;
		Spec.Style = format::getLLVMStyle();

		// Split the changes according to filename
		std::map<std::string, tooling::AtomicChanges> Files;
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
	tooling::AtomicChanges Changes{};
};

struct MyConsumer {
	explicit MyConsumer(tooling::AtomicChanges &Changes) : Changes(Changes) {}

	auto RefactorConsumer() {
		return [this](Expected<tooling::TransformerResult<std::string>> C) {
			if (not C) {
				throw std::runtime_error(
				    append_file_line("Error generating changes: " +
				                     llvm::toString(C.takeError()) + "\n"));
			}
			// Print the metadata of the change
			std::cout << C.get().Metadata << "\n";

			// Save the changes to be handled later
			Changes.insert(Changes.begin(), C.get().Changes.begin(),
			               C.get().Changes.end());
		};
	}

   private:
	tooling::AtomicChanges &Changes;
};

/// Stencil for retrieving extra information of a node
namespace NodeOps {

using resType = transformer::MatchConsumer<std::string>;

template <
    typename F,
    // Enable if F is callable with const EnumDecl*, const EnumConstantDecl* and
    // returns a string
    typename = std::enable_if_t<std::is_same_v<
        std::invoke_result_t<F &, const EnumDecl *, const EnumConstantDecl *>,
        std::string>>>
resType foreach_enum_const(StringRef Id, F callback) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Match)
	           -> Expected<std::string> {
		if (auto enum_decl = Match.Nodes.getNodeAs<EnumDecl>(Id)) {
			std::stringstream ss;
			for (const auto enum_const : enum_decl->enumerators()) {
				ss << callback(enum_decl, enum_const);
			}
			return ss.str();
		}

		throw std::invalid_argument(
		    append_file_line("ID not bound or not EnumDecl: " + Id.str()));
	};
}

resType case_enum_to_string(StringRef Id) {
	auto lambda = [](const EnumDecl *enum_decl,
	                 const EnumConstantDecl *enum_const_decl) {
		return "\t\tcase " + enum_decl->getNameAsString() +
		       "::" + enum_const_decl->getNameAsString() + ": return \"" +
		       enum_const_decl->getNameAsString() + "\";\n";
	};
	return foreach_enum_const(Id, lambda);
}

resType case_string_to_enum(StringRef Id) {
	auto lambda = [](const EnumDecl *enum_decl,
	                 const EnumConstantDecl *enum_const_decl) {
		return "\tif(str == \"" + enum_const_decl->getNameAsString() +
		       "\")\n\t\treturn " + enum_decl->getNameAsString() +
		       "::" + enum_const_decl->getNameAsString() + ";\n";
	};
	return foreach_enum_const<decltype(lambda)>(Id, lambda);
}

resType addNodeNameToCollection(StringRef Id, std::vector<StringRef> *decls) {
	auto lambda = [=](const ast_matchers::MatchFinder::MatchResult &Match)
	    -> Expected<std::string> {
		if (auto *decl = Match.Nodes.getNodeAs<NamedDecl>(Id)) {
			decls->push_back(decl->getName());
		}
		return "";
	};
	return lambda;
}

}  // end namespace NodeOps

int main(int argc, const char **argv) {
	// Configuring the command-line options
	llvm::cl::OptionCategory MyToolCategory("my-tool options");
	auto ExpectedParser =
	    clang::tooling::CommonOptionsParser::create(argc, argv, MyToolCategory);
	if (!ExpectedParser) {
		// Fail gracefully for unsupported options.
		llvm::errs() << ExpectedParser.takeError();
		return 1;
	}
	clang::tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();

	// Using refactoring tool since it allows `runAndSave` instead of `run`
	EnumStringGeneratorTool tool(OptionsParser.getCompilations(),
	                             OptionsParser.getSourcePathList());

	// Binding names
	auto enum_decl = "enumDecl";
	auto to_string_method = "to_string";

	// to_string method text
	auto to_string_text = transformer::cat(
	    "constexpr std::string_view to_string(", transformer::name(enum_decl),
	    " e){\n\tswitch(e) {\n",
	    transformer::run(NodeOps::case_enum_to_string(enum_decl)), "\t}\n}");

	// Collection of existing to_string methods
	std::vector<StringRef> enums;

	// Matcher of existing to_string methods
	auto find_exsisting_to_string =
	    ast_matchers::functionDecl(
	        ast_matchers::isExpansionInMainFile(),
	        ast_matchers::hasName(to_string_method),
	        ast_matchers::hasParameter(
	            0, ast_matchers::parmVarDecl(ast_matchers::hasType(
	                   ast_matchers::enumDecl().bind(enum_decl)))))
	        .bind(to_string_method);

	// Rule for existing to_string methods
	auto existing_to_string = transformer::makeRule(
	    find_exsisting_to_string,
	    {transformer::addInclude("string_view",
	                             transformer::IncludeFormat::Angled),
	     transformer::addInclude("stdexcept",
	                             transformer::IncludeFormat::Angled),
	     transformer::changeTo(
	         transformer::node(to_string_method),
	         transformer::cat(transformer::run(NodeOps::addNodeNameToCollection(
	                              enum_decl, &enums)),
	                          to_string_text))},
	    transformer::cat("Updating existing ", to_string_method, " method"));

	// Common consumer
	MyConsumer consumer(tool.getChanges());
	auto transformer_consumer = consumer.RefactorConsumer();

	// Helper method for invoking a rule using the tool
	auto runToolWithRule = [&](auto rule, bool save = false) {
		ast_matchers::MatchFinder finder;

		tooling::Transformer transformer{rule, transformer_consumer};
		transformer.registerMatchers(&finder);

		// Run the tool and save the changes on disk immediately.
		// See clang/tools/clang-rename/ClangRename.cpp:190-237 for other
		// options
		if (not save) {
			return tool.run(tooling::newFrontendActionFactory(&finder).get());
		} else {
			return tool.runAndSave(
			    tooling::newFrontendActionFactory(&finder).get());
		}
	};

	// Update existing to_string methods with enum parameters
	runToolWithRule(existing_to_string);

	// Matcher of enums that have no existing to_string method
	auto find_unfound_enums =
	    ast_matchers::enumDecl(
	        ast_matchers::isExpansionInMainFile(),
	        ast_matchers::unless(ast_matchers::hasAnyName(enums)))
	        .bind(enum_decl);

	// Rule to update the rest of the enums
	auto rest_of_enums = transformer::makeRule(
	    find_unfound_enums,
	    {transformer::addInclude("string_view",
	                             transformer::IncludeFormat::Angled),
	     transformer::addInclude("stdexcept",
	                             transformer::IncludeFormat::Angled),
	     transformer::changeTo(transformer::node(enum_decl),
	                           transformer::cat("\n\n", to_string_text))},
	    transformer::cat("Adding new ", to_string_method, " method"));

	// Run the second rule
	return runToolWithRule(rest_of_enums, true);
}
