"""Compile/run production CPU translation queue and adapter on Linux/WSL.

No phone or Vulkan driver is used. The adapter fixture controls backend fences.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sanitizers', default='address,undefined')
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[3]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    report = out / 'result.json'
    report.write_text(json.dumps({'passed': False, 'device_contacted': False}) + '\n')
    common = ['gcc', '-std=c11', '-D_GNU_SOURCE', '-g', '-Wall', '-Wextra', '-Werror', '-pthread']
    production = 'dm/android/nvn_translation_queue.c'
    tests = [
        ('queue', 'dm/android/tests/t_translation_queue.c', ['-Wl,--wrap=pthread_create'], [[]]),
        ('adapter', 'dm/android/tests/t_translation_present.c',
         ['-D__ANDROID__', '-Idm/android', '-Idm/gfx/nvn/include', '-Wno-unused-function'], [['0'], ['1']]),
    ]
    results = []

    def run(command, log):
        process = subprocess.run(command, cwd=repo, capture_output=True, text=True, timeout=90)
        (out / log).write_text(process.stdout + process.stderr, encoding='utf-8')
        if process.returncode:
            print(process.stdout + process.stderr, flush=True)
        process.check_returncode()
        return process.stdout

    modes = [('normal', ['-O2'])]
    if args.sanitizers:
        modes.append(('sanitized', ['-O1', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                                    '-fsanitize=' + args.sanitizers, '-fno-sanitize-recover=all']))
    for mode, options in modes:
        for name, source, flags, variants in tests:
            binary = out / (name + '-' + mode)
            run(common + options + [source, production] + flags + ['-o', str(binary)], binary.name + '-build.log')
            for arguments in variants:
                log = binary.name + '-' + ''.join(arguments) + '.log'
                stdout = run([str(binary)] + arguments, log)
                print(stdout.strip(), flush=True)
                results.append({'test': name, 'mode': mode, 'arguments': arguments, 'passed': True,
                                'command': [str(binary)] + arguments, 'log': log, 'stdout': stdout.strip()})
    sources = ['dm/android/nvn_translation_queue.h', 'dm/android/nvn_translation_sync.h', production,
               'dm/android/nvn_platform_present.inc', 'dm/android/nvn_platform_clock.c',
               'dm/android/nvn_platform_clock.h', 'dm/android/app_jni.c', 'dm/android/CMakeLists.txt',
               'dm/gfx/nvn/hle_nvn.c', 'dm/gfx/nvn/nvn_vk.c', 'dm/gfx/nvn/nvn_obj.h',
               'dm/android/tests/t_translation_queue.c', 'dm/android/tests/t_translation_present.c',
               'dm/android/tests/run_translation_checks.py']
    report.write_text(json.dumps({'passed': True, 'device_contacted': False, 'gpu_executed': False,
                                 'sanitizers': args.sanitizers, 'tests': results,
                                 'source_sha256': {s: hashlib.sha256((repo / s).read_bytes()).hexdigest()
                                                   for s in sources}}, indent=2) + '\n')


if __name__ == '__main__':
    main()
