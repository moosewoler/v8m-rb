
lo_split = 0,0   # 100% f1: intermit 20% failure
hi_split = -1,-1 

lo_split = 2574,2504  # 50% f0 then 50% f1: all pass
hi_split = -1,-1

lo_split = 0,0        # 50% f1 then 50% f0: intermit 20% failure
hi_split = 2574,2504

lo_split = 1789,1749  # 25% f0 then 25% f1 then 50% f0: all pass
hi_split = 2574,2504

lo_split = 0,0        # 25% f1 then 75% f0: intermit 25% failure
hi_split = 1789,1749

lo_split = 1310,1285  # 12% f0 then 13% f1 then 75% f0: intermit 20% failure
hi_split = 1789,1749

lo_split = 1310,1285  # 12% f0 then 6% f1 then 82% f0: intermit 20% failure
hi_split = 1428,1401

lo_split = 1310,1285  # intermit 20% failure
hi_split = 1345,1318

lo_split = 1310,1285  # intermit 20% failure
hi_split = 1332,1306

lo_split = 1310,1285  # intermit 20% failure
hi_split = 1315,1290

lo_split = 1310,1285  # pass
hi_split = 1313,1288

lo_split = 1313,1288  # intermit 20% failure, at a single change.
hi_split = 1315,1290


def main():
  l0 = open('src/arm/lithium-codegen-unarm.cc').readlines()
  l1 = open('src/arm/lithium-codegen-arm-generated.cc' ).readlines()
  fo = open('src/arm/lithium-codegen-arm.cc', 'w')
  lo = l0[:lo_split[0]] + l1[lo_split[1]:hi_split[1]] + l0[hi_split[0]:]
  fo.writelines(lo)

main()
