/* tools/tests/netsurf/jit/octane-run.js -- runs the Octane suite loaded before it (bench.sh) */
BenchmarkSuite.RunSuites({
	NotifyResult: function (name, result) { print(name + ': ' + result); },
	NotifyError: function (name, error) { print(name + ': ERROR ' + error); },
	NotifyScore: function (score) { print('Score: ' + score); },
});
