#include "BuilderBridge.h"
#include "dol_patch.hpp"
#include "clang/CodeGen/CodeGenAction.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/Utils.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Job.h"
#include "clang/Driver/Tool.h"
#include "lld/Common/Driver.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
#include <unistd.h>

LLD_HAS_DRIVER(macho)
extern "C" int refare_dolrecomp_main(int, char**);

namespace {
std::mutex compilerMutex;
void initialize() {
  static std::once_flag once;
  std::call_once(once, [] {
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
    LLVMInitializeAArch64AsmParser();
    LLVMInitializeAArch64AsmPrinter();
  });
}
}

int ReFareGenerate(const char* dol, const char* directory) {
  std::lock_guard lock(compilerMutex);
  // The C backend uses threads; the LLVM backend's process workers are not used.
  std::vector<std::string> arguments{"dolrecomp", "-j", "1", "--backend=c",
    "--cpu", "gekko", "--gamecube", dol, directory};
  std::vector<char*> pointers;
  for (auto& argument : arguments) pointers.push_back(argument.data());
  return refare_dolrecomp_main(static_cast<int>(pointers.size()), pointers.data());
}

int ReFareCompile(int argc, const char* const* argv) {
  std::lock_guard lock(compilerMutex);
  initialize();
  if (argc < 2) return 1;
  auto diagnostics = clang::CompilerInstance::createDiagnostics(
      *llvm::vfs::getRealFileSystem(), new clang::DiagnosticOptions);
  clang::driver::Driver driver(argv[0], "arm64-apple-ios15.0", *diagnostics);
  driver.setProbePrecompiled(false);
  std::unique_ptr<clang::driver::Compilation> jobs(driver.BuildCompilation(llvm::ArrayRef(argv, argc)));
  if (!jobs || diagnostics->hasErrorOccurred() || jobs->getJobs().size() != 1) return 1;
  const auto& job = *jobs->getJobs().begin();
  if (llvm::StringRef(job.getCreator().getName()) != "clang") return 1;
  auto invocation = std::make_shared<clang::CompilerInvocation>();
  if (!clang::CompilerInvocation::CreateFromArgs(*invocation, job.getArguments(), *diagnostics, argv[0])) return 1;
  if (invocation->getFrontendOpts().Inputs.size() != 1 || invocation->getFrontendOpts().OutputFile.empty()) {
    std::fprintf(stderr, "Compiler requires one input and an explicit object output\n");
    return 1;
  }
  clang::CompilerInstance compiler;
  compiler.setInvocation(std::move(invocation));
  compiler.setDiagnostics(diagnostics.get());
  if (!compiler.hasDiagnostics()) return 1;
  clang::EmitObjAction action;
  return compiler.ExecuteAction(action) ? 0 : 1;
}

int ReFareLink(int argc, const char* const* argv) {
  std::lock_guard lock(compilerMutex);
  static bool usable = true;
  if (!usable) return 2;
  initialize();
  // lldMain cleans its global linker context so a second export can run safely.
  const lld::DriverDef drivers[] = {{lld::Darwin, &lld::macho::link}};
  auto result = lld::lldMain(llvm::ArrayRef(argv, argc), llvm::outs(), llvm::errs(), drivers);
  // A fatal linker recovery cannot safely be followed by another link in this process.
  usable = usable && result.canRunAgain;
  return result.retCode == 0 && usable ? 0 : 1;
}

int ReFarePrepareDOL(const char* dol, const char* manifest) {
  bool changed = false;
  std::string error;
  if (moderngekko::frontend::ApplyDolPatchManifest(dol, manifest, &changed, &error)) return 0;
  std::fprintf(stderr, "Could not prepare DOL: %s\n", error.c_str());
  return 1;
}

void ReFareBuilderLog(const char* path) {
  std::freopen(path, "w", stderr);
  setvbuf(stderr, nullptr, _IONBF, 0);
  // DolRecomp writes its progress to stdout as well as stderr.
  std::fflush(stdout);
  dup2(fileno(stderr), fileno(stdout));
  setvbuf(stdout, nullptr, _IONBF, 0);
}
