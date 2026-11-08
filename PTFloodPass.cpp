#include "PTFloodPass.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace llvm;

llvm::cl::opt<bool> PTFloodEnable("obfptflood", llvm::cl::init(true), llvm::cl::desc("Master Toggle"));

static constexpr unsigned ThunkCount = 2048;
static_assert((ThunkCount & (ThunkCount - 1)) == 0, "ThunkCount must be a power of two");

static constexpr uint64_t FloodIterations = 32000000ULL;

static constexpr const char* TableName = "PTFloodTbl";
static constexpr const char* ThunkFmt = "PTFloodThunk%04u";
static constexpr const char* InjectedMd = "PTFloodInjected";

static bool EmittedRing = false;

static bool HasAnnotationTag(Function& f, const char* tag)
{
    Module* m = f.getParent();
    GlobalVariable* ga = m->getGlobalVariable("llvm.global.annotations");
    if (!ga) return false;
    ConstantArray* ca = dyn_cast<ConstantArray>(ga->getOperand(0));
    if (!ca) return false;
    for (unsigned i = 0; i < ca->getNumOperands(); ++i)
    {
        ConstantStruct* cs = dyn_cast<ConstantStruct>(ca->getOperand(i));
        if (!cs) continue;
        if (cs->getOperand(0)->stripPointerCasts() != &f) continue;
        auto* strgv = dyn_cast<GlobalVariable>(cs->getOperand(1)->stripPointerCasts());
        if (!strgv || !strgv->hasInitializer()) continue;
        auto* cds = dyn_cast<ConstantDataSequential>(strgv->getInitializer());
        if (!cds || !cds->isString()) continue;
        if (strcmp(cds->getAsString().str().c_str(), tag) == 0) return true;
    }
    return false;
}

static bool HasPTFloodAnnotation(Function& f)
{
    return HasAnnotationTag(f, "ptflood");
}

static void EmitThunkRing(Module& m)
{
    std::string buf;
    raw_string_ostream os(buf);

    os << ".intel_syntax noprefix\n";

    os << TableName << ":\n";
    for (unsigned i = 0; i < ThunkCount; ++i)
    {
        char namebuf[64];
        std::snprintf(namebuf, sizeof(namebuf), ThunkFmt, i);
        os << "    .quad " << namebuf << "\n";
    }
    os << "\n";

    // thunks need unique VAs so PT's IP compression can't fold them; a trivial "ret" body matches the inlined CALL per iteration without growing the stack
    for (unsigned i = 0; i < ThunkCount; ++i)
    {
        char namebuf[64];
        std::snprintf(namebuf, sizeof(namebuf), ThunkFmt, i);
        os << namebuf << ":\n";
        os << "    ret\n";
    }
    os << "\n";

    m.appendModuleInlineAsm(buf);
}

static void EmitInlineFlood(IRBuilder<>& b, LLVMContext& c)
{
    FunctionType* ft = FunctionType::get(Type::getVoidTy(c), {}, false);

    // [rip + sym + reg*8] emits an ADDR32 reloc that only links under /LARGEADDRESSAWARE:NO; materialize the table base via RIP-rel lea into r11 first
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        ".intel_syntax noprefix\n\t"
        "push rax\n\t"
        "push rcx\n\t"
        "push rdx\n\t"
        "push r11\n\t"
        "pushfq\n\t"
        "lea r11, [rip + %s]\n\t"
        "xor rdx, rdx\n\t"
        "mov rcx, %llu\n\t"
        "LPTFloodLoop_${:uid}:\n\t"
        "inc rdx\n\t"
        "and rdx, %u\n\t"
        "call qword ptr [r11 + rdx*8]\n\t"
        "dec rcx\n\t"
        "jnz LPTFloodLoop_${:uid}\n\t"
        "popfq\n\t"
        "pop r11\n\t"
        "pop rdx\n\t"
        "pop rcx\n\t"
        "pop rax\n\t"
        ".att_syntax prefix",
        TableName,
        (unsigned long long)FloodIterations,
        ThunkCount - 1);

    InlineAsm* ia = InlineAsm::get(ft, std::string(buf), "~{rax},~{rcx},~{rdx},~{r11},~{cc},~{memory},~{dirflag},~{fpsr},~{flags}", true, false, InlineAsm::AD_ATT);

    CallInst* call = b.CreateCall(ft, ia, {}, "");

    LLVMContext& ctx = c;
    MDNode* mark = MDNode::get(ctx, {MDString::get(ctx, "injected")});
    call->setMetadata(InjectedMd, mark);
}

static void CollectReturns(Function& f, std::vector<ReturnInst*>& out)
{
    for (BasicBlock& bb : f)
    {
        if (auto* ret = dyn_cast<ReturnInst>(bb.getTerminator())) out.push_back(ret);
    }
}

static void ProcessFunction(Function& f)
{
    if (f.isDeclaration()) return;
    if (!HasPTFloodAnnotation(f)) return;

    Module* m = f.getParent();
    if (!EmittedRing)
    {
        EmitThunkRing(*m);
        EmittedRing = true;
    }

    std::vector<ReturnInst*> rets;
    CollectReturns(f, rets);
    if (rets.empty()) return;

    LLVMContext& c = f.getContext();
    for (ReturnInst* ret : rets)
    {
        IRBuilder<> b(ret);
        EmitInlineFlood(b, c);
    }
}

bool RunPTFlood(Module& m)
{
    if (!PTFloodEnable) return false;

    EmittedRing = false;

    bool injected = false;
    for (Function& f : m)
    {
        if (f.isDeclaration()) continue;
        if (!HasPTFloodAnnotation(f)) continue;
        ProcessFunction(f);
        injected = true;
    }

    return injected;
}

PreservedAnalyses PTFloodPass::run(Module& m, ModuleAnalysisManager&)
{
    if (!RunPTFlood(m)) return PreservedAnalyses::all();
    return PreservedAnalyses::none();
}