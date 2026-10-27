// restart_lifecycle.cpp — Implementacion del modulo de ciclo de vida de reinicio.
//
// Separa la logica de reinicio del bucle DE. El DE loop sigue manejando
// generaciones, logging y resolucion del entorno; este modulo solo decide
// cuando y como reiniciar.

#include "restart_lifecycle.h"

#include <algorithm>
#include <cassert>

// -------------------------------------------------------------------------
// ResumenDiversidad y disparador.
// -------------------------------------------------------------------------

ResumenDiversidad resumir_diversidad(const std::vector<ldouble>& fitnesses) {
    ResumenDiversidad resumen;
    resumen.individuos_totales = static_cast<int>(fitnesses.size());
    long double suma = 0.0L;

    for (ldouble fitness : fitnesses) {
        if (!std::isfinite(fitness)) continue;
        resumen.individuos_finitos++;
        suma += fitness;
    }

    // Sin muestra no hay CV que calcular (0/0); la guarda de la decision
    // rechaza este caso porque la proporcion finita es cero.
    if (resumen.individuos_finitos == 0) return resumen;

    const long double media =
        suma / static_cast<long double>(resumen.individuos_finitos);

    // Las fitness son sumas de cuadrados (no negativas). Una media cero
    // implica que toda la muestra finita es cero: el colapso es maximo y el
    // CV se define como 0 para no dividir por cero (ES.105). En Python seria
    // `cv = pstdev(finitas) / media if media > 0 else 0.0`.
    if (media <= 0.0L) return resumen;

    long double suma_cuadrados = 0.0L;
    for (ldouble fitness : fitnesses) {
        if (!std::isfinite(fitness)) continue;
        const long double diferencia = fitness - media;
        suma_cuadrados += diferencia * diferencia;
    }

    // Desviacion poblacional (divide por n, no por n-1): la muestra ES la
    // poblacion de esa generacion. En Python: sqrt(suma_cuadrados / n) / media.
    const long double varianza =
        suma_cuadrados / static_cast<long double>(resumen.individuos_finitos);
    resumen.coeficiente_variacion = std::sqrt(varianza) / media;
    return resumen;
}

bool dispara_reinicio_por_diversidad(const ResumenDiversidad& resumen,
                                     bool hay_record,
                                     long double umbral) {
    if (!hay_record) return false;
    if (resumen.individuos_totales <= 0) return false;

    const long double proporcion_finita =
        static_cast<long double>(resumen.individuos_finitos) /
        static_cast<long double>(resumen.individuos_totales);
    if (proporcion_finita < FRACCION_MINIMA_FINITA) return false;

    // Comparacion estricta: "cae por debajo del umbral" no incluye tocarlo.
    return resumen.coeficiente_variacion < umbral;
}

// -------------------------------------------------------------------------
// ControlReinicio: implementacion de los metodos.
// -------------------------------------------------------------------------

ControlReinicio::ControlReinicio(const ConfigReinicio& config)
    : config_(config) {}

DecisionReinicio ControlReinicio::decidir(bool nuevo_record,
                                         const std::vector<ldouble>& fitnesses) {
    // `new_best` es el unico hecho que corta el estancamiento: resetea
    // tanto la ventana como el episodio de reinicios consecutivos. En
    // Python: `if nuevo_record: ...; self.reinicios_consecutivos = 0`.
    if (nuevo_record) {
        hay_record_ = true;
        generaciones_sin_mejora_ = 0;
        reinicios_consecutivos_ = 0;
    } else {
        generaciones_sin_mejora_++;
    }

    // Stale primero: si ambos canales disparan en la misma generacion,
    // gana el estancamiento y no se encadenan dos reinicios (issue #125).
    if (generaciones_sin_mejora_ >= config_.stale_limit) {
        return reiniciar(DisparadorReinicio::stale);
    }

    // La diversidad solo existe en adaptive (issue #129): en legacy el
    // resumen ni se calcula, los guards ni se evaluan y la corrida
    // historica sigue bit-reproducible. En Python:
    // `if self.modo is ADAPTIVE and dispara_reinicio_por_diversidad(resumir_diversidad(fitnesses), ...): ...`.
    if (config_.restart_mode == ModoReinicio::adaptive) {
        const ResumenDiversidad resumen = resumir_diversidad(fitnesses);
        if (dispara_reinicio_por_diversidad(resumen, hay_record_,
                                            config_.div_threshold)) {
            return reiniciar(DisparadorReinicio::diversity);
        }
    }

    // Sin reinicio: ventana por debajo del limite y diversidad sin colapso.
    return DecisionReinicio{};
}

DecisionReinicio ControlReinicio::reiniciar(DisparadorReinicio disparador) {
    // La evidencia se captura antes del reset: main() la usa para el
    // campo `stagnant_gens=` del log `restart |`.
    const int estancadas = generaciones_sin_mejora_;
    generaciones_sin_mejora_ = 0;

    // El ternario elige una sola rama: `consumir_siguiente` solo avanza la
    // escalera en adaptive, nunca en legacy. La cuota de legacy no la usa
    // `reiniciar_poblacion`, pero se informa el valor configurado.
    const ParametrosReinicio parametros =
        config_.restart_mode == ModoReinicio::adaptive
            ? consumir_siguiente()
            : ParametrosReinicio{config_.restart_fraction,
                                 config_.restart_mutation_share};

    return DecisionReinicio{disparador, parametros, estancadas};
}

ParametrosReinicio ControlReinicio::consumir_siguiente() {
    ParametrosReinicio parametros;

    if (reinicios_consecutivos_ >= config_.restart_cap) {
        parametros.fraccion = FRACCION_REFINAMIENTO;
        parametros.cuota_mutacion = CUOTA_MUTACION_REFINAMIENTO;
    } else {
        const long double fraccion_escalada =
            config_.restart_min_fraction +
            INCREMENTO_ESCALADO_REINICIO *
                static_cast<long double>(reinicios_consecutivos_);
        // std::min evita que la escalera supere el tope configurado.
        parametros.fraccion = std::min(config_.restart_fraction, fraccion_escalada);
        parametros.cuota_mutacion = config_.restart_mutation_share;
    }

    // El contador se satura en el cap: mas reinicios ya no cambian la
    // decision y asi no puede desbordar en una corrida larga (ES.103).
    if (reinicios_consecutivos_ < config_.restart_cap) {
        reinicios_consecutivos_++;
    }
    return parametros;
}

// -------------------------------------------------------------------------
// Reemplazo legacy de la poblacion (issues #82/#168). Movido tal cual desde
// parse_tree.cpp, salvo la dimension, que ahora se derive de la poblacion
// para no cruzar la costura con la configuracion de busqueda.
// -------------------------------------------------------------------------

int parte_entera(long double fraccion, int total) {
    return static_cast<int>(fraccion * total);
}

// reiniciar_poblacion: mecanismo de escape de minimos locales. Reemplaza la
// fraccion configurada de la poblacion con individuos aleatorios frescos para
// inyectar diversidad, pero antes restaura el mejor historico `record` en el
// indice reservado `indice_record` y no vuelve a tocar ese indice: asi el
// reinicio no puede perder el record (elitismo real). Devuelve cuantos
// individuos se reemplazaron, que es el campo `replaced=` del log `restart |`.
// En Python seria
// `reemplazados = reiniciar_poblacion(poblacion, indice, record, config, caso)`.
int reiniciar_poblacion(std::vector<FunbObj>& poblacion, int indice_record,
                        const std::vector<ldouble>& record,
                        const ConfigReinicio& config, int caso) {
    // Precondicion: el indice reservado apunta dentro de la poblacion (evita
    // el acceso fuera de rango si un refactor cambia el calculo de `mejor`).
    // En Python seria `assert 0 <= indice_record < len(poblacion)`.
    assert(indice_record >= 0 && indice_record < static_cast<int>(poblacion.size()));

    // Dimension de los reemplazos derivada de la poblacion: es la invariante
    // de la costura (poblacion no vacia con dimension uniforme) y equivale a
    // la dimension configurada de la corrida, asi que el sorteo de
    // create_random consume rand() exactamente igual que antes. En Python:
    // `dimension = len(poblacion[0])`.
    const int dimension = static_cast<int>(poblacion[0].vecvalue().size());
    // La uniformidad no se asume a ciegas: una poblacion mixta haria que unos
    // reemplazos salieran con otra dimension que los sobrevivientes.
    for (const FunbObj& individuo : poblacion) {
        assert(static_cast<int>(individuo.vecvalue().size()) == dimension);
    }

    // Elitismo: el record entra al indice reservado ANTES del barrido, para
    // que el bucle (que salta `indice_record`) lo deje intacto. Un `record`
    // vacio significa que la corrida nunca encontro un fitness finito: no hay
    // nada que restaurar y el indice queda como estaba.
    if (!record.empty()){
        poblacion[indice_record] = FunbObj(record, 0, 1, caso);
    }

    // La fraccion de reemplazo viene de config.restart_fraction (issue #108),
    // configurable via ODE_RESTART_FRACTION. Antes era hardcoded 0.8.
    const int tam_poblacion = static_cast<int>(poblacion.size());
    const int cantidad_reemplazar = parte_entera(config.restart_fraction, tam_poblacion);
    int reemplazados = 0;
    for (int j = 0; j < tam_poblacion && reemplazados < cantidad_reemplazar; j++){
        if (j != indice_record){
            poblacion[j] = FunbObj::create_random(dimension, caso);
            reemplazados++;
        }
    }
    return reemplazados;
}
