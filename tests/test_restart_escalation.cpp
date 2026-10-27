// test_restart_escalation.cpp — tests del escalado de reinicios (issue #128).
//
// El issue #128 pide que los reinicios consecutivos sin un `new_best` crezcan
// su fraccion de reemplazo (0.4 -> 0.6 -> 0.8) y que, tras agotar
// ODE_RESTART_CAP, el reinicio degenere a solo refinamiento (fraccion 0.2 y
// cuota de mutacion 1.0). Cualquier `new_best` reinicia la escalera.
//
// Se compila con -DODE_NO_MAIN y junto con dualfunctions.cpp y semilla.cpp;
// parse_tree.cpp se incluye mas abajo (FunbObj no tiene header), el mismo
// patron que test_population.cpp. En Python seria
// `from parse_tree import EscaladorReinicio, ConfigReinicio`.

#include <iostream>
#include <string>
#include <cmath>

#include "dualfunctions.h"
#include "token.h"

#include "parse_tree.cpp"

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

// -------------------------------------------------------------------------
// Seam: la secuencia publica de ParametrosReinicio que devuelve
// EscaladorReinicio::consumir_siguiente() con la configuracion por defecto.
// Atrapa: el primer reinicio no es suave, el incremento no es 0.2, el tope no
// se respeta o la degradacion a refinamiento llega antes/despues del cap.
// -------------------------------------------------------------------------
static void test_escalado_secuencia_por_defecto() {
    const ConfigReinicio config;  // defaults: min 0.4, cap 0.8, limite 3, share 0.5
    EscaladorReinicio escalador;

    // Primer reinicio del episodio: fraccion minima, composicion hibrida.
    ParametrosReinicio primero = escalador.consumir_siguiente(config);
    check_decimal("primer reinicio: fraccion 0.4", primero.fraccion, 0.4L);
    check_decimal("primer reinicio: cuota de mutacion 0.5",
                  primero.cuota_mutacion, 0.5L);

    // Cada reinicio consecutivo sin new_best sube 0.2.
    ParametrosReinicio segundo = escalador.consumir_siguiente(config);
    check_decimal("segundo reinicio: fraccion 0.6", segundo.fraccion, 0.6L);
    check_decimal("segundo reinicio: cuota de mutacion 0.5",
                  segundo.cuota_mutacion, 0.5L);

    // Tercer reinicio: tope cap, pero aun no es refinamiento-only.
    ParametrosReinicio tercero = escalador.consumir_siguiente(config);
    check_decimal("tercer reinicio: fraccion 0.8", tercero.fraccion, 0.8L);
    check_decimal("tercer reinicio: cuota de mutacion 0.5",
                  tercero.cuota_mutacion, 0.5L);

    // Tras ODE_RESTART_CAP=3 reinicios sin mejora: solo refinamiento.
    ParametrosReinicio refinamiento = escalador.consumir_siguiente(config);
    check_decimal("cuarto reinicio: fraccion de refinamiento 0.2",
                  refinamiento.fraccion, 0.2L);
    check_decimal("cuarto reinicio: toda la replacement es mutacion de elites",
                  refinamiento.cuota_mutacion, 1.0L);

    // La degradacion se mantiene mientras no haya un new_best.
    ParametrosReinicio siguiente = escalador.consumir_siguiente(config);
    check_decimal("quinto reinicio sigue en refinamiento (0.2)",
                  siguiente.fraccion, 0.2L);
    check_decimal("quinto reinicio sigue con cuota de mutacion 1.0",
                  siguiente.cuota_mutacion, 1.0L);
}

// -------------------------------------------------------------------------
// Seam: el escalado respeta ODE_RESTART_MIN_FRACTION, el tope
// ODE_RESTART_FRACTION, ODE_RESTART_CAP y ODE_RESTART_MUTATION_SHARE.
// Atrapa: valores de configuracion ignorados o formula hardcodeada.
// -------------------------------------------------------------------------
static void test_escalado_respeta_la_configuracion() {
    ConfigReinicio config;
    config.restart_min_fraction = 0.6L;
    config.restart_fraction = 0.7L;  // tope: 0.6 + 0.2 ya lo supera
    config.restart_cap = 3;
    config.restart_mutation_share = 0.75L;

    EscaladorReinicio escalador;

    ParametrosReinicio primero = escalador.consumir_siguiente(config);
    check_decimal("min fraction configurada: 0.6", primero.fraccion, 0.6L);
    check_decimal("mutation share configurado: 0.75",
                  primero.cuota_mutacion, 0.75L);

    // 0.6 + 0.2 = 0.8, pero el tope ODE_RESTART_FRACTION=0.7 lo recorta.
    ParametrosReinicio segundo = escalador.consumir_siguiente(config);
    check_decimal("la fraccion se recorta al tope configurado (0.7)",
                  segundo.fraccion, 0.7L);
    check_decimal("el share se conserva bajo el tope (0.75)",
                  segundo.cuota_mutacion, 0.75L);

    ParametrosReinicio tercero = escalador.consumir_siguiente(config);
    check_decimal("el tope se mantiene en el tercer reinicio (0.7)",
                  tercero.fraccion, 0.7L);
    check_decimal("el tercer reinicio aun no es refinamiento (share 0.75)",
                  tercero.cuota_mutacion, 0.75L);

    ParametrosReinicio refinamiento = escalador.consumir_siguiente(config);
    check_decimal("el cap configurado dispara el refinamiento (0.2)",
                  refinamiento.fraccion, 0.2L);
    check_decimal("el refinamiento fuerza mutacion de elites (1.0)",
                  refinamiento.cuota_mutacion, 1.0L);
}

// -------------------------------------------------------------------------
// Seam: un new_best reinicia la escalera desde el primer reinicio suave.
// Atrapa: el contador de reinicios consecutivos no se resetea o queda a medias.
// -------------------------------------------------------------------------
static void test_nuevo_record_reinicia_la_escalera() {
    const ConfigReinicio config;
    EscaladorReinicio escalador;

    // Subimos dos escalones: 0.4 y 0.6.
    escalador.consumir_siguiente(config);
    ParametrosReinicio segundo = escalador.consumir_siguiente(config);
    check_decimal("antes del new_best la escalera va en 0.6",
                  segundo.fraccion, 0.6L);

    // Un new_best corta el episodio: el siguiente reinicio vuelve al minimo.
    escalador.nuevo_record();
    ParametrosReinicio tras_record = escalador.consumir_siguiente(config);
    check_decimal("tras new_best la fraccion vuelve a 0.4",
                  tras_record.fraccion, 0.4L);
    check_decimal("tras new_best la cuota vuelve a 0.5",
                  tras_record.cuota_mutacion, 0.5L);

    // Tambien se sale del refinamiento-only con un new_best.
    escalador.consumir_siguiente(config);  // 0.6
    escalador.consumir_siguiente(config);  // 0.8
    ParametrosReinicio refinamiento = escalador.consumir_siguiente(config);
    check_decimal("la escalera llego a refinamiento (0.2)",
                  refinamiento.fraccion, 0.2L);

    escalador.nuevo_record();
    ParametrosReinicio reinicio = escalador.consumir_siguiente(config);
    check_decimal("new_best saca del refinamiento y vuelve a 0.4",
                  reinicio.fraccion, 0.4L);
    check_decimal("new_best restaura la cuota hibrida (0.5)",
                  reinicio.cuota_mutacion, 0.5L);
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
