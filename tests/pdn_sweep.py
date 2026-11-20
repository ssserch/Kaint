#!/usr/bin/env python3
# Kaint: examina todos los complementos de una carpeta con un proceso auxiliar de Paint.NET y dice
# cuántos efectos carga cada uno (para comparar el sistema .NET con mono o Wine).
#
#   python3 tests/pdn_sweep.py "dotnet build/pdnhost5/PdnBridge.dll" plugins/paintnet > dotnet.json
#   python3 tests/pdn_sweep.py "mono build/pdnbridge/kaint-pdn-host.exe" plugins/paintnet > mono.json
#   python3 tests/pdn_sweep.py --compare dotnet.json mono.json
#
# Con OpenD2D: KAINT_OPEND2D=build/opend2d. El resultado: por complemento, sus efectos (nombre y
# tipo de diálogo) y sus errores.
import glob, json, os, select, subprocess, sys
from collections import Counter

def sweep(command, folder):
    def start():
        return subprocess.Popen(command.split(), stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.DEVNULL, text=True, bufsize=1)
    host, out = start(), {}
    for dll in sorted(glob.glob(os.path.join(folder, "*.dll"))):
        name = os.path.basename(dll)
        try:
            host.stdin.write(json.dumps({"cmd": "inspect", "path": os.path.abspath(dll)}) + "\n")
            host.stdin.flush()
            if not select.select([host.stdout], [], [], 120)[0]:
                raise TimeoutError("no responde")
            line = host.stdout.readline()
            if not line:
                raise RuntimeError("el proceso terminó")
            r = json.loads(line)
            errors = r.get("errors", []) if r.get("ok", True) else [r.get("error")]
            out[name] = {"effects": [[e["name"], e.get("kind")] for e in r.get("effects", [])],
                         "errors": errors, "library": r.get("library", False)}
        except Exception as ex:
            out[name] = {"effects": [], "errors": ["CAÍDA: %s" % ex], "library": False}
            host.kill()
            host = start()
    host.stdin.close()
    return out

def compare(a_path, b_path):
    a, b = json.load(open(a_path)), json.load(open(b_path))
    total = lambda x: sum(len(v["effects"]) for v in x.values())
    print("efectos: %s=%d %s=%d" % (a_path, total(a), b_path, total(b)))
    print("tipos %s: %s" % (a_path, dict(Counter(k for v in a.values() for _, k in v["effects"]))))
    print("tipos %s: %s" % (b_path, dict(Counter(k for v in b.values() for _, k in v["effects"]))))
    for name in sorted(set(a) | set(b)):
        na, nb = len(a.get(name, {}).get("effects", [])), len(b.get(name, {}).get("effects", []))
        if na != nb:
            print("%s: %d / %d  %s" % (name, na, nb, (a.get(name) or b.get(name))["errors"][:1]))

if __name__ == "__main__":
    if len(sys.argv) == 4 and sys.argv[1] == "--compare":
        compare(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 3:
        json.dump(sweep(sys.argv[1], sys.argv[2]), sys.stdout, indent=1, ensure_ascii=False)
    else:
        sys.exit(__doc__ or "uso: pdn_sweep.py \"<proceso>\" <carpeta> | --compare a.json b.json")
