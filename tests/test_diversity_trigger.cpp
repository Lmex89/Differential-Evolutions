// test_diversity_trigger.cpp — tests del disparador por diversidad (issues #129 y #167).
//
// El issue #129 pide detectar colapso de diversidad en modo adaptive: se
// calcula el coeficiente de variacion (std/mean) sobre las fitness FINITAS y,
// si al menos 25% de la poblacion es finita, existe un record finito y el CV
// cae por debajo de ODE_DIV_THRESHOLD, se reinicia por el mismo canal que el
// estancamiento.
//
// Se compila con el modulo restart_lifecycle.cpp; el test cruza la costura del
// modulo sin incluir parse_tree.cpp. En Python seria
// `from restart_lifecycle import ResumenDiversidad, resumir_diversidad`.

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
// Seam: el resumen de diversidad sobre un vector de fitness conocido.
// Atrapa: contar individuos no finitos o calcular el CV con otra formula
// (desviacion muestral, media de los cuadrados, etc.).
// -------------------------------------------------------------------------
static void test_cv_sobre_finitos() {
    // Serie clasica de media 5 y desviacion poblacional 2: CV = 2/5 = 0.4.
    // El valor esperado es un literal independiente de la implementacion.
    const std::vector<ldouble> fitnesses = {2.0L, 4.0L, 4.0L, 4.0L,
                                            5.0L, 5.0L, 7.0L, 9.0L};

    const ResumenDiversidad resumen = resumir_diversidad(fitnesses);

    check_int("cuenta los ocho individuos finitos", resumen.individuos_finitos, 8);
    check_decimal("CV de media 5 y desviacion 2 es 0.4",
                  resumen.coeficiente_variacion, 0.4L);

    // La misma serie con INFINITY y NaN intercalados debe dar el mismo
    // resumen: los no finitos son arboles invalidos, no diversidad real.
    const std::vector<ldouble> con_no_finitos = {2.0L, INFINITY, 4.0L, 4.0L, NAN,
                                                 4.0L, 5.0L, 5.0L, 7.0L, 9.0L};
    const ResumenDiversidad filtrado = resumir_diversidad(con_no_finitos);

    check_int("INFINITY y NaN no cuentan como finitos",
              filtrado.individuos_finitos, 8);
    check_decimal("INFINITY y NaN no alteran el CV",
                  filtrado.coeficiente_variacion, 0.4L);
}

// -------------------------------------------------------------------------
// Seam: la guarda de proporcion finita. El issue exige al menos 25% de la
// poblacion con fitness finita; por debajo no se dispara aunque el CV sea 0.
// Atrapa: usar el total de la poblacion como denominador del CV (INFINITY
// contagiaria la media) o disparar con muy pocos individuos evaluables.
// -------------------------------------------------------------------------
static void test_guarda_de_finitos() {
    // 1 de 5 = 20%: por debajo del minimo, no dispara.
    const ResumenDiversidad veinte_por_ciento =
        resumir_diversidad({1.0L, INFINITY, INFINITY, INFINITY, INFINITY});
    check_bool("20% de finitos no dispara",
               !dispara_reinicio_por_diversidad(veinte_por_ciento, true, 0.05L));

    // 1 de 4 = 25% exacto: el limite es inclusivo, si dispara.
    const ResumenDiversidad veinticinco_por_ciento =
        resumir_diversidad({1.0L, INFINITY, INFINITY, INFINITY});
    check_bool("25% de finitos dispara (limite inclusivo)",
               dispara_reinicio_por_diversidad(veinticinco_por_ciento, true, 0.05L));

    // 2 de 4 = 50%: sobra muestra, con CV 0 dispara.
    const ResumenDiversidad cincuenta_por_ciento =
        resumir_diversidad({1.0L, 1.0L, INFINITY, INFINITY});
    check_bool("50% de finitos dispara",
               dispara_reinicio_por_diversidad(cincuenta_por_ciento, true, 0.05L));

    // Sin ningun finito no hay CV ni record posible: no dispara.
    const ResumenDiversidad sin_finitos =
        resumir_diversidad({INFINITY, INFINITY, INFINITY, INFINITY});
    check_bool("0% de finitos no dispara",
               !dispara_reinicio_por_diversidad(sin_finitos, false, 0.05L));
}

// -------------------------------------------------------------------------
// Seam: la guarda de record finito. Sin un `new_best` finito no hay solucion
// que el reinicio pueda preservar; el CV solo (aunque sea 0) no basta.
// Atrapa: reiniciar una corrida que todavia no encontro ningun individuo
// evaluable, perdiendo la unica ventana de exploracion inicial.
// -------------------------------------------------------------------------
static void test_guarda_de_record() {
    // Cuatro individuos identicos: CV 0, la diversidad ya colapso.
    const ResumenDiversidad colapso = resumir_diversidad({1.0L, 1.0L, 1.0L, 1.0L});

    check_bool("sin record finito no dispara",
               !dispara_reinicio_por_diversidad(colapso, false, 0.05L));
    check_bool("con record finito dispara",
               dispara_reinicio_por_diversidad(colapso, true, 0.05L));
}

// -------------------------------------------------------------------------
// Seam: el umbral es estricto (`CV < ODE_DIV_THRESHOLD`). Un CV que apenas
// toca el umbral todavia no es colapso; el borde se prueba con una serie de
// CV exactamente 0.5 y con CV 0 frente a umbral 0.
// Atrapa: usar <=, que dispara en el limite y con umbral 0.
// -------------------------------------------------------------------------
static void test_umbral_estricto() {
    // {1, 3}: media 2, desviacion poblacional 1, CV exacto 0.5.
    const ResumenDiversidad cv_medio = resumir_diversidad({1.0L, 3.0L});

    check_bool("CV igual al umbral no dispara (comparacion estricta)",
               !dispara_reinicio_por_diversidad(cv_medio, true, 0.5L));
    check_bool("CV apenas debajo del umbral dispara",
               dispara_reinicio_por_diversidad(cv_medio, true, 0.500001L));

    const ResumenDiversidad cv_cero = resumir_diversidad({2.0L, 2.0L});
    check_bool("CV 0 con umbral 0 no dispara",
               !dispara_reinicio_por_diversidad(cv_cero, true, 0.0L));
    check_bool("CV 0 con umbral positivo dispara",
               dispara_reinicio_por_diversidad(cv_cero, true, 0.05L));
}

int main() {
    std::cout << "== test_diversity_trigger: issue #129 (disparador por diversidad) ==\n";
    test_cv_sobre_finitos();
    test_guarda_de_finitos();
    test_guarda_de_record();
    test_umbral_estricto();
    std::cout << (g_fallos == 0 ? "OK" : "FALLARON") << ": "
              << (g_checks - g_fallos) << "/" << g_checks << " asserts\n";
    return g_fallos == 0 ? 0 : 1;
}
