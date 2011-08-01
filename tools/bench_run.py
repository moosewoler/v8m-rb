#!/usr/bin/env python

# Summarize the multiple runs of V8 benchmark runs
# Usage:    tools/bench_sum.py <datafile

import os, sys, subprocess, tempfile

V8_SHELL = 'shell14_7'
BENCH_RUNNER = 'run'
REPEATS = 20
RUN_OPTIONS = []

"""
The input file contains a sequence of benchmark suite scorings.
Each scoring is a sequence of lines like the following:

# ../shell run.js
Richards: 786
DeltaBlue: 914
Crypto: 875
RayTrace: 448
EarleyBoyer: 1695
RegExp: 243
Splay: 276
----
Score (version 6): 612
"""


def collect_scores(result_text, test_scores):
  for result_line in result_text.splitlines():
    if not result_line.startswith('----'):
      testname, score = result_line.split(': ', 1)
      test_scores.setdefault(testname, []).append(float(score))


def run_benchmarks():
  toolsdir = os.path.dirname(__file__)
  v8dir = os.path.dirname(toolsdir)
  cwd = os.path.join(v8dir, 'benchmarks')
  args = ['../'+V8_SHELL]
  args.extend(RUN_OPTIONS)
  args.append(BENCH_RUNNER+'.js')
  print '#' + ' '.join(args)
  result_file = tempfile.TemporaryFile()
  p = subprocess.Popen(args, cwd=cwd, stdout=result_file)
  p.wait()
  result_file.seek(0)
  result_text = result_file.read()
  print result_text
  return result_text


def standard_dev(samples, mean, nfree):
  if nfree < 1: return None
  sum = 0.
  for val in samples:
    sum += (val - mean)**2
  return (sum / nfree) ** 0.5


def show_stats(name, samples):
  n = len(samples)
  assert n
  mean = (sum(samples)+0.) / (n or 1)
  min_ = min(samples)
  max_ = max(samples)
  stdev = standard_dev(samples, mean, n-1)
  if stdev is None:
    dev_pct = ''
  else:
    dev_pct = '%6.1f%%' % (100*stdev/mean)
  print ('%12s %5.0f %7.1f %5.0f %7s %4d'
         % (name, max_, mean, min_, dev_pct, n))


test_order = [
  'Richards', 'DeltaBlue', 'Crypto', 'RayTrace',
  'EarleyBoyer', 'RegExp', 'Splay',
  ]


def main():
  test_scores = {}
  for i in xrange(REPEATS):
    print 'Run %d:' % (i+1)
    collect_scores(run_benchmarks(), test_scores)
  print
  print '        Test   Max    Avg    Min    Dev   Runs'
  for testname in test_order:
    if testname in test_scores:
      show_stats(testname, test_scores[testname])
  print
  show_stats('Full Suite', test_scores['Score (version 6)'])


main()

