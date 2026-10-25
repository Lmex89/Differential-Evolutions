#!/usr/bin/env bash
# =========================================================================
# scripts/ab_experiment.sh - Experimento A/B antes/despues (issue #4).
#
# Compila dos versiones del prototipo y las corre con la MISMA semilla para
# responder si los resultados historicos (archivos `out`/`out.txt`) son
# confiables:
#
#   - "antes"  : la revision bc70e95 (codigo con los bugs del issue #2),
#   - "despues": el HEAD del repo (con los fixes del issue #2).
#
# Ninguna de las dos revisiones aceptaba una semilla fija (usaban
# srand(time(NULL))), asi que a la version "antes" se le aplica el gancho de
# scripts/ab_seed_hook.patch: el mismo `resolver_semilla()` de ODE_SEED que
# usa "despues". El parche solo agrega el gancho; no toca la matematica.
#
# Salidas en $OUTDIR (por defecto un mktemp en /tmp/opencode):
#   - antes/main.exe, despues/build/ode-solver   binarios comparados
#   - antes-seedN.log, despues-seedN.log         salida completa de cada corrida
#   - metricas.tsv                               tabla de la comparacion
#
# El arnes comun (cabecera de metricas.tsv, estado de salida, parser del log)
# vive en scripts/ab_common.sh, compartido con scripts/restart_ab.sh.
#
# Uso:
#   scripts/ab_experiment.sh [--seed "1 42 2026"] [--timeout 300]
#                            [--outdir DIR] [--before REV]
#
# Para un desarrollador Python: este script cumple el rol de un
# `ab_experiment.py`; esta en bash porque el repo no tiene entorno Python y
# todas las piezas (git, parche, compilador, timeout) son de shell.
# =========================================================================
set -euo pipefail

REPO="$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

# Arnes compartido con scripts/restart_ab.sh: estado_de_status, crear_metricas,
# extraer_metricas, wall_de y mostrar_metricas.
source "$REPO/scripts/ab_common.sh"

# Valores por defecto, sobreescribibles por variables de entorno o por flags.
BEFORE_REV=${BEFORE_REV:-bc70e95}   # revision sin los fixes del issue #2
SEMILLAS=${SEMILLAS:-"1 42 2026"}   # una corrida de A y de B por semilla
TIMEOUT_S=${TIMEOUT_S:-300}         # segundos maximos por corrida
OUTDIR=${OUTDIR:-}

uso() {
    cat <<'FIN'
uso: scripts/ab_experiment.sh [opciones]

  --seed "N1 N2 .."   semillas a correr en ambas versiones (def: "1 42 2026")
  --timeout S         segundos maximos por corrida (def: 300)
  --outdir DIR        directorio de resultados (def: mktemp en /tmp/opencode)
  --before REV        revision de la version A (def: bc70e95)
  -h, --help          muestra esta ayuda
FIN
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --seed)    SEMILLAS="$2";   shift 2 ;;
        --timeout) TIMEOUT_S="$2";  shift 2 ;;
        --outdir)  OUTDIR="$2";     shift 2 ;;
        --before)  BEFORE_REV="$2"; shift 2 ;;
        -h|--help) uso; exit 0 ;;
        *) echo "opcion desconocida: $1" >&2; uso >&2; exit 2 ;;
    esac
done

if [[ -z "$OUTDIR" ]]; then
    OUTDIR="$(mktemp -d /tmp/opencode/ab-experiment.XXXXXX)"
fi
mkdir -p "$OUTDIR"

CXX=${CXX:-g++}
FLAGS="-std=c++17 -Wall -O3"   # los mismos del Makefile

echo "== Experimento A/B (issue #4) =="
echo "  version B (despues): $(git -C "$REPO" rev-parse --short HEAD)  $(git -C "$REPO" log -1 --format=%s)"
echo "  version A (antes)  : $BEFORE_REV"
echo "  semillas           : $SEMILLAS"
echo "  timeout por corrida: ${TIMEOUT_S}s"
echo "  compilador         : $($CXX --version | head -1)"
echo "  resultados en      : $OUTDIR"

# Si hay cambios sin commitear, "despues" no es exactamente HEAD: avisar.
if [[ -n "$(git -C "$REPO" status --porcelain)" ]]; then
    echo "  AVISO: hay cambios sin commitear; version B usa el HEAD commiteado,"
    echo "         pero el gancho de A sale del arbol de trabajo."
fi

# -------------------------------------------------------------------------
# 1) Copias limpias de las dos revisiones (sin tocar el repo).
#    git archive empaqueta solo lo commiteado: A nunca ve los fixes.
# -------------------------------------------------------------------------
mkdir -p "$OUTDIR/antes" "$OUTDIR/despues"
git -C "$REPO" archive "$BEFORE_REV" | tar -x -C "$OUTDIR/antes"
git -C "$REPO" archive HEAD          | tar -x -C "$OUTDIR/despues"

# -------------------------------------------------------------------------
# 2) Gancho de semilla en A: los mismos semilla.{h,cpp} que usa B y el
#    parche que reemplaza srand(time(NULL)) por resolver_semilla(...).
# -------------------------------------------------------------------------
cp "$REPO/src/semilla.h" "$REPO/src/semilla.cpp" "$OUTDIR/antes/"
patch --batch --silent -d "$OUTDIR/antes" -p1 < "$REPO/scripts/ab_seed_hook.patch"
echo "  sha256 semilla.cpp  : $(sha256sum "$REPO/src/semilla.cpp" | cut -d' ' -f1)"

# -------------------------------------------------------------------------
# 3) Compilar ambas versiones con los mismos flags.
#    "antes" (bc70e95) tiene layout plano; "despues" (HEAD) tiene layout nuevo
#    con src/ (los mismos fuentes que el target ode-solver de CMakeLists.txt;
#    src/salida.cpp es la redireccion de #112). Ambos producen binarios en sus
#    respectivos directorios.
# -------------------------------------------------------------------------
( cd "$OUTDIR/antes"   && $CXX $FLAGS dualfunctions.cpp semilla.cpp parse_tree.cpp -o main.exe )
( cd "$OUTDIR/despues" && mkdir -p build && $CXX $FLAGS src/dualfunctions.cpp src/funbobj.cpp src/semilla.cpp src/salida.cpp src/parse_tree.cpp -o build/ode-solver )
echo "  md5 antes/despues  : $(md5sum "$OUTDIR/antes/main.exe" | cut -d' ' -f1) / $(md5sum "$OUTDIR/despues/build/ode-solver" | cut -d' ' -f1)"

# -------------------------------------------------------------------------
# 4) Correr A y B con cada semilla y extraer las metricas del log. El parser
#    de los dos formatos de log (historico y nuevo) vive en
#    scripts/metricas.awk (issue #130), compartido con scripts/restart_ab.sh.
# -------------------------------------------------------------------------
METRICAS="$OUTDIR/metricas.tsv"
crear_metricas "$METRICAS"

for SEMILLA in $SEMILLAS; do
    for VERSION in antes despues; do
        LOG="$OUTDIR/$VERSION-seed$SEMILLA.log"
        T0=$(date +%s.%N)
        STATUS=0
        # "antes" tiene layout plano (main.exe); "despues" tiene layout nuevo (build/ode-solver).
        # Desde #112 "despues" escribe en ode-solver.log por defecto y deja
        # stdout vacio, pero el experimento captura la corrida con el redirect
        # de abajo: "despues" corre con ODE_LOG=terminal (issue #115). "antes"
        # (bc70e95) no conoce ODE_LOG, asi que su invocacion queda intacta.
        # En Python seria `extra = [] if version == "antes" else ["ODE_LOG=terminal"]`
        # pasado al env de subprocess.run; aqui ENV_SALIDA vacia expande a
        # cero palabras (por eso va sin comillas) y `env` recibe la asignacion
        # como argumento: un word que sale de una expansion no se re-parsea
        # como asignacion de shell.
        ENV_SALIDA=""
        if [[ "$VERSION" == "antes" ]]; then
            BINARIO="./main.exe"
        else
            BINARIO="./build/ode-solver"
            ENV_SALIDA="ODE_LOG=terminal"
        fi
        ( cd "$OUTDIR/$VERSION" && env ODE_SEED="$SEMILLA" $ENV_SALIDA timeout "$TIMEOUT_S" $BINARIO > "$LOG" 2>&1 ) || STATUS=$?
        # estado_de_status traduce el codigo: 124 = lo corto `timeout`,
        # otro distinto de 0 = error:N.
        ESTADO="$(estado_de_status "$STATUS")"
        T1=$(date +%s.%N)
        WALL="$(wall_de "$T0" "$T1")"
        printf '%s\t%s\t%s\n' "$VERSION" "$(extraer_metricas "$LOG" "$ESTADO")" "$WALL" >> "$METRICAS"
        echo "  corrida: $VERSION semilla=$SEMILLA estado=$ESTADO reloj=${WALL}s"
    done
done

# -------------------------------------------------------------------------
# 5) Mostrar la tabla y dejar el resto en OUTDIR.
# -------------------------------------------------------------------------
echo
mostrar_metricas "$METRICAS"
echo
echo "logs completos y metricas en: $OUTDIR"
