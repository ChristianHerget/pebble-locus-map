'use strict';

const assert = require('assert');
const childProcess = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = path.join(__dirname, '..');
const groups = ['maintenance', 'steps', 'metrics-localization', 'persistence-boundaries',
  'persistence-recovery', 'configuration-parsing', 'transfers-ordering', 'configuration-storage'];
const args = process.argv.slice(2);
if (args.length === 1 && args[0] === '--list') {
  process.stdout.write(groups.join('\n') + '\n');
  process.exit(0);
}
if (args.length > 1 || (args.length === 1 && !groups.includes(args[0]))) {
  process.stderr.write(`Unknown C test group: ${args.join(' ')}. Choose: ${groups.join(', ')}\n`);
  process.exit(1);
}
const selected = args.length ? args : groups;
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'locus-watch-core-'));
const sanitizerFlags = [
  '-fsanitize=address,undefined',
  '-fno-sanitize-recover=undefined',
  '-fno-omit-frame-pointer',
];
const commonFlags = [
  '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
  ...sanitizerFlags,
  '-I', path.join(root, 'test/fakes'),
  '-I', path.join(root, 'src/c'),
];

try {
  const sanitizerEnvironment = {
    ...process.env,
    ASAN_OPTIONS: 'detect_leaks=1:halt_on_error=1',
    UBSAN_OPTIONS: 'halt_on_error=1:print_stacktrace=1',
  };

  const productionSources = ['watch_config.c', 'watch_config_storage.c', 'watch_state.c', 'watch_maintenance.c', 'watch_maintenance_timer.c', 'watch_outbound_retry.c', 'watch_step_state.c', 'persistent_blob.c', 'i18n.c', 'ui_metrics.c'];
  const objects = [];
  productionSources.forEach(source => {
    const object = path.join(temporary, `${source}.o`);
    const compile = childProcess.spawnSync('cc', [
      ...commonFlags,
      '-Wframe-larger-than=512', '-Wstack-usage=512',
      '-c', path.join(root, 'src/c', source), '-o', object,
    ], {encoding: 'utf8'});
    assert.strictEqual(compile.status, 0, compile.stderr || compile.stdout);
    objects.push(object);
  });

  for (const group of selected) {
    const executable = path.join(temporary, group);
    const testObject = path.join(temporary, `${group}.o`);
    const compileTest = childProcess.spawnSync('cc', [
      ...commonFlags,
      '-c', path.join(root, `test/core_${group.replaceAll('-', '_')}_test.c`), '-o', testObject,
    ], {encoding: 'utf8'});
    assert.strictEqual(compileTest.status, 0, compileTest.stderr || compileTest.stdout);

    const link = childProcess.spawnSync('cc', [
      ...commonFlags,
      path.join(root, 'test/core_test_support.c'), testObject, ...objects, '-o', executable,
    ], {encoding: 'utf8'});
    assert.strictEqual(link.status, 0, link.stderr || link.stdout);

    const run = childProcess.spawnSync(executable, [], {
      encoding: 'utf8',
      env: sanitizerEnvironment,
    });
    assert.strictEqual(run.status, 0, `[${group}] ${run.stderr || run.stdout}`);
    process.stdout.write(run.stderr);
  }

  const undefinedBehaviorProbe = path.join(temporary, 'undefined_behavior_probe');
  const compileProbe = childProcess.spawnSync('cc', [
    ...commonFlags,
    '-x', 'c', '-', '-o', undefinedBehaviorProbe,
  ], {
    encoding: 'utf8',
    input: '#include <limits.h>\nint main(void) { volatile int value = INT_MAX; return value + 1; }\n',
  });
  assert.strictEqual(compileProbe.status, 0, compileProbe.stderr || compileProbe.stdout);
  const runProbe = childProcess.spawnSync(undefinedBehaviorProbe, [], {
    encoding: 'utf8',
    env: sanitizerEnvironment,
  });
  assert.notStrictEqual(runProbe.status, 0, 'UBSan must terminate a process after undefined behavior');
  assert.match(runProbe.stderr, /runtime error:/, 'the fail-closed probe must exercise UBSan');
} finally {
  fs.rmSync(temporary, {recursive: true, force: true});
}
