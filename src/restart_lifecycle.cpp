// restart_lifecycle.cpp — Implementacion del modulo de ciclo de vida de reinicio.
//
// Separa la logica de reinicio del bucle DE. El DE loop sigue manejando
// generaciones, logging y resolucion del entorno; este modulo solo decide
// cuando y como reiniciar.

#include "restart_lifecycle.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "token.h"

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

ControlReinicio::ControlReinicio(const ConfigReinicio& config, int caso)
    : config_(config), caso_(caso) {}

ResultadoReinicio ControlReinicio::reiniciar(
    bool nuevo_record, const std::vector<ldouble>& fitnesses,
    std::vector<FunbObj>& poblacion, int indice_record,
    const std::vector<ldouble>& record) {
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
    DisparadorReinicio disparador = DisparadorReinicio::ninguno;
    if (generaciones_sin_mejora_ >= config_.stale_limit) {
        disparador = DisparadorReinicio::stale;
    } else if (config_.restart_mode == ModoReinicio::adaptive) {
        // La diversidad solo existe en adaptive (issue #129): en legacy el
        // resumen ni se calcula, los guards ni se evaluan y la corrida
        // historica sigue bit-reproducible. En Python:
        // `if self.modo is ADAPTIVE and dispara_reinicio_por_diversidad(resumir_diversidad(fitnesses), ...): ...`.
        const ResumenDiversidad resumen = resumir_diversidad(fitnesses);
        if (dispara_reinicio_por_diversidad(resumen, hay_record_,
                                            config_.div_threshold)) {
            disparador = DisparadorReinicio::diversity;
        }
    }

    // Sin reinicio: ventana por debajo del limite y diversidad sin colapso.
    if (disparador == DisparadorReinicio::ninguno) return ResultadoReinicio{};

    // La operacion unica (issue #170) decide y aplica en la misma pasada: el
    // bucle DE ya no coordina una decision con un reemplazo posterior. En
    // Python: `resultado = self._preparar(trigger); resultado.reemplazados =
    // self._aplicar(...); resultado.conservados = len(poblacion) - reemplazados`.
    ResultadoReinicio resultado = preparar_reinicio(disparador);
    resultado.reemplazados =
        aplicar_reemplazo(resultado.parametros, poblacion, indice_record, record);
    resultado.conservados =
        static_cast<int>(poblacion.size()) - resultado.reemplazados;
    resultado.reportar_cuota_mutacion =
        config_.restart_mode == ModoReinicio::adaptive;
    return resultado;
}

ResultadoReinicio ControlReinicio::preparar_reinicio(
    DisparadorReinicio disparador) {
    // La evidencia se captura antes del reset: el resultado la lleva al
    // campo `stagnant_gens=` del log `restart |`.
    const int estancadas = generaciones_sin_mejora_;
    generaciones_sin_mejora_ = 0;

    // El ternario elige una sola rama: `consumir_siguiente` solo avanza la
    // escalera en adaptive, nunca en legacy. La cuota de legacy no la usa
    // el reemplazo, pero se informa el valor configurado.
    ResultadoReinicio resultado;
    resultado.disparador = disparador;
    resultado.parametros =
        config_.restart_mode == ModoReinicio::adaptive
            ? consumir_siguiente()
            : ParametrosReinicio{config_.restart_fraction,
                                 config_.restart_mutation_share};
    resultado.generaciones_sin_mejora = estancadas;
    return resultado;
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
// para no cruzar la costura con la configuracion de busqueda. Es interno: la
// unica costura publica es ControlReinicio::reiniciar() (issue #170).
// -------------------------------------------------------------------------

int parte_entera(long double fraccion, int total) {
    return static_cast<int>(fraccion * total);
}

namespace {

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

constexpr int CAMBIOS_PUNTUALES_MIN = 1;
constexpr int CAMBIOS_PUNTUALES_MAX = 2;

// Cambia 1-2 posiciones distintas; en Python seria random.sample(range(n), cambios).
std::vector<ldouble> mutacion_puntual(const std::vector<ldouble>& base) {
    const int n = static_cast<int>(base.size());
    if (n == 0) return base;

    int cambios = CAMBIOS_PUNTUALES_MIN +
                  std::rand() % (CAMBIOS_PUNTUALES_MAX - CAMBIOS_PUNTUALES_MIN + 1);
    if (cambios > n) cambios = n;

    std::vector<ldouble> copia(base);
    const int primera = std::rand() % n;
    int nuevo = 0;
    do { nuevo = random_token(); } while (nuevo == static_cast<int>(copia[primera]));
    copia[primera] = nuevo;

    if (cambios == CAMBIOS_PUNTUALES_MAX) {
        int segunda = std::rand() % (n - 1);
        if (segunda >= primera) segunda++;
        do { nuevo = random_token(); } while (nuevo == static_cast<int>(copia[segunda]));
        copia[segunda] = nuevo;
    }
    return copia;
}

int reiniciar_poblacion_adaptativo(std::vector<FunbObj>& poblacion,
                                   const std::vector<ldouble>& record,
                                   const ParametrosReinicio& parametros, int caso) {
    const int tam = static_cast<int>(poblacion.size());
    if (tam == 0) return 0;

    const int dimension = static_cast<int>(poblacion[0].vecvalue().size());
    for (const FunbObj& individuo : poblacion) {
        assert(static_cast<int>(individuo.vecvalue().size()) == dimension);
    }

    // stable_sort conserva los empates y el resultado con semilla fija;
    // en Python seria `sorted(range(tam), key=lambda i: poblacion[i].fitness())`.
    std::vector<int> orden(tam);
    for (int i = 0; i < tam; i++) orden[i] = i;
    std::stable_sort(orden.begin(), orden.end(), [&poblacion](int a, int b) {
        return poblacion[a].fitness() < poblacion[b].fitness();
    });

    int cantidad = parte_entera(parametros.fraccion, tam);
    if (cantidad > tam - 1) cantidad = tam - 1;
    if (cantidad < 0) cantidad = 0;
    int conservados = tam - cantidad;

    bool record_presente = false;
    if (!record.empty()) {
        for (int k = 0; k < conservados && !record_presente; k++) {
            if (poblacion[orden[k]].vecvalue() == record) record_presente = true;
        }
    }
    const bool inyectar = !record.empty() && !record_presente;
    // Un record sin cupo desplaza al peor y cuenta como reemplazo.
    if (inyectar && cantidad == 0) {
        cantidad = 1;
        conservados = tam - cantidad;
    }

    const int plazas_record = inyectar ? 1 : 0;
    const int mutaciones = parte_entera(parametros.cuota_mutacion,
                                        cantidad - plazas_record);
    for (int k = conservados; k < tam; k++) {
        const int lugar = k - conservados;
        if (lugar < plazas_record) {
            poblacion[orden[k]] = FunbObj(record, 0, 1, caso);
        } else if (lugar - plazas_record < mutaciones) {
            const std::vector<ldouble> elite =
                poblacion[orden[std::rand() % conservados]].vecvalue();
            poblacion[orden[k]] = FunbObj(mutacion_puntual(elite), 0, 1, caso);
        } else {
            poblacion[orden[k]] = FunbObj::create_random(dimension, caso);
        }
    }
    return cantidad;
}

}  // namespace

int ControlReinicio::aplicar_reemplazo(
    const ParametrosReinicio& parametros, std::vector<FunbObj>& poblacion,
    int indice_record, const std::vector<ldouble>& record) {
    // Legacy y adaptive conservan implementaciones separadas (issue #164):
    // solo comparten el contexto de la pasada. En Python:
    // `return reiniciar_poblacion_adaptativo(...) if adaptive else reiniciar_poblacion(...)`.
    if (config_.restart_mode == ModoReinicio::adaptive) {
        return reiniciar_poblacion_adaptativo(poblacion, record, parametros, caso_);
    }
    return reiniciar_poblacion(poblacion, indice_record, record, config_, caso_);
}
