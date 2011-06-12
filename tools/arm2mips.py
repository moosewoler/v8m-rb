#!/usr/bin/python2.6

# Convert generic risc version of V8 arm/lithium-codegen-arm.cc into
# a preliminary mips/lithium-codegen-mips.cc using Mips opcode names.
# The input file has already had dependencies on Arm-specific iset
# features (integer condition codes, skips, and shifted operands)
# removed by manual edit changes (to arm/lithium-codegen-unarm.cc)
# and then by mechanical changes via tools/disarm.py.

ARM_TO_MIPS = {
  'ldr' : 'lw'  ,                  'str' : 'sw' ,
  'ldrh': 'lhu' , 'ldrsh': 'lh'  , 'strh': 'sh' ,
  'ldrb': 'lbu' , 'ldrsb': 'lb'  , 'strb': 'sb' ,
  'vldr': 'ldc1', 'vstr':  'sdc1',
  'vmov': 'mov.d',
  }

ARM_TO_MIPS_IMM = {
  # arm : (macro,  reg,    immed  )  
  'and_': ('And' , 'and_', 'andi' ),
  'orr' : ('Or'  , 'or_',  'ori'  ),
  'eor' : ('Xor' , 'xor_', 'xori' ),
  'add' : ('Addu', 'addu', 'addiu'),
  'sub' : ('Subu', 'subu', 'addiu'),
  'mov' : ('Mov' , 'mov',  'li'   ),
  'lsl' : ('Sll' , 'sllv', 'sll'  ),
  'lsr' : ('Srl' , 'srlv', 'srl'  ),
  'asr' : ('Sra' , 'srav', 'sra'  ),
  }

CC_TO_BGEZ = {
  'eq': 'beqz', 'ne': 'bnez',
  'mi': 'bltz', 'pl': 'bgez',
  'ge': 'bgez', 'lt': 'bltz',
  'gt': 'bgtz', 'le': 'blez',
  }

UNSIGNED_COMPARES = {'lo': 'lt', 'ls': 'le', 'hs': 'ge', 'hi': 'gt'} 

COMMON_REGNAMES = set(['at', 'fp', 'sp', 'a0', 'a1', 'a2', 'a3',
                       'left', 'right',])

REG_RENAMES = {
  'r0': 'a0', 'r1': 'a1', 'r2': 'a2', 'r3': 'a3',
  'r4': 't0', 'r5': 't1', 'r6': 't2', 'r7': 't3',  # TODO(duanes): remove??
  'ip': 'at', 'lr': 'ra',
  # other dedicated regs with unchanged names: fp, sp
  # also globally replace some typenames:
  'DwVfpRegister': 'FPURegister',
  }

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

def is_name(arg):
  return arg.replace('_', '').isalnum()

def likely_reg(arg):
  return arg in COMMON_REGNAMES or arg.startswith('scratch')

def const_operand(arg):
  if arg.startswith('Operand('):
    imm_val = arg[8:-1]
    if not likely_reg(imm_val):
      return imm_val
  return ''

def reg_operand(arg):
  if arg.startswith('Operand('):
    reg = arg[8:-1]
    if likely_reg(reg):
      return reg
    return ''
  return arg

def rename_regs(line):
  for armreg,mipsreg in REG_RENAMES.iteritems():
    aw = len(armreg)
    mw = len(mipsreg)
    i = 0
    while True:
      i = line.find(armreg, i)
      if i < 0: break
      if (i > 0 and is_name(line[i-1])) or is_name(line[i+aw]):
        i += aw
      else:
        line = line[:i] + mipsreg + line[i+aw:]
        i += mw
  return line

def process_line(fi, fo, line1):
    line1 = rename_regs(line1)
    op1 = get_op(line1)
    iparts1 = parse_instr(line1)
    func1 = get_func(line1)
    # patterns that replace one line:
    if op1 in ARM_TO_MIPS:
      mips_op = ARM_TO_MIPS[op1]
      line1 = line1.replace(op1, mips_op, 1)
    elif op1 in ARM_TO_MIPS_IMM:
      if len(iparts1) >= 4 and const_operand(iparts1[-2]):
        # use immediate-operand form of Mips instr, replace Operand(i) by i
        mips_op = ARM_TO_MIPS_IMM[op1][2]
        indent = iparts1[0]
        args = ', '.join(iparts1[1:-2])
        imm_val = const_operand(iparts1[-2])
        comment = iparts1[-1]
        if op1 == 'sub':  # becomes addiu -n
          if not is_name(imm_val):
            imm_val = '(%s)' % imm_val
          imm_val = '-%s' % imm_val
        line1 = ('%s__ %s(%s, %s);%s\n'
                 % (indent, mips_op, args, imm_val, comment))

      elif len(iparts1) >= 4 and reg_operand(iparts1[-2]):
        # use register-operand form of Mips instr, replace Operand(reg) by reg
        mips_op = ARM_TO_MIPS_IMM[op1][1]
        indent = iparts1[0]
        args = ', '.join(iparts1[1:-2])
        reg = reg_operand(iparts1[-2])
        comment = iparts1[-1]
        line1 = ('%s__ %s(%s, %s);%s\n'
                 % (indent, mips_op, args, reg, comment))
      else:
        # Assume final arg is a plain register, not disguised as Operand().
        # Exceptions to this will be manually changed into macro call.
        mips_op = ARM_TO_MIPS_IMM[op1][1]
        line1 = line1.replace(op1, mips_op, 1)
    elif op1 == 'rsb' and iparts1[3] == 'Operand(0)':
      indent, reg1, reg2, operand3, comment = iparts1
      line1 = ('%s__ neg(%s, %s);%s\n'
               % (indent, reg1, reg2, comment))
    elif op1 == 'BranchCmp':
      indent, cond, reg1, operand2, label, comment = iparts1
      imm_val = const_operand(operand2)
      reg2 = reg_operand(operand2)
      if operand2 == 'Operand(0)' and cond in CC_TO_BGEZ:
        mips_op = CC_TO_BGEZ[cond]
        line1 = ('%s__ %s(%s, %s);%s\n'
                 % (indent, mips_op, reg1, label, comment))
      elif cond in ['eq', 'ne']:
        if reg2:
          line1 = ('%s__ b%s(%s, %s, %s);%s\n'
                   % (indent, cond, reg1, reg2, label, comment))
        elif imm_val:
          line1 = ('%s__ li(at, %s);%s\n'
                   '%s__ b%s(%s, at, %s);\n'
                   % (indent, imm_val, comment, indent, cond, reg1, label))
      elif cond in ['lt', 'ge', 'le', 'gt', 'lo', 'hs', 'ls', 'hi']:
        unsigned = ''
        if cond in UNSIGNED_COMPARES:
          cond = UNSIGNED_COMPARES[cond]
          unsigned = 'u'
        if reg2:
          if cond in ['le', 'gt']:
            reg1, reg2 = reg2, reg1
            cond = 'lt' if cond == 'gt' else 'ge'
          b_op = 'bnez' if cond == 'lt' else 'beqz'
          line1 = ('%s__ slt%s(at, %s, %s);%s\n'
                   '%s__ %s(at, %s);\n'
                   % (indent, unsigned, reg1, reg2, comment,
                      indent, b_op, label))
        elif imm_val:
          if cond in ['le', 'gt']:
            imm_val += '+1'
            cond = 'lt' if cond == 'le' else 'ge'
          b_op = 'bnez' if cond == 'lt' else 'beqz'
          line1 = ('%s__ slti%s(at, %s, %s);%s\n'
                   '%s__ %s(at, %s);\n'
                   % (indent, unsigned, reg1, imm_val, comment,
                      indent, b_op, label))

    elif op1 == 'BranchAnd':
      indent, cond, reg1, operand2, label, comment = iparts1
      imm_val = const_operand(operand2)
      if cond in ['eq', 'ne'] and imm_val:
        line1 = ('%s__ andi(at, %s, %s);%s\n'
                 '%s__ b%sz(at, %s);\n'
                 % (indent, reg1, imm_val, comment, indent, cond, label))
    elif func1 == 'EmitBranchAnd':
      fparts1 = parse_call(line1)
      indent, cond, reg1, operand2, tblock, fblock, comment = fparts1
      imm_val = const_operand(operand2)
      line1 = ('%s__ andi(at, %s, %s);%s\n'
               '%sEmitBranchCmp(%s, at, Object(0), %s, %s);\n'
               % (indent, reg1, imm_val, comment,
                  indent, cond, tblock, fblock)) 
    elif func1 == 'DeoptimizeIfAnd':
      fparts1 = parse_call(line1)
      indent, cond, reg1, operand2, instr, comment = fparts1
      imm_val = const_operand(operand2)
      line1 = ('%s__ andi(at, %s, %s);%s\n'
               '%sDeoptimizeIfCmp(%s, at, Object(0), %s);\n'
               % (indent, reg1, imm_val, comment, indent, cond, instr))
    elif line1.startswith('#include '):
      line1 = line1.replace('arm', 'mips')
    fo.write(line1)
    line2 = fi.readline()
    return line2

def main():
  fi = open('src/arm/lithium-codegen-arm.cc')
  fo = open('src/mips/lithium-codegen-mips-generated.cc', 'w')
  line1 = fi.readline()
  while line1:
    line1 = process_line(fi, fo, line1)

main()
