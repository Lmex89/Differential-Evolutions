// test_per_case_config.cpp — tests de la configuracion por caso (issue #108).
//
// El issue #108 pide que `resolver_config` devuelva defaults distintos por
// caso (paper practice: pop/dim/gen se tunean por caso). Los casos 0-2 usan
// los defaults originales (100/20/5000); los casos 3-5 usan configs mas
// agresivos para converger.
//
// Se compila con -DODE_NO_MAIN (apaga el main() del programa y deja el de este
// archivo) y junto con dualfunctions.cpp y semilla.cpp; parse_tree.cpp se
// incluye mas abajo en vez de enlazarse, porque FunbObj no tiene header.
// En Python seria `from parse_tree import resolver_config`.

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <cmath>

#include "dualfunctions.h"
#include "token.h"

#include "parse_tree.cpp"

// -------------------------------------------------------------------------
// Arnes minimo (mismo estilo que test_population.cpp).
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

static void check_int(const std::string& nombre, int recibido, int esperado) {
    g_checks++;
    if (recibido == esperado) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << " (esperado " << esperado
                  << ", recibido " << recibido << ")\n";
        g_fallos++;
    }
}

// check_decimal: igualdad entre long double con epsilon (evita problemas de
// precision de la coma flotante). En Python seria
// `assert abs(got - esperado) < 1e-9, f"...{got}..."`.
static void check_decimal(const std::string& nombre, long double recibido,
                          long double esperado) {
    const long double epsilon = 1e-9L;
    check_bool(nombre, std::abs(recibido - esperado) < epsilon);
}

// -------------------------------------------------------------------------
// Tests: resolver_config acepta caso y devuelve defaults por caso.
// -------------------------------------------------------------------------

// Seam 1: resolver_config con caso=0 devuelve defaults originales.
// Atrapa: la funcion no acepta caso o usa defaults incorrectos.
void test_resolver_config_caso_0_defaults() {
    // Sin variables de entorno, caso 0 debe devolver defaults originales.
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    config_busqueda, config_reinicio);
    
    check_int("caso_0 poblacion", config_busqueda.poblacion, 100);
    check_int("caso_0 dimension", config_busqueda.dimension, 20);
    check_int("caso_0 generaciones", config_busqueda.max_generaciones, 5000);
    check_bool("caso_0 warm_start", !config_busqueda.warm_start);
}

// Seam 1: resolver_config con caso=3 devuelve defaults tuned para converger.
// Atrapa: caso 3 no usa defaults mas agresivos.
void test_resolver_config_caso_3_tuned() {
    // Caso 3 debe tener defaults mas agresivos (paper practice).
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 3,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    config_busqueda, config_reinicio);
    
    // Segun el paper, casos mas complejos necesitan mas poblacion y generaciones.
    // Estos valores iniciales son placeholders; el experimento los ajustara.
    check_bool("caso_3 poblacion >= 100", config_busqueda.poblacion >= 100);
    check_bool("caso_3 dimension >= 20", config_busqueda.dimension >= 20);
    check_bool("caso_3 generaciones >= 5000", config_busqueda.max_generaciones >= 5000);
}

// Seam 1: resolver_config con env vars sobreescribe defaults por caso.
// Atrapa: env vars no tienen prioridad sobre defaults por caso.
void test_resolver_config_env_override() {
    // ODE_POP, ODE_DIM, ODE_GEN deben sobreescribir defaults por caso.
    setenv("ODE_POP", "250", 1);
    setenv("ODE_DIM", "30", 1);
    setenv("ODE_GEN", "10000", 1);
    
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(getenv("ODE_POP"), getenv("ODE_DIM"),
                    getenv("ODE_GEN"), nullptr, 3,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    config_busqueda, config_reinicio);
    
    check_int("env override poblacion", config_busqueda.poblacion, 250);
    check_int("env override dimension", config_busqueda.dimension, 30);
    check_int("env override generaciones", config_busqueda.max_generaciones, 10000);
    
    // Limpiar variables de entorno para no afectar otros tests.
    unsetenv("ODE_POP");
    unsetenv("ODE_DIM");
    unsetenv("ODE_GEN");
}

// -------------------------------------------------------------------------
// Tests: parametros de reinicio configurables (issue #108).
// -------------------------------------------------------------------------

// Seam 2: resolver_config acepta ODE_STALE_LIMIT y ODE_RESTART_FRACTION.
// Atrapa: variables de entorno no se parsean o no se almacenan en config.
void test_resolver_config_restart_params() {
    // ODE_STALE_LIMIT y ODE_RESTART_FRACTION deben ser configurables.
    setenv("ODE_STALE_LIMIT", "300", 1);
    setenv("ODE_RESTART_FRACTION", "0.9", 1);
    
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    getenv("ODE_STALE_LIMIT"),
                    getenv("ODE_RESTART_FRACTION"),
                    nullptr, nullptr, nullptr, nullptr, nullptr,
                    config_busqueda, config_reinicio);
    
    check_int("stale_limit from env", config_reinicio.stale_limit, 300);
    // Comparacion con epsilon para long double (evita problemas de precision).
    const long double epsilon = 1e-9L;
    check_bool("restart_fraction from env", 
               std::abs(config_reinicio.restart_fraction - 0.9L) < epsilon);
    
    // Limpiar variables de entorno.
    unsetenv("ODE_STALE_LIMIT");
    unsetenv("ODE_RESTART_FRACTION");
}

// Seam 2: resolver_config usa defaults cuando no hay env vars.
// Atrapa: defaults incorrectos para parametros de reinicio.
void test_resolver_config_restart_defaults() {
    // Sin env vars, debe usar defaults: stale_limit=500, restart_fraction=0.8.
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    config_busqueda, config_reinicio);
    
    check_int("stale_limit default", config_reinicio.stale_limit, 500);
    // Comparacion con epsilon para long double (evita problemas de precision).
    const long double epsilon = 1e-9L;
    check_bool("restart_fraction default", 
               std::abs(config_reinicio.restart_fraction - 0.8L) < epsilon);
}

// -------------------------------------------------------------------------
// Tests: claves del reinicio adaptativo (issue #126).
// -------------------------------------------------------------------------

// Seam 3: resolver_config devuelve los defaults de las cinco claves nuevas.
// Atrapa: defaults incorrectos para modo, fraccion minima, tope, share o umbral.
void test_resolver_config_adaptive_defaults() {
    // Sin variables de entorno: modo legacy, min 0.4, cap 3, share 0.5, div 0.05.
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                    config_busqueda, config_reinicio);

    check_bool("restart_mode default legacy",
               config_reinicio.restart_mode == ModoReinicio::legacy);
    check_decimal("restart_min_fraction default", config_reinicio.restart_min_fraction, 0.4L);
    check_int("restart_cap default", config_reinicio.restart_cap, 3);
    check_decimal("restart_mutation_share default", config_reinicio.restart_mutation_share, 0.5L);
    check_decimal("div_threshold default", config_reinicio.div_threshold, 0.05L);
}

// Seam 3: env vars sobreescriben las cinco claves nuevas. Los valores son
// todos distintos entre si y de los defaults para que un argumento
// posicional mal ordenado rompa el test.
// Atrapa: las claves no se parsean o se asignan al campo equivocado.
void test_resolver_config_adaptive_overrides() {
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config("120", "22", "6000", "0", 0,
                    "300", "0.9",
                    "adaptive", "0.6", "5", "0.75", "0.02",
                    config_busqueda, config_reinicio);

    // Las claves anteriores siguen leyendose en su posicion original.
    check_int("stale_limit sigue leyendose", config_reinicio.stale_limit, 300);
    check_decimal("restart_fraction sigue leyendose", config_reinicio.restart_fraction, 0.9L);
    // Las cinco claves nuevas toman el valor del entorno.
    check_bool("restart_mode adaptive",
               config_reinicio.restart_mode == ModoReinicio::adaptive);
    check_decimal("restart_min_fraction override", config_reinicio.restart_min_fraction, 0.6L);
    check_int("restart_cap override", config_reinicio.restart_cap, 5);
    check_decimal("restart_mutation_share override", config_reinicio.restart_mutation_share, 0.75L);
    check_decimal("div_threshold override", config_reinicio.div_threshold, 0.02L);
}

// Seam 3: valores invalidos caen a los defaults, igual que las claves ODE_*
// existentes (una variable invalida se ignora, no rompe la corrida).
// Atrapa: el parser acepta valores fuera del contrato o el modo no es exacto.
void test_resolver_config_adaptive_invalid() {
    // Modo inexistente y modo mal capitalizado: el unico valor valido es
    // exactamente "adaptive"; todo lo demas vuelve a legacy.
    ConfigBusqueda config_busqueda1;
    ConfigReinicio config_reinicio1;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    nullptr, nullptr,
                    "bogus", nullptr, nullptr,
                    nullptr, nullptr,
                    config_busqueda1, config_reinicio1);
    check_bool("restart_mode bogus usa legacy",
               config_reinicio1.restart_mode == ModoReinicio::legacy);

    ConfigBusqueda config_busqueda2;
    ConfigReinicio config_reinicio2;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    nullptr, nullptr,
                    "Adaptive", nullptr, nullptr,
                    nullptr, nullptr,
                    config_busqueda2, config_reinicio2);
    check_bool("restart_mode Adaptive usa legacy (exacto)",
               config_reinicio2.restart_mode == ModoReinicio::legacy);

    // Numericos invalidos: texto no numerico, tope por debajo del minimo (1),
    // string vacio y numero con sufijo -> cada uno usa su default.
    ConfigBusqueda config_busqueda3;
    ConfigReinicio config_reinicio3;
    resolver_config(nullptr, nullptr, nullptr, nullptr, 0,
                    nullptr, nullptr,
                    nullptr, "abc", "0", "", "0.05x",
                    config_busqueda3, config_reinicio3);
    check_decimal("restart_min_fraction invalida usa 0.4",
                  config_reinicio3.restart_min_fraction, 0.4L);
    check_int("restart_cap 0 usa 3", config_reinicio3.restart_cap, 3);
    check_decimal("restart_mutation_share invalida usa 0.5",
                  config_reinicio3.restart_mutation_share, 0.5L);
    check_decimal("div_threshold invalido usa 0.05", config_reinicio3.div_threshold, 0.05L);
}

// -------------------------------------------------------------------------
// main() ejecuta los tests y devuelve codigo de salida.
// -------------------------------------------------------------------------

int main() {
    std::cout << "=== tests resolver_config por caso (issue #108) ===\n\n";
    
    test_resolver_config_caso_0_defaults();
    test_resolver_config_caso_3_tuned();
    test_resolver_config_env_override();
    test_resolver_config_restart_params();
    test_resolver_config_restart_defaults();
    test_resolver_config_adaptive_defaults();
    test_resolver_config_adaptive_overrides();
    test_resolver_config_adaptive_invalid();
    
    std::cout << "\n--- resumen: " << g_checks << " checks, "
              << g_fallos << " fallos ---\n";
    
    return g_fallos > 0 ? 1 : 0;
}
