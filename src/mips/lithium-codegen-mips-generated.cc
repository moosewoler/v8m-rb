// File src/mips/lithium-codegen-mips-generated.cc was mechanically generated
// from src/arm/lithium-codegen-arm-generated.cc by tools/arm2mips.py.

// File src/arm/lithium-codegen-arm-generated.cc was mechanically generated
// from src/arm/lithium-codegen-unarm.cc by tools/disarm.py.
// Do not make permanent manual edits here.

// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "v8.h"

#include "mips/lithium-codegen-mips.h"
#include "mips/lithium-gap-resolver-mips.h"
#include "code-stubs.h"
#include "stub-cache.h"

//#define NotYet V8_Fatal(__FILE__, __LINE__, "not yet implemented")
#define NotYet Abort("Unimplemented: %s line %d", __func__, __LINE__)

#ifdef V8_TARGET_ARCH_MIPS
#define MIPS_STUB
#define c_rval_reg v0
#define transcend_rval_reg f4
//#define RuntimeAbort __ cvt_w_s(f0, f0)
#define RuntimeAbort Abort("Unimplemented: %s line %d", __func__, __LINE__)
#else
//#define MIPS_STUB  // comment out to get full crankshaft, on Arm only
#define c_rval_reg a0
#define transcend_rval_reg f2
//#define RuntimeAbort __ bkpt(12345)
#define RuntimeAbort Abort("Unimplemented: %s line %d", __func__, __LINE__)
#endif

namespace v8 {
namespace internal {


class SafepointGenerator : public CallWrapper {
 public:
  SafepointGenerator(LCodeGen* codegen,
                     LPointerMap* pointers,
                     int deoptimization_index)
      : codegen_(codegen),
        pointers_(pointers),
        deoptimization_index_(deoptimization_index) { }
  virtual ~SafepointGenerator() { }

  virtual void BeforeCall(int call_size) const {
    ASSERT(call_size >= 0);
    // Ensure that we have enough space after the previous safepoint position
    // for the generated code there.
    int call_end = codegen_->masm()->pc_offset() + call_size;
    int prev_jump_end =
        codegen_->LastSafepointEnd() + Deoptimizer::patch_size();
    if (call_end < prev_jump_end) {
      int padding_size = prev_jump_end - call_end;
      ASSERT_EQ(0, padding_size % Assembler::kInstrSize);
      while (padding_size > 0) {
        codegen_->masm()->nop();
        padding_size -= Assembler::kInstrSize;
      }
    }
  }

  virtual void AfterCall() const {
    codegen_->RecordSafepoint(pointers_, deoptimization_index_);
  }

 private:
  LCodeGen* codegen_;
  LPointerMap* pointers_;
  int deoptimization_index_;
};


#define __ masm()->

bool LCodeGen::GenerateCode() {
  HPhase phase("Code generation", chunk());
  ASSERT(is_unused());
  status_ = GENERATING;
  CpuFeatures::Scope scope1(FPU);
#ifndef V8_TARGET_ARCH_MIPS
  CpuFeatures::Scope scope2(ARMv7);
#endif
  return GeneratePrologue() &&
      GenerateBody() &&
      GenerateDeferredCode() &&
      GenerateDeoptJumpTable() &&
      GenerateSafepointTable();
}


void LCodeGen::FinishCode(Handle<Code> code) {
  ASSERT(is_done());
  code->set_stack_slots(GetStackSlotCount());
  code->set_safepoint_table_offset(safepoints_.GetCodeOffset());
  PopulateDeoptimizationData(code);
  Deoptimizer::EnsureRelocSpaceForLazyDeoptimization(code);
}


void LCodeGen::Abort(const char* format, ...) {
  if (FLAG_trace_bailout) {
    SmartPointer<char> name(info()->shared_info()->DebugName()->ToCString());
    PrintF("Aborting LCodeGen in @\"%s\": ", *name);
    va_list arguments;
    va_start(arguments, format);
    OS::VPrint(format, arguments);
    va_end(arguments);
    PrintF("\n");
  }
  status_ = ABORTED;
}


void LCodeGen::Comment(const char* format, ...) {
  if (!FLAG_code_comments) return;
  char buffer[4 * KB];
  StringBuilder builder(buffer, ARRAY_SIZE(buffer));
  va_list arguments;
  va_start(arguments, format);
  builder.AddFormattedList(format, arguments);
  va_end(arguments);

  // Copy the string before recording it in the assembler to avoid
  // issues when the stack allocated buffer goes out of scope.
  size_t length = builder.position();
  Vector<char> copy = Vector<char>::New(length + 1);
  memcpy(copy.start(), builder.Finalize(), copy.length());
  masm()->RecordComment(copy.start());
}


bool LCodeGen::GeneratePrologue() {
  ASSERT(is_generating());

#ifdef DEBUG
  if (strlen(FLAG_stop_at) > 0 &&
      info_->function()->name()->IsEqualTo(CStrVector(FLAG_stop_at))) {
    __ stop("stop_at");
  }
#endif

  // a1: Callee's JS function.
  // cp: Callee's context.
  // fp: Caller's frame pointer.
  // ra: Caller's pc.

  // Strict mode functions and builtins need to replace the receiver
  // with undefined when called as functions (without an explicit
  // receiver object). t1 is zero for method calls and non-zero for
  // function calls.
  if (info_->is_strict_mode() || info_->is_native()) {
    Label ok;
    __ Branch(&ok, eq, t1, Operand(zero_reg));
    int receiver_offset = scope()->num_parameters() * kPointerSize;
    __ LoadRoot(a2, Heap::kUndefinedValueRootIndex);
    __ sw(a2, MemOperand(sp, receiver_offset));
    __ bind(&ok);
  }

  __ Push(ra, fp, cp, a1);
  __ Addu(fp, sp, Operand(2 * kPointerSize));  // Adjust FP to point to saved FP.

  // Reserve space for the stack slots needed by the code.
  int slots = GetStackSlotCount();
  if (slots > 0) {
    if (FLAG_debug_code) {
      __ li(a0, Operand(slots));
      __ li(a2, Operand(kSlotsZapValue));
      Label loop;
      __ bind(&loop);
      __ push(a2);
      __ Subu(a0, a0, Operand(1));
      __ Branch(&loop, ne, a0, Operand(zero_reg));
    } else {
      __ Subu(sp,  sp, Operand(slots * kPointerSize));
    }
  }

  // Possibly allocate a local context.
  int heap_slots = scope()->num_heap_slots() - Context::MIN_CONTEXT_SLOTS;
  if (heap_slots > 0) {
    Comment(";;; Allocate local context");
    // Argument to NewContext is the function, which is in a1.
    __ push(a1);
    if (heap_slots <= FastNewContextStub::kMaximumSlots) {
      FastNewContextStub stub(heap_slots);
      __ CallStub(&stub);
    } else {
      __ CallRuntime(Runtime::kNewFunctionContext, 1);
    }
    RecordSafepoint(Safepoint::kNoDeoptimizationIndex);
    // Context is returned in both cp and c_rval_reg.  It replaces the context
    // passed to us.  It's saved in the stack and kept live in cp.
    __ sw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
    // Copy any necessary parameters into the context.
    int num_parameters = scope()->num_parameters();
    for (int i = 0; i < num_parameters; i++) {
      Slot* slot = scope()->parameter(i)->AsSlot();
      if (slot != NULL && slot->type() == Slot::CONTEXT) {
        int parameter_offset = StandardFrameConstants::kCallerSPOffset +
            (num_parameters - 1 - i) * kPointerSize;
        // Load parameter from stack.
        __ lw(a0, MemOperand(fp, parameter_offset));
        // Store it in the context.
        __ li(a1, Operand(Context::SlotOffset(slot->index())));
        __ addu(a2, cp, a1);
        __ sw(a0, MemOperand(a2));
        // Update the write barrier. This clobbers all involved
        // registers, so we have to use two more registers to avoid
        // clobbering cp.
        __ mov(a2, cp);
        __ RecordWrite(a2, Operand(a1), a3, a0);
      }
    }
    Comment(";;; End allocate local context");
  }

  // Trace the call.
  if (FLAG_trace) {
    __ CallRuntime(Runtime::kTraceEnter, 0);
  }
  return !is_aborted();
}


bool LCodeGen::GenerateBody() {
  ASSERT(is_generating());
  bool emit_instructions = true;
  for (current_instruction_ = 0;
       !is_aborted() && current_instruction_ < instructions_->length();
       current_instruction_++) {
    LInstruction* instr = instructions_->at(current_instruction_);
    if (instr->IsLabel()) {
      LLabel* label = LLabel::cast(instr);
      emit_instructions = !label->HasReplacement();
    }

    if (emit_instructions) {
      Comment(";;; @%d: %s.", current_instruction_, instr->Mnemonic());
      instr->CompileToNative(this);
    }
  }
  return !is_aborted();
}


LInstruction* LCodeGen::GetNextInstruction() {
  if (current_instruction_ < instructions_->length() - 1) {
    return instructions_->at(current_instruction_ + 1);
  } else {
    return NULL;
  }
}


bool LCodeGen::GenerateDeferredCode() {
  ASSERT(is_generating());
  for (int i = 0; !is_aborted() && i < deferred_.length(); i++) {
    LDeferredCode* code = deferred_[i];
    __ bind(code->entry());
    code->Generate();
    __ jmp(code->exit());
  }

#ifndef V8_TARGET_ARCH_MIPS
  // Force constant pool emission at the end of the deferred code to make
  // sure that no constant pools are emitted after.
  masm()->CheckConstPool(true, false);
#endif

  return !is_aborted();
}


bool LCodeGen::GenerateDeoptJumpTable() {
#ifdef MIPS_STUB
  // empty jump table is nonfatal
#else
  // Check that the jump table is accessible from everywhere in the function
  // code, ie that offsets to the table can be encoded in the 24bit signed
  // immediate of a branch instruction.
  // To simplify we consider the code size from the first instruction to the
  // end of the jump table. We also don't consider the pc load delta.
  // Each entry in the jump table generates one instruction and inlines one
  // 32bit data after it.
  if (!is_int24((masm()->pc_offset() / Assembler::kInstrSize) +
      deopt_jump_table_.length() * 2)) {
    Abort("Generated code is too large");
  }

  // Block the constant pool emission during the jump table emission.
  __ BlockConstPoolFor(deopt_jump_table_.length());
  __ RecordComment("[ Deoptimisation jump table");
  Label table_start;
  __ bind(&table_start);
  for (int i = 0; i < deopt_jump_table_.length(); i++) {
    __ bind(&deopt_jump_table_[i].label);
    __ lw(pc, MemOperand(pc, Assembler::kInstrSize - Assembler::kPcLoadDelta));
    __ dd(reinterpret_cast<uint32_t>(deopt_jump_table_[i].address));
  }
  ASSERT(masm()->InstructionsGeneratedSince(&table_start) ==
      deopt_jump_table_.length() * 2);
  __ RecordComment("]");
#endif

  // The deoptimization jump table is the last part of the instruction
  // sequence. Mark the generated code as done unless we bailed out.
  if (!is_aborted()) status_ = DONE;
  return !is_aborted();
}


bool LCodeGen::GenerateSafepointTable() {
  ASSERT(is_done());
  safepoints_.Emit(masm(), GetStackSlotCount());
  return !is_aborted();
}


Register LCodeGen::ToRegister(int index) const {
  return Register::FromAllocationIndex(index);
}


DoubleRegister LCodeGen::ToDoubleRegister(int index) const {
  return DoubleRegister::FromAllocationIndex(index);
}


Register LCodeGen::ToRegister(LOperand* op) const {
  ASSERT(op->IsRegister());
  return ToRegister(op->index());
}


Register LCodeGen::EmitLoadRegister(LOperand* op, Register scratch) {
  if (op->IsRegister()) {
    return ToRegister(op->index());
  } else if (op->IsConstantOperand()) {
    __ li(scratch, ToOperand(op));
    return scratch;
  } else if (op->IsStackSlot() || op->IsArgument()) {
    __ lw(scratch, ToMemOperand(op));
    return scratch;
  }
  UNREACHABLE();
  return scratch;
}


DoubleRegister LCodeGen::ToDoubleRegister(LOperand* op) const {
  ASSERT(op->IsDoubleRegister());
  return ToDoubleRegister(op->index());
}


#if 0   // dead code
DoubleRegister LCodeGen::EmitLoadDoubleRegister(LOperand* op,
                                                FPURegister flt_scratch,
                                                DoubleRegister dbl_scratch) {
  if (op->IsDoubleRegister()) {
    return ToDoubleRegister(op->index());
  } else if (op->IsConstantOperand()) {
    LConstantOperand* const_op = LConstantOperand::cast(op);
    Handle<Object> literal = chunk_->LookupLiteral(const_op);
    Representation r = chunk_->LookupLiteralRepresentation(const_op);
    if (r.IsInteger32()) {
      ASSERT(literal->IsNumber());
      __ li(at, Operand(static_cast<int32_t>(literal->Number())));
      __ mtc1(at, flt_scratch);
      __ cvt_d_w(dbl_scratch, flt_scratch);
      return dbl_scratch;
    } else if (r.IsDouble()) {
      Abort("unsupported double immediate");
    } else if (r.IsTagged()) {
      Abort("unsupported tagged immediate");
    }
  } else if (op->IsStackSlot() || op->IsArgument()) {
    __ ldc1(dbl_scratch, ToMemOperand(op));
    return dbl_scratch;
  }
  UNREACHABLE();
  return dbl_scratch;
}
#endif


int LCodeGen::ToInteger32(LConstantOperand* op) const {
  Handle<Object> value = chunk_->LookupLiteral(op);
  ASSERT(chunk_->LookupLiteralRepresentation(op).IsInteger32());
  ASSERT(static_cast<double>(static_cast<int32_t>(value->Number())) ==
      value->Number());
  return static_cast<int32_t>(value->Number());
}


Operand LCodeGen::ToOperand(LOperand* op) {
  if (op->IsConstantOperand()) {
    LConstantOperand* const_op = LConstantOperand::cast(op);
    Handle<Object> literal = chunk_->LookupLiteral(const_op);
    Representation r = chunk_->LookupLiteralRepresentation(const_op);
    if (r.IsInteger32()) {
      ASSERT(literal->IsNumber());
      return Operand(static_cast<int32_t>(literal->Number()));
    } else if (r.IsDouble()) {
      Abort("ToOperand Unsupported double immediate.");
    }
    ASSERT(r.IsTagged());
    return Operand(literal);
  } else if (op->IsRegister()) {
    return Operand(ToRegister(op));
  } else if (op->IsDoubleRegister()) {
    Abort("ToOperand IsDoubleRegister unimplemented");
    return Operand(zero_reg);
  }
  // Stack slots not implemented, use ToMemOperand instead.
  UNREACHABLE();
  return Operand(zero_reg);
}


MemOperand LCodeGen::ToMemOperand(LOperand* op) const {
  ASSERT(!op->IsRegister());
  ASSERT(!op->IsDoubleRegister());
  ASSERT(op->IsStackSlot() || op->IsDoubleStackSlot());
  int index = op->index();
  if (index >= 0) {
    // Local or spill slot. Skip the frame pointer, function, and
    // context in the fixed part of the frame.
    return MemOperand(fp, -(index + 3) * kPointerSize);
  } else {
    // Incoming parameter. Skip the return address.
    return MemOperand(fp, -(index - 1) * kPointerSize);
  }
}


MemOperand LCodeGen::ToHighMemOperand(LOperand* op) const {
  ASSERT(op->IsDoubleStackSlot());
  int index = op->index();
  if (index >= 0) {
    // Local or spill slot. Skip the frame pointer, function, context,
    // and the first word of the double in the fixed part of the frame.
    return MemOperand(fp, -(index + 3) * kPointerSize + kPointerSize);
  } else {
    // Incoming parameter. Skip the return address and the first word of
    // the double.
    return MemOperand(fp, -(index - 1) * kPointerSize + kPointerSize);
  }
}


void LCodeGen::WriteTranslation(LEnvironment* environment,
                                Translation* translation) {
  if (environment == NULL) return;

  // The translation includes one command per value in the environment.
  int translation_size = environment->values()->length();
  // The output frame height does not include the parameters.
  int height = translation_size - environment->parameter_count();

  WriteTranslation(environment->outer(), translation);
  int closure_id = DefineDeoptimizationLiteral(environment->closure());
  translation->BeginFrame(environment->ast_id(), closure_id, height);
  for (int i = 0; i < translation_size; ++i) {
    LOperand* value = environment->values()->at(i);
    // spilled_registers_ and spilled_double_registers_ are either
    // both NULL or both set.
    if (environment->spilled_registers() != NULL && value != NULL) {
      if (value->IsRegister() &&
          environment->spilled_registers()[value->index()] != NULL) {
        translation->MarkDuplicate();
        AddToTranslation(translation,
                         environment->spilled_registers()[value->index()],
                         environment->HasTaggedValueAt(i));
      } else if (
          value->IsDoubleRegister() &&
          environment->spilled_double_registers()[value->index()] != NULL) {
        translation->MarkDuplicate();
        AddToTranslation(
            translation,
            environment->spilled_double_registers()[value->index()],
            false);
      }
    }

    AddToTranslation(translation, value, environment->HasTaggedValueAt(i));
  }
}


void LCodeGen::AddToTranslation(Translation* translation,
                                LOperand* op,
                                bool is_tagged) {
  if (op == NULL) {
    // TODO(twuerthinger): Introduce marker operands to indicate that this value
    // is not present and must be reconstructed from the deoptimizer. Currently
    // this is only used for the arguments object.
    translation->StoreArgumentsObject();
  } else if (op->IsStackSlot()) {
    if (is_tagged) {
      translation->StoreStackSlot(op->index());
    } else {
      translation->StoreInt32StackSlot(op->index());
    }
  } else if (op->IsDoubleStackSlot()) {
    translation->StoreDoubleStackSlot(op->index());
  } else if (op->IsArgument()) {
    ASSERT(is_tagged);
    int src_index = GetStackSlotCount() + op->index();
    translation->StoreStackSlot(src_index);
  } else if (op->IsRegister()) {
    Register reg = ToRegister(op);
    if (is_tagged) {
      translation->StoreRegister(reg);
    } else {
      translation->StoreInt32Register(reg);
    }
  } else if (op->IsDoubleRegister()) {
    DoubleRegister reg = ToDoubleRegister(op);
    translation->StoreDoubleRegister(reg);
  } else if (op->IsConstantOperand()) {
    Handle<Object> literal = chunk()->LookupLiteral(LConstantOperand::cast(op));
    int src_index = DefineDeoptimizationLiteral(literal);
    translation->StoreLiteral(src_index);
  } else {
    UNREACHABLE();
  }
}


void LCodeGen::CallCode(Handle<Code> code,
                        RelocInfo::Mode mode,
                        LInstruction* instr) {
  CallCodeGeneric(code, mode, instr, RECORD_SIMPLE_SAFEPOINT);
}


void LCodeGen::CallCodeGeneric(Handle<Code> code,
                               RelocInfo::Mode mode,
                               LInstruction* instr,
                               SafepointMode safepoint_mode) {
  ASSERT(instr != NULL);
  LPointerMap* pointers = instr->pointer_map();
  RecordPosition(pointers->position());
  __ Call(code, mode);
  RegisterLazyDeoptimization(instr, safepoint_mode);
}


void LCodeGen::CallRuntime(const Runtime::Function* function,
                           int num_arguments,
                           LInstruction* instr) {
  ASSERT(instr != NULL);
  LPointerMap* pointers = instr->pointer_map();
  ASSERT(pointers != NULL);
  RecordPosition(pointers->position());

  __ CallRuntime(function, num_arguments);
  RegisterLazyDeoptimization(instr, RECORD_SIMPLE_SAFEPOINT);
}


void LCodeGen::CallRuntimeFromDeferred(Runtime::FunctionId id,
                                       int argc,
                                       LInstruction* instr) {
  __ CallRuntimeSaveDoubles(id);
  RecordSafepointWithRegisters(
      instr->pointer_map(), argc, Safepoint::kNoDeoptimizationIndex);
}


void LCodeGen::RegisterLazyDeoptimization(LInstruction* instr,
                                          SafepointMode safepoint_mode) {
  // Create the environment to bailout to. If the call has side effects
  // execution has to continue after the call otherwise execution can continue
  // from a previous bailout point repeating the call.
  LEnvironment* deoptimization_environment;
  if (instr->HasDeoptimizationEnvironment()) {
    deoptimization_environment = instr->deoptimization_environment();
  } else {
    deoptimization_environment = instr->environment();
  }

  RegisterEnvironmentForDeoptimization(deoptimization_environment);
  if (safepoint_mode == RECORD_SIMPLE_SAFEPOINT) {
    RecordSafepoint(instr->pointer_map(),
                    deoptimization_environment->deoptimization_index());
  } else {
    ASSERT(safepoint_mode == RECORD_SAFEPOINT_WITH_REGISTERS_AND_NO_ARGUMENTS);
    RecordSafepointWithRegisters(
        instr->pointer_map(),
        0,
        deoptimization_environment->deoptimization_index());
  }
}


void LCodeGen::RegisterEnvironmentForDeoptimization(LEnvironment* environment) {
  if (!environment->HasBeenRegistered()) {
    // Physical stack frame layout:
    // -x ............. -4  0 ..................................... y
    // [incoming arguments] [spill slots] [pushed outgoing arguments]

    // Layout of the environment:
    // 0 ..................................................... size-1
    // [parameters] [locals] [expression stack including arguments]

    // Layout of the translation:
    // 0 ........................................................ size - 1 + 4
    // [expression stack including arguments] [locals] [4 words] [parameters]
    // |>------------  translation_size ------------<|

    int frame_count = 0;
    for (LEnvironment* e = environment; e != NULL; e = e->outer()) {
      ++frame_count;
    }
    Translation translation(&translations_, frame_count);
    WriteTranslation(environment, &translation);
    int deoptimization_index = deoptimizations_.length();
    environment->Register(deoptimization_index, translation.index());
    deoptimizations_.Add(environment);
  }
}


void LCodeGen::DeoptimizeIf(Condition cond, LEnvironment* environment,
                            Register src1, const Operand& src2) {
  RegisterEnvironmentForDeoptimization(environment);
  ASSERT(environment->HasBeenRegistered());
  int id = environment->deoptimization_index();
  Address entry = Deoptimizer::GetDeoptimizationEntry(id, Deoptimizer::EAGER);
  ASSERT(entry != NULL);
  if (entry == NULL) {
    Abort("bailout was not prepared");
    return;
  }

  ASSERT(FLAG_deopt_every_n_times < 2);  // Other values not supported on ARM.
  if (FLAG_deopt_every_n_times == 1 &&
      info_->shared_info()->opt_count() == id) {
    cond = al;
  }
  if (cond == al) {
    __ Jump(entry, RelocInfo::RUNTIME_ENTRY);
  } else {
    if (FLAG_trap_on_deopt) {
       Label nonstop;
       __ Branch(&nonstop, NegateCondition(cond), src1, src2);
       __ stop("trap_on_deopt");
       __ bind(&nonstop);
    }

#ifdef MIPS_STUB
   __ Jump(entry, RelocInfo::RUNTIME_ENTRY, cond, src1, src2);
#else
    // We often have several deopts to the same entry, reuse the last
    // jump entry if this is the case.
    if (deopt_jump_table_.is_empty() ||
        (deopt_jump_table_.last().address != entry)) {
      deopt_jump_table_.Add(JumpTableEntry(entry));
    }
    __ Branch(&deopt_jump_table_.last().label, cond, src1, src2);
#endif
  }
}

#ifndef V8_TARGET_ARCH_MIPS
void LCodeGen::DeoptimizeIf(Condition cond, LEnvironment* environment) {
  // Used only after EmitVFPTruncate
  RegisterEnvironmentForDeoptimization(environment);
  ASSERT(environment->HasBeenRegistered());
  int id = environment->deoptimization_index();
  Address entry = Deoptimizer::GetDeoptimizationEntry(id, Deoptimizer::EAGER);
  ASSERT(entry != NULL);
  if (entry == NULL) {
    Abort("bailout was not prepared");
    return;
  }

  ASSERT(FLAG_deopt_every_n_times < 2);  // Other values not supported on ARM.
  if (FLAG_deopt_every_n_times == 1 &&
      info_->shared_info()->opt_count() == id) {
    cond = al;
  }
  if (cond == al) {
    __ Jump(entry, RelocInfo::RUNTIME_ENTRY);
  } else {
    if (FLAG_trap_on_deopt) {
       Label nonstop;
       __ Branch(NegateCondition(cond), &nonstop);
       __ stop("trap_on_deopt");
       __ bind(&nonstop);
    }

#ifdef MIPS_STUB
    __ Jump(entry, RelocInfo::RUNTIME_ENTRY, cond);
#else
    // We often have several deopts to the same entry, reuse the last
    // jump entry if this is the case.
    if (deopt_jump_table_.is_empty() ||
        (deopt_jump_table_.last().address != entry)) {
      deopt_jump_table_.Add(JumpTableEntry(entry));
    }
    __ Branch(cond, &deopt_jump_table_.last().label);
#endif
  }
}
#endif


void LCodeGen::PopulateDeoptimizationData(Handle<Code> code) {
  int length = deoptimizations_.length();
  if (length == 0) return;
  ASSERT(FLAG_deopt);
  Handle<DeoptimizationInputData> data =
      factory()->NewDeoptimizationInputData(length, TENURED);

  Handle<ByteArray> translations = translations_.CreateByteArray();
  data->SetTranslationByteArray(*translations);
  data->SetInlinedFunctionCount(Smi::FromInt(inlined_function_count_));

  Handle<FixedArray> literals =
      factory()->NewFixedArray(deoptimization_literals_.length(), TENURED);
  for (int i = 0; i < deoptimization_literals_.length(); i++) {
    literals->set(i, *deoptimization_literals_[i]);
  }
  data->SetLiteralArray(*literals);

  data->SetOsrAstId(Smi::FromInt(info_->osr_ast_id()));
  data->SetOsrPcOffset(Smi::FromInt(osr_pc_offset_));

  // Populate the deoptimization entries.
  for (int i = 0; i < length; i++) {
    LEnvironment* env = deoptimizations_[i];
    data->SetAstId(i, Smi::FromInt(env->ast_id()));
    data->SetTranslationIndex(i, Smi::FromInt(env->translation_index()));
    data->SetArgumentsStackHeight(i,
                                  Smi::FromInt(env->arguments_stack_height()));
  }
  code->set_deoptimization_data(*data);
}


int LCodeGen::DefineDeoptimizationLiteral(Handle<Object> literal) {
  int result = deoptimization_literals_.length();
  for (int i = 0; i < deoptimization_literals_.length(); ++i) {
    if (deoptimization_literals_[i].is_identical_to(literal)) return i;
  }
  deoptimization_literals_.Add(literal);
  return result;
}


void LCodeGen::PopulateDeoptimizationLiteralsWithInlinedFunctions() {
  ASSERT(deoptimization_literals_.length() == 0);

  const ZoneList<Handle<JSFunction> >* inlined_closures =
      chunk()->inlined_closures();

  for (int i = 0, length = inlined_closures->length();
       i < length;
       i++) {
    DefineDeoptimizationLiteral(inlined_closures->at(i));
  }

  inlined_function_count_ = deoptimization_literals_.length();
}


void LCodeGen::RecordSafepoint(
    LPointerMap* pointers,
    Safepoint::Kind kind,
    int arguments,
    int deoptimization_index) {
  ASSERT(expected_safepoint_kind_ == kind);

  const ZoneList<LOperand*>* operands = pointers->operands();
  Safepoint safepoint = safepoints_.DefineSafepoint(masm(),
      kind, arguments, deoptimization_index);
  for (int i = 0; i < operands->length(); i++) {
    LOperand* pointer = operands->at(i);
    if (pointer->IsStackSlot()) {
      safepoint.DefinePointerSlot(pointer->index());
    } else if (pointer->IsRegister() && (kind & Safepoint::kWithRegisters)) {
      safepoint.DefinePointerRegister(ToRegister(pointer));
    }
  }
  if (kind & Safepoint::kWithRegisters) {
    // Register cp always contains a pointer to the context.
    safepoint.DefinePointerRegister(cp);
  }
}


void LCodeGen::RecordSafepoint(LPointerMap* pointers,
                               int deoptimization_index) {
  RecordSafepoint(pointers, Safepoint::kSimple, 0, deoptimization_index);
}


void LCodeGen::RecordSafepoint(int deoptimization_index) {
  LPointerMap empty_pointers(RelocInfo::kNoPosition);
  RecordSafepoint(&empty_pointers, deoptimization_index);
}


void LCodeGen::RecordSafepointWithRegisters(LPointerMap* pointers,
                                            int arguments,
                                            int deoptimization_index) {
  RecordSafepoint(pointers, Safepoint::kWithRegisters, arguments,
      deoptimization_index);
}


void LCodeGen::RecordSafepointWithRegistersAndDoubles(
    LPointerMap* pointers,
    int arguments,
    int deoptimization_index) {
  RecordSafepoint(pointers, Safepoint::kWithRegistersAndDoubles, arguments,
      deoptimization_index);
}


void LCodeGen::RecordPosition(int position) {
  if (position == RelocInfo::kNoPosition) return;
  masm()->positions_recorder()->RecordPosition(position);
}


void LCodeGen::DoLabel(LLabel* label) {
  if (label->is_loop_header()) {
    Comment(";;; B%d - LOOP entry", label->block_id());
  } else {
    Comment(";;; B%d", label->block_id());
  }
  __ bind(label->label());
  current_block_ = label->block_id();
  DoGap(label);
}


void LCodeGen::DoParallelMove(LParallelMove* move) {
  resolver_.Resolve(move);
}


void LCodeGen::DoGap(LGap* gap) {
  for (int i = LGap::FIRST_INNER_POSITION;
       i <= LGap::LAST_INNER_POSITION;
       i++) {
    LGap::InnerPosition inner_pos = static_cast<LGap::InnerPosition>(i);
    LParallelMove* move = gap->GetParallelMove(inner_pos);
    if (move != NULL) DoParallelMove(move);
  }

  LInstruction* next = GetNextInstruction();
  if (next != NULL && next->IsLazyBailout()) {
    int pc = masm()->pc_offset();
    safepoints_.SetPcAfterGap(pc);
  }
}


void LCodeGen::DoInstructionGap(LInstructionGap* instr) {
  DoGap(instr);
}


void LCodeGen::DoParameter(LParameter* instr) {
  // Nothing to do.
}


void LCodeGen::DoCallStub(LCallStub* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  switch (instr->hydrogen()->major_key()) {
    case CodeStub::RegExpConstructResult: {
      RegExpConstructResultStub stub;
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::RegExpExec: {
      RegExpExecStub stub;
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::SubString: {
      SubStringStub stub;
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::NumberToString: {
      NumberToStringStub stub;
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::StringAdd: {
      StringAddStub stub(NO_STRING_ADD_FLAGS);
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::StringCompare: {
      StringCompareStub stub;
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    case CodeStub::TranscendentalCache: {
      __ lw(a0, MemOperand(sp, 0));
      TranscendentalCacheStub stub(instr->transcendental_type(),
                                   TranscendentalCacheStub::TAGGED);
      CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
      break;
    }
    default:
      UNREACHABLE();
  }
}


void LCodeGen::DoUnknownOSRValue(LUnknownOSRValue* instr) {
  // Nothing to do.
}


void LCodeGen::DoModI(LModI* instr) {
  if (instr->hydrogen()->HasPowerOf2Divisor()) {
    Register dividend = ToRegister(instr->InputAt(0));
    Register result = ToRegister(instr->result());

    int32_t divisor =
        HConstant::cast(instr->hydrogen()->right())->Integer32Value();

    if (divisor < 0) divisor = -divisor;

    Label positive_dividend, done;
    __ Branch(&positive_dividend, ge, dividend, Operand(zero_reg));
    __ negu(result, dividend);
    __ And(result, result, Operand(divisor - 1));
    if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      DeoptimizeIf(eq, instr->environment(), result, Operand(zero_reg));
    }
    __ negu(result, result);
    __ Branch(&done);
    __ bind(&positive_dividend);
    __ And(result, dividend, Operand(divisor - 1));
    __ bind(&done);
    return;
  }

  // These registers hold untagged 32 bit values.
  Register left = ToRegister(instr->InputAt(0));
  Register right = ToRegister(instr->InputAt(1));
  Register result = ToRegister(instr->result());

  Register scratch = scratch0();
  Register scratch2 = ToRegister(instr->TempAt(0));
  FPURegister dividend = ToDoubleRegister(instr->TempAt(1));
  FPURegister divisor = ToDoubleRegister(instr->TempAt(2));
  FPURegister quotient = double_scratch0();

  ASSERT(!dividend.is(divisor));
  ASSERT(!dividend.is(quotient));
  ASSERT(!divisor.is(quotient));
  ASSERT(!scratch.is(left));
  ASSERT(!scratch.is(right));
  ASSERT(!scratch.is(result));

  Label done, vfp_modulo, both_positive, right_negative;

  // Check for x % 0.
  if (instr->hydrogen()->CheckFlag(HValue::kCanBeDivByZero)) {
    DeoptimizeIf(eq, instr->environment(), right, Operand(zero_reg));
  }

  __ Move(result, left);

  // (0 % x) must yield 0 (if x is finite, which is the case here).
  __ Branch(&done, eq, left, Operand(zero_reg));
  // Preload right in a vfp register.
  __ mtc1(right, divisor.low());
  __ Branch(&vfp_modulo, lt, left, Operand(zero_reg));

  __ Branch(&done, lt, left, Operand(right));

  // Check for (positive) power of two on the right hand side.
  __ Subu(scratch, right, Operand(1));
  __ Branch(&right_negative, lt, scratch, Operand(zero_reg));
  __ and_(at, scratch, right);
  __ Branch(&both_positive, ne, at, Operand(zero_reg));

  // Perform modulo operation (scratch contains right - 1).
  __ and_(result, scratch, left);
  __ Branch(&done);

  __ bind(&right_negative);
  // Negate right. The sign of the divisor does not matter.
  __ negu(right, right);

  __ bind(&both_positive);
  const int kUnfolds = 3;
  // If the right hand side is smaller than the (nonnegative)
  // left hand side, the left hand side is the result.
  // Else try a few subtractions of the left hand side.
  __ mov(scratch, left);
  for (int i = 0; i < kUnfolds; i++) {
    Label more;
    // Check if the left hand side is less or equal than the
    // the right hand side.
    __ Branch(&more, ge, scratch, Operand(right));
    if (!result.is(scratch))
      __ mov(result, scratch);
    __ Branch(&done);
    __ bind(&more);
    // If not, reduce the left hand side by the right hand
    // side and check again.
    if (i < kUnfolds - 1)
       __ subu(scratch, scratch, right);
  }

  __ bind(&vfp_modulo);
  // Load the arguments in VFP registers.
  // The divisor value is preloaded before. Be careful that 'right' is only live
  // on entry.
  __ mtc1(left, dividend.low());
  // From here on don't use right as it may have been reallocated (for example
  // to scratch2).
  right = no_reg;

  __ cvt_d_w(dividend, dividend.low());
  __ cvt_d_w(divisor, divisor.low());

  // We do not care about the sign of the divisor.
  __ abs_d(divisor, divisor);
  // Compute the quotient and round it to a 32bit integer.
  __ div_d(quotient, dividend, divisor);
  __ cvt_w_d(quotient.low(), quotient);
  __ cvt_d_w(quotient, quotient.low());

  // Compute the remainder in result.
  FPURegister double_scratch = dividend;
  __ mul_d(double_scratch, divisor, quotient);
  __ cvt_w_d(double_scratch.low(), double_scratch);
  __ mfc1(scratch, double_scratch.low());

  if (!instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    __ subu(result, left, scratch);
  } else {
    Label ok;
    // Check for -0.
    __ subu(scratch2, left, scratch);
    __ Branch(&ok, ne, scratch2, Operand(zero_reg));
    DeoptimizeIf(lt, instr->environment(), left, Operand(zero_reg));
    __ bind(&ok);
    // Load the result and we are done.
    __ mov(result, scratch2);
  }

  __ bind(&done);
}


void LCodeGen::DoDivI(LDivI* instr) {
  class DeferredDivI: public LDeferredCode {
   public:
    DeferredDivI(LCodeGen* codegen, LDivI* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() {
      codegen()->DoDeferredBinaryOpStub(instr_, Token::DIV);
    }
   private:
    LDivI* instr_;
  };

  const Register left = ToRegister(instr->InputAt(0));
  const Register right = ToRegister(instr->InputAt(1));
  const Register scratch = scratch0();
  const Register result = ToRegister(instr->result());
  Label more1, more2, more3;

  // Check for x / 0.
  if (instr->hydrogen()->CheckFlag(HValue::kCanBeDivByZero)) {
    DeoptimizeIf(eq, instr->environment(), right, Operand(zero_reg));
  }

  // Check for (0 / -x) that will produce negative zero.
  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    Label left_not_zero;
    __ Branch(&left_not_zero, ne, left, Operand(zero_reg));
    DeoptimizeIf(lt, instr->environment(), right, Operand(zero_reg));
    __ bind(&left_not_zero);
  }

  // Check for (-kMinInt / -1).
  if (instr->hydrogen()->CheckFlag(HValue::kCanOverflow)) {
    Label left_not_min_int;
    __ Branch(&left_not_min_int, ne, left, Operand(kMinInt));
    DeoptimizeIf(eq, instr->environment(), right, Operand(-1));
    __ bind(&left_not_min_int);
  }

  Label done, deoptimize;
  // Test for a few common cases first.
  __ Branch(&more1, ne, right, Operand(1));
  if (!result.is(left))
    __ mov(result, left);
  __ Branch(&done);
  __ bind(&more1);

  __ Branch(&more2, ne, right, Operand(2));
  __ And(at, left, Operand(1));
  __ Branch(&more2, ne, at, Operand(zero_reg));
  __ sra(result, left, 1);
  __ Branch(&done);
  __ bind(&more2);

  __ Branch(&more3, ne, right, Operand(4));
  __ And(at, left, Operand(3));
  __ Branch(&more3, ne, at, Operand(zero_reg));
  __ sra(result, left, 2);
  __ Branch(&done);
  __ bind(&more3);

  // Call the stub. The numbers in left and right have
  // to be tagged to Smis. If that is not possible, deoptimize.
  DeferredDivI* deferred = new DeferredDivI(this, instr);

  __ TrySmiTag(left, &deoptimize, scratch);
  __ TrySmiTag(right, &deoptimize, scratch);

  __ Branch(deferred->entry());
  __ bind(deferred->exit());

  // If the value in result is a Smi, untag it, else deoptimize.
  __ JumpIfNotSmi(result, &deoptimize);
  __ SmiUntag(result);
  __ Branch(&done);

  __ bind(&deoptimize);
  Deoptimize(instr->environment());
  __ bind(&done);
}


template<int T>
void LCodeGen::DoDeferredBinaryOpStub(LTemplateInstruction<1, 2, T>* instr,
                                      Token::Value op) {
  Register left = ToRegister(instr->InputAt(0));
  Register right = ToRegister(instr->InputAt(1));
  Register result = ToRegister(instr->result());

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegistersAndDoubles);
  // Move left to a1 and right to a0 for the stub call.
  if (left.is(a1)) {
    __ Move(a0, right);
  } else if (left.is(a0) && right.is(a1)) {
    __ Swap(a0, a1, a2);
  } else if (left.is(a0)) {
    ASSERT(!right.is(a1));
    __ mov(a1, a0);
    __ mov(a0, right);
  } else {
    ASSERT(!left.is(a0) && !right.is(a0));
    __ mov(a0, right);
    __ mov(a1, left);
  }
  BinaryOpStub stub(op, OVERWRITE_LEFT);
  __ CallStub(&stub);
  RecordSafepointWithRegistersAndDoubles(instr->pointer_map(),
                                         0,
                                         Safepoint::kNoDeoptimizationIndex);
  // Overwrite the stored value of c_rval_reg with the result of the stub.
  __ StoreToSafepointRegistersAndDoublesSlot(c_rval_reg, result);
}


void LCodeGen::DoMulI(LMulI* instr) {
  Register scratch = scratch0();
  Register result = ToRegister(instr->result());
  // Note that result may alias left.
  Register left = ToRegister(instr->InputAt(0));
  LOperand* right_op = instr->InputAt(1);

  bool can_overflow = instr->hydrogen()->CheckFlag(HValue::kCanOverflow);
  bool bailout_on_minus_zero =
    instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero);

  if (right_op->IsConstantOperand() && !can_overflow) {
    // Use optimized code for specific constants.
    int32_t constant = ToInteger32(LConstantOperand::cast(right_op));

    if (bailout_on_minus_zero && (constant < 0)) {
      // The case of a null constant will be handled separately.
      // If constant is negative and left is null, the result should be -0.
      DeoptimizeIf(eq, instr->environment(), left, Operand(zero_reg));
    }

    switch (constant) {
      case -1:
        __ negu(result, left);
        break;
      case 0:
        if (bailout_on_minus_zero) {
          // If left is strictly negative and the constant is null, the
          // result is -0. Deoptimize if required, otherwise return 0.
          DeoptimizeIf(lt, instr->environment(), left, Operand(zero_reg));
        }
        __ mov(result, zero_reg);
        break;
      case 1:
        __ Move(result, left);
        break;
      default:
        // Multiplying by powers of two and powers of two plus or minus
        // one can be done faster with shifted operands.
        // For other constants we emit standard code.
        int32_t mask = constant >> 31;
        uint32_t constant_abs = (constant + mask) ^ mask;

        if (IsPowerOf2(constant_abs) ||
            IsPowerOf2(constant_abs - 1) ||
            IsPowerOf2(constant_abs + 1)) {
          if (IsPowerOf2(constant_abs)) {
            int32_t shift = WhichPowerOf2(constant_abs);
            __ sll(result, left, shift);
          } else if (IsPowerOf2(constant_abs - 1)) {
            int32_t shift = WhichPowerOf2(constant_abs - 1);
            __ sll(at, left, shift);
            __ addu(result, left, at);
          } else if (IsPowerOf2(constant_abs + 1)) {
            int32_t shift = WhichPowerOf2(constant_abs + 1);
            __ sll(at, left, shift);
            __ subu(result, at, left);
          }

          // Correct the sign of the result is the constant is negative.
          if (constant < 0)
            __ negu(result, result);

        } else {
          // Generate standard code.
          __ li(at, Operand(constant));
          __ mul(result, left, at);
        }
    }

  } else {
    Register right = EmitLoadRegister(right_op, scratch);
    if (bailout_on_minus_zero) {
      __ or_(ToRegister(instr->TempAt(0)), left, right);
    }

    if (can_overflow) {
      // scratch:result = left * right.
      __ mult(left, right);
      __ mflo(result);
      __ mfhi(scratch);
      __ sra(at, result, 31);
      DeoptimizeIf(ne, instr->environment(), scratch, at);
    } else {
      __ mul(result, left, right);
    }

    if (bailout_on_minus_zero) {
      // Bail out if the result is supposed to be negative zero.
      Label done;
      __ Branch(&done, ne, result, Operand(zero_reg));
      DeoptimizeIf(lt, instr->environment(), ToRegister(instr->TempAt(0)), Operand(zero_reg));
      __ bind(&done);
    }
  }
}


void LCodeGen::DoBitI(LBitI* instr) {
  LOperand* left_op = instr->InputAt(0);
  LOperand* right_op = instr->InputAt(1);
  ASSERT(left_op->IsRegister());
  Register left = ToRegister(left_op);
  Register result = ToRegister(instr->result());
  Operand righto(no_reg);

  if (right_op->IsStackSlot() || right_op->IsArgument()) {
    righto = Operand(EmitLoadRegister(right_op, at));
  } else {
    ASSERT(right_op->IsRegister() || right_op->IsConstantOperand());
    righto = ToOperand(right_op);
  }

  switch (instr->op()) {
    case Token::BIT_AND:
      __ And(result, left, righto);
      break;
    case Token::BIT_OR:
      __ Or(result, left, righto);
      break;
    case Token::BIT_XOR:
      __ Xor(result, left, righto);
      break;
    default:
      UNREACHABLE();
      break;
  }
}


void LCodeGen::DoShiftI(LShiftI* instr) {
  // Both 'left' and 'right' are "used at start" (see LCodeGen::DoShift), so
  // result may alias either of them.
  LOperand* right_op = instr->InputAt(1);
  Register left = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();
  if (right_op->IsRegister()) {
    // Mask the right_op operand.
    __ And(scratch, ToRegister(right_op), Operand(0x1F));
    switch (instr->op()) {
      case Token::SAR:
        __ srav(result, left, scratch);
        break;
      case Token::SHR:
        __ srlv(result, left, scratch);
        if (instr->can_deopt()) {
          DeoptimizeIf(lt, instr->environment(), result, Operand(zero_reg));
        }
        break;
      case Token::SHL:
        __ sllv(result, left, scratch);
        break;
      default:
        UNREACHABLE();
        break;
    }
  } else {
    // Mask the right_op operand.
    int value = ToInteger32(LConstantOperand::cast(right_op));
    uint8_t shift_count = static_cast<uint8_t>(value & 0x1F);
    switch (instr->op()) {
      case Token::SAR:
        if (shift_count != 0) {
          __ sra(result, left, shift_count);
        } else {
          __ Move(result, left);
        }
        break;
      case Token::SHR:
        if (shift_count != 0) {
          __ srl(result, left, shift_count);
        } else {
          if (instr->can_deopt()) {
            DeoptimizeIf(lt, instr->environment(), left, Operand(zero_reg));
          }
          __ Move(result, left);
        }
        break;
      case Token::SHL:
        if (shift_count != 0) {
          __ sll(result, left, shift_count);
        } else {
          __ Move(result, left);
        }
        break;
      default:
        UNREACHABLE();
        break;
    }
  }
}


void LCodeGen::DoSubI(LSubI* instr) {
  LOperand* left = instr->InputAt(0);
  LOperand* right = instr->InputAt(1);
  LOperand* result = instr->result();
  Operand right_operand(no_reg);
  Register left_reg = ToRegister(left);
  bool can_overflow = instr->hydrogen()->CheckFlag(HValue::kCanOverflow);

  if (right->IsStackSlot() || right->IsArgument()) {
    Register right_reg = EmitLoadRegister(right, at);
    right_operand = Operand(right_reg);
  } else {
    ASSERT(right->IsRegister() || right->IsConstantOperand());
    right_operand = ToOperand(right);
  }
  if (can_overflow) {
    // Following sequence really needs more temp regs
    // Cannot use at, which is in use by right_operand
    Register scratch = scratch0();
    ASSERT(!scratch.is(left_reg));
    ASSERT(!(right_operand.rm().is_valid() && right_operand.rm().is(scratch)));
    Label or_else;
    __ Xor(scratch, left_reg, right_operand);
    __ Branch(&or_else, ge, scratch, Operand(zero_reg));  // pos: inputs have same signs so no overflow
    // inputs have different sign
    __ Subu(scratch, left_reg, right_operand);  // trial sub, preserving inputs
    __ xor_(scratch, scratch, left_reg); // neg: result changed sign
    __ bind(&or_else);  // neg: overflow 
    DeoptimizeIf(lt, instr->environment(), scratch, Operand(zero_reg));
  }
  __ Subu(ToRegister(result), left_reg, right_operand);
}


void LCodeGen::DoConstantI(LConstantI* instr) {
  ASSERT(instr->result()->IsRegister());
  __ li(ToRegister(instr->result()), Operand(instr->value()));
}


void LCodeGen::DoConstantD(LConstantD* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  ASSERT(instr->result()->IsDoubleRegister());
  FPURegister result = ToDoubleRegister(instr->result());
  double v = instr->value();
  __ mov_d(result, v);
#endif
}


void LCodeGen::DoConstantT(LConstantT* instr) {
  ASSERT(instr->result()->IsRegister());
  __ li(ToRegister(instr->result()), Operand(instr->value()));
}


void LCodeGen::DoJSArrayLength(LJSArrayLength* instr) {
  Register result = ToRegister(instr->result());
  Register array = ToRegister(instr->InputAt(0));
  __ lw(result, FieldMemOperand(array, JSArray::kLengthOffset));
}


void LCodeGen::DoExternalArrayLength(LExternalArrayLength* instr) {
  Register result = ToRegister(instr->result());
  Register array = ToRegister(instr->InputAt(0));
  __ lw(result, FieldMemOperand(array, ExternalArray::kLengthOffset));
}


void LCodeGen::DoFixedArrayLength(LFixedArrayLength* instr) {
  Register result = ToRegister(instr->result());
  Register array = ToRegister(instr->InputAt(0));
  __ lw(result, FieldMemOperand(array, FixedArray::kLengthOffset));
}


void LCodeGen::DoElementsKind(LElementsKind* instr) {
  Register result = ToRegister(instr->result());
  Register input = ToRegister(instr->InputAt(0));

  // Load map into |result|.
  __ lw(result, FieldMemOperand(input, HeapObject::kMapOffset));
  // Load the map's "bit field 2" into |result|. We only need the first byte,
  // but the following bit field extraction takes care of that anyway.
  __ lw(result, FieldMemOperand(result, Map::kBitField2Offset));
  // Retrieve elements_kind from bit field 2.
  __ Ext(result, result, Map::kElementsKindShift, Map::kElementsKindBitCount);
}


void LCodeGen::DoValueOf(LValueOf* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  Register map = ToRegister(instr->TempAt(0));
  Label done;

  // If the object is a smi return the object.
  __ Move(result, input);
  __ JumpIfSmi(input, &done);

  // If the object is not a value type, return the object.
  __ GetObjectType(input, map, map);
  __ Branch(&done, ne, map, Operand(JS_VALUE_TYPE));
  __ lw(result, FieldMemOperand(input, JSValue::kValueOffset));

  __ bind(&done);
}


void LCodeGen::DoBitNotI(LBitNotI* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  __ not_(result, input);
}


void LCodeGen::DoThrow(LThrow* instr) {
  Register input_reg = EmitLoadRegister(instr->InputAt(0), at);
  __ push(input_reg);
  CallRuntime(Runtime::kThrow, 1, instr);

  if (FLAG_debug_code) {
    __ stop("Unreachable code.");
  }
}


void LCodeGen::DoAddI(LAddI* instr) {
  LOperand* left = instr->InputAt(0);
  LOperand* right = instr->InputAt(1);
  LOperand* result = instr->result();
  Operand right_operand(no_reg);
  Register left_reg = ToRegister(left);
  bool can_overflow = instr->hydrogen()->CheckFlag(HValue::kCanOverflow);

  if (right->IsStackSlot() || right->IsArgument()) {
    Register right_reg = EmitLoadRegister(right, at);
    right_operand = Operand(right_reg);
  } else {
    ASSERT(right->IsRegister() || right->IsConstantOperand());
    right_operand = ToOperand(right);
  }
  if (can_overflow) {
    // Following sequence really needs more temp regs
    // Cannot use at, which is in use by right_operand
    Register scratch = scratch0();
    ASSERT(!scratch.is(left_reg));
    ASSERT(!(right_operand.rm().is_valid() && right_operand.rm().is(scratch)));
    Label or_else;
    __ Xor(scratch, left_reg, right_operand);
    __ Branch(&or_else, lt, scratch, Operand(zero_reg));  // neg: inputs have different signs so no overflow
    // inputs have same sign
    __ Addu(scratch, left_reg, right_operand);  // trial add, preserving inputs
    __ xor_(scratch, scratch, left_reg); // neg: result changed sign
    __ not_(scratch, scratch);  // pos: result changed sign
    __ bind(&or_else);  // pos: overflow 
    DeoptimizeIf(ge, instr->environment(), scratch, Operand(zero_reg));
  }
  __ Addu(ToRegister(result), left_reg, right_operand);
}


void LCodeGen::DoArithmeticD(LArithmeticD* instr) {
  DoubleRegister left = ToDoubleRegister(instr->InputAt(0));
  DoubleRegister right = ToDoubleRegister(instr->InputAt(1));
  DoubleRegister result = ToDoubleRegister(instr->result());
  switch (instr->op()) {
    case Token::ADD:
      __ add_d(result, left, right);
      break;
    case Token::SUB:
      __ sub_d(result, left, right);
      break;
    case Token::MUL:
      __ mul_d(result, left, right);
      break;
    case Token::DIV:
      __ div_d(result, left, right);
      break;
    case Token::MOD: {
#ifdef MIPS_STUB
      NotYet;
#else
      // Save a0-a3 on the stack.
      __ stm(db_w, sp, a0.bit() | a1.bit() | a2.bit() | a3.bit());

      __ PrepareCallCFunction(0, 2, scratch0());
      __ SetCallCDoubleArguments(left, right);
      __ CallCFunction(
          ExternalReference::double_fp_operation(Token::MOD, isolate()),
          0, 2);
      // Move the result in the double result register.
      __ GetCFunctionDoubleResult(result);

      // Restore a0-a3.
      __ ldm(ia_w, sp, a0.bit() | a1.bit() | a2.bit() | a3.bit());
#endif
      break;
    }
    default:
      UNREACHABLE();
      break;
  }
}


void LCodeGen::DoArithmeticT(LArithmeticT* instr) {
  ASSERT(ToRegister(instr->InputAt(0)).is(a1));
  ASSERT(ToRegister(instr->InputAt(1)).is(a0));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  BinaryOpStub stub(instr->op(), NO_OVERWRITE);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
}


int LCodeGen::GetNextEmittedBlock(int block) {
  for (int i = block + 1; i < graph()->blocks()->length(); ++i) {
    LLabel* label = chunk_->GetLabel(i);
    if (!label->HasReplacement()) return i;
  }
  return -1;
}


#ifndef V8_TARGET_ARCH_MIPS
void LCodeGen::EmitBranch(int left_block, int right_block, Condition cond) {
  int next_block = GetNextEmittedBlock(current_block_);
  right_block = chunk_->LookupDestination(right_block);
  left_block = chunk_->LookupDestination(left_block);

  if (cond == kNoCondition)
    return;
  if (right_block == left_block) {
    EmitGoto(left_block);
  } else if (left_block == next_block) {
    __ Branch(NegateCondition(cond), chunk_->GetAssemblyLabel(right_block));
  } else if (right_block == next_block) {
    __ Branch(cond, chunk_->GetAssemblyLabel(left_block));
  } else {
    __ Branch(cond, chunk_->GetAssemblyLabel(left_block));
    __ Branch(chunk_->GetAssemblyLabel(right_block));
  }
}
#endif


void LCodeGen::EmitBranch(int left_block, int right_block,
                          Condition cond, Register src1, const Operand& src2) {
  int next_block = GetNextEmittedBlock(current_block_);
  right_block = chunk_->LookupDestination(right_block);
  left_block = chunk_->LookupDestination(left_block);

  if (cond == kNoCondition)
    return;
  if (right_block == left_block) {
    EmitGoto(left_block);
  } else if (left_block == next_block) {
    cond = NegateCondition(cond);
    __ Branch(chunk_->GetAssemblyLabel(right_block), cond, src1, src2);
  } else if (right_block == next_block) {
    __ Branch(chunk_->GetAssemblyLabel(left_block), cond, src1, src2);
  } else {
    __ Branch(chunk_->GetAssemblyLabel(left_block), cond, src1, src2);
    __ Branch(chunk_->GetAssemblyLabel(right_block));
  }
}


void LCodeGen::DoBranch(LBranch* instr) {
  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  Representation r = instr->hydrogen()->value()->representation();
  if (r.IsInteger32()) {
    Register reg = ToRegister(instr->InputAt(0));
    EmitBranch(true_block, false_block, ne, reg, Operand(zero_reg));
  } else if (r.IsDouble()) {
#ifdef MIPS_STUB
    NotYet;
#else
    DoubleRegister reg = ToDoubleRegister(instr->InputAt(0));
    Register scratch = scratch0();

    // Test the double value. Zero and NaN are false.
    __ VFPCompareAndLoadFlags(reg, 0.0, scratch);
    __ And(at, scratch, Operand(kVFPZConditionFlagBit | kVFPVConditionFlagBit));
    EmitBranch(true_block, false_block, ne, at, Operand(zero_reg));
#endif
  } else {
    ASSERT(r.IsTagged());
    Register reg = ToRegister(instr->InputAt(0));
    if (instr->hydrogen()->value()->type().IsBoolean()) {
      __ LoadRoot(at, Heap::kTrueValueRootIndex);
      EmitBranch(true_block, false_block, eq, reg, at);
    } else {
      Label* true_label = chunk_->GetAssemblyLabel(true_block);
      Label* false_label = chunk_->GetAssemblyLabel(false_block);

      __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
      __ Branch(false_label, eq, reg, at);
      __ LoadRoot(at, Heap::kTrueValueRootIndex);
      __ Branch(true_label, eq, reg, at);
      __ LoadRoot(at, Heap::kFalseValueRootIndex);
      __ Branch(false_label, eq, reg, at);
      __ Branch(false_label, eq, reg, Operand(zero_reg));
      __ JumpIfSmi(reg, true_label);

      // Test double values. Zero and NaN are false.
      Register scratch = scratch0();
#ifdef MIPS_STUB
      // TODO(plind): I think this is optimization, and stub below
      // can handle everything.
#else
      Label call_stub;
      DoubleRegister dbl_scratch = double_scratch0();
      __ lw(scratch, FieldMemOperand(reg, HeapObject::kMapOffset));
      __ LoadRoot(at, Heap::kHeapNumberMapRootIndex);
      __ Branch(&call_stub, ne, scratch, Operand(at));
      __ Subu(at, reg, Operand(kHeapObjectTag));
      __ ldc1(dbl_scratch, MemOperand(at, HeapNumber::kValueOffset));
      __ VFPCompareAndLoadFlags(dbl_scratch, 0.0, scratch);
      __ And(at, scratch, Operand(kVFPZConditionFlagBit | kVFPVConditionFlagBit));
      __ Branch(false_label, ne, at, Operand(zero_reg));
      __ Branch(true_label);

      __ bind(&call_stub);
#endif
      // The conversion stub doesn't cause garbage collections so it's
      // safe to not record a safepoint after the call.
      ToBooleanStub stub(reg);
      RegList saved_regs = (kJSCallerSaved | kCalleeSaved) & ~scratch.bit();
      __ MultiPush(saved_regs);
      __ CallStub(&stub);
      __ mov(scratch, reg);
      __ MultiPop(saved_regs);
      EmitBranch(true_block, false_block, ne, scratch, Operand(zero_reg));
    }
  }
}


void LCodeGen::EmitGoto(int block) {
  block = chunk_->LookupDestination(block);
  int next_block = GetNextEmittedBlock(current_block_);
  if (block != next_block) {
    __ jmp(chunk_->GetAssemblyLabel(block));
  }
}


void LCodeGen::DoGoto(LGoto* instr) {
  EmitGoto(instr->block_id());
}


Condition LCodeGen::TokenToCondition(Token::Value op, bool is_unsigned) {
  Condition cond = kNoCondition;
  switch (op) {
    case Token::EQ:
    case Token::EQ_STRICT:
      cond = eq;
      break;
    case Token::LT:
      cond = is_unsigned ? lo : lt;
      break;
    case Token::GT:
      cond = is_unsigned ? hi : gt;
      break;
    case Token::LTE:
      cond = is_unsigned ? ls : le;
      break;
    case Token::GTE:
      cond = is_unsigned ? hs : ge;
      break;
    case Token::IN:
    case Token::INSTANCEOF:
    default:
      UNREACHABLE();
  }
  return cond;
}


void LCodeGen::DoCmpIDAndBranch(LCmpIDAndBranch* instr) {
  LOperand* left = instr->InputAt(0);
  LOperand* right = instr->InputAt(1);
  int false_block = chunk_->LookupDestination(instr->false_block_id());
  int true_block = chunk_->LookupDestination(instr->true_block_id());
  Condition cond = TokenToCondition(instr->op(), instr->is_double());

  if (instr->is_double()) {
#ifdef MIPS_STUB
    NotYet;
#else
    // Compare left and right as doubles and load the
    // resulting flags into the normal status register.
    __ VFPCompareAndSetFlags(ToDoubleRegister(left), ToDoubleRegister(right));
    // If a NaN is involved, i.e. the result is unordered (V set),
    // jump to false block label.
    __ Branch(vs, chunk_->GetAssemblyLabel(false_block));
    EmitBranch(true_block, false_block, cond);
#endif
  } else {
    EmitBranch(true_block, false_block, cond, ToRegister(left), ToRegister(right));
  }
}


void LCodeGen::DoCmpObjectEqAndBranch(LCmpObjectEqAndBranch* instr) {
  Register left = ToRegister(instr->InputAt(0));
  Register right = ToRegister(instr->InputAt(1));
  int false_block = chunk_->LookupDestination(instr->false_block_id());
  int true_block = chunk_->LookupDestination(instr->true_block_id());

  EmitBranch(true_block, false_block, eq, left, Operand(right));
}


void LCodeGen::DoCmpConstantEqAndBranch(LCmpConstantEqAndBranch* instr) {
  Register left = ToRegister(instr->InputAt(0));
  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  EmitBranch(true_block, false_block, eq, left, Operand(instr->hydrogen()->right()));
}


void LCodeGen::DoIsNullAndBranch(LIsNullAndBranch* instr) {
  Register scratch = scratch0();
  Register reg = ToRegister(instr->InputAt(0));

  // TODO(fsc): If the expression is known to be a smi, then it's
  // definitely not null. Jump to the false block.

  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  __ LoadRoot(at, Heap::kNullValueRootIndex);
  if (instr->is_strict()) {
    EmitBranch(true_block, false_block, eq, reg, at);
  } else {
    Label* true_label = chunk_->GetAssemblyLabel(true_block);
    Label* false_label = chunk_->GetAssemblyLabel(false_block);
    __ Branch(true_label, eq, reg, at);
    __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
    __ Branch(true_label, eq, reg, at);
    __ JumpIfSmi(reg, false_label);
    // Check for undetectable objects by looking in the bit field in
    // the map. The object has already been smi checked.
    __ lw(scratch, FieldMemOperand(reg, HeapObject::kMapOffset));
    __ lbu(scratch, FieldMemOperand(scratch, Map::kBitFieldOffset));
    __ And(at, scratch, Operand(1 << Map::kIsUndetectable));
    EmitBranch(true_block, false_block, ne, at, Operand(zero_reg));
  }
}


void LCodeGen::EmitIsObject(Register input,
                            Register temp1,
                            Register temp2,
                            Label* is_not_object,
                            Label* is_object) {
  __ JumpIfSmi(input, is_not_object);

  __ LoadRoot(temp2, Heap::kNullValueRootIndex);
  __ Branch(is_object, eq, input, temp2);

  // Load map.
  __ lw(temp1, FieldMemOperand(input, HeapObject::kMapOffset));
  // Undetectable objects behave like undefined.
  __ lbu(temp2, FieldMemOperand(temp1, Map::kBitFieldOffset));
  __ And(at, temp2, Operand(1 << Map::kIsUndetectable));
  __ Branch(is_not_object, ne, at, Operand(zero_reg));

  // Load instance type and check that it is in object type range.
  __ lbu(temp2, FieldMemOperand(temp1, Map::kInstanceTypeOffset));
  __ Branch(is_not_object, lt, temp2, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
  // In non-jump cases, fall through with object type in temp2.
  // Expected next instructions are
  //   cmp(temp2, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
  //   b(le, &is_object)
}


void LCodeGen::DoIsObjectAndBranch(LIsObjectAndBranch* instr) {
  Register reg = ToRegister(instr->InputAt(0));
  Register temp1 = ToRegister(instr->TempAt(0));
  Register temp2 = scratch0();

  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());
  Label* true_label = chunk_->GetAssemblyLabel(true_block);
  Label* false_label = chunk_->GetAssemblyLabel(false_block);

  EmitIsObject(reg, temp1, temp2, false_label, true_label);
  EmitBranch(true_block, false_block, le, temp2, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
}


void LCodeGen::DoIsSmiAndBranch(LIsSmiAndBranch* instr) {
  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  Register input_reg = EmitLoadRegister(instr->InputAt(0), at);
  __ And(at, input_reg, Operand(kSmiTagMask));
  EmitBranch(true_block, false_block, eq, at, Operand(zero_reg));
}


void LCodeGen::DoIsUndetectableAndBranch(LIsUndetectableAndBranch* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register temp = ToRegister(instr->TempAt(0));

  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  __ JumpIfSmi(input, chunk_->GetAssemblyLabel(false_block));
  __ lw(temp, FieldMemOperand(input, HeapObject::kMapOffset));
  __ lbu(temp, FieldMemOperand(temp, Map::kBitFieldOffset));
  __ And(at, temp, Operand(1 << Map::kIsUndetectable));
  EmitBranch(true_block, false_block, ne, at, Operand(zero_reg));
}


static InstanceType TestType(HHasInstanceTypeAndBranch* instr) {
  InstanceType from = instr->from();
  InstanceType to = instr->to();
  if (from == FIRST_TYPE) return to;
  ASSERT(from == to || to == LAST_TYPE);
  return from;
}


static Condition BranchCondition(HHasInstanceTypeAndBranch* instr) {
  InstanceType from = instr->from();
  InstanceType to = instr->to();
  if (from == to) return eq;
  if (to == LAST_TYPE) return hs;
  if (from == FIRST_TYPE) return ls;
  UNREACHABLE();
  return eq;
}


void LCodeGen::DoHasInstanceTypeAndBranch(LHasInstanceTypeAndBranch* instr) {
  Register scratch = scratch0();
  Register input = ToRegister(instr->InputAt(0));

  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  Label* false_label = chunk_->GetAssemblyLabel(false_block);

  __ JumpIfSmi(input, false_label);

  __ GetObjectType(input, scratch, scratch);
  EmitBranch(true_block, false_block, BranchCondition(instr->hydrogen()), scratch, Operand(TestType(instr->hydrogen())));
}


void LCodeGen::DoGetCachedArrayIndex(LGetCachedArrayIndex* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());

  if (FLAG_debug_code) {
    __ AbortIfNotString(input);
  }

  __ lw(result, FieldMemOperand(input, String::kHashFieldOffset));
  __ IndexFromHash(result, result);
}


void LCodeGen::DoHasCachedArrayIndexAndBranch(
    LHasCachedArrayIndexAndBranch* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register scratch = scratch0();

  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  __ lw(scratch,
         FieldMemOperand(input, String::kHashFieldOffset));
  __ And(at, scratch, Operand(String::kContainsCachedArrayIndexMask));
  EmitBranch(true_block, false_block, eq, at, Operand(zero_reg));
}


// Branches to a label or falls through with class name in reg temp.  Trashes
// the temp registers, but not the input.  Only input and temp2 may alias.
void LCodeGen::EmitClassOfTest(Label* is_true,
                               Label* is_false,
                               Handle<String>class_name,
                               Register input,
                               Register temp,
                               Register temp2) {
  Label* label;
  ASSERT(!input.is(temp));
  ASSERT(!temp.is(temp2));  // But input and temp2 may be the same register.
  __ JumpIfSmi(input, is_false);
  __ GetObjectType(input, temp, temp2);
  __ Branch(is_false, lt, temp2, Operand(FIRST_SPEC_OBJECT_TYPE));

  // Map is now in temp.
  // Functions have class 'Function'.
  label = (class_name->IsEqualTo(CStrVector("Function")) ? is_true : is_false);
  // CompareInstanceType(temp, temp2, FIRST_CALLABLE_SPEC_OBJECT_TYPE);
  __ lbu(temp2, FieldMemOperand(temp, Map::kInstanceTypeOffset));
  __ Branch(label, ge, temp2, Operand(FIRST_CALLABLE_SPEC_OBJECT_TYPE));

  // Check if the constructor in the map is a function.
  __ lw(temp, FieldMemOperand(temp, Map::kConstructorOffset));

  // As long as LAST_CALLABLE_SPEC_OBJECT_TYPE is the last instance type and
  // FIRST_CALLABLE_SPEC_OBJECT_TYPE comes right after
  // LAST_NONCALLABLE_SPEC_OBJECT_TYPE, we can avoid checking for the latter.
  STATIC_ASSERT(LAST_TYPE == LAST_CALLABLE_SPEC_OBJECT_TYPE);
  STATIC_ASSERT(FIRST_CALLABLE_SPEC_OBJECT_TYPE ==
                LAST_NONCALLABLE_SPEC_OBJECT_TYPE + 1);

  // Objects with a non-function constructor have class 'Object'.
  label = (class_name->IsEqualTo(CStrVector("Object")) ? is_true : is_false);
  __ GetObjectType(temp, temp2, temp2);
  __ Branch(label, ne, temp2, Operand(JS_FUNCTION_TYPE));

  // temp now contains the constructor function. Grab the
  // instance class name from there.
  __ lw(temp, FieldMemOperand(temp, JSFunction::kSharedFunctionInfoOffset));
  __ lw(temp, FieldMemOperand(temp,
                               SharedFunctionInfo::kInstanceClassNameOffset));
  // The class name we are testing against is a symbol because it's a literal.
  // The name in the constructor is a symbol because of the way the context is
  // booted.  This routine isn't expected to work for random API-created
  // classes and it doesn't have to because you can't access it with natives
  // syntax.  Since both sides are symbols it is sufficient to use an identity
  // comparison.
  // In non-jump cases, fall through with actual class name in register temp.
  // Expected next instruction is
  // cmp(temp, Operand(class_name));
  // End with the answer in flags.
}


void LCodeGen::DoClassOfTestAndBranch(LClassOfTestAndBranch* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register temp = scratch0();
  Register temp2 = ToRegister(instr->TempAt(0));
  Handle<String> class_name = instr->hydrogen()->class_name();

  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  Label* true_label = chunk_->GetAssemblyLabel(true_block);
  Label* false_label = chunk_->GetAssemblyLabel(false_block);

  EmitClassOfTest(true_label, false_label, class_name, input, temp, temp2);
  EmitBranch(true_block, false_block, eq, temp, Operand(class_name));
}


void LCodeGen::DoCmpMapAndBranch(LCmpMapAndBranch* instr) {
  Register reg = ToRegister(instr->InputAt(0));
  Register temp = ToRegister(instr->TempAt(0));
  int true_block = instr->true_block_id();
  int false_block = instr->false_block_id();

  __ lw(temp, FieldMemOperand(reg, HeapObject::kMapOffset));
  EmitBranch(true_block, false_block, eq, temp, Operand(instr->map()));
}


void LCodeGen::DoInstanceOf(LInstanceOf* instr) {
  Label true_label, done;
  ASSERT(ToRegister(instr->InputAt(0)).is(a0));  // Object is in a0.
  ASSERT(ToRegister(instr->InputAt(1)).is(a1));  // Function is in a1.
  Register result = ToRegister(instr->result());
  
  InstanceofStub stub(InstanceofStub::kArgsInRegisters);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);

  __ Branch(&true_label, eq, c_rval_reg, Operand(zero_reg));
  __ li(result, Operand(factory()->false_value()));
  __ Branch(&done);
  __ bind(&true_label);
  __ li(result, Operand(factory()->true_value()));
  __ bind(&done);
}


void LCodeGen::DoInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr) {
  class DeferredInstanceOfKnownGlobal: public LDeferredCode {
   public:
    DeferredInstanceOfKnownGlobal(LCodeGen* codegen,
                                  LInstanceOfKnownGlobal* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() {
      codegen()->DoDeferredLInstanceOfKnownGlobal(instr_, &map_check_);
    }

    Label* map_check() { return &map_check_; }

   private:
    LInstanceOfKnownGlobal* instr_;
    Label map_check_;
  };

  DeferredInstanceOfKnownGlobal* deferred;
  deferred = new DeferredInstanceOfKnownGlobal(this, instr);

  Label done, false_result;
  Register object = ToRegister(instr->InputAt(0));
  Register temp = ToRegister(instr->TempAt(0));
  Register result = ToRegister(instr->result());

  ASSERT(object.is(a0));

  // A Smi is not instance of anything.
  __ JumpIfSmi(object, &false_result);

  // This is the inlined call site instanceof cache. The two occurences of the
  // hole value will be patched to the last map/result pair generated by the
  // instanceof stub.
  Label cache_miss;
  Register map = temp;
  __ lw(map, FieldMemOperand(object, HeapObject::kMapOffset));
  __ bind(deferred->map_check());  // Label for calculating code patching.
  // We use Factory::the_hole_value() on purpose instead of loading from the
  // root array to force relocation to be able to later patch with
  // the cached map.
  __ li(at, Operand(factory()->the_hole_value()));
  __ Branch(&cache_miss, ne, map, Operand(at));
  // We use Factory::the_hole_value() on purpose instead of loading from the
  // root array to force relocation to be able to later patch
  // with true or false.
  __ li(result, Operand(factory()->the_hole_value()));
  __ Branch(&done);

  // The inlined call site cache did not match. Check null and string before
  // calling the deferred code.
  __ bind(&cache_miss);
  // Null is not instance of anything.
  __ LoadRoot(at, Heap::kNullValueRootIndex);
  __ Branch(&false_result, eq, object, Operand(at));

  // String values is not instance of anything.
  // IsObjectStringType(object, temp):
  __ lw(temp, FieldMemOperand(object, HeapObject::kMapOffset));
  __ lbu(temp, FieldMemOperand(temp, Map::kInstanceTypeOffset));
  __ And(temp, temp, Operand(kIsNotStringMask));
  __ Branch(&false_result, eq, temp, Operand(zero_reg));

  // Go to the deferred code.
  __ Branch(deferred->entry());

  __ bind(&false_result);
  __ LoadRoot(result, Heap::kFalseValueRootIndex);

  // Here result has either true or false. Deferred code also produces true or
  // false object.
  __ bind(deferred->exit());
  __ bind(&done);
}


void LCodeGen::DoDeferredLInstanceOfKnownGlobal(LInstanceOfKnownGlobal* instr,
                                                Label* map_check) {
#ifdef MIPS_STUB
  RuntimeAbort;  // patching of inlined stubs not yet enabled for Mips
#endif
  Register result = ToRegister(instr->result());

  InstanceofStub::Flags flags = InstanceofStub::kNoFlags;
  flags = static_cast<InstanceofStub::Flags>(
      flags | InstanceofStub::kArgsInRegisters);
  flags = static_cast<InstanceofStub::Flags>(
      flags | InstanceofStub::kCallSiteInlineCheck);
  flags = static_cast<InstanceofStub::Flags>(
      flags | InstanceofStub::kReturnTrueFalseObject);
  InstanceofStub stub(flags);

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);

  // Get the temp register reserved by the instruction. This needs to be t0 as
  // its slot of the pushing of safepoint registers is used to communicate the
  // offset to the location of the map check.
  Register temp = ToRegister(instr->TempAt(0));
  ASSERT(temp.is(t0));
  __ li(InstanceofStub::right(), Operand(instr->function()));
  static const int kAdditionalDelta = 4;
  int delta = masm_->InstructionsGeneratedSince(map_check) + kAdditionalDelta;
  Label before_push_delta;
  __ bind(&before_push_delta);
#ifndef V8_TARGET_ARCH_MIPS
  __ BlockConstPoolFor(kAdditionalDelta);
#endif
  __ li(temp, Operand(delta * kPointerSize));
  __ StoreToSafepointRegisterSlot(temp, temp);
  CallCodeGeneric(stub.GetCode(),
                  RelocInfo::CODE_TARGET,
                  instr,
                  RECORD_SAFEPOINT_WITH_REGISTERS_AND_NO_ARGUMENTS);
  // Put the result value into the result register slot and
  // restore all registers.
  __ StoreToSafepointRegisterSlot(c_rval_reg, result);
}


static Condition ComputeCompareCondition(Token::Value op) {
  switch (op) {
    case Token::EQ_STRICT:
    case Token::EQ:
      return eq;
    case Token::LT:
      return lt;
    case Token::GT:
      return gt;
    case Token::LTE:
      return le;
    case Token::GTE:
      return ge;
    default:
      UNREACHABLE();
      return kNoCondition;
  }
}


void LCodeGen::DoCmpT(LCmpT* instr) {
  Token::Value op = instr->op();
  Register result = ToRegister(instr->result());
  Label is_true, done;

  Handle<Code> ic = CompareIC::GetUninitialized(op);
  CallCode(ic, RelocInfo::CODE_TARGET, instr);

  Condition condition = ComputeCompareCondition(op);
  if (op == Token::GT || op == Token::LTE) {
    condition = ReverseCondition(condition);
  }
  __ Branch(&is_true, condition, c_rval_reg, Operand(zero_reg));  // This instruction also signals no smi code inlined.
  __ LoadRoot(result, Heap::kFalseValueRootIndex);
  __ Branch(&done);
  __ bind(&is_true);
  __ LoadRoot(result, Heap::kTrueValueRootIndex);
  __ bind(&done);
}


void LCodeGen::DoReturn(LReturn* instr) {
  if (FLAG_trace) {
    // Push the return value on the stack as the parameter.
    __ push(c_rval_reg);  // result from Runtime::TraceExit
    __ CallRuntime(Runtime::kTraceExit, 1);
  }
  int32_t sp_delta = (GetParameterCount() + 1) * kPointerSize;
  __ mov(sp, fp);
  __ Pop(ra, fp);
  __ Addu(sp, sp, Operand(sp_delta));
  __ Jump(ra);
}


void LCodeGen::DoLoadGlobalCell(LLoadGlobalCell* instr) {
  Register result = ToRegister(instr->result());
  __ li(at, Operand(Handle<Object>(instr->hydrogen()->cell())));
  __ lw(result, FieldMemOperand(at, JSGlobalPropertyCell::kValueOffset));
  if (instr->hydrogen()->check_hole_value()) {
    __ LoadRoot(at, Heap::kTheHoleValueRootIndex);
    DeoptimizeIf(eq, instr->environment(), result, at);
  }
}


void LCodeGen::DoLoadGlobalGeneric(LLoadGlobalGeneric* instr) {
  ASSERT(ToRegister(instr->global_object()).is(a0));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  __ li(a2, Operand(instr->name()));
  RelocInfo::Mode mode = instr->for_typeof() ? RelocInfo::CODE_TARGET
                                             : RelocInfo::CODE_TARGET_CONTEXT;
  Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
  CallCode(ic, mode, instr);
}


void LCodeGen::DoStoreGlobalCell(LStoreGlobalCell* instr) {
  Register value = ToRegister(instr->InputAt(0));
  Register scratch = scratch0();

  // Load the cell.
  __ li(scratch, Operand(Handle<Object>(instr->hydrogen()->cell())));

  // If the cell we are storing to contains the hole it could have
  // been deleted from the property dictionary. In that case, we need
  // to update the property details in the property dictionary to mark
  // it as no longer deleted.
  if (instr->hydrogen()->check_hole_value()) {
    Register scratch2 = ToRegister(instr->TempAt(0));
    __ lw(scratch2,
           FieldMemOperand(scratch, JSGlobalPropertyCell::kValueOffset));
    __ LoadRoot(at, Heap::kTheHoleValueRootIndex);
    DeoptimizeIf(eq, instr->environment(), scratch2, at);
  }

  // Store the value.
  __ sw(value, FieldMemOperand(scratch, JSGlobalPropertyCell::kValueOffset));
}


void LCodeGen::DoStoreGlobalGeneric(LStoreGlobalGeneric* instr) {
  ASSERT(ToRegister(instr->global_object()).is(a1));
  ASSERT(ToRegister(instr->value()).is(a0));

  __ li(a2, Operand(instr->name()));
  Handle<Code> ic = instr->strict_mode()
      ? isolate()->builtins()->StoreIC_Initialize_Strict()
      : isolate()->builtins()->StoreIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET_CONTEXT, instr);
}


void LCodeGen::DoLoadContextSlot(LLoadContextSlot* instr) {
  Register context = ToRegister(instr->context());
  Register result = ToRegister(instr->result());
  __ lw(result, ContextOperand(context, instr->slot_index()));
}


void LCodeGen::DoStoreContextSlot(LStoreContextSlot* instr) {
  Register context = ToRegister(instr->context());
  Register value = ToRegister(instr->value());
  __ sw(value, ContextOperand(context, instr->slot_index()));
  if (instr->needs_write_barrier()) {
    int offset = Context::SlotOffset(instr->slot_index());
    __ RecordWrite(context, Operand(offset), value, scratch0());
  }
}


void LCodeGen::DoLoadNamedField(LLoadNamedField* instr) {
  Register object = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  if (instr->hydrogen()->is_in_object()) {
    __ lw(result, FieldMemOperand(object, instr->hydrogen()->offset()));
  } else {
    __ lw(result, FieldMemOperand(object, JSObject::kPropertiesOffset));
    __ lw(result, FieldMemOperand(result, instr->hydrogen()->offset()));
  }
}


void LCodeGen::EmitLoadFieldOrConstantFunction(Register result,
                                               Register object,
                                               Handle<Map> type,
                                               Handle<String> name) {
  LookupResult lookup;
  type->LookupInDescriptors(NULL, *name, &lookup);
  ASSERT(lookup.IsProperty() &&
         (lookup.type() == FIELD || lookup.type() == CONSTANT_FUNCTION));
  if (lookup.type() == FIELD) {
    int index = lookup.GetLocalFieldIndexFromMap(*type);
    int offset = index * kPointerSize;
    if (index < 0) {
      // Negative property indices are in-object properties, indexed
      // from the end of the fixed part of the object.
      __ lw(result, FieldMemOperand(object, offset + type->instance_size()));
    } else {
      // Non-negative property indices are in the properties array.
      __ lw(result, FieldMemOperand(object, JSObject::kPropertiesOffset));
      __ lw(result, FieldMemOperand(result, offset + FixedArray::kHeaderSize));
    }
  } else {
    Handle<JSFunction> function(lookup.GetConstantFunctionFromMap(*type));
    LoadHeapObject(result, Handle<HeapObject>::cast(function));
  }
}


void LCodeGen::DoLoadNamedFieldPolymorphic(LLoadNamedFieldPolymorphic* instr) {
  Register object = ToRegister(instr->object());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();
  int map_count = instr->hydrogen()->types()->length();
  Handle<String> name = instr->hydrogen()->name();
  if (map_count == 0) {
    ASSERT(instr->hydrogen()->need_generic());
    ASSERT(result.is(c_rval_reg));
    __ li(a2, Operand(name));
    Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
    CallCode(ic, RelocInfo::CODE_TARGET, instr);
  } else {
    Label done;
    __ lw(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
    for (int i = 0; i < map_count - 1; ++i) {
      Handle<Map> map = instr->hydrogen()->types()->at(i);
      Label next;
      __ Branch(&next, ne, scratch, Operand(map));
      EmitLoadFieldOrConstantFunction(result, object, map, name);
      __ Branch(&done);
      __ bind(&next);
    }
    Handle<Map> map = instr->hydrogen()->types()->last();
    if (instr->hydrogen()->need_generic()) {
      ASSERT(result.is(c_rval_reg));
      Label generic;
      __ Branch(&generic, ne, scratch, Operand(map));
      EmitLoadFieldOrConstantFunction(result, object, map, name);
      __ Branch(&done);
      __ bind(&generic);
      __ li(a2, Operand(name));
      Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
      CallCode(ic, RelocInfo::CODE_TARGET, instr);
    } else {
      DeoptimizeIf(ne, instr->environment(), scratch, Operand(map));
      EmitLoadFieldOrConstantFunction(result, object, map, name);
    }
    __ bind(&done);
  }
}


void LCodeGen::DoLoadNamedGeneric(LLoadNamedGeneric* instr) {
  ASSERT(ToRegister(instr->object()).is(a0));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  // Name is always in a2.
  __ li(a2, Operand(instr->name()));
  Handle<Code> ic = isolate()->builtins()->LoadIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoLoadFunctionPrototype(LLoadFunctionPrototype* instr) {
  Register scratch = scratch0();
  Register function = ToRegister(instr->function());
  Register result = ToRegister(instr->result());

  // Check that the function really is a function. Load map into the
  // result register.
  __ GetObjectType(function, result, scratch);
  DeoptimizeIf(ne, instr->environment(), scratch, Operand(JS_FUNCTION_TYPE));

  // Make sure that the function has an instance prototype.
  Label non_instance;
  __ lbu(scratch, FieldMemOperand(result, Map::kBitFieldOffset));
  __ And(at, scratch, Operand(1 << Map::kHasNonInstancePrototype));
  __ Branch(&non_instance, ne, at, Operand(zero_reg));

  // Get the prototype or initial map from the function.
  __ lw(result,
         FieldMemOperand(function, JSFunction::kPrototypeOrInitialMapOffset));

  // Check that the function has a prototype or an initial map.
  __ LoadRoot(at, Heap::kTheHoleValueRootIndex);
  DeoptimizeIf(eq, instr->environment(), result, at);

  // If the function does not have an initial map, we're done.
  Label done;
  __ GetObjectType(result, scratch, scratch);
  __ Branch(&done, ne, scratch, Operand(MAP_TYPE));

  // Get the prototype from the initial map.
  __ lw(result, FieldMemOperand(result, Map::kPrototypeOffset));
  __ jmp(&done);

  // Non-instance prototype: Fetch prototype from constructor field
  // in initial map.
  __ bind(&non_instance);
  __ lw(result, FieldMemOperand(result, Map::kConstructorOffset));

  // All done.
  __ bind(&done);
}


void LCodeGen::DoLoadElements(LLoadElements* instr) {
  Register result = ToRegister(instr->result());
  Register input = ToRegister(instr->InputAt(0));
  Register scratch = scratch0();

  __ lw(result, FieldMemOperand(input, JSObject::kElementsOffset));
  if (FLAG_debug_code) {
    Label done, fail;
    __ lw(scratch, FieldMemOperand(result, HeapObject::kMapOffset));
    __ LoadRoot(at, Heap::kFixedArrayMapRootIndex);
    __ Branch(&done, eq, scratch, at);
    __ LoadRoot(at, Heap::kFixedCOWArrayMapRootIndex);
    __ Branch(&done, eq, scratch, at);
    // |scratch| still contains |input|'s map.
    __ lw(scratch, FieldMemOperand(scratch, Map::kBitField2Offset));
    __ Ext(scratch, scratch, Map::kElementsKindShift,
            Map::kElementsKindBitCount);
    __ Branch(&done, eq, scratch, Operand(JSObject::FAST_ELEMENTS));
    __ Branch(&fail, lt, scratch, Operand(JSObject::FIRST_EXTERNAL_ARRAY_ELEMENTS_KIND));
    __ Branch(&done, le, scratch, Operand(JSObject::LAST_EXTERNAL_ARRAY_ELEMENTS_KIND));
    __ bind(&fail);
    __ Abort("Check for fast or external elements failed.");
    __ bind(&done);
  }
}


void LCodeGen::DoLoadExternalArrayPointer(
    LLoadExternalArrayPointer* instr) {
  Register to_reg = ToRegister(instr->result());
  Register from_reg  = ToRegister(instr->InputAt(0));
  __ lw(to_reg, FieldMemOperand(from_reg,
                                 ExternalArray::kExternalPointerOffset));
}


void LCodeGen::DoAccessArgumentsAt(LAccessArgumentsAt* instr) {
  Register arguments = ToRegister(instr->arguments());
  Register length = ToRegister(instr->length());
  Register index = ToRegister(instr->index());
  Register result = ToRegister(instr->result());

  // Bailout index is not a valid argument index. Use unsigned check to get
  // negative check for free.
  DeoptimizeIf(ls, instr->environment(), length, index);
  __ subu(length, length, index);

  // There are two words between the frame pointer and the last argument.
  // Subtracting from length accounts for one of them add one more.
  __ Addu(length, length, Operand(1));
  __ sll(length, length, kPointerSizeLog2);
  __ addu(result, arguments, length);
  __ lw(result, MemOperand(result, 0));
}


void LCodeGen::DoLoadKeyedFastElement(LLoadKeyedFastElement* instr) {
  Register elements = ToRegister(instr->elements());
  Register key = EmitLoadRegister(instr->key(), scratch0());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();

  // Load the result.
  __ sll(scratch, key, kPointerSizeLog2);
  __ addu(scratch, scratch, elements);
  __ lw(result, FieldMemOperand(scratch, FixedArray::kHeaderSize));

  // Check for the hole value.
  if (instr->hydrogen()->RequiresHoleCheck()) {
    __ LoadRoot(scratch, Heap::kTheHoleValueRootIndex);
    DeoptimizeIf(eq, instr->environment(), result, scratch);
  }
}


void LCodeGen::DoLoadKeyedSpecializedArrayElement(
    LLoadKeyedSpecializedArrayElement* instr) {
  Register external_pointer = ToRegister(instr->external_pointer());
  Register key = no_reg;
  JSObject::ElementsKind elements_kind = instr->elements_kind();
  bool key_is_constant = instr->key()->IsConstantOperand();
  int constant_key = 0;
  if (key_is_constant) {
    constant_key = ToInteger32(LConstantOperand::cast(instr->key()));
    if (constant_key & 0xF0000000) {
      Abort("array index constant value too big.");
    }
  } else {
    key = ToRegister(instr->key());
  }
  int shift_size = ElementsKindToShiftSize(elements_kind);

  if (elements_kind == JSObject::EXTERNAL_FLOAT_ELEMENTS ||
      elements_kind == JSObject::EXTERNAL_DOUBLE_ELEMENTS) {
    CpuFeatures::Scope scope(FPU);
    FPURegister result(ToDoubleRegister(instr->result()));
    if (key_is_constant) {
      __ Addu(scratch0(), external_pointer,
             Operand(constant_key * (1 << shift_size)));
    } else {
      __ sll(scratch0(), key, shift_size);
      __ addu(scratch0(), scratch0(), external_pointer);
    }
    if (elements_kind == JSObject::EXTERNAL_FLOAT_ELEMENTS) {
      __ ldc1(result.low(), MemOperand(scratch0(), 0));
      __ cvt_d_s(result, result.low());
    } else  {  // i.e. elements_kind == JSObject::EXTERNAL_DOUBLE_ELEMENTS
      __ ldc1(result, MemOperand(scratch0(), 0));
    }
  } else {
    Register result(ToRegister(instr->result()));
    MemOperand mem_operand = MemOperand(no_reg);
    if (key_is_constant) {
      mem_operand = MemOperand(external_pointer,
                               constant_key * (1 << shift_size));
    } else {
      __ sll(scratch0(), key, shift_size);
      __ addu(scratch0(), scratch0(), external_pointer);
      mem_operand = MemOperand(scratch0(), 0);
    }
    switch (elements_kind) {
      case JSObject::EXTERNAL_BYTE_ELEMENTS:
        __ lb(result, mem_operand);
        break;
      case JSObject::EXTERNAL_PIXEL_ELEMENTS:
      case JSObject::EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
        __ lbu(result, mem_operand);
        break;
      case JSObject::EXTERNAL_SHORT_ELEMENTS:
        __ lh(result, mem_operand);
        break;
      case JSObject::EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
        __ lhu(result, mem_operand);
        break;
      case JSObject::EXTERNAL_INT_ELEMENTS:
        __ lw(result, mem_operand);
        break;
      case JSObject::EXTERNAL_UNSIGNED_INT_ELEMENTS:
        __ lw(result, mem_operand);
        // TODO(danno): we could be more clever here, perhaps having a special
        // version of the stub that detects if the overflow case actually
        // happens, and generate code that returns a double rather than int.
        DeoptimizeIf(lt, instr->environment(), result, Operand(zero_reg));
        break;
      case JSObject::EXTERNAL_FLOAT_ELEMENTS:
      case JSObject::EXTERNAL_DOUBLE_ELEMENTS:
      case JSObject::FAST_DOUBLE_ELEMENTS:
      case JSObject::FAST_ELEMENTS:
      case JSObject::DICTIONARY_ELEMENTS:
      case JSObject::NON_STRICT_ARGUMENTS_ELEMENTS:
        UNREACHABLE();
        break;
    }
  }
}


void LCodeGen::DoLoadKeyedGeneric(LLoadKeyedGeneric* instr) {
  ASSERT(ToRegister(instr->object()).is(a1));
  ASSERT(ToRegister(instr->key()).is(a0));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  Handle<Code> ic = isolate()->builtins()->KeyedLoadIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoArgumentsElements(LArgumentsElements* instr) {
  Register scratch = scratch0();
  Register result = ToRegister(instr->result());

  // Check if the calling frame is an arguments adaptor frame.
  Label done, adapted;
  __ lw(scratch, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ lw(result, MemOperand(scratch, StandardFrameConstants::kContextOffset));
  __ Branch(&adapted, eq, result, Operand(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));
  // Result is the frame pointer for the frame if not adapted.
  __ mov(result, fp);
  __ Branch(&done);
  // Result is the real frame below the adaptor frame if adapted.
  __ bind(&adapted);
  if (!result.is(scratch))
    __ mov(result, scratch);
  __ bind(&done);
}


void LCodeGen::DoArgumentsLength(LArgumentsLength* instr) {
  Register elem = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());

  Label done, more;

  // If no arguments adaptor frame the number of arguments is fixed.
  __ Branch(&more, ne, fp, elem);
  __ li(result, Operand(scope()->num_parameters()));
  __ Branch( &done);
  __ bind(&more);

  // Arguments adaptor frame present. Get argument length from there.
  __ lw(result, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
  __ lw(result,
         MemOperand(result, ArgumentsAdaptorFrameConstants::kLengthOffset));
  __ SmiUntag(result);

  // Argument length is in result register.
  __ bind(&done);
}


void LCodeGen::DoApplyArguments(LApplyArguments* instr) {
  Register receiver = ToRegister(instr->receiver());
  Register function = ToRegister(instr->function());
  Register length = ToRegister(instr->length());
  Register elements = ToRegister(instr->elements());
  Register scratch = scratch0();
  ASSERT(receiver.is(a0));  // Used for parameter count.
  ASSERT(function.is(a1));  // Required by InvokeFunction.
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  // If the receiver is null or undefined, we have to pass the global
  // object as a receiver to normal functions. Values have to be
  // passed unchanged to builtins and strict-mode functions.
  Label global_object, receiver_ok;

  // Do not transform the receiver to object for strict mode
  // functions.
  __ lw(scratch,
         FieldMemOperand(function, JSFunction::kSharedFunctionInfoOffset));
  __ lw(scratch,
         FieldMemOperand(scratch, SharedFunctionInfo::kCompilerHintsOffset));
  __ And(at, scratch, Operand(1 << (SharedFunctionInfo::kStrictModeFunction + kSmiTagSize)));
  __ Branch(&receiver_ok, ne, at, Operand(zero_reg));

  // Do not transform the receiver to object for builtins.
  __ And(at, scratch, Operand(1 << (SharedFunctionInfo::kNative + kSmiTagSize)));
  __ Branch(&receiver_ok, ne, at, Operand(zero_reg));

  // Normal function. Replace undefined or null with global receiver.
  __ LoadRoot(scratch, Heap::kNullValueRootIndex);
  __ Branch(&global_object, eq, receiver, scratch);
  __ LoadRoot(scratch, Heap::kUndefinedValueRootIndex);
  __ Branch(&global_object, eq, receiver, scratch);

  // Deoptimize if the receiver is not a JS object.
  __ And(at, receiver, Operand(kSmiTagMask));
  DeoptimizeIf(eq, instr->environment(), at, Operand(zero_reg));
  __ GetObjectType(receiver, scratch, scratch);
  DeoptimizeIf(lt, instr->environment(), scratch, Operand(FIRST_SPEC_OBJECT_TYPE));
  __ jmp(&receiver_ok);

  __ bind(&global_object);
  __ lw(receiver, GlobalObjectOperand());
  __ lw(receiver,
         FieldMemOperand(receiver, JSGlobalObject::kGlobalReceiverOffset));
  __ bind(&receiver_ok);

  // Copy the arguments to this function possibly from the
  // adaptor frame below it.
  const uint32_t kArgumentsLimit = 1 * KB;
  DeoptimizeIf(hi, instr->environment(), length, Operand(kArgumentsLimit));

  // Push the receiver and use the register to keep the original
  // number of arguments.
  __ push(receiver);
  __ mov(receiver, length);
  // The arguments are at a one pointer size offset from elements.
  __ Addu(elements, elements, Operand(1 * kPointerSize));

  // Loop through the arguments pushing them onto the execution
  // stack.
  Label invoke, loop;
  // length is a small non-negative integer, due to the test above.
  __ Branch(&invoke, eq, length, Operand(zero_reg));
  __ bind(&loop);
  __ sll(scratch, length, 2);
  __ addu(scratch, scratch, elements);  
  __ lw(scratch, MemOperand(scratch, 0));
  __ push(scratch);
  __ Subu(length, length, Operand(1));
  __ Branch(&loop, ne, length, Operand(zero_reg));

  __ bind(&invoke);
  ASSERT(instr->HasPointerMap() && instr->HasDeoptimizationEnvironment());
  LPointerMap* pointers = instr->pointer_map();
  LEnvironment* env = instr->deoptimization_environment();
  RecordPosition(pointers->position());
  RegisterEnvironmentForDeoptimization(env);
  SafepointGenerator safepoint_generator(this,
                                         pointers,
                                         env->deoptimization_index());
  // The number of arguments is stored in receiver which is a0, as expected
  // by InvokeFunction.
  v8::internal::ParameterCount actual(receiver);
  __ InvokeFunction(function, actual, CALL_FUNCTION,
                    safepoint_generator, CALL_AS_METHOD);
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoPushArgument(LPushArgument* instr) {
  LOperand* argument = instr->InputAt(0);
  if (argument->IsDoubleRegister() || argument->IsDoubleStackSlot()) {
    Abort("DoPushArgument not implemented for double type.");
  } else {
    Register argument_reg = EmitLoadRegister(argument, at);
    __ push(argument_reg);
  }
}


void LCodeGen::DoThisFunction(LThisFunction* instr) {
  Register result = ToRegister(instr->result());
  __ lw(result, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
}


void LCodeGen::DoContext(LContext* instr) {
  Register result = ToRegister(instr->result());
  __ mov(result, cp);
}


void LCodeGen::DoOuterContext(LOuterContext* instr) {
  Register context = ToRegister(instr->context());
  Register result = ToRegister(instr->result());
  __ lw(result,
         MemOperand(context, Context::SlotOffset(Context::PREVIOUS_INDEX)));
}


void LCodeGen::DoGlobalObject(LGlobalObject* instr) {
  Register context = ToRegister(instr->context());
  Register result = ToRegister(instr->result());
  __ lw(result, ContextOperand(cp, Context::GLOBAL_INDEX));
}


void LCodeGen::DoGlobalReceiver(LGlobalReceiver* instr) {
  Register global = ToRegister(instr->global());
  Register result = ToRegister(instr->result());
  __ lw(result, FieldMemOperand(global, GlobalObject::kGlobalReceiverOffset));
}


void LCodeGen::CallKnownFunction(Handle<JSFunction> function,
                                 int arity,
                                 LInstruction* instr,
                                 CallKind call_kind) {
  // Change context if needed.
  bool change_context =
      (info()->closure()->context() != function->context()) ||
      scope()->contains_with() ||
      (scope()->num_heap_slots() > 0);
  if (change_context) {
    __ lw(cp, FieldMemOperand(a1, JSFunction::kContextOffset));
  }

  // Set a0 to arguments count if adaption is not needed. Assumes that a0
  // is available to write to at this point.
  if (!function->NeedsArgumentsAdaption()) {
    __ li(a0, Operand(arity));
  }

  LPointerMap* pointers = instr->pointer_map();
  RecordPosition(pointers->position());

  // Invoke function.
  __ SetCallKind(t1, call_kind);
  __ lw(at, FieldMemOperand(a1, JSFunction::kCodeEntryOffset));
  __ Call(at);

  // Setup deoptimization.
  RegisterLazyDeoptimization(instr, RECORD_SIMPLE_SAFEPOINT);

  // Restore context.
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoCallConstantFunction(LCallConstantFunction* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  //TODO(duanes): setting a0 here is likely pointless:
  if (!a0.is(c_rval_reg))
    __ mov(a0, c_rval_reg);
  __ li(a1, Operand(instr->function()));
  CallKnownFunction(instr->function(),
                    instr->arity(),
                    instr,
                    CALL_AS_METHOD);
}


void LCodeGen::DoDeferredMathAbsTaggedHeapNumber(LUnaryMathOperation* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();

  // Deoptimize if not a heap number.
  __ lw(scratch, FieldMemOperand(input, HeapObject::kMapOffset));
  __ LoadRoot(at, Heap::kHeapNumberMapRootIndex);
  DeoptimizeIf(ne, instr->environment(), scratch, Operand(at));

  Label done;
  Register exponent = scratch0();
  scratch = no_reg;
  __ lw(exponent, FieldMemOperand(input, HeapNumber::kExponentOffset));
  // Check the sign of the argument. If the argument is positive, just
  // return it.
  __ Move(result, input);
  __ And(at, exponent, Operand(HeapNumber::kSignMask));
  __ Branch(&done, eq, at, Operand(zero_reg));

  // Input is negative. Reverse its sign.
  // Preserve the value of all registers.
  {
    PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);

    // Registers were saved at the safepoint, so we can use
    // many scratch registers.
    Register tmp1 = input.is(a1) ? a0 : a1;
    Register tmp2 = input.is(a2) ? a0 : a2;
    Register tmp3 = input.is(a3) ? a0 : a3;
    Register tmp4 = input.is(t0) ? a0 : t0;

    // exponent: floating point exponent value.

    Label allocated, slow;
    __ LoadRoot(tmp4, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(tmp1, tmp2, tmp3, tmp4, &slow);
    __ Branch(&allocated);

    // Slow case: Call the runtime system to do the number allocation.
    __ bind(&slow);

    CallRuntimeFromDeferred(Runtime::kAllocateHeapNumber, 0, instr);
    // Set the pointer to the new heap number in tmp.
    if (!tmp1.is(c_rval_reg))
      __ mov(tmp1, c_rval_reg);
    // Restore input_reg after call to runtime.
    __ LoadFromSafepointRegisterSlot(input, input);
    __ lw(exponent, FieldMemOperand(input, HeapNumber::kExponentOffset));

    __ bind(&allocated);
    // exponent: floating point exponent value.
    // tmp1: allocated heap number.
    __ And(exponent, exponent, Operand(~HeapNumber::kSignMask));
    __ sw(exponent, FieldMemOperand(tmp1, HeapNumber::kExponentOffset));
    __ lw(tmp2, FieldMemOperand(input, HeapNumber::kMantissaOffset));
    __ sw(tmp2, FieldMemOperand(tmp1, HeapNumber::kMantissaOffset));

    __ StoreToSafepointRegisterSlot(tmp1, result);
  }

  __ bind(&done);
}


void LCodeGen::EmitIntegerMathAbs(LUnaryMathOperation* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  Label done;
  __ Move(result, input);
  __ Branch(&done, ge, input, Operand(zero_reg));
  __ negu(result, input);
  // Overflow if result is still negative, ie 0x80000000.
  DeoptimizeIf(lt, instr->environment(), result, Operand(zero_reg));
  __ bind(&done);
}


void LCodeGen::DoMathAbs(LUnaryMathOperation* instr) {
  // Class for deferred case.
  class DeferredMathAbsTaggedHeapNumber: public LDeferredCode {
   public:
    DeferredMathAbsTaggedHeapNumber(LCodeGen* codegen,
                                    LUnaryMathOperation* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() {
      codegen()->DoDeferredMathAbsTaggedHeapNumber(instr_);
    }
   private:
    LUnaryMathOperation* instr_;
  };

  Representation r = instr->hydrogen()->value()->representation();
  if (r.IsDouble()) {
    FPURegister input = ToDoubleRegister(instr->InputAt(0));
    FPURegister result = ToDoubleRegister(instr->result());
    __ abs_d(result, input);
  } else if (r.IsInteger32()) {
    EmitIntegerMathAbs(instr);
  } else {
    // Representation is tagged.
    DeferredMathAbsTaggedHeapNumber* deferred =
        new DeferredMathAbsTaggedHeapNumber(this, instr);
    Register input = ToRegister(instr->InputAt(0));
    // Smi check.
    __ JumpIfNotSmi(input, deferred->entry());
    // If smi, handle it directly.
    EmitIntegerMathAbs(instr);
    __ bind(deferred->exit());
  }
}


void LCodeGen::DoMathFloor(LUnaryMathOperation* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  DoubleRegister input = ToDoubleRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  FPURegister single_scratch = double_scratch0().low();
  Register scratch1 = scratch0();
  Register scratch2 = ToRegister(instr->TempAt(0));

  __ EmitVFPTruncate(kRoundToMinusInf,
                     single_scratch,
                     input,
                     scratch1,
                     scratch2);
  DeoptimizeIf(ne, instr->environment());

  __ mov_d(result, single_scratch);

  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    // Test for -0.
    Label done;
    __ Branch(&done, ne, result, Operand(zero_reg));
    __ mfc1(scratch1, input.high());
    __ And(at, scratch1, Operand(HeapNumber::kSignMask));
    DeoptimizeIf(ne, instr->environment(), at, Operand(zero_reg));
    __ bind(&done);
  }
#endif
}


void LCodeGen::DoMathRound(LUnaryMathOperation* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  DoubleRegister input = ToDoubleRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  Register scratch1 = result;
  Register scratch2 = scratch0();
  Label done, check_sign_on_zero;
  Label more1, more2;

  // Extract exponent bits.
  __ mfc1(scratch1, input.high());
  __ Ext(scratch2,
          scratch1,
          HeapNumber::kExponentShift,
          HeapNumber::kExponentBits);

  // If the number is in ]-0.5, +0.5[, the result is +/- 0.
  __ Branch(&more1, gt, scratch2, Operand(HeapNumber::kExponentBias - 2));
  __ mov(result, zero_reg);
  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    __ Branch(&check_sign_on_zero);
  } else {
    __ Branch(&done);
  }
  __ bind(&more1);

  // The following conversion will not work with numbers
  // outside of ]-2^32, 2^32[.
  DeoptimizeIf(ge, instr->environment(), scratch2, Operand(HeapNumber::kExponentBias + 32));

  // Save the original sign for later comparison.
  __ And(scratch2, scratch1, Operand(HeapNumber::kSignMask));

  __ mov_d(double_scratch0(), 0.5);
  __ add_d(input, input, double_scratch0());

  // Check sign of the result: if the sign changed, the input
  // value was in ]0.5, 0[ and the result should be -0.
  __ mfc1(scratch1, input.high());
  __ xor_(scratch1, scratch1, scratch2);
  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    DeoptimizeIf(lt, instr->environment(), scratch1, Operand(zero_reg));
  } else {
    __ Branch(&more2, ge, scratch1, Operand(zero_reg));
    __ mov(result, zero_reg);
    __ Branch(&done);
    __ bind(&more2);
  }

  __ EmitVFPTruncate(kRoundToMinusInf,
                     double_scratch0().low(),
                     input,
                     scratch1,
                     scratch2);
  DeoptimizeIf(ne, instr->environment());
  __ mfc1(result, double_scratch0().low());

  if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
    // Test for -0.
    __ Branch(&done, ne, result, Operand(zero_reg));
    __ bind(&check_sign_on_zero);
    __ mfc1(scratch1, input.high());
    __ And(at, scratch1, Operand(HeapNumber::kSignMask));
    DeoptimizeIf(ne, instr->environment(), at, Operand(zero_reg));
  }
  __ bind(&done);
#endif
}


void LCodeGen::DoMathSqrt(LUnaryMathOperation* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  DoubleRegister input = ToDoubleRegister(instr->InputAt(0));
  DoubleRegister result = ToDoubleRegister(instr->result());
  __ vsqrt(result, input);
#endif
}


void LCodeGen::DoMathPowHalf(LUnaryMathOperation* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  DoubleRegister input = ToDoubleRegister(instr->InputAt(0));
  DoubleRegister result = ToDoubleRegister(instr->result());
  // Add +0 to convert -0 to +0.
  __ add_d(result, input, kDoubleRegZero);
  __ vsqrt(result, result);
#endif
}


void LCodeGen::DoPower(LPower* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  LOperand* left = instr->InputAt(0);
  LOperand* right = instr->InputAt(1);
  Register scratch = scratch0();
  DoubleRegister result_reg = ToDoubleRegister(instr->result());
  Representation exponent_type = instr->hydrogen()->right()->representation();
  if (exponent_type.IsDouble()) {
    // Prepare arguments and call C function.
    __ PrepareCallCFunction(0, 2, scratch);
    __ SetCallCDoubleArguments(ToDoubleRegister(left),
                               ToDoubleRegister(right));
    __ CallCFunction(
        ExternalReference::power_double_double_function(isolate()), 0, 2);
  } else if (exponent_type.IsInteger32()) {
    ASSERT(ToRegister(right).is(a0));
    // Prepare arguments and call C function.
    __ PrepareCallCFunction(1, 1, scratch);
    __ SetCallCDoubleArguments(ToDoubleRegister(left), ToRegister(right));
    __ CallCFunction(
        ExternalReference::power_double_int_function(isolate()), 1, 1);
  } else {
    ASSERT(exponent_type.IsTagged());
    ASSERT(instr->hydrogen()->left()->representation().IsDouble());

    Register right_reg = ToRegister(right);

    // Check for smi on the right hand side.
    Label non_smi, call;
    __ JumpIfNotSmi(right_reg, &non_smi);

    // Untag smi and convert it to a double.
    __ SmiUntag(right_reg);
    FPURegister single_scratch = double_scratch0().low();
    __ mtc1(right_reg, single_scratch);
    __ cvt_d_w(result_reg, single_scratch);
    __ jmp(&call);

    // Heap number map check.
    __ bind(&non_smi);
    __ lw(scratch, FieldMemOperand(right_reg, HeapObject::kMapOffset));
    __ LoadRoot(at, Heap::kHeapNumberMapRootIndex);
    DeoptimizeIf(ne, instr->environment(), scratch, Operand(at));
    int32_t value_offset = HeapNumber::kValueOffset - kHeapObjectTag;
    __ Addu(scratch, right_reg, Operand(value_offset));
    __ ldc1(result_reg, MemOperand(scratch, 0));

    // Prepare arguments and call C function.
    __ bind(&call);
    __ PrepareCallCFunction(0, 2, scratch);
    __ SetCallCDoubleArguments(ToDoubleRegister(left), result_reg);
    __ CallCFunction(
        ExternalReference::power_double_double_function(isolate()), 0, 2);
  }
  // Store the result in the result register.
  __ GetCFunctionDoubleResult(result_reg);
#endif
}


void LCodeGen::DoMathLog(LUnaryMathOperation* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(transcend_rval_reg));
  TranscendentalCacheStub stub(TranscendentalCache::LOG,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoMathCos(LUnaryMathOperation* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(transcend_rval_reg));
  TranscendentalCacheStub stub(TranscendentalCache::COS,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoMathSin(LUnaryMathOperation* instr) {
  ASSERT(ToDoubleRegister(instr->result()).is(transcend_rval_reg));
  TranscendentalCacheStub stub(TranscendentalCache::SIN,
                               TranscendentalCacheStub::UNTAGGED);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoUnaryMathOperation(LUnaryMathOperation* instr) {
  switch (instr->op()) {
    case kMathAbs:
      DoMathAbs(instr);
      break;
    case kMathFloor:
      DoMathFloor(instr);
      break;
    case kMathRound:
      DoMathRound(instr);
      break;
    case kMathSqrt:
      DoMathSqrt(instr);
      break;
    case kMathPowHalf:
      DoMathPowHalf(instr);
      break;
    case kMathCos:
      DoMathCos(instr);
      break;
    case kMathSin:
      DoMathSin(instr);
      break;
    case kMathLog:
      DoMathLog(instr);
      break;
    default:
      Abort("Unimplemented type of LUnaryMathOperation.");
      UNREACHABLE();
  }
}


void LCodeGen::DoInvokeFunction(LInvokeFunction* instr) {
  ASSERT(ToRegister(instr->function()).is(a1));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  ASSERT(instr->HasPointerMap());
  ASSERT(instr->HasDeoptimizationEnvironment());
  LPointerMap* pointers = instr->pointer_map();
  LEnvironment* env = instr->deoptimization_environment();
  RecordPosition(pointers->position());
  RegisterEnvironmentForDeoptimization(env);
  SafepointGenerator generator(this, pointers, env->deoptimization_index());
  ParameterCount count(instr->arity());
  __ InvokeFunction(a1, count, CALL_FUNCTION, generator, CALL_AS_METHOD);
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoCallKeyed(LCallKeyed* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  int arity = instr->arity();
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeKeyedCallInitialize(arity, NOT_IN_LOOP);
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoCallNamed(LCallNamed* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  int arity = instr->arity();
  RelocInfo::Mode mode = RelocInfo::CODE_TARGET;
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeCallInitialize(arity, NOT_IN_LOOP, mode);
  __ li(a2, Operand(instr->name()));
  CallCode(ic, mode, instr);
  // Restore context register.
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoCallFunction(LCallFunction* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  int arity = instr->arity();
  CallFunctionStub stub(arity, NOT_IN_LOOP, RECEIVER_MIGHT_BE_IMPLICIT);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
  __ Drop(1);
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoCallGlobal(LCallGlobal* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  int arity = instr->arity();
  RelocInfo::Mode mode = RelocInfo::CODE_TARGET_CONTEXT;
  Handle<Code> ic =
      isolate()->stub_cache()->ComputeCallInitialize(arity, NOT_IN_LOOP, mode);
  __ li(a2, Operand(instr->name()));
  CallCode(ic, mode, instr);
  __ lw(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
}


void LCodeGen::DoCallKnownGlobal(LCallKnownGlobal* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  __ li(a1, Operand(instr->target()));
  CallKnownFunction(instr->target(), instr->arity(), instr, CALL_AS_FUNCTION);
}


void LCodeGen::DoCallNew(LCallNew* instr) {
  ASSERT(ToRegister(instr->InputAt(0)).is(a1));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));

  Handle<Code> builtin = isolate()->builtins()->JSConstructCall();
  __ li(a0, Operand(instr->arity()));
  CallCode(builtin, RelocInfo::CONSTRUCT_CALL, instr);
}


void LCodeGen::DoCallRuntime(LCallRuntime* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  CallRuntime(instr->function(), instr->arity(), instr);
}


void LCodeGen::DoStoreNamedField(LStoreNamedField* instr) {
  Register object = ToRegister(instr->object());
  Register value = ToRegister(instr->value());
  Register scratch = scratch0();
  int offset = instr->offset();

  ASSERT(!object.is(value));

  if (!instr->transition().is_null()) {
    __ li(scratch, Operand(instr->transition()));
    __ sw(scratch, FieldMemOperand(object, HeapObject::kMapOffset));
  }

  // Do the store.
  if (instr->is_in_object()) {
    __ sw(value, FieldMemOperand(object, offset));
    if (instr->needs_write_barrier()) {
      // Update the write barrier for the object for in-object properties.
      __ RecordWrite(object, Operand(offset), value, scratch);
    }
  } else {
    __ lw(scratch, FieldMemOperand(object, JSObject::kPropertiesOffset));
    __ sw(value, FieldMemOperand(scratch, offset));
    if (instr->needs_write_barrier()) {
      // Update the write barrier for the properties array.
      // object is used as a scratch register.
      __ RecordWrite(scratch, Operand(offset), value, object);
    }
  }
}


void LCodeGen::DoStoreNamedGeneric(LStoreNamedGeneric* instr) {
  ASSERT(ToRegister(instr->object()).is(a1));
  ASSERT(ToRegister(instr->value()).is(a0));

  // Name is always in a2.
  __ li(a2, Operand(instr->name()));
  Handle<Code> ic = instr->strict_mode()
      ? isolate()->builtins()->StoreIC_Initialize_Strict()
      : isolate()->builtins()->StoreIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoBoundsCheck(LBoundsCheck* instr) {
  DeoptimizeIf(hs, instr->environment(), ToRegister(instr->index()), ToRegister(instr->length()));
}


void LCodeGen::DoStoreKeyedFastElement(LStoreKeyedFastElement* instr) {
  Register value = ToRegister(instr->value());
  Register elements = ToRegister(instr->object());
  Register key = instr->key()->IsRegister() ? ToRegister(instr->key()) : no_reg;
  Register scratch = scratch0();

  // Do the store.
  if (instr->key()->IsConstantOperand()) {
    ASSERT(!instr->hydrogen()->NeedsWriteBarrier());
    LConstantOperand* const_operand = LConstantOperand::cast(instr->key());
    int offset =
        ToInteger32(const_operand) * kPointerSize + FixedArray::kHeaderSize;
    __ sw(value, FieldMemOperand(elements, offset));
  } else {
    __ sll(scratch, key, kPointerSizeLog2);
    __ addu(scratch, scratch, elements);
    __ sw(value, FieldMemOperand(scratch, FixedArray::kHeaderSize));
  }

  if (instr->hydrogen()->NeedsWriteBarrier()) {
    // Compute address of modified element and store it into key register.
    __ Addu(key, scratch, Operand(FixedArray::kHeaderSize));
    __ RecordWrite(elements, key, value);
  }
}


void LCodeGen::DoStoreKeyedSpecializedArrayElement(
    LStoreKeyedSpecializedArrayElement* instr) {

  Register external_pointer = ToRegister(instr->external_pointer());
  Register key = no_reg;
  JSObject::ElementsKind elements_kind = instr->elements_kind();
  bool key_is_constant = instr->key()->IsConstantOperand();
  int constant_key = 0;
  if (key_is_constant) {
    constant_key = ToInteger32(LConstantOperand::cast(instr->key()));
    if (constant_key & 0xF0000000) {
      Abort("array index constant value too big.");
    }
  } else {
    key = ToRegister(instr->key());
  }
  int shift_size = ElementsKindToShiftSize(elements_kind);

  if (elements_kind == JSObject::EXTERNAL_FLOAT_ELEMENTS ||
      elements_kind == JSObject::EXTERNAL_DOUBLE_ELEMENTS) {
    CpuFeatures::Scope scope(FPU);
    FPURegister value(ToDoubleRegister(instr->value()));
    if (key_is_constant) {
      __ Addu(scratch0(), external_pointer,
             Operand(constant_key << shift_size));
    } else {
      __ sll(scratch0(), key, shift_size);
      __ addu(scratch0(), scratch0(), external_pointer);
    }
    if (elements_kind == JSObject::EXTERNAL_FLOAT_ELEMENTS) {
      __ cvt_s_d(double_scratch0().low(), value);
      __ sdc1(double_scratch0().low(), MemOperand(scratch0(), 0));
    } else {  // i.e. elements_kind == JSObject::EXTERNAL_DOUBLE_ELEMENTS
      __ sdc1(value, MemOperand(scratch0(), 0));
    }
  } else {
    Register value(ToRegister(instr->value()));
    MemOperand mem_operand = MemOperand(no_reg);
    if (key_is_constant) {
      mem_operand = MemOperand(external_pointer,
                               constant_key << shift_size);
    } else {
      __ sll(scratch0(), key, shift_size);
      __ addu(scratch0(), scratch0(), external_pointer);
      mem_operand = MemOperand(scratch0(), 0);
    }
    switch (elements_kind) {
      case JSObject::EXTERNAL_PIXEL_ELEMENTS:
      case JSObject::EXTERNAL_BYTE_ELEMENTS:
      case JSObject::EXTERNAL_UNSIGNED_BYTE_ELEMENTS:
        __ sb(value, mem_operand);
        break;
      case JSObject::EXTERNAL_SHORT_ELEMENTS:
      case JSObject::EXTERNAL_UNSIGNED_SHORT_ELEMENTS:
        __ sh(value, mem_operand);
        break;
      case JSObject::EXTERNAL_INT_ELEMENTS:
      case JSObject::EXTERNAL_UNSIGNED_INT_ELEMENTS:
        __ sw(value, mem_operand);
        break;
      case JSObject::EXTERNAL_FLOAT_ELEMENTS:
      case JSObject::EXTERNAL_DOUBLE_ELEMENTS:
      case JSObject::FAST_DOUBLE_ELEMENTS:
      case JSObject::FAST_ELEMENTS:
      case JSObject::DICTIONARY_ELEMENTS:
      case JSObject::NON_STRICT_ARGUMENTS_ELEMENTS:
        UNREACHABLE();
        break;
    }
  }
}


void LCodeGen::DoStoreKeyedGeneric(LStoreKeyedGeneric* instr) {
  ASSERT(ToRegister(instr->object()).is(a2));
  ASSERT(ToRegister(instr->key()).is(a1));
  ASSERT(ToRegister(instr->value()).is(a0));

  Handle<Code> ic = instr->strict_mode()
      ? isolate()->builtins()->KeyedStoreIC_Initialize_Strict()
      : isolate()->builtins()->KeyedStoreIC_Initialize();
  CallCode(ic, RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoStringAdd(LStringAdd* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  __ push(ToRegister(instr->left()));
  __ push(ToRegister(instr->right()));
  StringAddStub stub(NO_STRING_CHECK_IN_STUB);
  CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
}


void LCodeGen::DoStringCharCodeAt(LStringCharCodeAt* instr) {
  class DeferredStringCharCodeAt: public LDeferredCode {
   public:
    DeferredStringCharCodeAt(LCodeGen* codegen, LStringCharCodeAt* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() { codegen()->DoDeferredStringCharCodeAt(instr_); }
   private:
    LStringCharCodeAt* instr_;
  };

  Register scratch = scratch0();
  Register string = ToRegister(instr->string());
  Register index = no_reg;
  int const_index = -1;
  if (instr->index()->IsConstantOperand()) {
    const_index = ToInteger32(LConstantOperand::cast(instr->index()));
    STATIC_ASSERT(String::kMaxLength <= Smi::kMaxValue);
    if (!Smi::IsValid(const_index)) {
      // Guaranteed to be out of bounds because of the assert above.
      // So the bounds check that must dominate this instruction must
      // have deoptimized already.
      if (FLAG_debug_code) {
        __ Abort("StringCharCodeAt: out of bounds index.");
      }
      // No code needs to be generated.
      return;
    }
  } else {
    index = ToRegister(instr->index());
  }
  Register result = ToRegister(instr->result());

  DeferredStringCharCodeAt* deferred =
      new DeferredStringCharCodeAt(this, instr);

  Label flat_string, ascii_string, done;

  // Fetch the instance type of the receiver into result register.
  __ lw(result, FieldMemOperand(string, HeapObject::kMapOffset));
  __ lbu(result, FieldMemOperand(result, Map::kInstanceTypeOffset));

  // We need special handling for non-flat strings.
  STATIC_ASSERT(kSeqStringTag == 0);
  __ And(at, result, Operand(kStringRepresentationMask));
  __ Branch(&flat_string, eq, at, Operand(zero_reg));

  // Handle non-flat strings.
  __ And(at, result, Operand(kIsConsStringMask));
  __ Branch(deferred->entry(), eq, at, Operand(zero_reg));

  // ConsString.
  // Check whether the right hand side is the empty string (i.e. if
  // this is really a flat string in a cons string). If that is not
  // the case we would rather go to the runtime system now to flatten
  // the string.
  __ lw(scratch, FieldMemOperand(string, ConsString::kSecondOffset));
  __ LoadRoot(at, Heap::kEmptyStringRootIndex);
  __ Branch(deferred->entry(), ne, scratch, at);
  // Get the first of the two strings and load its instance type.
  __ lw(string, FieldMemOperand(string, ConsString::kFirstOffset));
  __ lw(result, FieldMemOperand(string, HeapObject::kMapOffset));
  __ lbu(result, FieldMemOperand(result, Map::kInstanceTypeOffset));
  // If the first cons component is also non-flat, then go to runtime.
  STATIC_ASSERT(kSeqStringTag == 0);
  __ And(at, result, Operand(kStringRepresentationMask));
  __ Branch(deferred->entry(), ne, at, Operand(zero_reg));

  // Check for 1-byte or 2-byte string.
  __ bind(&flat_string);
  STATIC_ASSERT(kAsciiStringTag != 0);
  __ And(at, result, Operand(kStringEncodingMask));
  __ Branch(&ascii_string, ne, at, Operand(zero_reg));

  // 2-byte string.
  // Load the 2-byte character code into the result register.
  STATIC_ASSERT(kSmiTag == 0 && kSmiTagSize == 1);
  if (instr->index()->IsConstantOperand()) {
    __ lhu(result,
            FieldMemOperand(string,
                            SeqTwoByteString::kHeaderSize + 2 * const_index));
  } else {
    __ sll(scratch, index, 1);
    __ addu(scratch, scratch, string);
    __ lhu(result, MemOperand(scratch,
                               SeqTwoByteString::kHeaderSize - kHeapObjectTag));
  }
  __ jmp(&done);

  // ASCII string.
  // Load the byte into the result register.
  __ bind(&ascii_string);
  if (instr->index()->IsConstantOperand()) {
    __ lbu(result, FieldMemOperand(string,
                                    SeqAsciiString::kHeaderSize + const_index));
  } else {
    __ addu(scratch, string, index);
    __ lbu(result, MemOperand(scratch,
                               SeqAsciiString::kHeaderSize - kHeapObjectTag));

  }
  __ bind(&done);
  __ bind(deferred->exit());
}


void LCodeGen::DoDeferredStringCharCodeAt(LStringCharCodeAt* instr) {
  Register string = ToRegister(instr->string());
  Register result = ToRegister(instr->result());
  Register scratch = scratch0();

  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  __ mov(result, zero_reg);

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  __ push(string);
  // Push the index as a smi. This is safe because of the checks in
  // DoStringCharCodeAt above.
  if (instr->index()->IsConstantOperand()) {
    int const_index = ToInteger32(LConstantOperand::cast(instr->index()));
    __ li(scratch, Operand(Smi::FromInt(const_index)));
    __ push(scratch);
  } else {
    Register index = ToRegister(instr->index());
    __ SmiTag(index);
    __ push(index);
  }
  CallRuntimeFromDeferred(Runtime::kStringCharCodeAt, 2, instr);
  if (FLAG_debug_code) {
    __ AbortIfNotSmi(c_rval_reg);
  }
  __ SmiUntag(c_rval_reg);
  __ StoreToSafepointRegisterSlot(c_rval_reg, result);
}


void LCodeGen::DoStringCharFromCode(LStringCharFromCode* instr) {
  class DeferredStringCharFromCode: public LDeferredCode {
   public:
    DeferredStringCharFromCode(LCodeGen* codegen, LStringCharFromCode* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() { codegen()->DoDeferredStringCharFromCode(instr_); }
   private:
    LStringCharFromCode* instr_;
  };

  DeferredStringCharFromCode* deferred =
      new DeferredStringCharFromCode(this, instr);

  ASSERT(instr->hydrogen()->value()->representation().IsInteger32());
  Register char_code = ToRegister(instr->char_code());
  Register result = ToRegister(instr->result());
  ASSERT(!char_code.is(result));

  __ Branch(deferred->entry(), hi, char_code, Operand(String::kMaxAsciiCharCode));
  __ LoadRoot(result, Heap::kSingleCharacterStringCacheRootIndex);
  __ sll(scratch0(), char_code, kPointerSizeLog2);
  __ addu(result, result, scratch0());
  __ lw(result, FieldMemOperand(result, FixedArray::kHeaderSize));
  __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
  __ Branch(deferred->entry(), eq, result, at);
  __ bind(deferred->exit());
}


void LCodeGen::DoDeferredStringCharFromCode(LStringCharFromCode* instr) {
  Register char_code = ToRegister(instr->char_code());
  Register result = ToRegister(instr->result());

  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  __ mov(result, zero_reg);

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  __ SmiTag(char_code);
  __ push(char_code);
  CallRuntimeFromDeferred(Runtime::kCharFromCode, 1, instr);
  __ StoreToSafepointRegisterSlot(c_rval_reg, result);
}


void LCodeGen::DoStringLength(LStringLength* instr) {
  Register string = ToRegister(instr->InputAt(0));
  Register result = ToRegister(instr->result());
  __ lw(result, FieldMemOperand(string, String::kLengthOffset));
}


void LCodeGen::DoInteger32ToDouble(LInteger32ToDouble* instr) {
  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister() || input->IsStackSlot());
  LOperand* output = instr->result();
  ASSERT(output->IsDoubleRegister());
  FPURegister single_scratch = double_scratch0().low();
  if (input->IsStackSlot()) {
    Register scratch = scratch0();
    __ lw(scratch, ToMemOperand(input));
    __ mtc1(scratch, single_scratch);
  } else {
    __ mtc1(ToRegister(input), single_scratch);
  }
  __ cvt_d_w(ToDoubleRegister(output), single_scratch);
}


void LCodeGen::DoNumberTagI(LNumberTagI* instr) {
  class DeferredNumberTagI: public LDeferredCode {
   public:
    DeferredNumberTagI(LCodeGen* codegen, LNumberTagI* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() { codegen()->DoDeferredNumberTagI(instr_); }
   private:
    LNumberTagI* instr_;
  };

  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister() && input->Equals(instr->result()));
  Register reg = ToRegister(input);

  DeferredNumberTagI* deferred = new DeferredNumberTagI(this, instr);
  __ TrySmiTag(reg, deferred->entry(), scratch0());
  __ bind(deferred->exit());
}


void LCodeGen::DoDeferredNumberTagI(LNumberTagI* instr) {
  Label slow;
  Register reg = ToRegister(instr->InputAt(0));
  DoubleRegister dbl_scratch = double_scratch0();
  FPURegister flt_scratch = dbl_scratch.low();

  // Preserve the value of all registers.
  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);

  // Try to allocate a heap number in new space and store
  // the value in there. If that fails, call the runtime system.
  Label done;
  __ mtc1(reg, flt_scratch);
  __ cvt_d_w(dbl_scratch, flt_scratch);
  if (FLAG_inline_new) {
    __ LoadRoot(t2, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(t1, a3, t0, t2, &slow);
    if (!reg.is(t1))
      __ mov(reg, t1);
    __ Branch(&done);
  }

  // Slow case: Call the runtime system to do the number allocation.
  __ bind(&slow);

  // TODO(3095996): Put a valid pointer value in the stack slot where the result
  // register is stored, as this register is in the pointer map, but contains an
  // integer value.
  __ mov(at, zero_reg);
  __ StoreToSafepointRegisterSlot(at, reg);
  CallRuntimeFromDeferred(Runtime::kAllocateHeapNumber, 0, instr);
  if (!reg.is(c_rval_reg))
     __ mov(reg, c_rval_reg);

  // Done. Put the value in dbl_scratch into the value of the allocated heap
  // number.
  __ bind(&done);
  __ Subu(at, reg, Operand(kHeapObjectTag));
  __ sdc1(dbl_scratch, MemOperand(at, HeapNumber::kValueOffset));
  __ StoreToSafepointRegisterSlot(reg, reg);
}


void LCodeGen::DoNumberTagD(LNumberTagD* instr) {
  class DeferredNumberTagD: public LDeferredCode {
   public:
    DeferredNumberTagD(LCodeGen* codegen, LNumberTagD* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() { codegen()->DoDeferredNumberTagD(instr_); }
   private:
    LNumberTagD* instr_;
  };

  DoubleRegister input_reg = ToDoubleRegister(instr->InputAt(0));
  Register scratch = scratch0();
  Register reg = ToRegister(instr->result());
  Register temp1 = ToRegister(instr->TempAt(0));
  Register temp2 = ToRegister(instr->TempAt(1));

  DeferredNumberTagD* deferred = new DeferredNumberTagD(this, instr);
  if (FLAG_inline_new) {
    __ LoadRoot(scratch, Heap::kHeapNumberMapRootIndex);
    __ AllocateHeapNumber(reg, temp1, temp2, scratch, deferred->entry());
  } else {
    __ jmp(deferred->entry());
  }
  __ bind(deferred->exit());
  __ Subu(at, reg, Operand(kHeapObjectTag));
  __ sdc1(input_reg, MemOperand(at, HeapNumber::kValueOffset));
}


void LCodeGen::DoDeferredNumberTagD(LNumberTagD* instr) {
  // TODO(3095996): Get rid of this. For now, we need to make the
  // result register contain a valid pointer because it is already
  // contained in the register pointer map.
  Register reg = ToRegister(instr->result());
  __ mov(reg, zero_reg);

  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  CallRuntimeFromDeferred(Runtime::kAllocateHeapNumber, 0, instr);
  __ StoreToSafepointRegisterSlot(c_rval_reg, reg);
}


void LCodeGen::DoSmiTag(LSmiTag* instr) {
  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister() && input->Equals(instr->result()));
  ASSERT(!instr->hydrogen_value()->CheckFlag(HValue::kCanOverflow));
  __ SmiTag(ToRegister(input));
}


void LCodeGen::DoSmiUntag(LSmiUntag* instr) {
  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister() && input->Equals(instr->result()));
  Register reg = ToRegister(input);
  if (instr->needs_check()) {
    ASSERT(kHeapObjectTag == 1);
    __ And(at, reg, Operand(1));
    DeoptimizeIf(ne, instr->environment(), at, Operand(zero_reg));
  }
  __ SmiUntag(reg);
}


void LCodeGen::EmitNumberUntagD(Register input_reg,
                                DoubleRegister result_reg,
                                bool deoptimize_on_undefined,
                                LInstruction* instr) {
  Register scratch = scratch0();
  FPURegister flt_scratch = double_scratch0().low();
  ASSERT(!result_reg.is(double_scratch0()));

  Label load_smi, heap_number, done;

  // Smi check.
  __ JumpIfSmi(input_reg, &load_smi);

  // Heap number map check.
  __ lw(scratch, FieldMemOperand(input_reg, HeapObject::kMapOffset));
  __ LoadRoot(at, Heap::kHeapNumberMapRootIndex);
  if (deoptimize_on_undefined) {
    DeoptimizeIf(ne, instr->environment(), scratch, Operand(at));
  } else {
    Label heap_number;
    __ Branch(&heap_number, eq, scratch, Operand(at));

    __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
    DeoptimizeIf(ne, instr->environment(), input_reg, Operand(at));  

    // Convert undefined to NaN.
    __ LoadRoot(at, Heap::kNanValueRootIndex);
    __ Subu(at, at, Operand(kHeapObjectTag));
    __ ldc1(result_reg, MemOperand(at, HeapNumber::kValueOffset));
    __ jmp(&done);

    __ bind(&heap_number);
  }
  // Heap number to double register conversion.
  __ Subu(at, input_reg, Operand(kHeapObjectTag));
  __ ldc1(result_reg, MemOperand(at, HeapNumber::kValueOffset));
  __ jmp(&done);

  // Smi to double register conversion
  __ bind(&load_smi);
  __ SmiUntag(input_reg);  // Untag smi before converting to float.
  __ mtc1(input_reg, flt_scratch);
  __ cvt_d_w(result_reg, flt_scratch);
  __ SmiTag(input_reg);  // Retag smi.
  __ bind(&done);
}


class DeferredTaggedToI: public LDeferredCode {
 public:
  DeferredTaggedToI(LCodeGen* codegen, LTaggedToI* instr)
      : LDeferredCode(codegen), instr_(instr) { }
  virtual void Generate() { codegen()->DoDeferredTaggedToI(instr_); }
 private:
  LTaggedToI* instr_;
};


void LCodeGen::DoDeferredTaggedToI(LTaggedToI* instr) {
  Register input_reg = ToRegister(instr->InputAt(0));
  Register scratch1 = scratch0();
  Register scratch2 = ToRegister(instr->TempAt(0));
  FPURegister double_scratch = double_scratch0();
  FPURegister single_scratch = double_scratch.low();

  ASSERT(!scratch1.is(input_reg) && !scratch1.is(scratch2));
  ASSERT(!scratch2.is(input_reg) && !scratch2.is(scratch1));
  ASSERT(ToRegister(instr->result()).is(input_reg));

  Label done;

  // The input is a tagged HeapObject.
  ASSERT(kHeapObjectTag == 1);
  // Heap number map check.
  __ lw(scratch1, FieldMemOperand(input_reg, HeapObject::kMapOffset));
  __ LoadRoot(at, Heap::kHeapNumberMapRootIndex);

  if (instr->truncating()) {
    Register scratch3 = ToRegister(instr->TempAt(1));
    FPURegister double_scratch2 = ToDoubleRegister(instr->TempAt(2));
    ASSERT(!scratch3.is(input_reg) &&
           !scratch3.is(scratch1) &&
           !scratch3.is(scratch2));
    // Performs a truncating conversion of a floating point number as used by
    // the JS bitwise operations.
    Label heap_number;
    __ Branch(&heap_number, eq, scratch1, Operand(at));  // HeapNumber map?
    // Check for undefined. Undefined is converted to zero for truncating
    // conversions.
    __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
    DeoptimizeIf(ne, instr->environment(), input_reg, Operand(at));
    __ mov(input_reg, zero_reg);
    __ Branch(&done);

    __ bind(&heap_number);
    __ Subu(scratch1, input_reg, Operand(kHeapObjectTag));
    __ ldc1(double_scratch2, MemOperand(scratch1, HeapNumber::kValueOffset));

    __ EmitECMATruncate(input_reg,
                        double_scratch2,
                        single_scratch,
                        scratch1,
                        scratch2,
                        scratch3);

  } else {
    CpuFeatures::Scope scope(FPU);
    // Deoptimize if we don't have a heap number.
    DeoptimizeIf(ne, instr->environment(), scratch1, Operand(at));

    __ ldc1(double_scratch, FieldMemOperand(input_reg, HeapNumber::kValueOffset));
#ifdef V8_TARGET_ARCH_MIPS
    // TODO(plind): this code is still untested.
    __ cfc1(scratch1, FCSR);  // Save current FCSR
    __ ctc1(zero_reg, FCSR);  // Disable FP exceptions
    // TODO(plind): it appears this instuction rounds towards zero regardless
    // of the rounding mode in FCSR. Verify, fix if needed, and remove comment.
    __ trunc_w_d(single_scratch, double_scratch);
    __ cfc1(scratch2, FCSR);  // Test flags
    __ ctc1(scratch1, FCSR);  // Restore prior FCSR

    // Check for inexact conversion or exception (non-zero flags).
    // Deopt if the operation did not succeed.
    __ And(at, scratch2, Operand(kFCSRFlagMask));
    DeoptimizeIf(ne, instr->environment(), at, Operand(zero_reg));
#else
    __ EmitVFPTruncate(kRoundToZero,
                       single_scratch,
                       double_scratch,
                       scratch1,
                       scratch2,
                       kCheckForInexactConversion);
    DeoptimizeIf(ne, instr->environment());
#endif
    // Load the result.
    __ mfc1(input_reg, single_scratch);

    if (instr->hydrogen()->CheckFlag(HValue::kBailoutOnMinusZero)) {
      __ Branch(&done, ne, input_reg, Operand(zero_reg));
      __ mfc1(scratch1, double_scratch.high());
      __ And(at, scratch1, Operand(HeapNumber::kSignMask));
      DeoptimizeIf(ne, instr->environment(), at, Operand(zero_reg));
    }
  }
  __ bind(&done);
}


void LCodeGen::DoTaggedToI(LTaggedToI* instr) {
  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister());
  ASSERT(input->Equals(instr->result()));

  Register input_reg = ToRegister(input);

  DeferredTaggedToI* deferred = new DeferredTaggedToI(this, instr);

  // Branch to deferred code if the input was tagged.
  __ JumpIfNotSmi(input_reg, deferred->entry());
  __ SmiUntag(input_reg);
  __ bind(deferred->exit());
}


void LCodeGen::DoNumberUntagD(LNumberUntagD* instr) {
  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister());
  LOperand* result = instr->result();
  ASSERT(result->IsDoubleRegister());

  Register input_reg = ToRegister(input);
  DoubleRegister result_reg = ToDoubleRegister(result);

  EmitNumberUntagD(input_reg, result_reg,
                   instr->hydrogen()->deoptimize_on_undefined(),
                   instr);
}


void LCodeGen::DoDoubleToI(LDoubleToI* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  Register result_reg = ToRegister(instr->result());
  Register scratch1 = scratch0();
  Register scratch2 = ToRegister(instr->TempAt(0));
  FPURegister double_input = ToDoubleRegister(instr->InputAt(0));
  FPURegister double_scratch = double_scratch0();
  FPURegister single_scratch = double_scratch0().low();

  Label done;

  if (instr->truncating()) {
    Register scratch3 = ToRegister(instr->TempAt(1));
    __ EmitECMATruncate(result_reg,
                        double_input,
                        single_scratch,
                        scratch1,
                        scratch2,
                        scratch3);
  } else {
    VFPRoundingMode rounding_mode = kRoundToMinusInf;
    __ EmitVFPTruncate(rounding_mode,
                       single_scratch,
                       double_input,
                       scratch1,
                       scratch2,
                       kCheckForInexactConversion);
    // Deoptimize if we had a vfp invalid exception,
    // including inexact operation.
    DeoptimizeIf(ne, instr->environment());
    // Retrieve the result.
    __ mov_d(result_reg, single_scratch);
  }
    __ bind(&done);
#endif
}


void LCodeGen::DoCheckSmi(LCheckSmi* instr) {
  LOperand* input = instr->InputAt(0);
  __ And(at, ToRegister(input), Operand(kSmiTagMask));
  DeoptimizeIf(ne, instr->environment(), at, Operand(zero_reg));
}


void LCodeGen::DoCheckNonSmi(LCheckNonSmi* instr) {
  LOperand* input = instr->InputAt(0);
  __ And(at, ToRegister(input), Operand(kSmiTagMask));
  DeoptimizeIf(eq, instr->environment(), at, Operand(zero_reg));
}


void LCodeGen::DoCheckInstanceType(LCheckInstanceType* instr) {
  Register input = ToRegister(instr->InputAt(0));
  Register scratch = scratch0();

  __ lw(scratch, FieldMemOperand(input, HeapObject::kMapOffset));
  __ lbu(scratch, FieldMemOperand(scratch, Map::kInstanceTypeOffset));

  if (instr->hydrogen()->is_interval_check()) {
    InstanceType first;
    InstanceType last;
    instr->hydrogen()->GetCheckInterval(&first, &last);

    // If there is only one type in the interval check for equality.
    if (first == last) {
      DeoptimizeIf(ne, instr->environment(), scratch, Operand(first));
    } else {
      DeoptimizeIf(lo, instr->environment(), scratch, Operand(first));
      // Omit check for the last type.
      if (last != LAST_TYPE) {
        DeoptimizeIf(hi, instr->environment(), scratch, Operand(last));
      }
    }
  } else {
    uint8_t mask;
    uint8_t tag;
    instr->hydrogen()->GetCheckMaskAndTag(&mask, &tag);

    if (IsPowerOf2(mask)) {
      ASSERT(tag == 0 || IsPowerOf2(tag));
      __ And(at, scratch, Operand(mask));
      DeoptimizeIf(tag == 0 ? ne : eq, instr->environment(), at, Operand(zero_reg));
    } else {
      __ And(scratch, scratch, Operand(mask));
      DeoptimizeIf(ne, instr->environment(), scratch, Operand(tag));
    }
  }
}


void LCodeGen::DoCheckFunction(LCheckFunction* instr) {
  ASSERT(instr->InputAt(0)->IsRegister());
  Register reg = ToRegister(instr->InputAt(0));
  DeoptimizeIf(ne, instr->environment(), reg, Operand(instr->hydrogen()->target()));
}


void LCodeGen::DoCheckMap(LCheckMap* instr) {
  Register scratch = scratch0();
  LOperand* input = instr->InputAt(0);
  ASSERT(input->IsRegister());
  Register reg = ToRegister(input);
  __ lw(scratch, FieldMemOperand(reg, HeapObject::kMapOffset));
  DeoptimizeIf(ne, instr->environment(), scratch, Operand(instr->hydrogen()->map()));
}


void LCodeGen::DoClampDToUint8(LClampDToUint8* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  DoubleRegister value_reg = ToDoubleRegister(instr->unclamped());
  Register result_reg = ToRegister(instr->result());
  DoubleRegister temp_reg = ToDoubleRegister(instr->TempAt(0));
  __ ClampDoubleToUint8(result_reg, value_reg, temp_reg);
#endif
}


void LCodeGen::DoClampIToUint8(LClampIToUint8* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  Register unclamped_reg = ToRegister(instr->unclamped());
  Register result_reg = ToRegister(instr->result());
  __ ClampUint8(result_reg, unclamped_reg);
#endif
}


void LCodeGen::DoClampTToUint8(LClampTToUint8* instr) {
#ifdef MIPS_STUB
  NotYet;
#else
  Register scratch = scratch0();
  Register input_reg = ToRegister(instr->unclamped());
  Register result_reg = ToRegister(instr->result());
  DoubleRegister temp_reg = ToDoubleRegister(instr->TempAt(0));
  Label is_smi, done, heap_number;

  // Both smi and heap number cases are handled.
  __ JumpIfSmi(input_reg, &is_smi);

  // Check for heap number
  __ lw(scratch, FieldMemOperand(input_reg, HeapObject::kMapOffset));
  __ Branch(&heap_number, eq, scratch, Operand(factory()->heap_number_map()));

  // Check for undefined. Undefined is converted to zero for clamping
  // conversions.
  DeoptimizeIf(ne, instr->environment(), input_reg, Operand(factory()->undefined_value()));
  __ movt(input_reg, 0);
  __ jmp(&done);

  // Heap number
  __ bind(&heap_number);
  __ vldr(double_scratch0(), FieldMemOperand(input_reg,
                                             HeapNumber::kValueOffset));
  __ ClampDoubleToUint8(result_reg, double_scratch0(), temp_reg);
  __ jmp(&done);

  // smi
  __ bind(&is_smi);
  __ SmiUntag(result_reg, input_reg);
  __ ClampUint8(result_reg, result_reg);

  __ bind(&done);
#endif
}


void LCodeGen::LoadHeapObject(Register result,
                              Handle<HeapObject> object) {
  if (heap()->InNewSpace(*object)) {
    Handle<JSGlobalPropertyCell> cell =
        factory()->NewJSGlobalPropertyCell(object);
    __ li(result, Operand(cell));
    __ lw(result, FieldMemOperand(result, JSGlobalPropertyCell::kValueOffset));
  } else {
    __ li(result, Operand(object));
  }
}


void LCodeGen::DoCheckPrototypeMaps(LCheckPrototypeMaps* instr) {
  Register temp1 = ToRegister(instr->TempAt(0));
  Register temp2 = ToRegister(instr->TempAt(1));

  Handle<JSObject> holder = instr->holder();
  Handle<JSObject> current_prototype = instr->prototype();

  // Load prototype object.
  LoadHeapObject(temp1, current_prototype);

  // Check prototype maps up to the holder.
  while (!current_prototype.is_identical_to(holder)) {
    __ lw(temp2, FieldMemOperand(temp1, HeapObject::kMapOffset));
    DeoptimizeIf(ne, instr->environment(), temp2, Operand(Handle<Map>(current_prototype->map())));
    current_prototype =
        Handle<JSObject>(JSObject::cast(current_prototype->GetPrototype()));
    // Load next prototype object.
    LoadHeapObject(temp1, current_prototype);
  }

  // Check the holder map.
  __ lw(temp2, FieldMemOperand(temp1, HeapObject::kMapOffset));
  DeoptimizeIf(ne, instr->environment(), temp2, Operand(Handle<Map>(current_prototype->map())));
}


void LCodeGen::DoArrayLiteral(LArrayLiteral* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  __ lw(a3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ lw(a3, FieldMemOperand(a3, JSFunction::kLiteralsOffset));
  __ li(a2, Operand(Smi::FromInt(instr->hydrogen()->literal_index())));
  __ li(a1, Operand(instr->hydrogen()->constant_elements()));
  __ Push(a3, a2, a1);

  // Pick the right runtime function or stub to call.
  int length = instr->hydrogen()->length();
  if (instr->hydrogen()->IsCopyOnWrite()) {
    ASSERT(instr->hydrogen()->depth() == 1);
    FastCloneShallowArrayStub::Mode mode =
        FastCloneShallowArrayStub::COPY_ON_WRITE_ELEMENTS;
    FastCloneShallowArrayStub stub(mode, length);
    CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
  } else if (instr->hydrogen()->depth() > 1) {
    CallRuntime(Runtime::kCreateArrayLiteral, 3, instr);
  } else if (length > FastCloneShallowArrayStub::kMaximumClonedLength) {
    CallRuntime(Runtime::kCreateArrayLiteralShallow, 3, instr);
  } else {
    FastCloneShallowArrayStub::Mode mode =
        FastCloneShallowArrayStub::CLONE_ELEMENTS;
    FastCloneShallowArrayStub stub(mode, length);
    CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
  }
}


void LCodeGen::DoObjectLiteral(LObjectLiteral* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  __ lw(t0, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ lw(t0, FieldMemOperand(t0, JSFunction::kLiteralsOffset));
  __ li(a3, Operand(Smi::FromInt(instr->hydrogen()->literal_index())));
  __ li(a2, Operand(instr->hydrogen()->constant_properties()));
  __ li(a1, Operand(Smi::FromInt(instr->hydrogen()->fast_elements() ? 1 : 0)));
  __ Push(t0, a3, a2, a1);

  // Pick the right runtime function to call.
  if (instr->hydrogen()->depth() > 1) {
    CallRuntime(Runtime::kCreateObjectLiteral, 4, instr);
  } else {
    CallRuntime(Runtime::kCreateObjectLiteralShallow, 4, instr);
  }
}


void LCodeGen::DoToFastProperties(LToFastProperties* instr) {
  ASSERT(ToRegister(instr->InputAt(0)).is(a0));
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  __ push(a0);
  CallRuntime(Runtime::kToFastProperties, 1, instr);
}


void LCodeGen::DoRegExpLiteral(LRegExpLiteral* instr) {
  Label materialized;
  // Registers will be used as follows:
  // a3 = JS function.
  // t3 = literals array.
  // a1 = regexp literal.
  // a0 = regexp literal clone.
  // a2 and t0-t2 are used as temporaries.
  __ lw(a3, MemOperand(fp, JavaScriptFrameConstants::kFunctionOffset));
  __ lw(t3, FieldMemOperand(a3, JSFunction::kLiteralsOffset));
  int literal_offset = FixedArray::kHeaderSize +
      instr->hydrogen()->literal_index() * kPointerSize;
  __ lw(a1, FieldMemOperand(t3, literal_offset));
  __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
  __ Branch(&materialized, ne, a1, at);

  // Create regexp literal using runtime function
  // Result will be in c_rval_reg.
  __ li(t2, Operand(Smi::FromInt(instr->hydrogen()->literal_index())));
  __ li(t1, Operand(instr->hydrogen()->pattern()));
  __ li(t0, Operand(instr->hydrogen()->flags()));
  __ Push(t3, t2, t1, t0);
  CallRuntime(Runtime::kMaterializeRegExpLiteral, 4, instr);
  __ mov(a1, c_rval_reg);

  __ bind(&materialized);
  int size = JSRegExp::kSize + JSRegExp::kInObjectFieldCount * kPointerSize;
  Label allocated, runtime_allocate;

  __ AllocateInNewSpace(size, a0, a2, a3, &runtime_allocate, TAG_OBJECT);
  __ jmp(&allocated);

  __ bind(&runtime_allocate);
  __ li(a0, Operand(Smi::FromInt(size)));
  __ Push(a1, a0);
  CallRuntime(Runtime::kAllocateInNewSpace, 1, instr);
  __ pop(a1);

  __ bind(&allocated);
  // Copy the content into the newly allocated memory.
  // (Unroll copy loop once for better throughput).
  for (int i = 0; i < size - kPointerSize; i += 2 * kPointerSize) {
    __ lw(a3, FieldMemOperand(a1, i));
    __ lw(a2, FieldMemOperand(a1, i + kPointerSize));
    __ sw(a3, FieldMemOperand(a0, i));
    __ sw(a2, FieldMemOperand(a0, i + kPointerSize));
  }
  if ((size % (2 * kPointerSize)) != 0) {
    __ lw(a3, FieldMemOperand(a1, size - kPointerSize));
    __ sw(a3, FieldMemOperand(a0, size - kPointerSize));
  }
}


void LCodeGen::DoFunctionLiteral(LFunctionLiteral* instr) {
  // Use the fast case closure allocation code that allocates in new
  // space for nested functions that don't need literals cloning.
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  Handle<SharedFunctionInfo> shared_info = instr->shared_info();
  bool pretenure = instr->hydrogen()->pretenure();
  if (!pretenure && shared_info->num_literals() == 0) {
    FastNewClosureStub stub(
        shared_info->strict_mode() ? kStrictMode : kNonStrictMode);
    __ li(a1, Operand(shared_info));
    __ push(a1);
    CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
  } else {
    __ li(a2, Operand(shared_info));
    __ li(a1, Operand(pretenure
                       ? factory()->true_value()
                       : factory()->false_value()));
    __ Push(cp, a2, a1);
    CallRuntime(Runtime::kNewClosure, 3, instr);
  }
}


void LCodeGen::DoTypeof(LTypeof* instr) {
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  Register input = ToRegister(instr->InputAt(0));
  __ push(input);
  CallRuntime(Runtime::kTypeof, 1, instr);
}


void LCodeGen::DoTypeofIsAndBranch(LTypeofIsAndBranch* instr) {
  Register input = ToRegister(instr->InputAt(0));
  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());
  Label* true_label = chunk_->GetAssemblyLabel(true_block);
  Label* false_label = chunk_->GetAssemblyLabel(false_block);

  Condition final_branch_condition = EmitTypeofIs(true_label,
                                                  false_label,
                                                  input,
                                                  instr->type_literal());
  EmitBranch(true_block, false_block, final_branch_condition, at, Operand(zero_reg));
}


Condition LCodeGen::EmitTypeofIs(Label* true_label,
                                 Label* false_label,
                                 Register input,
                                 Handle<String> type_name) {
  Condition final_branch_condition = kNoCondition;
  Register scratch = scratch0();
  if (type_name->Equals(heap()->number_symbol())) {
    __ JumpIfSmi(input, true_label);
    __ lw(input, FieldMemOperand(input, HeapObject::kMapOffset));
    __ LoadRoot(at, Heap::kHeapNumberMapRootIndex);
    __ subu(at, at, input);
    // cmp(at, Operand(zero_reg));
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->string_symbol())) {
    __ JumpIfSmi(input, false_label);
    __ GetObjectType(input, input, scratch);
    __ Branch(false_label, ge, scratch, Operand(FIRST_NONSTRING_TYPE));
    __ lbu(at, FieldMemOperand(input, Map::kBitFieldOffset));
    __ And(at, at, Operand(1 << Map::kIsUndetectable));
    // cmp(at, Operand(zero_reg));
    final_branch_condition = eq;

  } else if (type_name->Equals(heap()->boolean_symbol())) {
    // CompareRoot(input, Heap::kTrueValueRootIndex);
    ASSERT(!input.is(at));
    __ LoadRoot(at, Heap::kTrueValueRootIndex);
    __ Branch(true_label, eq, input, at);
    // CompareRoot(input, Heap::kFalseValueRootIndex);
    ASSERT(!input.is(at));
    __ LoadRoot(at, Heap::kFalseValueRootIndex);
    __ Branch(true_label, eq, input, at);
    __ Branch(false_label);
    final_branch_condition = kNoCondition;

  } else if (type_name->Equals(heap()->undefined_symbol())) {
    // CompareRoot(input, Heap::kUndefinedValueRootIndex);
    ASSERT(!input.is(at));
    __ LoadRoot(at, Heap::kUndefinedValueRootIndex);
    __ Branch(true_label, eq, input, at);
    __ JumpIfSmi(input, false_label);
    // Check for undetectable objects => true.
    __ lw(input, FieldMemOperand(input, HeapObject::kMapOffset));
    __ lbu(at, FieldMemOperand(input, Map::kBitFieldOffset));
    __ And(at, at, Operand(1 << Map::kIsUndetectable));
    // cmp(at, Operand(zero_reg));
    final_branch_condition = ne;

  } else if (type_name->Equals(heap()->function_symbol())) {
    __ JumpIfSmi(input, false_label);
    __ GetObjectType(input, input, scratch);
    __ Branch(true_label, ge, scratch, Operand(FIRST_CALLABLE_SPEC_OBJECT_TYPE));
    __ Branch(false_label);
    final_branch_condition = kNoCondition;

  } else if (type_name->Equals(heap()->object_symbol())) {
    __ JumpIfSmi(input, false_label);
    // CompareRoot(input, Heap::kNullValueRootIndex);
    ASSERT(!input.is(at));
    __ LoadRoot(at, Heap::kNullValueRootIndex);
    __ Branch(true_label, eq, input, at);
    __ GetObjectType(input, input, scratch);
    __ Branch(false_label, lt, scratch, Operand(FIRST_NONCALLABLE_SPEC_OBJECT_TYPE));
    // CompareInstanceType(input, scratch, LAST_NONCALLABLE_SPEC_OBJECT_TYPE);
    __ lbu(scratch, FieldMemOperand(input, Map::kInstanceTypeOffset));
    __ Branch(false_label, gt, scratch, Operand(LAST_NONCALLABLE_SPEC_OBJECT_TYPE));
    // Check for undetectable objects => false.
    __ lbu(at, FieldMemOperand(input, Map::kBitFieldOffset));
    __ And(at, at, Operand(1 << Map::kIsUndetectable));
    // cmp(at, Operand(zero_reg));
    final_branch_condition = eq;

  } else {
    final_branch_condition = kNoCondition;
    __ Branch(false_label);
  }

  return final_branch_condition;
}


void LCodeGen::DoIsConstructCallAndBranch(LIsConstructCallAndBranch* instr) {
  Register temp1 = ToRegister(instr->TempAt(0));
  int true_block = chunk_->LookupDestination(instr->true_block_id());
  int false_block = chunk_->LookupDestination(instr->false_block_id());

  EmitIsConstructCall(temp1, scratch0());
  EmitBranch(true_block, false_block, eq, temp1, Operand(Smi::FromInt(StackFrame::CONSTRUCT)));
}


void LCodeGen::EmitIsConstructCall(Register temp1, Register temp2) {
  ASSERT(!temp1.is(temp2));
  // Get the frame pointer for the calling frame.
  __ lw(temp1, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));

  // Skip the arguments adaptor frame if it exists.
  Label check_frame_marker;
  __ lw(temp2, MemOperand(temp1, StandardFrameConstants::kContextOffset));
  __ Branch(&check_frame_marker, ne, temp2, Operand(Smi::FromInt(StackFrame::ARGUMENTS_ADAPTOR)));
  __ lw(temp1, MemOperand(temp1, StandardFrameConstants::kCallerFPOffset));

  // Check the marker in the calling frame.
  __ bind(&check_frame_marker);
  __ lw(temp1, MemOperand(temp1, StandardFrameConstants::kMarkerOffset));
  // cmp(temp1, Operand(Smi::FromInt(StackFrame::CONSTRUCT)));
}


void LCodeGen::DoLazyBailout(LLazyBailout* instr) {
  // No code for lazy bailout instruction. Used to capture environment after a
  // call for populating the safepoint data with deoptimization data.
}


void LCodeGen::DoDeoptimize(LDeoptimize* instr) {
  Deoptimize(instr->environment());
}


void LCodeGen::DoDeleteProperty(LDeleteProperty* instr) {
  Register object = ToRegister(instr->object());
  Register key = ToRegister(instr->key());
  Register strict = scratch0();
  __ li(strict, Operand(Smi::FromInt(strict_mode_flag())));
  __ Push(object, key, strict);
  ASSERT(instr->HasPointerMap() && instr->HasDeoptimizationEnvironment());
  LPointerMap* pointers = instr->pointer_map();
  LEnvironment* env = instr->deoptimization_environment();
  RecordPosition(pointers->position());
  RegisterEnvironmentForDeoptimization(env);
  SafepointGenerator safepoint_generator(this,
                                         pointers,
                                         env->deoptimization_index());
  __ InvokeBuiltin(Builtins::DELETE, CALL_FUNCTION, safepoint_generator);
}


void LCodeGen::DoIn(LIn* instr) {
  Register obj = ToRegister(instr->object());
  Register key = ToRegister(instr->key());
  ASSERT(ToRegister(instr->result()).is(c_rval_reg));
  __ Push(key, obj);
  ASSERT(instr->HasPointerMap() && instr->HasDeoptimizationEnvironment());
  LPointerMap* pointers = instr->pointer_map();
  LEnvironment* env = instr->deoptimization_environment();
  RecordPosition(pointers->position());
  RegisterEnvironmentForDeoptimization(env);
  SafepointGenerator safepoint_generator(this,
                                         pointers,
                                         env->deoptimization_index());
  __ InvokeBuiltin(Builtins::IN, CALL_FUNCTION, safepoint_generator);
}


void LCodeGen::DoDeferredStackCheck(LStackCheck* instr) {
  PushSafepointRegistersScope scope(this, Safepoint::kWithRegisters);
  CallRuntimeFromDeferred(Runtime::kStackGuard, 0, instr);
}


void LCodeGen::DoStackCheck(LStackCheck* instr) {
  class DeferredStackCheck: public LDeferredCode {
   public:
    DeferredStackCheck(LCodeGen* codegen, LStackCheck* instr)
        : LDeferredCode(codegen), instr_(instr) { }
    virtual void Generate() { codegen()->DoDeferredStackCheck(instr_); }
   private:
    LStackCheck* instr_;
  };

  if (instr->hydrogen()->is_function_entry()) {
    // Perform stack overflow check.
    Label done;
    __ LoadRoot(at, Heap::kStackLimitRootIndex);
    __ Branch(&done, hs, sp, Operand(at));
    StackCheckStub stub;
    CallCode(stub.GetCode(), RelocInfo::CODE_TARGET, instr);
    __ bind(&done);
  } else {
    ASSERT(instr->hydrogen()->is_backwards_branch());
    // Perform stack overflow check if this goto needs it before jumping.
    DeferredStackCheck* deferred_stack_check =
        new DeferredStackCheck(this, instr);
    __ LoadRoot(at, Heap::kStackLimitRootIndex);
    __ Branch(deferred_stack_check->entry(), lo, sp, Operand(at));
    __ bind(instr->done_label());
    deferred_stack_check->SetExit(instr->done_label());
  }
}


void LCodeGen::DoOsrEntry(LOsrEntry* instr) {
  // This is a pseudo-instruction that ensures that the environment here is
  // properly registered for deoptimization and records the assembler's PC
  // offset.
  LEnvironment* environment = instr->environment();
  environment->SetSpilledRegisters(instr->SpilledRegisterArray(),
                                   instr->SpilledDoubleRegisterArray());

  // If the environment were already registered, we would have no way of
  // backpatching it with the spill slot operands.
  ASSERT(!environment->HasBeenRegistered());
  RegisterEnvironmentForDeoptimization(environment);
  ASSERT(osr_pc_offset_ == -1);
  osr_pc_offset_ = masm()->pc_offset();
}


#undef __

} }  // namespace v8::internal