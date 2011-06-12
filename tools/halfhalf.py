f0_split = -1  # eof  pass
f1_split = -1  # eof

f0_split = 0  # bof  fail, anywhere in f1
f1_split = 0  # bof

f0_split = 3504  # all orig 2+45 fails are in 3521 .. end
f1_split = 3520
# current 2+1 fail is not in 3521 .. end

#f0_split = 3643  # all orig 2+45 fails are in 3670 .. end
#f1_split = 3669

#f0_split = 3720
#f1_split = 3746  # all orig 2+45 fails are in 3746 .. end

f0_split = 2681  # current 2+1 fail is not in 2684 .. end
f1_split = 2684

f0_split = 2649
f1_split = 2651


def main():
  l0 = open('lithium-codegen-arm.cc.31').readlines()
  l1 = open('lithium-codegen-arm.cc.3' ).readlines()
  fo = open('lithium-codegen-arm.cc', 'w')
  lo = l0[:f0_split] + l1[f1_split:]
  fo.writelines(lo)

main()
