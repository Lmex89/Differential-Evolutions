// restart_lifecycle.h — Modulo del ciclo de vida de reinicio (issues #166 a
// #170).
//
// Separa la configuracion, el resumen de diversidad, la decision y el
// reemplazo del bucle DE. El DE loop sigue manejando generaciones, logging y
// resolucion del entorno; este modulo decide cuando reiniciar, aplica el
// reemplazo y devuelve el resultado completo para el log.
//
// En Python seria un modulo `restart_lifecycle.py` con las clases
// `ConfigReinicio`, `ResumenDiversidad`, `ControlReinicio` y
// `ResultadoReinicio`.

#ifndef RESTART_LIFECYCLE_H
#define RESTART_LIFECYCLE_H

#include <vector>
#include <cmath>

#include "funbobj.h"  // FunbObj: los reemplazos trabajan sobre la poblacion nativa

// ldouble es el alias de long double usado en todo el proyecto. En Python
// no existe este detalle (float es doble precision siempre).
using ldouble = long double;

// -------------------------------------------------------------------------
// ModoReinicio: vocabulario legacy/adaptive (issue #126).
// `legacy` conserva el reinicio historico; `adaptive` combina la fraccion
// y la cuota escaladas por #128 con el reinicio elitista de #127.
// En Python seria un Enum.
enum class ModoReinicio { legacy, adaptive };

// -------------------------------------------------------------------------
// ConfigReinicio: configuracion especifica del reinicio (issue #166).
// Solo lleva hechos de stale, modo, escalacion, mutation-share y Diversity.
// El DE loop resuelve el entorno y construye esta config; el modulo no
// lee variables de entorno directamente.
// En Python seria una dataclass con estos campos.
struct ConfigReinicio {
    int stale_limit = 500;
    long double restart_fraction = 0.8L;
    ModoReinicio restart_mode = ModoReinicio::legacy;
    long double restart_min_fraction = 0.4L;
    int restart_cap = 3;
    long double restart_mutation_share = 0.5L;
    long double div_threshold = 0.05L;
};

// -------------------------------------------------------------------------
// ParametrosReinicio: decision concreta de un reinicio (fraccion y cuota).
// El escalador adapta estos parametros en modo adaptive; legacy usa la
// fraccion configurada.
// En Python seria una dataclass con `fraccion` y `cuota_mutacion`.
struct ParametrosReinicio {
    long double fraccion = 0.8L;
    long double cuota_mutacion = 0.5L;
};

// -------------------------------------------------------------------------
// ResumenDiversidad: resumen de la generacion para el disparador (issue #129).
// `individuos_finitos` cuenta solo los valores evaluables; `individuos_totales`
// es el tamano de la muestra; el CV usa solo los finitos.
// En Python seria una dataclass con esos tres campos.
struct ResumenDiversidad {
    int individuos_finitos = 0;
    int individuos_totales = 0;
    long double coeficiente_variacion = 0.0L;
};

// Proporcion minima de la poblacion que debe tener fitness finita para que
// el CV sea una senal de diversidad y no el reflejo de unos pocos individuos.
// En Python: `FRACCION_MINIMA_FINITA = 0.25`.
constexpr long double FRACCION_MINIMA_FINITA = 0.25L;

// Incremento por escalon de la escalera de reinicios (issue #128).
// En Python: `INCREMENTO_ESCALADO_REINICIO = 0.2`.
constexpr long double INCREMENTO_ESCALADO_REINICIO = 0.2L;

// Fraccion y cuota del nivel de refinamiento (tras el cap). En Python:
// `FRACCION_REFINAMIENTO = 0.2` y `CUOTA_MUTACION_REFINAMIENTO = 1.0`.
constexpr long double FRACCION_REFINAMIENTO = 0.2L;
constexpr long double CUOTA_MUTACION_REFINAMIENTO = 1.0L;

// -------------------------------------------------------------------------
// resumir_diversidad: calcula el CV sobre fitness finitos (issue #129).
// `individuos_finitos` cuenta solo los valores evaluables; el CV usa solo
// los finitos. Sin muestra finita, CV queda en 0 y el consumidor decide
// con la guarda de proporcion. En Python:
// `finitas = [f for f in fitnesses if math.isfinite(f)]` y luego
// `statistics.pstdev(finitas) / statistics.mean(finitas)`.
ResumenDiversidad resumir_diversidad(const std::vector<ldouble>& fitnesses);

// -------------------------------------------------------------------------
// dispara_reinicio_por_diversidad: decide si el resumen activa el reinicio
// por colapso de diversidad (issue #129). Sin un record finito, sin la
// proporcion minima de finitos, o con CV >= umbral, no dispara. La guarda
// de proporcion evita que unos pocos individuos evaluables disparen el canal.
// En Python:
// `return hay_record and finitos / total >= 0.25 and cv < umbral`.
bool dispara_reinicio_por_diversidad(const ResumenDiversidad& resumen,
                                     bool hay_record,
                                     long double umbral);

// -------------------------------------------------------------------------
// DisparadorReinicio: canal que motivo la decision. `ninguno` es "no
// reiniciar"; `stale` y `diversity` coinciden con los valores historicos
// del campo `trigger=` del log. En Python seria un Enum.
enum class DisparadorReinicio { ninguno, stale, diversity };

// -------------------------------------------------------------------------
// ResultadoReinicio: resultado completo de una pasada del ciclo de vida
// (issue #170). El campo `disparador` distingue "no reinicio" de un reinicio
// real sin usar punteros que main() no puede leer despues. `conservados` y
// `reemplazados` son los campos `kept=` y `replaced=` del log; la cuota de
// mutacion solo se informa en adaptive (`reportar_cuota_mutacion`). En Python
// seria un dataclass con `trigger`, `parametros`, `generaciones_sin_mejora`,
// `reemplazados`, `conservados` y `reportar_cuota_mutacion`.
struct ResultadoReinicio {
    DisparadorReinicio disparador = DisparadorReinicio::ninguno;
    ParametrosReinicio parametros{};
    int generaciones_sin_mejora = 0;
    int reemplazados = 0;
    int conservados = 0;
    bool reportar_cuota_mutacion = false;

    // hay_reinicio: predicado legible para main() y para los tests; evita
    // comparar contra `ninguno` en cada consumidor. En Python:
    // `return self.disparador is not DisparadorReinicio.NINGUNO`.
    bool hay_reinicio() const {
        return disparador != DisparadorReinicio::ninguno;
    }
};

// -------------------------------------------------------------------------
// ControlReinicio: estado del ciclo de vida de una corrida (issues #138 y
// #170). La configuracion y el caso ODE se guardan por valor y son inmutables;
// la ventana de estancamiento y la escalera de escalado son estado mutable.
// `reiniciar()` es la unica operacion por generacion: recibe la evidencia
// (nuevo record y fitness), decide, aplica el reemplazo legacy o adaptive
// sobre la poblacion nativa y devuelve el resultado. El bucle DE solo aporta
// los datos y registra el resultado; no decide ni reemplaza. En Python seria
// una clase con `reiniciar(nuevo_record, fitnesses, poblacion, indice, record)`
// y estado interno.
class ControlReinicio {
public:
    ControlReinicio(const ConfigReinicio& config, int caso);

    // reiniciar: una pasada del ciclo de vida. `fitnesses` son los valores de
    // la poblacion de esta generacion (el modulo calcula el CV a partir de los
    // finitos). Stale tiene prioridad sobre diversity si ambos disparan en la
    // misma generacion; cualquier reinicio resetea la ventana, pero solo
    // `nuevo_record` resetea la escalera. Si hay reinicio, la poblacion se
    // reemplaza in situ y el resultado trae trigger, ventana, parametros y
    // conteos; si no, devuelve un resultado sin disparador. `indice_record` es
    // el indice que el reemplazo legacy reserva para el record historico. En
    // Python:
    // `if nuevo_record: ...; if generaciones >= stale_limit: return self._aplicar(STALE, poblacion, record)`.
    ResultadoReinicio reiniciar(bool nuevo_record,
                                const std::vector<ldouble>& fitnesses,
                                std::vector<FunbObj>& poblacion,
                                int indice_record,
                                const std::vector<ldouble>& record);

private:
    // preparar_reinicio: arma la parte de decision del resultado y resetea la
    // ventana, pero no la escalera (solo `nuevo_record` la resetea). En
    // adaptive avanza la escalera; en legacy usa la fraccion configurada y no
    // la toca. En Python: `return ResultadoReinicio(trigger, parametros, estancadas)`.
    ResultadoReinicio preparar_reinicio(DisparadorReinicio disparador);

    // aplicar_reemplazo: ejecuta la implementacion legacy o adaptive segun el
    // modo y devuelve cuantos individuos se reemplazaron. En Python:
    // `return reiniciar_poblacion(...) if legacy else reiniciar_poblacion_adaptativo(...)`.
    int aplicar_reemplazo(const ParametrosReinicio& parametros,
                          std::vector<FunbObj>& poblacion,
                          int indice_record,
                          const std::vector<ldouble>& record);

    // consumir_siguiente: parametros del proximo reinicio segun cuantos
    // reinicios consecutivos sin mejora lleva el episodio (issue #128), y
    // avanza la escalera. Es el unico lugar donde la escalera existe: vive
    // dentro del modulo y no se expone como colaborador. En Python:
    // `min(config.restart_fraction, config.restart_min_fraction + 0.2 * self.n)`.
    ParametrosReinicio consumir_siguiente();

    const ConfigReinicio config_;
    const int caso_;
    int generaciones_sin_mejora_ = 0;
    int reinicios_consecutivos_ = 0;  // escalera de escalado (#128)
    bool hay_record_ = false;
};

// -------------------------------------------------------------------------
// parte_entera: cuantos elementos representa la fraccion `fraccion` de un
// total `total`, truncando hacia abajo. Es el int(f*n) que el reemplazo usa
// tanto para la fraccion legacy como para la cuota de mutacion adaptativa,
// en un solo lugar (vivía en parse_tree.cpp; el issue #168 lo trae al modulo
// porque el reemplazo legacy ahora vive aqui). En Python: `int(fraccion * total)`.
int parte_entera(long double fraccion, int total);

#endif // RESTART_LIFECYCLE_H
