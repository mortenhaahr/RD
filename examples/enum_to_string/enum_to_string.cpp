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

		// FIXME: We should probably cleanup the result by default as well.
		Spec.Cleanup = false;

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

resType name_plus_string(StringRef Id, StringRef append) {
	return [=](const MatchFinder::MatchResult &Match) -> Expected<std::string> {
		auto src_rng = transformer::name(Id.str())(Match).get();
		auto text = tooling::getText(src_rng, *Match.Context);
		return text.str() + append.str();
	};
}

}	// end namespace NodeOps

static std::optional<DynTypedNode>
try_get_node(const ast_matchers::BoundNodes &Nodes, StringRef ID) {
	auto &NodesMap = Nodes.getMap();
	auto It = NodesMap.find(ID);
	if (It == NodesMap.end())
		return std::nullopt;
	return It->second;
}

transformer::RangeSelector
if_bound_range(std::string ID, transformer::RangeSelector true_eval,
			   transformer::RangeSelector false_eval) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Result)
			   -> Expected<CharSourceRange> {
		std::optional<DynTypedNode> Node = try_get_node(Result.Nodes, ID);
		if (Node.has_value()) {
			return true_eval(Result);
		}
		return false_eval(Result);
	};
}

namespace matchers {
const DeclContext *recl_decl_context(const DeclContext *context) {
	auto p = context->getParent();
	if (p) {
		recl_decl_context(p);
	}
	return context;
}

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

}	// namespace matchers

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

	auto has_enum_to_string =
		functionDecl(
			allOf(hasName("to_string"),
				  hasParameter(
					  0, parmVarDecl(hasType(elaboratedType(
							 namesType(hasDeclaration(
								 enumDecl(equalsBoundNode("enumDecl")))),
							 optionally(
								 hasQualifier(matchers::rec_specifies_namespace(
									 namespaceDecl().bind("namespace"))))))))))
			.bind("toString");

	auto enumFinder =
		enumDecl(
			isExpansionInMainFile(),
			has(enumConstantDecl(hasDeclContext(enumDecl().bind("enumDecl")))),
			optionally(matchers::has_rec_decl_context(
				hasDescendant(has_enum_to_string))))
			.bind("enumDecl");

	auto potentially_print_namespace =
		transformer::ifBound("namespace",	// if namespace bound
							 transformer::run(NodeOps::name_plus_string(
								 "namespace", "::")),	// Print namespace
							 transformer::cat(""));		// Else do nothing

	auto findEnums = transformer::makeRule(
		enumFinder,
		{
			addInclude("string_view", transformer::IncludeFormat::Angled),
			addInclude("stdexcept", transformer::IncludeFormat::Angled),
			transformer::changeTo(
				if_bound_range(
					"toString", transformer::node("toString"),
					transformer::after(transformer::node("enumDecl"))),
				transformer::cat(
					// to_string method
					"\n\nconstexpr std::string_view to_string(",
					potentially_print_namespace, transformer::name("enumDecl"),
					" e){\n\tswitch(e) {\n",
					transformer::run(NodeOps::case_enum_to_string("enumDecl")),
					"\t}\n}"))
			//				 // to_enum method
			//				 "\n\nconstexpr ",
			//				 transformer::name("enumDecl"),
			//				 " to_enum(const std::string_view& str,
			//[[maybe_unused]]
			//", 				 transformer::name("enumDecl"), " _ = {}){\n",
			//	 transformer::run(NodeOps::case_string_to_enum("enumDecl")),
			// "\n\tthrow std::invalid_argument(\"Not a valid enum
			// of type ", 				 transformer::name("enumDecl"),
			// "\");\n}"))
		},
		transformer::cat("Found something"));

	ast_matchers::MatchFinder finder;
	MyConsumer consumer(tool.getChanges());

	tooling::Transformer transformer{findEnums, consumer.RefactorConsumer()};
	transformer.registerMatchers(&finder);

	// Run the tool and save the changes on disk immediately.
	// See clang/tools/clang-rename/ClangRename.cpp:190-237 for other options
	return tool.runAndSave(tooling::newFrontendActionFactory(&finder).get());
}
