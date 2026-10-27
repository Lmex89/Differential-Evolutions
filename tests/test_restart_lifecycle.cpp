// test_restart_lifecycle.cpp — tests del ciclo de vida de reinicios (issues #139 y #167).
//
// El issue #138 definio la costura entre el bucle DE y la decision de reinicio:
// ControlReinicio recibe la configuracion inmutable una sola vez y, por
// generacion, recibe los hechos ya calculados (si hubo `new_best` y el resumen
// de diversidad) y devuelve un valor de decision. El ciclo completo (issue
// #139) cubre el estancamiento, la diversidad (solo adaptive), la prioridad de
// stale, el reset por `new_best`, la supresion legacy y el refinamiento tras
// el cap.
//
// Se compila con el modulo restart_lifecycle.cpp; el test cruza la costura del
// modulo sin incluir parse_tree.cpp. En Python seria
// `from restart_lifecycle import ControlReinicio, DecisionReinicio`.

#include <iostream>
#include <string>
#include <cmath>

#include "restart_lifecycle.h"

// -------------------------------------------------------------------------
// Arnes minimo (mismo estilo que test_restart_escalation.cpp).
// -------------------------------------------------------------------------

static int g_checks = 0;
static int g_fallos = 0;

static void check_bool(const std::string& nombre, bool cond) {
    g_checks++;
    if (cond) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "\n";
        g_fallos++;
    }
}

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

// resumen_colapsado: resumen de diversidad con CV bajo y suficientes finitos
// (8 de 10 = 80%), el caso que dispara el canal de diversidad. En Python seria
// un fixture `resumen_colapsado()`.
static ResumenDiversidad resumen_colapsado() {
    return ResumenDiversidad{8, 10, 0.05L};
}

// -------------------------------------------------------------------------
// Seam: ControlReinicio::decidir() con la configuracion por defecto y una
// generacion sin `new_best` ni senal de diversidad.
// Atrapa: que el esqueleto reinicie en la primera generacion o que el valor
// de decision no distinga "no reinicio" de un reinicio real.
// -------------------------------------------------------------------------
static void test_primera_generacion_sin_reinicio() {
    const ConfigReinicio config;
    ControlReinicio control(config);

    // Primera generacion sin record nuevo y sin resumen de diversidad: en
    // Python seria `decision = control.decidir(False, ResumenDiversidad())`.
    const DecisionReinicio decision =
        control.decidir(false, ResumenDiversidad{});

    check_bool("primera generacion no reinicia", !decision.hay_reinicio());
    check_bool("primera generacion no reporta disparador",
               decision.disparador == DisparadorReinicio::ninguno);
}

// -------------------------------------------------------------------------
// Seam: ControlReinicio::decidir() rastrea generaciones sin mejora y NO
// reinicia mientras el contador esté por debajo del stale_limit configurado.
// Atrapa: reiniciar antes de tiempo o no llevar el contador correctamente.
// -------------------------------------------------------------------------
static void test_sin_reinicio_debajo_del_limite() {
    ConfigReinicio config;
    config.stale_limit = 5;  // Límite corto para probar el tracking
    ControlReinicio control(config);

    // 4 generaciones sin new_best y sin diversidad: aún no debe reiniciar
    for (int i = 0; i < 4; ++i) {
        const DecisionReinicio decision =
            control.decidir(false, ResumenDiversidad{});
        check_bool("generacion " + std::to_string(i + 1) +
                       " no reinicia (debajo del limite)",
                   !decision.hay_reinicio());
    }
}

// -------------------------------------------------------------------------
// Seam: en adaptive, por debajo del stale limit, el canal de diversidad se
// evalua pero sus guards pueden no pasar (pocos finitos, CV alto). Esa rama
// de la regla 1 no existe en legacy, donde la diversidad no se evalua.
// Atrapa: disparar diversidad sin cumplir los guards o saltarse el conteo.
// -------------------------------------------------------------------------
static void test_adaptive_bajo_limite_con_guardas_fallando() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 5;
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    // Hay record, pero los resumenes no cumplen los guards: 1/10 finitos
    // (10% < 25%) y CV alto (0.5 >= 0.1).
    control.decidir(true, ResumenDiversidad{});
    const ResumenDiversidad pocos_finitos{1, 10, 0.05L};
    const ResumenDiversidad cv_alto{10, 10, 0.5L};

    for (int i = 0; i < 4; ++i){
        const ResumenDiversidad& resumen = (i % 2 == 0) ? pocos_finitos : cv_alto;
        const DecisionReinicio decision = control.decidir(false, resumen);
        check_bool("adaptive bajo limite con guards fallando no reinicia " +
                       std::to_string(i + 1),
                   !decision.hay_reinicio());
    }

    // El contador no se toco: la quinta generacion sin mejora dispara stale.
    const DecisionReinicio quinta = control.decidir(false, pocos_finitos);
    check_bool("el conteo sigue vivo pese a evaluar diversidad",
               quinta.hay_reinicio() &&
                   quinta.disparador == DisparadorReinicio::stale);
}

// -------------------------------------------------------------------------
// Seam: ControlReinicio::decidir() dispara reinicio con trigger=stale cuando
// las generaciones sin mejora alcanzan el stale_limit configurado.
// Atrapa: no disparar en el límite, usar otro trigger, o no resetear el
// contador tras el reinicio.
// -------------------------------------------------------------------------
static void test_reinicio_en_el_limite_stale() {
    ConfigReinicio config;
    config.stale_limit = 5;
    ControlReinicio control(config);

    // 4 generaciones sin new_best: aún no reinicia
    for (int i = 0; i < 4; ++i) {
        control.decidir(false, ResumenDiversidad{});
    }

    // 5ta generación: dispara stale
    const DecisionReinicio decision =
        control.decidir(false, ResumenDiversidad{});
    check_bool("quinta generacion reinicia por stale", decision.hay_reinicio());
    check_bool("disparador es stale",
               decision.disparador == DisparadorReinicio::stale);

    // Tras el reinicio, el contador se resetea: las siguientes 4 no reinician
    for (int i = 0; i < 4; ++i) {
        const DecisionReinicio siguiente =
            control.decidir(false, ResumenDiversidad{});
        check_bool("generacion post-reinicio " + std::to_string(i + 1) +
                       " no reinicia (contador reseteado)",
                   !siguiente.hay_reinicio());
    }
}

// -------------------------------------------------------------------------
// Seam: ControlReinicio::decidir() con nuevo_record=true debe resetear tanto
// la ventana de estancamiento (generaciones_sin_mejora_) como la escalera del
// escalador (reinicios_consecutivos_), para que el próximo reinicio vuelva a
// fraccion mínima (0.4) en lugar de continuar escalando.
// Atrapa: que nuevo_record solo resetee una de las dos cosas, o ninguna.
// -------------------------------------------------------------------------
static void test_nuevo_record_reinicia_ventana_y_escalera() {
    ConfigReinicio config;
    // La escalera solo existe en adaptive; legacy usa restart_fraction fija.
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 3;
    config.restart_min_fraction = 0.4L;
    config.restart_fraction = 0.8L;
    config.restart_mutation_share = 0.5L;
    config.restart_cap = 3;
    ControlReinicio control(config);

    // Primer reinicio: fraccion 0.4 (primer escalon)
    for (int i = 0; i < 2; ++i) control.decidir(false, ResumenDiversidad{});
    DecisionReinicio primero = control.decidir(false, ResumenDiversidad{});
    check_bool("primer reinicio stale dispara", primero.hay_reinicio());
    check_decimal("primer reinicio: fraccion 0.4", primero.parametros.fraccion, 0.4L);

    // Segundo reinicio consecutivo sin new_best: fraccion 0.6 (segundo escalon)
    for (int i = 0; i < 2; ++i) control.decidir(false, ResumenDiversidad{});
    DecisionReinicio segundo = control.decidir(false, ResumenDiversidad{});
    check_bool("segundo reinicio stale dispara", segundo.hay_reinicio());
    check_decimal("segundo reinicio: fraccion 0.6 (escalada)", segundo.parametros.fraccion, 0.6L);

    // Ahora llega un new_best: resetea escalera y ventana
    control.decidir(true, ResumenDiversidad{});

    // Tercer reinicio tras new_best: debe volver a 0.4 (no 0.8)
    for (int i = 0; i < 2; ++i) control.decidir(false, ResumenDiversidad{});
    DecisionReinicio tras_record = control.decidir(false, ResumenDiversidad{});
    check_bool("tercer reinicio tras new_best dispara", tras_record.hay_reinicio());
    check_decimal("tras new_best la fraccion vuelve a 0.4",
                  tras_record.parametros.fraccion, 0.4L);
    check_decimal("tras new_best la cuota vuelve a 0.5",
                  tras_record.parametros.cuota_mutacion, 0.5L);
}

// -------------------------------------------------------------------------
// Seam: En modo adaptive, si hay record finito y el resumen de diversidad
// muestra colapso (CV bajo, suficientes individuos finitos), dispara reinicio
// por diversidad ANTES de llegar al stale limit.
// Atrapa: que no se evalúe diversidad, que se dispare stale en vez de diversity,
// o que se dispare sin haber tenido un new_best previo.
// -------------------------------------------------------------------------
static void test_diversidad_dispara_en_modo_adaptive() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 10;  // Lo suficientemente alto para que no dispar stale
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    // Primero necesitamos un record finito (new_best)
    control.decidir(true, ResumenDiversidad{});

    // Ahora simulamos una generación con diversidad colapsada:
    // CV bajo (0.05 < 0.1), suficientes individuos finitos (8 de 10 = 80%)
    const DecisionReinicio decision =
        control.decidir(false, resumen_colapsado());

    check_bool("diversidad dispara reinicio en modo adaptive", decision.hay_reinicio());
    check_bool("disparador es diversity", decision.disparador == DisparadorReinicio::diversity);
}

// -------------------------------------------------------------------------
// Seam: un reinicio por diversidad resetea la ventana de estancamiento, igual
// que el stale. Sin el reset, el contador seguiria creciendo y el canal stale
// dispararia antes de tiempo.
// Atrapa: no resetear `generaciones_sin_mejora_` tras diversity.
// -------------------------------------------------------------------------
static void test_diversidad_resetea_estancamiento() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 10;
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    control.decidir(true, ResumenDiversidad{});  // hay record
    for (int i = 0; i < 4; ++i){
        control.decidir(false, ResumenDiversidad{});  // ventana = 4
    }

    const DecisionReinicio diversidad =
        control.decidir(false, resumen_colapsado());
    check_bool("diversidad dispara antes del limite",
               diversidad.hay_reinicio() &&
                   diversidad.disparador == DisparadorReinicio::diversity);

    // Ventana reseteada: cinco generaciones sin mejora quedan en 5, por
    // debajo del limite 10. Sin el reset, la quinta seria la decima y
    // dispararia stale.
    for (int i = 0; i < 5; ++i){
        const DecisionReinicio decision =
            control.decidir(false, ResumenDiversidad{});
        check_bool("post-diversidad " + std::to_string(i + 1) +
                       " no reinicia (ventana reseteada)",
                   !decision.hay_reinicio());
    }

    // Cuatro mas (9) siguen sin disparar; la decima desde el reset si.
    for (int i = 0; i < 4; ++i){
        control.decidir(false, ResumenDiversidad{});
    }
    const DecisionReinicio stale = control.decidir(false, ResumenDiversidad{});
    check_bool("la ventana reseteada vuelve a disparar stale en el limite",
               stale.hay_reinicio() &&
                   stale.disparador == DisparadorReinicio::stale);
}

// -------------------------------------------------------------------------
// Seam: En modo legacy, nunca se evalúa diversidad aunque las condiciones
// estén dadas. Solo stale puede disparar reinicio.
// Atrapa: que se dispare diversidad en modo legacy.
// -------------------------------------------------------------------------
static void test_legacy_suprime_diversidad() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::legacy;
    config.stale_limit = 10;
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    // Tenemos record y diversidad colapsada
    control.decidir(true, ResumenDiversidad{});

    const DecisionReinicio decision =
        control.decidir(false, resumen_colapsado());

    check_bool("legacy no dispara con diversidad colapsada", !decision.hay_reinicio());
}

// -------------------------------------------------------------------------
// Seam: en legacy el reinicio usa `restart_fraction` de la configuracion y no
// consume el escalador; por eso no escala ni alcanza refinamiento. Asi el
// ciclo de vida coincide con el reinicio historico del bucle DE.
// Atrapa: que la decision legacy consuma el escalador y devuelva 0.4/0.2.
// -------------------------------------------------------------------------
static void test_legacy_no_consume_el_escalador() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::legacy;
    config.stale_limit = 2;
    config.restart_fraction = 0.8L;
    config.restart_min_fraction = 0.4L;
    config.restart_mutation_share = 0.5L;
    config.restart_cap = 3;
    ControlReinicio control(config);

    // Cuatro reinicios legacy consecutivos: si consumiera el escalador, a
    // partir del cuarto devolveria refinamiento (0.2). Legacy siempre usa la
    // fraccion configurada.
    for (int reinicio = 0; reinicio < 4; ++reinicio){
        control.decidir(false, ResumenDiversidad{});
        const DecisionReinicio decision =
            control.decidir(false, ResumenDiversidad{});
        check_bool("legacy reinicia por stale " + std::to_string(reinicio + 1),
                   decision.hay_reinicio() &&
                       decision.disparador == DisparadorReinicio::stale);
        check_decimal("legacy conserva la fraccion configurada " +
                          std::to_string(reinicio + 1),
                      decision.parametros.fraccion, 0.8L);
    }
}

// -------------------------------------------------------------------------
// Seam: Cuando ambas condiciones (stale y diversity) se cumplen en la misma
// generación, stale tiene prioridad sobre diversity.
// Atrapa: que se dispare diversity cuando stale también debería disparar.
// -------------------------------------------------------------------------
static void test_stale_tiene_prioridad_sobre_diversity() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 3;
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    // Tenemos record
    control.decidir(true, ResumenDiversidad{});

    // Llamamos decidir 2 veces sin new_best (stale counter = 2)
    control.decidir(false, ResumenDiversidad{});
    control.decidir(false, ResumenDiversidad{});

    // En la tercera llamada, stale_limit se alcanza (counter = 3)
    // Y además la diversidad está colapsada
    const DecisionReinicio decision =
        control.decidir(false, resumen_colapsado());

    check_bool("stale dispara cuando ambos condiciones se cumplen", decision.hay_reinicio());
    check_bool("stale tiene prioridad sobre diversity",
               decision.disparador == DisparadorReinicio::stale);
}

// -------------------------------------------------------------------------
// Seam: Después de `cap` reinicios consecutivos sin `new_best`, los parámetros
// deben ser de refinamiento (fraction=0.2, mutation_share=1.0).
// Atrapa: que no se escalone a refinamiento, o que los valores sean incorrectos.
// -------------------------------------------------------------------------
static void test_refinamiento_despues_del_cap() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 2;
    config.restart_min_fraction = 0.4L;
    config.restart_fraction = 0.8L;
    config.restart_mutation_share = 0.5L;
    config.restart_cap = 3;
    ControlReinicio control(config);

    // Primer reinicio: fracción mínima 0.4. La primera llamada solo avanza el
    // contador; la segunda es la que dispara.
    control.decidir(false, ResumenDiversidad{});
    const DecisionReinicio r1 = control.decidir(false, ResumenDiversidad{});
    check_bool("primer reinicio dispara", r1.hay_reinicio());
    check_decimal("primer reinicio: fraccion 0.4", r1.parametros.fraccion, 0.4L);

    // Segundo reinicio: 0.6
    control.decidir(false, ResumenDiversidad{});
    const DecisionReinicio r2 = control.decidir(false, ResumenDiversidad{});
    check_bool("segundo reinicio dispara", r2.hay_reinicio());
    check_decimal("segundo reinicio: fraccion 0.6", r2.parametros.fraccion, 0.6L);

    // Tercer reinicio: 0.8
    control.decidir(false, ResumenDiversidad{});
    const DecisionReinicio r3 = control.decidir(false, ResumenDiversidad{});
    check_bool("tercer reinicio dispara", r3.hay_reinicio());
    check_decimal("tercer reinicio: fraccion 0.8", r3.parametros.fraccion, 0.8L);

    // Cuarto reinicio (después del cap=3): refinamiento
    control.decidir(false, ResumenDiversidad{});
    const DecisionReinicio r4 = control.decidir(false, ResumenDiversidad{});
    check_bool("cuarto reinicio dispara", r4.hay_reinicio());
    check_decimal("cuarto reinicio: fraccion refinamiento 0.2", r4.parametros.fraccion, 0.2L);
    check_decimal("cuarto reinicio: mutation_share 1.0", r4.parametros.cuota_mutacion, 1.0L);
}

int main() {
    std::cout << "== test_restart_lifecycle: issue #139 (ciclo de vida de ControlReinicio) ==\n";
    test_primera_generacion_sin_reinicio();
    test_sin_reinicio_debajo_del_limite();
    test_adaptive_bajo_limite_con_guardas_fallando();
    test_reinicio_en_el_limite_stale();
    test_nuevo_record_reinicia_ventana_y_escalera();
    test_diversidad_dispara_en_modo_adaptive();
    test_diversidad_resetea_estancamiento();
    test_legacy_suprime_diversidad();
    test_legacy_no_consume_el_escalador();
    test_stale_tiene_prioridad_sobre_diversity();
    test_refinamiento_despues_del_cap();
    std::cout << (g_fallos == 0 ? "OK" : "FALLARON") << ": "
              << (g_checks - g_fallos) << "/" << g_checks << " asserts\n";
    return g_fallos == 0 ? 0 : 1;
}
