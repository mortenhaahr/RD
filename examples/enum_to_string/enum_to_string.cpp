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
#include "llvm/Support/CommandLine.h"

#include <iostream>
#include <optional>
#include <sstream>
#include <type_traits>

using namespace clang;
using namespace clang::ast_matchers;

#define append_file_line(arg) append_file_line_impl(arg, __FILE__, __LINE__)

std::string append_file_line_impl(const std::string &what, const char *file,
								  int line) {
	return what + "\nIn file: " + file + " on line: " + std::to_string(line) +
		   "\n";
}

static llvm::cl::OptionCategory MyToolCategory(
	"Enum to string generator",
	"This tool will generate to_string methods for all the enums in the "
	"specified source code.");
static llvm::cl::opt<bool> Inplace(
	"in_place",
	llvm::cl::desc("Inplace edit <file>s, if specified. If not specified the "
				   "generated code will be printed to cout."),
	llvm::cl::cat(MyToolCategory));
static llvm::cl::opt<bool> DebugMsgs(
	"debug_info", llvm::cl::desc("Print debug information to cout."),
	llvm::cl::cat(MyToolCategory));

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

			if (!Inplace) {
				llvm::outs() << new_code.get();
				continue;
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
			if (DebugMsgs) {
				llvm::errs() << "Debug: " << C.get().Metadata << "\n";
			}

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

template <typename F,
		  // Enable if F is callable with const
		  // ast_matchers::MatchFinder::MatchResult&, const EnumDecl*, const
		  // EnumConstantDecl* and returns a string
		  typename = std::enable_if_t<std::is_same_v<
			  std::invoke_result_t<
				  F &, const ast_matchers::MatchFinder::MatchResult &,
				  const EnumDecl *, const EnumConstantDecl *>,
			  std::string>>>
resType foreach_enum_const(StringRef Id, F callback) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Match)
			   -> Expected<std::string> {
		if (auto enum_decl = Match.Nodes.getNodeAs<EnumDecl>(Id)) {
			std::stringstream ss;
			for (const auto enum_const : enum_decl->enumerators()) {
				ss << callback(Match, enum_decl, enum_const);
			}
			return ss.str();
		}

		throw std::invalid_argument(
			append_file_line("ID not bound or not EnumDecl: " + Id.str()));
	};
}

resType case_enum_to_string(transformer::Stencil getName, StringRef Id) {
	auto lambda =
		[getName](const ast_matchers::MatchFinder::MatchResult &Match,
					   const EnumDecl *enum_decl,
					   const EnumConstantDecl *enum_const_decl) {
			auto ns = getName->eval(Match);
			if (!ns) {
				throw std::invalid_argument(
					append_file_line("Could get potential namespace"));
			}
			auto names = ns.get();
			return "\t\tcase " + ns.get() +
				   "::" + enum_const_decl->getNameAsString() + ": return \"" +
				   enum_const_decl->getNameAsString() + "\";\n";
		};
	return foreach_enum_const(Id, lambda);
}

resType get_declarator_type_text(StringRef Id) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Match)
			   -> Expected<std::string> {
		auto node = Match.Nodes.getNodeAs<DeclaratorDecl>(Id);
		if (!node) {
			throw std::invalid_argument(append_file_line(
				"ID not bound or not DeclaratorDecl: " + Id.str()));
		}
		auto sourceRange =
			node->getTypeSourceInfo()->getTypeLoc().getSourceRange();
		auto tokenRange = CharSourceRange::getTokenRange(sourceRange);
		auto sourceText = tooling::getText(tokenRange, *Match.Context).str();
		return sourceText;
	};
}

}	// end namespace NodeOps

namespace matchers {

/// Warning: Use at own risk. Might match multiple types e.g. on record
/// declarations. E.g. based on the example from the reference for
/// `hasDeclContext` It matches twice on class D. Once on the declaration and
/// once on the definition
AST_MATCHER_P(Decl, has_rec_decl_context,
			  clang::ast_matchers::internal::Matcher<Decl>, InnerMatcher) {
	auto cur_ctx = Node.getDeclContext();
	if (!cur_ctx) {
		return false;
	}
	const DeclContext *nxt_ctx = nullptr;
	while (true) {
		nxt_ctx = cur_ctx->getParent();
		if (!nxt_ctx) {
			return InnerMatcher.matches(*Decl::castFromDeclContext(cur_ctx),
										Finder, Builder);
		}
		cur_ctx = nxt_ctx;
	}
}

AST_MATCHER_P(NestedNameSpecifier, rec_specifies_namespace,
			  clang::ast_matchers::internal::Matcher<NamespaceDecl>,
			  InnerMatcher) {
	auto ns = Node.getAsNamespace();
	if (!ns) {
		return false;
	}
	auto prefix = Node.getPrefix();
	while (true) {
		if (!prefix) {
			return InnerMatcher.matches(*ns, Finder, Builder);
		}
		ns = prefix->getAsNamespace();
		prefix = prefix->getPrefix();
	}
}

/// Returns false if not named - e.g. unnamed enum
AST_MATCHER(NamedDecl, is_named) {
	return Node.getIdentifier(); // nullptr if no name
}

}	// namespace matchers

int main(int argc, const char **argv) {
	// Configuring the command-line options
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

	// NOTE: We currently bind namespace - which turned out to be unnecessary but quite a learning experience
	auto has_enum_to_string =
		functionDecl(
			hasName("to_string"),
			      parameterCountIs(1),
				  hasParameter(
					  0, parmVarDecl(hasType(elaboratedType(
							 namesType(hasDeclaration(
								 equalsBoundNode("enumDecl"))),
							 optionally(
								 hasQualifier(matchers::rec_specifies_namespace(
									 namespaceDecl().bind("namespace"))))))).bind("parmVar")))
			.bind("toString");

	auto enumFinder =
		enumDecl(
			isExpansionInMainFile(),
			has(enumConstantDecl(hasDeclContext(enumDecl().bind("enumDecl")))),
			matchers::is_named(),
			optionally(matchers::has_rec_decl_context(
				hasDescendant(has_enum_to_string))));

	auto print_correct_name = transformer::ifBound("toString",	// if toString bound
												   transformer::run(NodeOps::get_declarator_type_text(
													   "parmVar")),	// Print name based on parmVar
												   transformer::cat(transformer::name("enumDecl"))); // Print name based on enumDecl
	auto findEnums = transformer::makeRule(
		enumFinder,
		{addInclude("string_view", transformer::IncludeFormat::Angled),
		 addInclude("stdexcept", transformer::IncludeFormat::Angled),
		 transformer::changeTo(
			 transformer::ifBound("toString", transformer::node("toString"),
							transformer::after(transformer::node("enumDecl"))),
			 transformer::cat(
				 // to_string method
				 "\n\nconstexpr std::string_view to_string(",
				 print_correct_name,
				 " e){\n\tswitch(e) {\n",
				 transformer::run(NodeOps::case_enum_to_string(print_correct_name, "enumDecl")),
				 "\t}\n}"))},
		transformer::cat("Found something"));

	ast_matchers::MatchFinder finder;
	MyConsumer consumer(tool.getChanges());

	tooling::Transformer transformer{findEnums, consumer.RefactorConsumer()};
	transformer.registerMatchers(&finder);

	// Run the tool and save the changes on disk immediately.
	// See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
	return tool.runAndSave(tooling::newFrontendActionFactory(&finder).get());
}
