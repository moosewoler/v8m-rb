
lo_split = 2222,2410
hi_split = -1,-1  # 50% l0, then 50% l1: no bug

lo_split = 1151,1264
hi_split = -1,-1  # 25% l0, then 75% l1: bug present

lo_split = 1151,1264
hi_split = 2222,2410 # 25% l0, then 25% l1, then 50% l0: bug present

lo_split = 1151,1264
hi_split = 1649,1845 # 25% l0, then 12% l1, then 63% l0: bug present

lo_split = 1151,1264
hi_split = 1649,1845 # 25% l0, then 12% l1, then 63% l0: bug present

lo_split = 1151,1264
hi_split = 1390,1532 # 25% l0, then 6% l1, then 69% l0: bug present

lo_split = 1151,1264
hi_split = 1290,1409 #  bug present

lo_split = 1176,1292
hi_split = 1290,1409 #  bug present

lo_split = 1267,1383
hi_split = 1290,1411 #  bug present in single change: deopt on zero-cnt shift of neg value





def main():
  l0 = open('src/arm/lithium-codegen-arm.cc.1').readlines()
  l1 = open('src/arm/lithium-codegen-unarm.cc').readlines()
  fo = open('src/arm/lithium-codegen-arm.cc', 'w')
  lo = l0[:lo_split[0]] + l1[lo_split[1]:hi_split[1]] + l0[hi_split[0]:]
  fo.writelines(lo)

main()
