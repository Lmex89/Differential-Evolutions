// test_restart_escalation.cpp — tests del escalado de reinicios (issues #128 y #167).
//
// El issue #128 pide que los reinicios consecutivos sin un `new_best` crezcan
// su fraccion de reemplazo (0.4 -> 0.6 -> 0.8) y que, tras agotar
// ODE_RESTART_CAP, el reinicio degenere a solo refinamiento (fraccion 0.2 y
// cuota de mutacion 1.0). Cualquier `new_best` reinicia la escalera.
//
// Desde el issue #167 la escalera vive dentro del modulo del ciclo de vida y
// desde el #170 el test cruza la operacion unica `ControlReinicio::reiniciar()`
// (la evidencia y la poblacion entran, el ResultadoReinicio sale); la escalera
// no es un collaborator suelto. Se compila con el modulo restart_lifecycle.cpp,
// sin incluir parse_tree.cpp. En Python seria
// `from restart_lifecycle import ControlReinicio, ConfigReinicio`.

#include <iostream>
#include <string>
#include <cmath>

#include "restart_lifecycle.h"

// -------------------------------------------------------------------------
// Arnes minimo (mismo estilo que test_population.cpp).
// -------------------------------------------------------------------------

static int g_checks = 0;
static int g_fallos = 0;

// check_decimal: igualdad entre long double con epsilon, como en
// test_per_case_config.cpp.
static void check_decimal(const std::string& nombre, long double recibido,
                          long double esperado) {
    const long double epsilon = 1e-9L;
    g_checks++;
    if (std::abs(recibido - esperado) < epsilon) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "  (got=" << recibido
                  << " esperado=" << esperado << ")\n";
        g_fallos++;
    }
}

// config_escalador: configuracion que reinicia en cada generacion sin
// mejora (stale_limit = 1), para observar la secuencia de la escalera sin
// esperar a un limite largo. En Python seria un fixture `config_escalador()`.
static ConfigReinicio config_escalador() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 1;
    return config;
}

// poblacion_fixture: poblacion nativa de 4 individuos para la operacion
// unica. Los tests examinan parametros, no vectores.
static std::vector<FunbObj> poblacion_fixture() {
    std::vector<FunbObj> poblacion;
    for (int i = 0; i < 4; i++) {
        poblacion.push_back(FunbObj::create_random(20, 0));
    }
    return poblacion;
}

// paso: invoca la operacion unica por generacion (issue #170) con una
// poblacion nativa. La evidencia de fitness no importa aqui: stale_limit=1
// dispara siempre y el test examina los parametros del resultado.
static ResultadoReinicio paso(ControlReinicio& control, bool nuevo_record,
                              std::vector<FunbObj>& poblacion) {
    return control.reiniciar(nuevo_record, {}, poblacion, 0, {});
}

// -------------------------------------------------------------------------
// Seam: la secuencia de reinicios que devuelve ControlReinicio::reiniciar()
// con la configuracion por defecto.
// Atrapa: el primer reinicio no es suave, el incremento no es 0.2, el tope no
// se respeta o la degradacion a refinamiento llega antes/despues del cap.
// -------------------------------------------------------------------------
static void test_escalado_secuencia_por_defecto() {
    ControlReinicio control(config_escalador(), 0);
    std::vector<FunbObj> poblacion = poblacion_fixture();

    // Primer reinicio del episodio: fraccion minima, composicion hibrida.
    const ResultadoReinicio primero = paso(control, false, poblacion);
    check_decimal("primer reinicio: fraccion 0.4",
                  primero.parametros.fraccion, 0.4L);
    check_decimal("primer reinicio: cuota de mutacion 0.5",
                  primero.parametros.cuota_mutacion, 0.5L);

    // Cada reinicio consecutivo sin new_best sube 0.2.
    const ResultadoReinicio segundo = paso(control, false, poblacion);
    check_decimal("segundo reinicio: fraccion 0.6",
                  segundo.parametros.fraccion, 0.6L);
    check_decimal("segundo reinicio: cuota de mutacion 0.5",
                  segundo.parametros.cuota_mutacion, 0.5L);

    // Tercer reinicio: tope cap, pero aun no es refinamiento-only.
    const ResultadoReinicio tercero = paso(control, false, poblacion);
    check_decimal("tercer reinicio: fraccion 0.8",
                  tercero.parametros.fraccion, 0.8L);
    check_decimal("tercer reinicio: cuota de mutacion 0.5",
                  tercero.parametros.cuota_mutacion, 0.5L);

    // Tras ODE_RESTART_CAP=3 reinicios sin mejora: solo refinamiento.
    const ResultadoReinicio refinamiento = paso(control, false, poblacion);
    check_decimal("cuarto reinicio: fraccion de refinamiento 0.2",
                  refinamiento.parametros.fraccion, 0.2L);
    check_decimal("cuarto reinicio: toda la replacement es mutacion de elites",
                  refinamiento.parametros.cuota_mutacion, 1.0L);

    // La degradacion se mantiene mientras no haya un new_best.
    const ResultadoReinicio siguiente = paso(control, false, poblacion);
    check_decimal("quinto reinicio sigue en refinamiento (0.2)",
                  siguiente.parametros.fraccion, 0.2L);
    check_decimal("quinto reinicio sigue con cuota de mutacion 1.0",
                  siguiente.parametros.cuota_mutacion, 1.0L);
}

// -------------------------------------------------------------------------
// Seam: el escalado respeta ODE_RESTART_MIN_FRACTION, el tope
// ODE_RESTART_FRACTION, ODE_RESTART_CAP y ODE_RESTART_MUTATION_SHARE.
// Atrapa: valores de configuracion ignorados o formula hardcodeada.
// -------------------------------------------------------------------------
static void test_escalado_respeta_la_configuracion() {
    ConfigReinicio config = config_escalador();
    config.restart_min_fraction = 0.6L;
    config.restart_fraction = 0.7L;  // tope: 0.6 + 0.2 ya lo supera
    config.restart_cap = 3;
    config.restart_mutation_share = 0.75L;

    ControlReinicio control(config, 0);
    std::vector<FunbObj> poblacion = poblacion_fixture();

    const ResultadoReinicio primero = paso(control, false, poblacion);
    check_decimal("min fraction configurada: 0.6", primero.parametros.fraccion, 0.6L);
    check_decimal("mutation share configurado: 0.75",
                  primero.parametros.cuota_mutacion, 0.75L);

    // 0.6 + 0.2 = 0.8, pero el tope ODE_RESTART_FRACTION=0.7 lo recorta.
    const ResultadoReinicio segundo = paso(control, false, poblacion);
    check_decimal("la fraccion se recorta al tope configurado (0.7)",
                  segundo.parametros.fraccion, 0.7L);
    check_decimal("el share se conserva bajo el tope (0.75)",
                  segundo.parametros.cuota_mutacion, 0.75L);

    const ResultadoReinicio tercero = paso(control, false, poblacion);
    check_decimal("el tope se mantiene en el tercer reinicio (0.7)",
                  tercero.parametros.fraccion, 0.7L);
    check_decimal("el tercer reinicio aun no es refinamiento (share 0.75)",
                  tercero.parametros.cuota_mutacion, 0.75L);

    const ResultadoReinicio refinamiento = paso(control, false, poblacion);
    check_decimal("el cap configurado dispara el refinamiento (0.2)",
                  refinamiento.parametros.fraccion, 0.2L);
    check_decimal("el refinamiento fuerza mutacion de elites (1.0)",
                  refinamiento.parametros.cuota_mutacion, 1.0L);
}

// -------------------------------------------------------------------------
// Seam: un new_best reinicia la escalera desde el primer reinicio suave.
// Atrapa: el contador de reinicios consecutivos no se resetea o queda a medias.
// -------------------------------------------------------------------------
static void test_nuevo_record_reinicia_la_escalera() {
    ControlReinicio control(config_escalador(), 0);
    std::vector<FunbObj> poblacion = poblacion_fixture();

    // Subimos dos escalones: 0.4 y 0.6.
    paso(control, false, poblacion);
    const ResultadoReinicio segundo = paso(control, false, poblacion);
    check_decimal("antes del new_best la escalera va en 0.6",
                  segundo.parametros.fraccion, 0.6L);

    // Un new_best corta el episodio: el siguiente reinicio vuelve al minimo.
    paso(control, true, poblacion);
    const ResultadoReinicio tras_record = paso(control, false, poblacion);
    check_decimal("tras new_best la fraccion vuelve a 0.4",
                  tras_record.parametros.fraccion, 0.4L);
    check_decimal("tras new_best la cuota vuelve a 0.5",
                  tras_record.parametros.cuota_mutacion, 0.5L);

    // Tambien se sale del refinamiento-only con un new_best.
    paso(control, false, poblacion);  // 0.6
    paso(control, false, poblacion);  // 0.8
    const ResultadoReinicio refinamiento = paso(control, false, poblacion);
    check_decimal("la escalera llego a refinamiento (0.2)",
                  refinamiento.parametros.fraccion, 0.2L);

    paso(control, true, poblacion);
    const ResultadoReinicio reinicio = paso(control, false, poblacion);
    check_decimal("new_best saca del refinamiento y vuelve a 0.4",
                  reinicio.parametros.fraccion, 0.4L);
    check_decimal("new_best restaura la cuota hibrida (0.5)",
                  reinicio.parametros.cuota_mutacion, 0.5L);
}

int main() {
    std::cout << "== test_restart_escalation: issue #128 (escalado de reinicios) ==\n";
    test_escalado_secuencia_por_defecto();
    test_escalado_respeta_la_configuracion();
    test_nuevo_record_reinicia_la_escalera();
    std::cout << (g_fallos == 0 ? "OK" : "FALLARON") << ": "
              << (g_checks - g_fallos) << "/" << g_checks << " asserts\n";
    return g_fallos == 0 ? 0 : 1;
}
