#!/usr/bin/env python
# Run tests one time, in crankshaft-only mode

import test

test.VARIANT_FLAGS = [['--always-opt']]
test.Main()

# Note: Mips version of tool/test.py attempts to modify its VARIANT_FLAGS
# 'global variable' in response to certain meta-options.  But those
# changes to VARIANT_FLAGS, XML, and VERBOSE default do not stick when
# tool/test.py is invoked as the main python module.  This is due to two
# problems:  tools/test.py has circular import dependency with its
# test/<suite>/testcfg.py modules.  And circular imports fail in Python
# when one of the files is executed as the main module, because the outer
# instance of that module runs as module named '__main__' instead of
# using its filename as module name.  So module loader loads two copies
# of that file, as separate modules with separate global vars.  
