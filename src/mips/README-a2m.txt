The source file mips/lithium-codegen-mips-generated.cc is an alternate
implementation of lithium-codegen-mips, derived semi-mechanically from
arm/lithium-codegen-arm.cc.

This creation of a Mips-generating crankshaft codegen from the Arm
version has the following steps:

1. Manual removal of use of certain Arm instruction set features,
   including conditional skips, and the SetCC option, and shifts
   as part of operand address modes.  (Plus all other manual changes
   needed.)
      arm/lithium-codegen-arm.cc  --> arm/lithium-codegen-unarm.cc
   by a sequence of manual edits.
   The full version of unarm passes all tests on Arm simulator.

   The preprocessor symbol MIPS_STUB controls whether to fail out of
   certain routines that are interimly unimplemented on Mips.
   The subsetted version of Arm crankshaft fails a small number of
   mjsunit tests.

2. Mechanical removal of all other uses of Arm-specific instruction
   set features.  Includes merging of cmp or tst with following branch
   instructions.  The result makes no use of integer condition codes.
      arm/lithium-codegen-unarm.cc --> arm/lithium-codegen-arm-generated.cc
   by applying python tool   tools/disarm.py
   It still uses Arm-specific opcode names but is otherwise for a
   generic Risc machine with features common to Arm and Mips.
   The full generated verstion passes all tests on Arm simulator.
   The MIPS_STUB subsetted version fails a small number of mjsunit tests.

3. Mechanical replacement of Arm opcode names by the Mips equivalent.
      arm/lithium-codegen-arm-generated --> mips/lithium-codegen-mips-generated.cc
   by applying python tool  tools/arm2mips.py
   This makes a crankshaft lithium codegen for Mips, which covers most (but
   not all) Lithium opcodes and generates untuned Mips code.
   The result passes most tests on Mips simulator, but with some additional
   mjsunit failures beyond those of the subset lithium-codegen-arm-generated.

To build with these files, overwrite the lithium-codegen-X.cc file with
the lithium-codegen-X-generated.cc file and then build and run normallly.
Be careful to restore the original lithium-codegen-X.cc file before
doing git commits.

To build a codegen which combines methods from lithium-codegen-mips-generated
and the current manually-authored lithium-codegen-mips.cc, first upgrade
the latter to work with the *h files expected by the generated module.
You can then mix and match methods freely.
