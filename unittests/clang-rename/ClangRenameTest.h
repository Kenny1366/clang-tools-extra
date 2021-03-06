//===-- ClangRenameTests.cpp - clang-rename unit tests --------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "RenamingAction.h"
#include "USRFindingAction.h"
#include "unittests/Tooling/RewriterTestContext.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/FileSystemOptions.h"
#include "clang/Basic/VirtualFileSystem.h"
#include "clang/Format/Format.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/PCHContainerOperations.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "gtest/gtest.h"
#include <memory>
#include <string>
#include <vector>

namespace clang {
namespace clang_rename {
namespace test {

struct Case {
  std::string Before;
  std::string After;
  std::string OldName;
  std::string NewName;
};

class ClangRenameTest : public testing::Test,
                        public testing::WithParamInterface<Case> {
protected:
  void AppendToHeader(StringRef Code) { HeaderContent += Code.str(); }

  std::string runClangRenameOnCode(llvm::StringRef Code,
                                   llvm::StringRef OldName,
                                   llvm::StringRef NewName) {
    std::string NewCode;
    llvm::raw_string_ostream(NewCode) << llvm::format(
        "#include \"%s\"\n%s", HeaderName.c_str(), Code.str().c_str());
    tooling::FileContentMappings FileContents = {{HeaderName, HeaderContent},
                                                 {CCName, NewCode}};
    clang::RewriterTestContext Context;
    Context.createInMemoryFile(HeaderName, HeaderContent);
    clang::FileID InputFileID = Context.createInMemoryFile(CCName, NewCode);

    rename::USRFindingAction FindingAction({}, {OldName});
    std::unique_ptr<tooling::FrontendActionFactory> USRFindingActionFactory =
        tooling::newFrontendActionFactory(&FindingAction);

    if (!tooling::runToolOnCodeWithArgs(
            USRFindingActionFactory->create(), NewCode, {"-std=c++11"}, CCName,
            "clang-rename", std::make_shared<PCHContainerOperations>(),
            FileContents))
      return "";

    const std::vector<std::vector<std::string>> &USRList =
        FindingAction.getUSRList();
    std::vector<std::string> NewNames = {NewName};
    std::map<std::string, tooling::Replacements> FileToReplacements;
    rename::QualifiedRenamingAction RenameAction(NewNames, USRList,
                                                 FileToReplacements);
    auto RenameActionFactory = tooling::newFrontendActionFactory(&RenameAction);
    if (!tooling::runToolOnCodeWithArgs(
            RenameActionFactory->create(), NewCode, {"-std=c++11"}, CCName,
            "clang-rename", std::make_shared<PCHContainerOperations>(),
            FileContents))
      return "";

    formatAndApplyAllReplacements(FileToReplacements, Context.Rewrite, "llvm");
    return Context.getRewrittenText(InputFileID);
  }

  void CompareSnippets(StringRef Expected, StringRef Actual) {
    std::string ExpectedCode;
    llvm::raw_string_ostream(ExpectedCode) << llvm::format(
        "#include \"%s\"\n%s", HeaderName.c_str(), Expected.str().c_str());
    EXPECT_EQ(format(ExpectedCode), format(Actual));
  }

  std::string format(llvm::StringRef Code) {
    tooling::Replacements Replaces = format::reformat(
        format::getLLVMStyle(), Code, {tooling::Range(0, Code.size())});
    auto ChangedCode = tooling::applyAllReplacements(Code, Replaces);
    EXPECT_TRUE(static_cast<bool>(ChangedCode));
    if (!ChangedCode) {
      llvm::errs() << llvm::toString(ChangedCode.takeError());
      return "";
    }
    return *ChangedCode;
  }

  std::string HeaderContent;
  std::string HeaderName = "header.h";
  std::string CCName = "input.cc";
};

} // namespace test
} // namespace clang_rename
} // namesdpace clang
