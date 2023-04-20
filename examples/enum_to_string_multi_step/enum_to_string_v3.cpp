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

static llvm::cl::OptionCategory MyToolCategory(
    "Enum to string generator",
    "This tool will generate to_string methods for all the enums in the "
    "specified source code.");
static llvm::cl::opt<bool> Inplace(
    "in_place",
    llvm::cl::desc("Inplace edit <file>s, if specified. If not specified the "
                   "generated code will be printed to cout."),
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
		return
		    [this](Expected<llvm::MutableArrayRef<tooling::AtomicChange>> C) {
			    if (not C) {
				    throw std::runtime_error(
				        append_file_line("Error generating changes: " +
				                         llvm::toString(C.takeError()) + "\n"));
			    }

			    // Save the changes to be handled later
			    Changes.insert(Changes.begin(), C.get().begin(), C.get().end());
		    };
	}

   private:
	tooling::AtomicChanges &Changes;
};

/// Stencil for retrieving extra information of a node
namespace NodeOps {

struct transform_info {
	CharSourceRange type;
	CharSourceRange start;
};

std::unordered_map<std::string, transform_info> enums;

using resType = transformer::MatchConsumer<std::string>;

transformer::RangeSelector getDeclaratorType(StringRef Id) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Match)
	           -> Expected<CharSourceRange> {
		auto node = Match.Nodes.getNodeAs<DeclaratorDecl>(Id);
		if (!node) {
			throw std::invalid_argument(append_file_line(
			    "Node not bound or not a DeclaratorDecl: " + Id.str()));
		}
		auto sourceRange =
		    node->getTypeSourceInfo()->getTypeLoc().getSourceRange();
		return CharSourceRange::getTokenRange(sourceRange);
	};
}

transformer::RangeSelector generateTransformInfoForDeclarator(StringRef Id) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Match)
	           -> Expected<CharSourceRange> {
		if (auto node = Match.Nodes.getNodeAs<DeclaratorDecl>(Id)) {
			auto qualNodeName = node->getQualifiedNameAsString();

			auto wholeNode = transformer::node(Id.str())(Match).get();
			qualNodeName = node->getType().getAsString();

			enums[qualNodeName] = transform_info{
			    .type = getDeclaratorType(Id)(Match).get(), .start = wholeNode};

			return wholeNode;
		}

		throw std::invalid_argument(append_file_line(
		    "Node not bound or not a DeclaratorDecl: " + Id.str()));
	};
}

std::optional<transform_info> getStoredInfo(
    StringRef Id, const ast_matchers::MatchFinder::MatchResult &Match) {
	if (auto node = Match.Nodes.getNodeAs<NamedDecl>(Id)) {
		auto qualName = node->getQualifiedNameAsString();

		auto info = enums.find(qualName);
		if (info != enums.end()) {
			return info->second;
		}
	}

	return std::nullopt;
}

transformer::RangeSelector transformationRange(StringRef Id) {
	return [=](const ast_matchers::MatchFinder::MatchResult &Match)
	           -> Expected<CharSourceRange> {
		auto info = getStoredInfo(Id, Match);
		if (info) {
			return info.value().start;
		}

		return transformer::after(transformer::node(Id.str()))(Match).get();
	};
}

transformer::Stencil generateToStringFromEnum(StringRef Id) {
	return transformer::run([=](const ast_matchers::MatchFinder::MatchResult
	                                &Match) -> Expected<std::string> {
		auto enum_decl = Match.Nodes.getNodeAs<EnumDecl>(Id);
		if (!enum_decl) {
			throw std::invalid_argument(append_file_line(
			    "Node not bound or not an EnumDecl: " + Id.str()));
		}

		auto info = getStoredInfo(Id, Match);

		auto type = tooling::getText(
		    info ? info.value().type : transformer::name(Id.str())(Match).get(),
		    *Match.Context);

		std::string ret;

		ret += (not info ? "\n\n" : "");
		ret += "constexpr std::string_view to_string(" + type.str() +
		       " e){\n\tswitch(e) {\n";

		for (const auto enum_const : enum_decl->enumerators()) {
			auto enum_const_name = enum_const->getNameAsString();
			ret += "\t\t" + type.str() + "::" + enum_const_name +
			       ": return \"" + enum_const_name + "\";\n";
		}

		ret += "\t}\n}";
		return ret;
	});
}

}  // end namespace NodeOps

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

	// Common consumer
	MyConsumer consumer(tool.getChanges());
	auto transformer_consumer = consumer.RefactorConsumer();

	auto findAllParmVarDeclsWithEnums = ast_matchers::functionDecl(
	    ast_matchers::isExpansionInMainFile(),
	    ast_matchers::hasName("to_string"), ast_matchers::parameterCountIs(1),
	    ast_matchers::hasParameter(
	        0, ast_matchers::parmVarDecl(
	               ast_matchers::hasType(ast_matchers::enumDecl()))
	               .bind("enumParm")));

	auto ruleCollectTransformInfoForAllToStringMethods = transformer::makeRule(
	    findAllParmVarDeclsWithEnums,
	    transformer::noopEdit(
	        NodeOps::generateTransformInfoForDeclarator("enumParm")));
	{
		ast_matchers::MatchFinder finder;
		tooling::Transformer transformer{
		    ruleCollectTransformInfoForAllToStringMethods, [](auto r) {}};

		transformer.registerMatchers(&finder);
		tool.run(tooling::newFrontendActionFactory(&finder).get());
	}

	for (auto e : NodeOps::enums) {
		std::cout << e.first << "\n";
	}

	auto findAllEnums =
	    ast_matchers::enumDecl(ast_matchers::isExpansionInMainFile())
	        .bind("enumDecl");

	auto ruleGenerateToString = transformer::makeRule(
	    findAllEnums,
	    {transformer::addInclude("string_view",
	                             transformer::IncludeFormat::Angled),
	     transformer::changeTo(NodeOps::transformationRange("enumDecl"),
	                           NodeOps::generateToStringFromEnum("enumDecl"))});
	{
		ast_matchers::MatchFinder finder;
		tooling::Transformer transformer{ruleGenerateToString,
		                                 consumer.RefactorConsumer()};

		transformer.registerMatchers(&finder);
		tool.runAndSave(tooling::newFrontendActionFactory(&finder).get());
	}

	return 0;
}
