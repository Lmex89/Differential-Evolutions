#!/usr/bin/env python3
"""Verifica el cableado del disparador por diversidad (issue #129).

`test_diversity_trigger.cpp` cubre la logica pura (CV sobre finitos, guardas de
proporcion y record, umbral estricto). Este checker cubre el otro extremo:
ejecuta `build/ode-solver` en modo adaptive y valida la linea `restart |` que
emite el bucle real:

1. Con `ODE_DIV_THRESHOLD` enorme y `ODE_STALE_LIMIT` corto, los reinicios son
   `trigger=diversity` y el contador de estancamiento se resetea tras cada uno
   (nunca llega al limite, asi que no aparece `trigger=stale`).
2. La escalera compartida (issue #128) avanza 0.4 -> 0.6 -> 0.8 -> 0.2 y se
   resetea con cada `new_best`, igual que en el canal de estancamiento.
3. Con `ODE_DIV_THRESHOLD=0` el canal de diversidad no dispara y el mismo
   bucle sigue reiniciando por `trigger=stale`.
4. En modo legacy no existe `trigger=diversity`: la linea conserva
   `fraction=0.80`, `trigger=stale` y no imprime `mutation_share=`.
5. Con `ODE_WARM_START=1` en el caso 1 (issue #148), la cuota warm a
   poblacion 4 es ceil(0.05*4)=1: la cabecera informa `warm_seeds=1` y el
   disparador por diversidad sigue evaluable en la frontera inclusiva del
   25% de finitos, porque la unica semilla inyectada basta para pasar la
   guarda aunque los tres aleatorios queden rotos.

En Python seria un test de integracion de pytest que ejecuta el entry point
como subproceso y valida su stdout, el mismo patron que
check_restart_escalation.py.
"""

import os
import re
import subprocess
import sys
from pathlib import Path


BINARIO = Path(__file__).resolve().parents[1] / "build" / "ode-solver"

# Poblacion minima (4). Las corridas del criterio 1-4 usan arranque aleatorio
# (warm start apagado): en el caso 0 la primera base del catalogo (#143) es la
# solucion exacta y la corrida convergeria en la generacion 1 sin ejercitar el
# ciclo de reinicios. Con la semilla fija 42 la poblacion aleatoria mantiene
# individuos finitos y el reinicio conserva el record, asi que la proporcion
# finita no cae por debajo del 25% y el disparador se evalua cada generacion.
# El criterio 5 (issue #148) si activa el warm start, pero en el caso 1, cuya
# primera base es una serie truncada: finita, no convergente, cuota 1.
POBLACION = 4
GENERACIONES = 20
SEMILLA = 42
STALE_LIMIT_DIVERSIDAD = 5

RE_REINICIO_ADAPTATIVO = re.compile(
    r"restart \| gen=(\d+) stagnant_gens=(\d+) replaced=(\d+) "
    r"fraction=([0-9.]+) mutation_share=([0-9.]+) "
    r"trigger=(\w+) kept=(\d+)"
)
RE_REINICIO_LEGACY = re.compile(
    r"restart \| gen=(\d+) stagnant_gens=(\d+) replaced=(\d+) "
    r"fraction=([0-9.]+) trigger=(\w+) kept=(\d+)\s*$"
)
RE_NUEVO_RECORD = re.compile(r"new_best \|")

MIN_FRACCION = 0.4
MAX_FRACCION = 0.8
CAP = 3
CUOTA_HIBRIDA = 0.5
FRACCION_REFINAMIENTO = 0.2
CUOTA_REFINAMIENTO = 1.0


def entorno_base():
    """Entorno comun de las corridas, sin depender del shell del usuario."""
    return {
        "ODE_LOG": "terminal",
        "ODE_SEED": str(SEMILLA),
        "ODE_CASE": "0",
        "ODE_GEN": str(GENERACIONES),
        "ODE_POP": str(POBLACION),
        "ODE_DIM": "20",
        "ODE_WARM_START": "0",
        "ODE_RESTART_MODE": "adaptive",
        "ODE_RESTART_MIN_FRACTION": str(MIN_FRACCION),
        "ODE_RESTART_FRACTION": str(MAX_FRACCION),
        "ODE_RESTART_CAP": str(CAP),
        "ODE_RESTART_MUTATION_SHARE": str(CUOTA_HIBRIDA),
    }


def correr_solver(overrides):
    """Ejecuta el solver con el entorno base mas los overrides del caso."""
    entorno = dict(os.environ)
    entorno.update(entorno_base())
    entorno.update(overrides)
    # ODE_LOG=terminal gana, pero eliminar ODE_LOG_FILE evita cualquier duda.
    entorno.pop("ODE_LOG_FILE", None)
    return subprocess.run(
        [BINARIO],
        env=entorno,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )


def parametros_esperados(nivel):
    """(fraccion, cuota) del reinicio `nivel` dentro de un episodio."""
    if nivel >= CAP:
        return FRACCION_REFINAMIENTO, CUOTA_REFINAMIENTO
    return min(MAX_FRACCION, MIN_FRACCION + 0.2 * nivel), CUOTA_HIBRIDA


def verificar_escalera(lineas, errores, etiqueta):
    """Comprueba la escalera compartida y el reset en cada `new_best`.

    Reconstruye el nivel del episodio leyendo el log en orden: un `new_best`
    vuelve al primer escalon, cualquier reinicio consume el siguiente. En
    Python seria la misma maquina de estados que `EscaladorReinicio`.
    """
    nivel = 0
    reinicios = 0
    for linea in lineas:
        if RE_NUEVO_RECORD.search(linea):
            nivel = 0
            continue

        coincidencia = RE_REINICIO_ADAPTATIVO.search(linea)
        if not coincidencia:
            continue

        gen, _stagnant, reemplazados, fraccion, cuota, _trigger, _kept = (
            coincidencia.groups()
        )
        gen = int(gen)
        reemplazados = int(reemplazados)
        fraccion = float(fraccion)
        cuota = float(cuota)
        fraccion_esperada, cuota_esperada = parametros_esperados(nivel)

        if abs(fraccion - fraccion_esperada) > 0.005:
            errores.append(
                f"{etiqueta}: gen={gen} fraction={fraccion:.2f}, esperado "
                f"{fraccion_esperada:.2f} en el reinicio {nivel}"
            )
        if abs(cuota - cuota_esperada) > 0.005:
            errores.append(
                f"{etiqueta}: gen={gen} mutation_share={cuota:.2f}, esperado "
                f"{cuota_esperada:.2f} en el reinicio {nivel}"
            )
        esperados = round(fraccion * POBLACION)
        if abs(reemplazados - esperados) > 1:
            errores.append(
                f"{etiqueta}: gen={gen} replaced={reemplazados}, esperado "
                f"~{esperados} para fraction={fraccion:.2f}"
            )

        nivel += 1
        reinicios += 1

    return reinicios


def verificar_corrida_diversidad(overrides=None, etiqueta="diversity",
                                 warm_seeds_esperado=None):
    """Criterios 1-2 (y 5): diversity dispara, resetea el estancamiento y escala.

    `overrides` cambia el entorno de la corrida (el escenario warm del criterio 5
    activa `ODE_WARM_START=1` en el caso 1). Con `warm_seeds_esperado` ademas se
    valida la cabecera: la cuota ceil(0.05*4)=1 debe inyectar exactamente esa
    cantidad de semillas finitas del catalogo (issue #148). En Python seria un
    parametro `esperado=1` con default `None`.
    """
    proceso = correr_solver(
        {
            # Umbral enorme: cualquier CV finito cae por debajo.
            "ODE_DIV_THRESHOLD": "1000000000",
            "ODE_STALE_LIMIT": str(STALE_LIMIT_DIVERSIDAD),
            **(overrides or {}),
        }
    )
    if proceso.returncode != 0:
        return [f"corrida {etiqueta} salio con codigo {proceso.returncode}"]

    lineas = [linea for linea in proceso.stdout.splitlines() if linea.strip()]
    errores = []
    reinicios = []

    # Criterio 5: la cabecera informa el warm start real de la corrida. La
    # cuota a poblacion 4 es ceil(0.05*4)=1 y la primera base del caso 1
    # (x, serie truncada de Si(x)) es finita, asi que warm_seeds=1.
    if warm_seeds_esperado is not None:
        cabecera = next((linea for linea in lineas
                         if "de_run_start |" in linea), "")
        for campo in ("warm_start=1", f"warm_seeds={warm_seeds_esperado}"):
            if campo not in cabecera:
                errores.append(
                    f"{etiqueta}: la cabecera no informa {campo}: {cabecera!r}"
                )

    for linea in lineas:
        coincidencia = RE_REINICIO_ADAPTATIVO.search(linea)
        if not coincidencia:
            continue
        (gen, stagnant, reemplazados, fraccion, cuota, trigger,
         kept) = coincidencia.groups()
        gen = int(gen)
        stagnant = int(stagnant)
        reemplazados = int(reemplazados)
        kept = int(kept)
        reinicios.append((gen, stagnant, reemplazados, float(fraccion), float(cuota)))
        if kept + reemplazados != POBLACION:
            errores.append(
                f"gen={gen}: kept={kept} + replaced={reemplazados} != "
                f"population={POBLACION}"
            )
        if trigger != "diversity":
            errores.append(
                f"{etiqueta}: gen={gen}: trigger={trigger}, esperado diversity "
                "con ODE_DIV_THRESHOLD enorme"
            )
        # El reinicio resetea el contador: nunca debe acercarse al limite que
        # habilitaria el canal de estancamiento.
        if stagnant >= STALE_LIMIT_DIVERSIDAD:
            errores.append(
                f"{etiqueta}: gen={gen}: stagnant_gens={stagnant} no se "
                "resetearon tras el reinicio por diversidad"
            )

    if not reinicios:
        errores.append(f"la corrida {etiqueta} no emitio ningun reinicio por "
                       "diversidad")
    else:
        verificar_escalera(lineas, errores, etiqueta)

    return errores


def verificar_corrida_diversidad_warm():
    """Criterio 5 (issue #148): frontera warm de la cuota a poblacion 4.

    Con `ODE_WARM_START=1` la cuota es ceil(0.05*4)=1: solo la primera base
    del catalogo entra en el censo. En el caso 0 esa base es la solucion
    exacta (fitness 0.0) y la corrida converge en la generacion 1 sin dejar
    evaluar el disparador — por eso las corridas del criterio 1-2 siguen con
    arranque aleatorio. El caso 1 usa la mejor serie truncada expresable (x,
    fitness ~4.5e-2): finita pero no convergente, de modo que el canal de
    diversidad debe seguir evaluable con la unica semilla inyectada como
    proporcion finita justo en el 25% inclusivo (1 de 4).
    """
    return verificar_corrida_diversidad(
        overrides={"ODE_CASE": "1", "ODE_WARM_START": "1"},
        etiqueta="diversity-warm",
        warm_seeds_esperado=1,
    )


def verificar_corrida_estancamiento():
    """Criterio 3: el canal compartido sigue etiquetando trigger=stale."""
    proceso = correr_solver(
        {
            # CV < 0 es imposible: el canal de diversidad queda apagado.
            "ODE_DIV_THRESHOLD": "0",
            "ODE_STALE_LIMIT": "2",
        }
    )
    if proceso.returncode != 0:
        return [f"corrida stale salio con codigo {proceso.returncode}"]

    lineas = [linea for linea in proceso.stdout.splitlines() if linea.strip()]
    errores = []
    reinicios = []

    for linea in lineas:
        coincidencia = RE_REINICIO_ADAPTATIVO.search(linea)
        if not coincidencia:
            continue
        (gen, stagnant, reemplazados, fraccion, cuota, trigger,
         kept) = coincidencia.groups()
        gen = int(gen)
        reemplazados = int(reemplazados)
        kept = int(kept)
        reinicios.append(
            (gen, int(stagnant), reemplazados, float(fraccion), float(cuota))
        )
        if kept + reemplazados != POBLACION:
            errores.append(
                f"gen={gen}: kept={kept} + replaced={reemplazados} != "
                f"population={POBLACION}"
            )
        if trigger != "stale":
            errores.append(
                f"gen={gen}: trigger={trigger}, esperado stale con "
                "ODE_DIV_THRESHOLD=0"
            )

    if not reinicios:
        errores.append("la corrida no emitio ningun reinicio por estancamiento")
    else:
        verificar_escalera(lineas, errores, "stale")

    return errores


def verificar_corrida_legacy():
    """Criterio 4: legacy conserva su fraccion y nunca dispara por diversidad."""
    proceso = correr_solver(
        {
            "ODE_RESTART_MODE": "legacy",
            "ODE_STALE_LIMIT": "2",
        }
    )
    if proceso.returncode != 0:
        return [f"corrida legacy salio con codigo {proceso.returncode}"]

    lineas = [linea for linea in proceso.stdout.splitlines() if linea.strip()]
    errores = []
    reinicios = 0

    for linea in lineas:
        if "restart |" not in linea:
            continue
        reinicios += 1
        coincidencia = RE_REINICIO_LEGACY.search(linea)
        if not coincidencia:
            errores.append(f"linea restart legacy fuera de formato: {linea!r}")
            continue

        (gen, _stagnant, reemplazados, fraccion, trigger,
         kept) = coincidencia.groups()
        if trigger != "stale":
            errores.append(
                f"gen={gen}: legacy emitio trigger={trigger}, esperado stale"
            )
        if abs(float(fraccion) - 0.80) > 0.005:
            errores.append(
                f"gen={gen}: legacy fraction={fraccion}, esperado 0.80"
            )
        if int(kept) + int(reemplazados) != POBLACION:
            errores.append(
                f"gen={gen}: kept={kept} + replaced={reemplazados} != "
                f"population={POBLACION}"
            )
        if "mutation_share=" in linea:
            errores.append(
                f"gen={gen}: legacy no debe imprimir mutation_share=: {linea!r}"
            )

    if reinicios == 0:
        errores.append("la corrida legacy no emitio ningun reinicio")

    return errores


def main():
    if not BINARIO.exists():
        print(
            "check_diversity_trigger: falta build/ode-solver; corre "
            "'cmake -B build && cmake --build build'",
            file=sys.stderr,
        )
        return 1

    errores = []
    try:
        errores.extend(verificar_corrida_diversidad())
        errores.extend(verificar_corrida_diversidad_warm())
        errores.extend(verificar_corrida_estancamiento())
        errores.extend(verificar_corrida_legacy())
    except subprocess.TimeoutExpired:
        print("check_diversity_trigger: build/ode-solver excedio los 60s",
              file=sys.stderr)
        return 1

    if errores:
        for error in errores:
            print(f"check_diversity_trigger: FAIL | {error}", file=sys.stderr)
        return 1

    print(
        "check_diversity_trigger: PASS | trigger=diversity resetea el "
        "estancamiento, comparte la escalera, legacy no dispara por "
        "diversidad y la cuota warm=1 a poblacion 4 mantiene el canal "
        "evaluable (issue #148)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
