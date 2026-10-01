/* tools/tests/netsurf/jit/octane-fixed.js -- the Octane suite loaded before it, each benchmark
 * run a fixed number of times (ITER, default 3) instead of for a time: the same work in every
 * build, for counting instructions (callgrind: COUNT=1 bench.sh) */
var ITER = typeof ITER === 'number' ? ITER : 3;
BenchmarkSuite.suites.forEach(function (suite) {
	suite.benchmarks.forEach(function (b) {
		b.Setup();
		for (var i = 0; i < ITER; i++) b.run();
		b.TearDown();
		print(b.name + ': ran ' + ITER);
	});
});
