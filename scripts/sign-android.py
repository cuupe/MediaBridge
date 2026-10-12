"""Align and sign a release APK; keep credentials in an environment variable."""
import argparse
import os
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--input', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--build-tools', type=Path, required=True)
p.add_argument('--keystore', type=Path, required=True)
p.add_argument('--alias', required=True)
p.add_argument('--password-env', default='MB_SIGNING_PASSWORD')
a = p.parse_args()
env = {k.upper():v for k,v in os.environ.items()} if os.name == 'nt' else dict(os.environ)
if not env.get(a.password_env):
    raise SystemExit('Set the signing password environment variable before running')
a.output.parent.mkdir(parents=True, exist_ok=True)
aligned = a.output.with_suffix('.aligned.apk')
aligner = a.build_tools / ('zipalign.exe' if os.name == 'nt' else 'zipalign')
signer = a.build_tools / ('apksigner.bat' if os.name == 'nt' else 'apksigner')
def run(command):
    subprocess.run([str(x) for x in command], env=env, check=True)
run([aligner, '-P', '16', '-f', '4', a.input, aligned])
run([signer, 'sign', '--ks', a.keystore, '--ks-key-alias', a.alias,
     '--ks-pass', 'env:'+a.password_env, '--key-pass', 'env:'+a.password_env,
     '--out', a.output, aligned])
run([signer, 'verify', '--verbose', '--print-certs', a.output])
run([aligner, '-c', '-P', '16', '4', a.output])
aligned.unlink()
print('Signed APK verified:', a.output)
