// test_restart_lifecycle.cpp — tests del ciclo de vida de reinicios (issues #139, #167 y #168).
//
// El issue #138 definio la costura entre el bucle DE y la decision de reinicio:
// ControlReinicio recibe la configuracion inmutable una sola vez y, por
// generacion, recibe la evidencia de la generacion (si hubo `new_best` y los
// fitness de la poblacion) y devuelve un valor de decision. Desde el issue
// #167 el modulo calcula el resumen de diversidad a partir de esa evidencia:
// el bucle DE ya no decide si la diversidad se evalua.
//
// El ciclo completo (issue #139) cubre el estancamiento, la diversidad (solo
// adaptive), la prioridad de stale, el reset por `new_best`, la supresion
// legacy y el refinamiento tras el cap.
//
// Desde el issue #168 la costura del modulo incluye tambien el reemplazo
// legacy: `reiniciar_poblacion` recibe la poblacion nativa y no la
// configuracion de busqueda (la dimension se derive de la poblacion).
//
// Se compila con el modulo restart_lifecycle.cpp; el test cruza la costura del
// modulo sin incluir parse_tree.cpp. En Python seria
// `from restart_lifecycle import ControlReinicio, reiniciar_poblacion`.

#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>

#include "restart_lifecycle.h"
#include "funbobj.h"   // FunbObj: los tests de reemplazo construyen individuos
#include "token.h"     // TOKEN_MUL/TOKEN_CONST*/TOKEN_X del fixture legacy

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

// check_int: igualdad exacta para conteos enteros (reemplazados/tamano),
// mismo estilo que test_diversity_trigger.cpp. En Python seria
// `assert recibido == esperado`.
static void check_int(const std::string& nombre, int recibido, int esperado) {
    g_checks++;
    if (recibido == esperado) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "  (got=" << recibido
                  << " esperado=" << esperado << ")\n";
        g_fallos++;
    }
}

// -------------------------------------------------------------------------
// Fixtures: la evidencia de una generacion es la lista de fitness de la
// poblacion (el modulo calcula el CV a partir de ella). En Python seria un
// fixture `poblacion_colapsada()`.
// -------------------------------------------------------------------------

// poblacion_colapsada: 8 de 10 individuos con el mismo fitness (CV = 0) y 2
// no evaluables. Es la senal de colapso que dispara el canal de diversidad.
static std::vector<ldouble> poblacion_colapsada() {
    return {1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L,
            INFINITY, INFINITY};
}

// poblacion_dispersa: 10 individuos finitos con CV ≈ 0.69, por encima del
// umbral que usan los tests: con esta evidencia el canal de diversidad no
// dispara aunque la configuracion sea la misma.
static std::vector<ldouble> poblacion_dispersa() {
    return {1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 1.0L, 4.0L};
}

// poblacion_pocos_finitos: solo 1 de 10 evaluable (10% < 25%); la guarda de
// proporcion finita bloquea el canal de diversidad.
static std::vector<ldouble> poblacion_pocos_finitos() {
    return {1.0L, INFINITY, INFINITY, INFINITY, INFINITY,
            INFINITY, INFINITY, INFINITY, INFINITY, INFINITY};
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

    // Primera generacion sin record nuevo y sin evidencia de diversidad: en
    // Python seria `decision = control.decidir(False, [])`.
    const DecisionReinicio decision = control.decidir(false, {});

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
        const DecisionReinicio decision = control.decidir(false, {});
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

    // Hay record, pero la evidencia no cumple los guards: 1/10 finitos
    // (10% < 25%) y CV alto (0.69 >= 0.1).
    control.decidir(true, {});
    const std::vector<ldouble> pocos_finitos = poblacion_pocos_finitos();
    const std::vector<ldouble> cv_alto = poblacion_dispersa();

    for (int i = 0; i < 4; ++i){
        const std::vector<ldouble>& evidencia =
            (i % 2 == 0) ? pocos_finitos : cv_alto;
        const DecisionReinicio decision = control.decidir(false, evidencia);
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
        control.decidir(false, {});
    }

    // 5ta generación: dispara stale
    const DecisionReinicio decision = control.decidir(false, {});
    check_bool("quinta generacion reinicia por stale", decision.hay_reinicio());
    check_bool("disparador es stale",
               decision.disparador == DisparadorReinicio::stale);

    // Tras el reinicio, el contador se resetea: las siguientes 4 no reinician
    for (int i = 0; i < 4; ++i) {
        const DecisionReinicio siguiente = control.decidir(false, {});
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
    for (int i = 0; i < 2; ++i) control.decidir(false, {});
    DecisionReinicio primero = control.decidir(false, {});
    check_bool("primer reinicio stale dispara", primero.hay_reinicio());
    check_decimal("primer reinicio: fraccion 0.4", primero.parametros.fraccion, 0.4L);

    // Segundo reinicio consecutivo sin new_best: fraccion 0.6 (segundo escalon)
    for (int i = 0; i < 2; ++i) control.decidir(false, {});
    DecisionReinicio segundo = control.decidir(false, {});
    check_bool("segundo reinicio stale dispara", segundo.hay_reinicio());
    check_decimal("segundo reinicio: fraccion 0.6 (escalada)", segundo.parametros.fraccion, 0.6L);

    // Ahora llega un new_best: resetea escalera y ventana
    control.decidir(true, {});

    // Tercer reinicio tras new_best: debe volver a 0.4 (no 0.8)
    for (int i = 0; i < 2; ++i) control.decidir(false, {});
    DecisionReinicio tras_record = control.decidir(false, {});
    check_bool("tercer reinicio tras new_best dispara", tras_record.hay_reinicio());
    check_decimal("tras new_best la fraccion vuelve a 0.4",
                  tras_record.parametros.fraccion, 0.4L);
    check_decimal("tras new_best la cuota vuelve a 0.5",
                  tras_record.parametros.cuota_mutacion, 0.5L);
}

// -------------------------------------------------------------------------
// Seam: En modo adaptive, si hay record finito y la evidencia de la
// generacion muestra colapso (CV bajo, suficientes individuos finitos),
// dispara reinicio por diversidad ANTES de llegar al stale limit.
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
    control.decidir(true, {});

    // Ahora simulamos una generación con diversidad colapsada:
    // CV bajo (0 < 0.1), suficientes individuos finitos (8 de 10 = 80%)
    const DecisionReinicio decision =
        control.decidir(false, poblacion_colapsada());

    check_bool("diversidad dispara reinicio en modo adaptive", decision.hay_reinicio());
    check_bool("disparador es diversity", decision.disparador == DisparadorReinicio::diversity);
}

// -------------------------------------------------------------------------
// Seam: el modulo calcula el resumen de diversidad a partir de la evidencia
// de la generacion: con la misma configuracion, una poblacion dispersa (CV
// ≈ 0.69) no dispara y una colapsada (CV = 0) si.
// Atrapa: que el CV se reciba ya calculado (el seam seria el resumen, no la
// evidencia) o que la decision ignore la evidencia de la poblacion.
// -------------------------------------------------------------------------
static void test_diversidad_calculada_desde_la_evidencia() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::adaptive;
    config.stale_limit = 10;
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    control.decidir(true, {});  // record finito

    const DecisionReinicio dispersa =
        control.decidir(false, poblacion_dispersa());
    check_bool("una poblacion dispersa no dispara diversidad",
               !dispersa.hay_reinicio());

    const DecisionReinicio colapsada =
        control.decidir(false, poblacion_colapsada());
    check_bool("la misma configuracion con la poblacion colapsada si dispara",
               colapsada.hay_reinicio() &&
                   colapsada.disparador == DisparadorReinicio::diversity);
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

    control.decidir(true, {});  // hay record
    for (int i = 0; i < 4; ++i){
        control.decidir(false, {});  // ventana = 4
    }

    const DecisionReinicio diversidad =
        control.decidir(false, poblacion_colapsada());
    check_bool("diversidad dispara antes del limite",
               diversidad.hay_reinicio() &&
                   diversidad.disparador == DisparadorReinicio::diversity);

    // Ventana reseteada: cinco generaciones sin mejora quedan en 5, por
    // debajo del limite 10. Sin el reset, la quinta seria la decima y
    // dispararia stale.
    for (int i = 0; i < 5; ++i){
        const DecisionReinicio decision = control.decidir(false, {});
        check_bool("post-diversidad " + std::to_string(i + 1) +
                       " no reinicia (ventana reseteada)",
                   !decision.hay_reinicio());
    }

    // Cuatro mas (9) siguen sin disparar; la decima desde el reset si.
    for (int i = 0; i < 4; ++i){
        control.decidir(false, {});
    }
    const DecisionReinicio stale = control.decidir(false, {});
    check_bool("la ventana reseteada vuelve a disparar stale en el limite",
               stale.hay_reinicio() &&
                   stale.disparador == DisparadorReinicio::stale);
}

// -------------------------------------------------------------------------
// Seam: En modo legacy, nunca se evalúa diversidad aunque la evidencia de la
// generacion este colapsada. Solo stale puede disparar reinicio.
// Atrapa: que se dispare diversidad en modo legacy.
// -------------------------------------------------------------------------
static void test_legacy_suprime_diversidad() {
    ConfigReinicio config;
    config.restart_mode = ModoReinicio::legacy;
    config.stale_limit = 10;
    config.div_threshold = 0.1L;
    ControlReinicio control(config);

    // Tenemos record y evidencia colapsada
    control.decidir(true, {});

    const DecisionReinicio decision =
        control.decidir(false, poblacion_colapsada());

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
        control.decidir(false, {});
        const DecisionReinicio decision = control.decidir(false, {});
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
    control.decidir(true, {});

    // Llamamos decidir 2 veces sin new_best (stale counter = 2)
    control.decidir(false, {});
    control.decidir(false, {});

    // En la tercera llamada, stale_limit se alcanza (counter = 3)
    // Y además la diversidad está colapsada
    const DecisionReinicio decision =
        control.decidir(false, poblacion_colapsada());

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
    control.decidir(false, {});
    const DecisionReinicio r1 = control.decidir(false, {});
    check_bool("primer reinicio dispara", r1.hay_reinicio());
    check_decimal("primer reinicio: fraccion 0.4", r1.parametros.fraccion, 0.4L);

    // Segundo reinicio: 0.6
    control.decidir(false, {});
    const DecisionReinicio r2 = control.decidir(false, {});
    check_bool("segundo reinicio dispara", r2.hay_reinicio());
    check_decimal("segundo reinicio: fraccion 0.6", r2.parametros.fraccion, 0.6L);

    // Tercer reinicio: 0.8
    control.decidir(false, {});
    const DecisionReinicio r3 = control.decidir(false, {});
    check_bool("tercer reinicio dispara", r3.hay_reinicio());
    check_decimal("tercer reinicio: fraccion 0.8", r3.parametros.fraccion, 0.8L);

    // Cuarto reinicio (después del cap=3): refinamiento
    control.decidir(false, {});
    const DecisionReinicio r4 = control.decidir(false, {});
    check_bool("cuarto reinicio dispara", r4.hay_reinicio());
    check_decimal("cuarto reinicio: fraccion refinamiento 0.2", r4.parametros.fraccion, 0.2L);
    check_decimal("cuarto reinicio: mutation_share 1.0", r4.parametros.cuota_mutacion, 1.0L);
}

// -------------------------------------------------------------------------
// Seam (issue #168): reemplazo legacy de la poblacion detras de la costura del
// modulo. `reiniciar_poblacion` recibe la poblacion nativa, el indice
// reservado, el record historico y la configuracion de reinicio; la dimension
// de los reemplazos se derive de la poblacion (no cruza config de busqueda).
// En Python seria
// `reemplazados = reiniciar_poblacion(poblacion, indice, record, config)`.
// -------------------------------------------------------------------------

// Individuos {TOKEN_MUL, k, TOKEN_X} (= k*x). Para k=1..5 su fitness en el
// caso 0 crece con k; la dimension 3 tambien comprueba que no se use el
// default de 20. En Python seria
// `[FunbObj([TOKEN_MUL, k, TOKEN_X], 0, 1, 0) for k in constantes]`.
static std::vector<FunbObj> poblacion_fixture(const std::vector<int>& constantes) {
    std::vector<FunbObj> poblacion;
    for (size_t i = 0; i < constantes.size(); i++) {
        poblacion.push_back(FunbObj(
            std::vector<ldouble>{static_cast<ldouble>(TOKEN_MUL),
                                 static_cast<ldouble>(constantes[i]),
                                 static_cast<ldouble>(TOKEN_X)},
            0, 1, 0));
    }
    return poblacion;
}

// Atrapa: que el reemplazo legacy no respete la fraccion configurada, que
// cambie el tamano de la poblacion o que use una dimension distinta a la de
// la poblacion que recibe.
static void test_reinicio_legacy_reemplaza_la_fraccion() {
    srand(20261013);

    // 15 individuos: con la fraccion por defecto 0.8, int(0.8*15) = 12, el
    // mismo criterio que la linea del log `replaced=12`.
    std::vector<FunbObj> poblacion = poblacion_fixture(
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15});

    std::vector<std::vector<ldouble>> antes;
    for (size_t i = 0; i < poblacion.size(); i++) {
        antes.push_back(poblacion[i].vecvalue());
    }

    const int indice_record = 3;
    const std::vector<ldouble> record = poblacion[indice_record].vecvalue();

    const ConfigReinicio config;  // restart_fraction = 0.8 por defecto
    const int reemplazados =
        reiniciar_poblacion(poblacion, indice_record, record, config, 0);

    check_int("legacy reemplaza el 80% de la poblacion (12 de 15)",
              reemplazados, 12);
    check_int("legacy conserva el tamano de la poblacion",
              static_cast<int>(poblacion.size()), 15);

    // Con el record igual al vector del indice reservado, solo los
    // reemplazados cambian de vector. srand fijo: el sorteo es determinista.
    int cambiados = 0;
    bool dimension_ok = true;
    for (size_t i = 0; i < poblacion.size(); i++) {
        if (poblacion[i].vecvalue() != antes[i]) cambiados++;
        if (poblacion[i].vecvalue().size() != antes[i].size())
            dimension_ok = false;
    }
    check_int("solo los reemplazados cambian de vector", cambiados,
              reemplazados);
    check_bool("los reemplazos conservan la dimension de la poblacion",
               dimension_ok);
}

// Atrapa: que el record historico no vuelva al indice reservado (elitismo).
static void test_reinicio_legacy_restaura_el_record() {
    srand(20261013);

    std::vector<FunbObj> poblacion =
        poblacion_fixture({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    // Record simulado: un vector fijo distinto del individuo que hoy ocupa el
    // indice reservado (en una corrida estancada el mejor de la generacion no
    // es el record, que suele vivir en otro indice).
    const std::vector<ldouble> record = {18, 13, 17, 11};
    const int indice_record = 4;
    check_bool("el record simulado no es el individuo del indice reservado",
               poblacion[indice_record].vecvalue() != record);

    const ConfigReinicio config;  // restart_fraction = 0.8 por defecto
    reiniciar_poblacion(poblacion, indice_record, record, config, 0);

    check_bool("el indice reservado contiene exactamente el record restaurado",
               poblacion[indice_record].vecvalue() == record);
}

// Atrapa: con un record vacio (la corrida nunca encontro fitness finito) no
// hay nada que restaurar, pero la fraccion de reemplazo sigue aplicandose y
// el indice reservado no se toca.
static void test_reinicio_legacy_sin_record_no_toca_el_indice_reservado() {
    srand(20261013);

    std::vector<FunbObj> poblacion =
        poblacion_fixture({1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

    const int indice_record = 5;
    const std::vector<ldouble> original = poblacion[indice_record].vecvalue();
    const std::vector<ldouble> sin_record;

    const ConfigReinicio config;  // restart_fraction = 0.8 por defecto
    const int reemplazados =
        reiniciar_poblacion(poblacion, indice_record, sin_record, config, 0);

    check_int("sin record legacy sigue reemplazando el 80% (8 de 10)",
              reemplazados, 8);
    check_bool("sin record el indice reservado queda intacto",
               poblacion[indice_record].vecvalue() == original);
}

static void test_reinicio_adaptativo_conserva_los_mejores() {
    srand(20261017);
    std::vector<FunbObj> poblacion = poblacion_fixture({3, 1, 5, 2, 4});
    const auto primero = poblacion[1].vecvalue();
    const auto segundo = poblacion[3].vecvalue();
    const auto tercero = poblacion[0].vecvalue();
    const auto cuarto = poblacion[4].vecvalue();
    const auto quinto = poblacion[2].vecvalue();

    const int reemplazados = reiniciar_poblacion_adaptativo(
        poblacion, primero, {0.4L, 0.5L}, 0);

    check_int("adaptive reemplaza 2 de 5", reemplazados, 2);
    check_int("adaptive conserva el tamano", static_cast<int>(poblacion.size()), 5);
    check_bool("adaptive conserva los 3 individuos mas aptos",
               poblacion[1].vecvalue() == primero &&
               poblacion[3].vecvalue() == segundo &&
               poblacion[0].vecvalue() == tercero);
    check_bool("adaptive reemplaza los dos peores y conserva su dimension",
               poblacion[4].vecvalue() != cuarto && poblacion[2].vecvalue() != quinto &&
               poblacion[4].vecvalue().size() == 3 && poblacion[2].vecvalue().size() == 3);
}

static void test_reinicio_adaptativo_inyecta_el_record() {
    srand(20261018);
    auto poblacion = poblacion_fixture({2, 1, 4, 3});
    const auto primero = poblacion[1].vecvalue();
    const auto segundo = poblacion[0].vecvalue();
    const auto peor = poblacion[2].vecvalue();
    const std::vector<ldouble> record = {TOKEN_ADD, TOKEN_CONST1, TOKEN_X};

    const int reemplazados = reiniciar_poblacion_adaptativo(
        poblacion, record, {0.5L, 0.5L}, 0);

    check_int("el record cuenta entre 2 reemplazos", reemplazados, 2);
    check_bool("la inyeccion conserva los dos mejores",
               poblacion[1].vecvalue() == primero && poblacion[0].vecvalue() == segundo);
    check_bool("el record ocupa un lugar reemplazado", poblacion[3].vecvalue() == record);
    check_bool("tambien se reemplaza el peor", poblacion[2].vecvalue() != peor);
}

static void test_reinicio_adaptativo_inyecta_el_record_sin_cupo() {
    auto poblacion = poblacion_fixture({2, 1});
    const auto mejor = poblacion[1].vecvalue();
    const std::vector<ldouble> record = {TOKEN_ADD, TOKEN_CONST1, TOKEN_X};

    const int reemplazados = reiniciar_poblacion_adaptativo(
        poblacion, record, {0.4L, 0.5L}, 0);

    check_int("sin cupo el record cuenta como un reemplazo", reemplazados, 1);
    check_bool("sin cupo el mejor se conserva y el record desplaza al peor",
               poblacion[1].vecvalue() == mejor && poblacion[0].vecvalue() == record);
}

static int distancia_hamming(const std::vector<ldouble>& a,
                            const std::vector<ldouble>& b) {
    if (a.size() != b.size()) return -1;
    int distancia = 0;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) distancia++;
    }
    return distancia;
}

static void test_reinicio_adaptativo_compone_reemplazos_hibridos() {
    struct Escenario {
        long double cuota;
        int mutaciones;
        bool inyectar_record;
    };
    const Escenario escenarios[] = {
        {0.0L, 0, false}, {0.5L, 2, false}, {1.0L, 4, false},
        {0.5L, 1, true},
    };

    for (const Escenario& escenario : escenarios) {
        srand(20261019);
        std::vector<FunbObj> poblacion;
        std::vector<std::vector<ldouble>> antes;
        for (int i = 0; i < 10; i++) {
            poblacion.push_back(FunbObj::create_random(20, 0));
            antes.push_back(poblacion.back().vecvalue());
        }
        std::vector<ldouble> record;
        if (escenario.inyectar_record) {
            record = {TOKEN_ADD, TOKEN_CONST1, TOKEN_X};
            record.resize(20, TOKEN_CONST1);
        }
        const int reemplazados = reiniciar_poblacion_adaptativo(
            poblacion, record, {0.4L, escenario.cuota}, 0);

        std::vector<int> sobrevivientes;
        std::vector<int> cambiados;
        for (int i = 0; i < 10; i++) {
            if (poblacion[i].vecvalue() == antes[i]) sobrevivientes.push_back(i);
            else cambiados.push_back(i);
        }

        // Con esta semilla, los individuos frescos distan al menos 3 tokens
        // de cada sobreviviente; las mutaciones distan 1-2.
        int mutaciones = 0;
        int frescos = 0;
        int records = 0;
        for (int indice : cambiados) {
            const auto vector = poblacion[indice].vecvalue();
            if (!record.empty() && vector == record) {
                records++;
                continue;
            }
            int minima = 21;
            for (int sobreviviente : sobrevivientes) {
                const int distancia = distancia_hamming(vector, poblacion[sobreviviente].vecvalue());
                if (distancia < minima) minima = distancia;
            }
            if (minima >= 1 && minima <= 2) mutaciones++;
            if (minima >= 3 && minima <= 20) frescos++;
        }
        check_int("adaptive reemplaza 4 de 10", reemplazados, 4);
        check_int("adaptive conserva 6 individuos", static_cast<int>(sobrevivientes.size()), 6);
        check_int("adaptive respeta la cuota de mutaciones", mutaciones, escenario.mutaciones);
        check_int("adaptive reserva el lugar del record", records, escenario.inyectar_record ? 1 : 0);
        check_int("adaptive completa con individuos frescos", frescos,
                  4 - escenario.mutaciones - records);
    }
}

static void test_reinicio_adaptativo_muta_uno_o_dos_tokens() {
    srand(20261020);
    std::vector<ldouble> elite(20, TOKEN_CONST1);
    elite[0] = TOKEN_X;
    std::vector<FunbObj> poblacion;
    poblacion.emplace_back(elite, 0, 1, 0);
    for (int i = 0; i < 40; i++) {
        poblacion.emplace_back(std::vector<ldouble>(20, TOKEN_CONST1), 0, 1, 0);
    }
    const int reemplazados = reiniciar_poblacion_adaptativo(
        poblacion, elite, {1.0L, 1.0L}, 0);
    int minimo = 20;
    int maximo = 0;
    for (size_t i = 1; i < poblacion.size(); i++) {
        const int distancia = distancia_hamming(elite, poblacion[i].vecvalue());
        if (distancia < minimo) minimo = distancia;
        if (distancia > maximo) maximo = distancia;
    }
    check_int("adaptive conserva una elite con fraccion 1", reemplazados, 40);
    check_int("las mutaciones cambian al menos un token", minimo, 1);
    check_int("las mutaciones cambian como maximo dos tokens", maximo, 2);

    std::vector<FunbObj> cortos = {
        FunbObj({TOKEN_X}, 0, 1, 0), FunbObj({TOKEN_CONST1}, 0, 1, 0)};
    reiniciar_poblacion_adaptativo(cortos, {TOKEN_X}, {1.0L, 1.0L}, 0);
    check_int("una elite de un token produce un cambio",
              distancia_hamming(cortos[0].vecvalue(), cortos[1].vecvalue()), 1);
}

int main() {
    std::cout << "== test_restart_lifecycle: issue #139 (ciclo de vida de ControlReinicio) ==\n";
    test_primera_generacion_sin_reinicio();
    test_sin_reinicio_debajo_del_limite();
    test_adaptive_bajo_limite_con_guardas_fallando();
    test_reinicio_en_el_limite_stale();
    test_nuevo_record_reinicia_ventana_y_escalera();
    test_diversidad_dispara_en_modo_adaptive();
    test_diversidad_calculada_desde_la_evidencia();
    test_diversidad_resetea_estancamiento();
    test_legacy_suprime_diversidad();
    test_legacy_no_consume_el_escalador();
    test_stale_tiene_prioridad_sobre_diversity();
    test_refinamiento_despues_del_cap();
    test_reinicio_legacy_reemplaza_la_fraccion();
    test_reinicio_legacy_restaura_el_record();
    test_reinicio_legacy_sin_record_no_toca_el_indice_reservado();
    test_reinicio_adaptativo_conserva_los_mejores();
    test_reinicio_adaptativo_inyecta_el_record();
    test_reinicio_adaptativo_inyecta_el_record_sin_cupo();
    test_reinicio_adaptativo_compone_reemplazos_hibridos();
    test_reinicio_adaptativo_muta_uno_o_dos_tokens();
    std::cout << (g_fallos == 0 ? "OK" : "FALLARON") << ": "
              << (g_checks - g_fallos) << "/" << g_checks << " asserts\n";
    return g_fallos == 0 ? 0 : 1;
}
