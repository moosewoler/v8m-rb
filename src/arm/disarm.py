#!/usr/bin/python2.6

# Eliminate most uses of integer condition codes, skips, and shifted operands 
# in the Arm version of V8 lithium-codegen.cc.
# The result is closer to a generic risc (Mips-like but using Arm opcode names)
# but still builds and passes all V8 tests via the Arm simulator.
# The converted source is a prelude to semi-mechanically producing a Mips
# version of lithium-codegen using Mips opcodes.

SHIFT_OPS = ['LSL', 'LSR', 'ASR']

def get_op(line):
  halves = line.split(' __ ', 1)
  if len(halves) > 1 and not halves[0].lstrip():
    return halves[1].split('(', 1)[0]
  return ''

def get_func(line):
  return line.lstrip().split('(', 1)[0]

def parse_arg(line):
  i = 0
  nest = 0
  while True:
    if i >= len(line):
      raise SyntaxError
    c = line[i]
    if (c == ')' or c == ',') and nest <= 0: break
    if c == '(':
      nest += 1
    elif c == ')':
      nest -= 1
    i += 1
  return line[:i].strip(), line[i:]

def parse_call(line):
  if '(' not in line:
    return []
  indent_op,rest = line.split('(', 1)
  op = indent_op.lstrip()
  if ' ' in op:
    return []
  indent = indent_op[:-len(op)]
  args = [indent]
  rest = rest.lstrip()
  try:
    if rest[0] != ')':
      while True:
        arg,rest = parse_arg(rest)
        args.append(arg)
        if rest[0] != ',': break
        rest = rest[1:].lstrip()
    if rest[0:2] != ');': raise SyntaxError
    comment = rest[2:-1]
    args.append(comment)
  except (IndexError,SyntaxError):
    args = []
  return args

def parse_instr(line):
  args = []
  if line.lstrip().startswith('__ '):
    indent, rest = line.split('__ ', 1)
    try:
      args = [indent] + parse_call(rest)[1:]
    except SyntaxError:
      print 'parse_instr', repr(line[:-1]), args
      pass
  return args

def has_shiftop(operand):
  for op in SHIFT_OPS:
    if (', %s, ' % op) in operand: return True
  return False

def parse_shift(operand):
  if not operand.startswith('Operand('):
    return None
  reg1,rest = parse_arg(operand[8:])
  assert rest.startswith(', ')
  shiftop,rest = parse_arg(rest[2:])
  assert shiftop in SHIFT_OPS and rest.startswith(', ')
  count3,rest = parse_arg(rest[2:])
  assert rest == ')'
  return reg1, shiftop.lower(), count3

def process_line(fi, fo, line1):
    op1 = get_op(line1)
    iparts1 = parse_instr(line1)
    # patterns that expand one line to two:
    if op1 == 'CompareRoot' and len(iparts1) == 4:
      indent, obj1, index2, comment = iparts1
      line1 = ('%s// CompareRoot(%s, %s);%s\n'
               '%sASSERT(!%s.is(ip));\n'
               '%s__ LoadRoot(ip, %s);\n'
               % (indent, obj1, index2, comment, indent, obj1, indent, index2))
      line2 = '%s__ cmp(%s, ip);\n' % (indent, obj1)
    elif op1 == 'CompareInstanceType' and len(iparts1) == 5:
      indent, mapreg1, typereg2, type3, comment = iparts1
      line1 = ('%s// CompareInstanceType(%s, %s, %s);%s\n'
               '%s__ ldrb(%s, FieldMemOperand(%s, Map::kInstanceTypeOffset));\n'
               % (indent, mapreg1, typereg2, type3, comment, indent, typereg2, mapreg1))
      line2 = '%s__ cmp(%s, Operand(%s));\n' % (indent, typereg2, type3)
    elif op1 == 'CompareObjectType' and len(iparts1) == 6:
      indent, objreg1, mapreg2, typereg3, type4, comment = iparts1
      line1 = ('%s// CompareObjectType(%s, %s, %s, %s);%s\n'
               '%s__ ldr(%s, FieldMemOperand(%s, HeapObject::kMapOffset));\n'
               '%s__ ldrb(%s, FieldMemOperand(%s, Map::kInstanceTypeOffset));\n'
               % (indent, objreg1, mapreg2, typereg3, type4, comment,
                  indent, mapreg2, objreg1, indent, typereg3, mapreg2))
      line2 = '%s__ cmp(%s, Operand(%s));\n' % (indent, typereg3, type4)
    else:
      line2 = fi.readline()
      op2 = get_op(line2)
      iparts2 = parse_instr(line2)
      func2 = get_func(line2)
      fparts2 = parse_call(line2)
      # patterns that combine two lines into one:
      if op1 == 'cmp' and op2 == 'b':
          indent, reg1, operand2, comment1 = iparts1
          indent2, cond, label, comment2 = iparts2
          line1 = '%s__ BranchCmp(%s, %s, %s, %s);%s\n' % (
                  indent, cond, reg1, operand2, label, comment1+comment2)
          line2 = fi.readline()
      elif op1 == 'tst' and op2 == 'b':
          indent, reg1, operand2, comment1 = iparts1
          indent2, cond, label, comment2 = iparts2
          line1 = '%s__ BranchAnd(%s, %s, %s, %s);%s\n' % (
                  indent, cond, reg1, operand2, label, comment1+comment2)
          line2 = fi.readline()
      elif op1 == 'cmp' and func2 == 'DeoptimizeIf':
          indent, reg1, operand2, comment1 = iparts1
          indent2, cond, env, comment2 = fparts2
          if env != 'instr->environment()':
            print 'error: expecting instr->environment() in', repr(line2[:-1])
          line1 = '%sDeoptimizeIfCmp(%s, %s, %s, instr);%s\n' % (
                  indent, cond, reg1, operand2, comment1+comment2)
          line2 = fi.readline()
      elif op1 == 'tst' and func2 == 'DeoptimizeIf':
          indent, reg1, operand2, comment1 = iparts1
          indent2, cond, env, comment2 = fparts2
          assert env == 'instr->environment()'
          line1 = '%sDeoptimizeIfAnd(%s, %s, %s, instr);%s\n' % (
                  indent, cond, reg1, operand2, comment1+comment2)
          line2 = fi.readline()
      elif op1 == 'cmp' and func2 == 'EmitBranch':
          indent, reg1, operand2, comment1 = iparts1
          indent2, true_block, false_block, cond, comment2 = fparts2
          line1 = '%sEmitBranchCmp(%s, %s, %s, %s, %s);%s\n' % (
                  indent, cond, reg1, operand2, true_block, false_block, comment1+comment2)
          line2 = fi.readline()
      elif op1 == 'tst' and func2 == 'EmitBranch':
          indent, reg1, operand2, comment1 = iparts1
          indent2, true_block, false_block, cond, comment2 = fparts2
          line1 = '%sEmitBranchAnd(%s, %s, %s, %s, %s);%s\n' % (
                  indent, cond, reg1, operand2, true_block, false_block, comment1+comment2)
          line2 = fi.readline()
      elif op1 == 'mov' and len(iparts1) == 4 and has_shiftop(iparts1[2]):
          # pattern that replaces one line:
          indent, reg1, operand2, comment = iparts1
          reg2, shiftop, count3 = parse_shift(operand2)
          line1 = ('%s__ %s(%s, %s, Operand(%s));%s\n'
                   % (indent, shiftop, reg1, reg2, count3, comment))
    fo.write(line1)
    return line2

def main():
  fi = open('lithium-codegen-arm.cc.5')
  fo = open('lithium-codegen-arm.cc.6', 'w')
  line1 = fi.readline()
  while line1:
    line1 = process_line(fi, fo, line1)

main()
