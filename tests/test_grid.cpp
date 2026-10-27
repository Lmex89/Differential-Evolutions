// test_grid.cpp — tests de la rejilla de evaluacion (issue #7) y del funcional
// de fitness del paper (issue #27).
//
// Para un desarrollador Python: este archivo es como un modulo `test_*.py` de
// pytest, pero sin framework: cada `test_*()` imprime PASS/FAIL y `main()` al
// final hace de resumen (seria el `pytest.main()` + exit code). El binario
// termina con codigo != 0 si algo fallo, igual que pytest.
//
// Enlaza funbobj.cpp y dualfunctions.cpp (issue #158): FunbObj y x_rango viven
// tras funbobj.h, asi que el test ya no compila el bucle DE ni necesita
// ODE_NO_MAIN.

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>

#include "dualfunctions.h"  // ahora autocontenido: incluye <vector>/<string> por si mismo

// Los dos modulos bajo prueba viven tras funbobj.h (issue #157): FunbObj y
// x_rango. Desde #158 el test enlaza funbobj.cpp y dualfunctions.cpp y ya no
// compila parse_tree.cpp, asi que no necesita ODE_NO_MAIN.
#include "funbobj.h"
// token.h es la fuente de verdad de TOKEN_X2/TOKEN_ADD/TOKEN_X3, que antes
// llegaban de rebote al incluir parse_tree.cpp.
#include "token.h"

// -------------------------------------------------------------------------
// Arnes minimo (mismo estilo que test_dual/test_mutation/test_crossover):
// contadores globales y helpers check_* que imprimen PASS/FAIL.
// -------------------------------------------------------------------------

static int g_checks = 0;   // total de asserts ejecutados
static int g_fallos = 0;   // asserts que fallaron

// check_int: como `assert got == esperado` en pytest, pero sin abortar.
static void check_int(const std::string& nombre, int got, int esperado) {
    g_checks++;
    if (got == esperado) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "  (got=" << got
                  << " esperado=" << esperado << ")\n";
        g_fallos++;
    }
}

// check_ld: compara long doubles con tolerancia, como
// `pytest.approx(esperado, abs=1e-9)`. long double no tiene equivalente en
// Python (es mas ancho que float); la tolerancia absorbe el redondeo binario.
static void check_ld(const std::string& nombre, long double got, long double esperado,
                     long double tol = 1e-9L) {
    g_checks++;
    bool ok = !std::isnan(got) && std::fabs(got - esperado) <= tol;
    if (ok) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "  (got=" << std::setprecision(17)
                  << static_cast<double>(got) << " esperado=" << static_cast<double>(esperado) << ")\n";
        g_fallos++;
    }
}

// check_bool: para invariantes que no son igualdades (`assert cond, mensaje`).
static void check_bool(const std::string& nombre, bool cond) {
    g_checks++;
    if (cond) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "\n";
        g_fallos++;
    }
}

// -------------------------------------------------------------------------
// Slice 1: x_rango() produce la rejilla de evaluacion del paper — 100 puntos
// igualmente espaciados en [0,1], con ambos extremos incluidos. La formula
// esperada es x_i = start + (stop-start)*i/(npoints-1), i = 0..npoints-1
// (que en Python seria `[start + (stop-start)*i/(npoints-1) for i in
// range(npoints)]`); el test calcula los valores por su cuenta, sin llamar a
// la implementacion para obtener el valor esperado.
// -------------------------------------------------------------------------

static void test_x_rango_cantidad_de_puntos() {
    // El tercer argumento es un conteo: con 100 se obtienen exactamente 100
    // puntos (no 101, como daria un paso fijo de 0.01 en un intervalo cerrado).
    const std::vector<std::vector<long double>> pts100 = x_rango(0.0L, 1.0L, 100);
    check_int("x_rango(0,1,100) produce 100 puntos",
              static_cast<int>(pts100.size()), 100);
    // El mismo contrato con otro conteo: la rejilla historica de 11 puntos.
    const std::vector<std::vector<long double>> pts11 = x_rango(0.0L, 1.0L, 11);
    check_int("x_rango(0,1,11) produce 11 puntos",
              static_cast<int>(pts11.size()), 11);
}

static void test_x_rango_extremos_incluidos() {
    std::vector<std::vector<long double>> pts = x_rango(0.0L, 1.0L, 100);
    check_ld("primer punto es x=0", pts.front()[0], 0.0L);
    check_ld("ultimo punto es x=1", pts.back()[0], 1.0L);
}

static void test_x_rango_espaciado_uniforme() {
    std::vector<std::vector<long double>> pts = x_rango(0.0L, 1.0L, 100);
    // x_i = i/99 (paso 1/99 ≈ 0.0101, el "0.01 o similar" del issue). El
    // valor esperado se calcula aqui mismo a partir de la especificacion.
    bool uniforme = true;
    for (size_t i = 0; i < pts.size(); i++) {
        long double esperado = (long double) i / 99.0L;
        if (std::fabs(pts[i][0] - esperado) > 1e-12L) { uniforme = false; break; }
    }
    check_bool("x_i = i/99 para i=0..99 (paso uniforme 1/99)", uniforme);
}

static void test_x_rango_triple_dual() {
    // Cada punto es el 2dual [x, 1, 0]: x con dx/dx=1 y d2x/dx2=0 (eq. (14)
    // del paper). Toda la evaluacion de fitness depende de esta convencion.
    std::vector<std::vector<long double>> pts = x_rango(0.0L, 1.0L, 100);
    bool convencion = true;
    for (size_t i = 0; i < pts.size(); i++) {
        if (pts[i].size() != 3 || pts[i][1] != 1.0L || pts[i][2] != 0.0L) {
            convencion = false;
            break;
        }
    }
    check_bool("cada punto es [x, 1, 0] (dual de dx/dx)", convencion);
}

// -------------------------------------------------------------------------
// Slice 2 (issue #27): el funcional de fitness del paper (ec. (4)):
//     R = lambda1*h1^2 + lambda2*h2^2 + integral(f^2 dx)
// evaluado en los 100 puntos igualmente espaciados de [0,1] con
// lambda1 = lambda2 = 10 (paper, seccion 5) y el factor de integracion
// Delta x = (stop-start)/(npoints-1) = 1/99 de la suma de Riemann.
// Antes de #27 el codigo sumaba potencias cuartas con pesos 300/700 y sin
// Delta x. Ejemplo trabajado con `x` (token 11), caso 0 (y''+y=0, y(0)=1,
// y'(0)=1):
//     parsv([11], [x,1,0]) = [x,1,0]  =>  residuo (y''+y)^2 = (0+x)^2 = x^2
//     fitness(x) = 10*(0-1)^2 + 10*(1-1)^2 + (1/99)*sum_i (i/99)^2
//                = 10 + (1/99) * sum_{i=0}^{99} (i/99)^2
// La suma se calcula con la formula cerrada sum i^2 = n(n+1)(2n+1)/6 para
// n=99 (99*100*199/6 = 328350) y 99^3 = 970299: un valor analitico
// independiente, no un bucle que repita el de la implementacion.
// -------------------------------------------------------------------------

static void test_fitness_potencias_y_pesos_del_paper() {
    // sin(x): residuo nulo en toda la rejilla; y(0)=0, y'(0)=1 =>
    // R = 10*(0-1)^2 + 10*(1-1)^2 = 10. En Python seria `assert f.fitness() == 10`.
    FunbObj f_sin({13, 11}, 0.0L, 1.0L);
    check_ld("fitness(sin(x)) caso 0 = 10 (lambda=10, cuadrados)",
             f_sin.fitness(), 10.0L);
    // cos(x): residuo nulo; y(0)=1, y'(0)=0 => R = 10*(1-1)^2 + 10*(0-1)^2 = 10.
    FunbObj f_cos({14, 11}, 0.0L, 1.0L);
    check_ld("fitness(cos(x)) caso 0 = 10 (lambda=10, cuadrados)",
             f_cos.fitness(), 10.0L);
    // cos(x)+sin(x): residuo nulo y ambas condiciones exactas => R = 0.
    FunbObj f_sol({18, 14, 11, 13, 11}, 0.0L, 1.0L);
    check_ld("fitness(cos(x)+sin(x)) caso 0 = 0 (solucion exacta)",
             f_sol.fitness(), 0.0L);
}

static void test_fitness_x_caso0_incluye_factor_deltax() {
    // Esperado analitico: 10 + 328350/970299 = 10.338400843451348...
    // En Python: `10 + 328350/970299`.
    FunbObj f({11}, 0.0L, 1.0L);
    check_ld("fitness(x) caso 0 = 10 + Delta x*sum x_i^2",
             f.fitness(), 10.0L + 328350.0L / 970299.0L, 1e-12L);
}

// -------------------------------------------------------------------------
// Slice 3 (issue #27): el Caso 1 del paper (ODE_CASE=1). La fitness usa el
// residuo de la ecuacion (18), x*y'' + y' - cos(x) = 0 (la forma multiplicada
// de (16) que el paper minimiza), las condiciones iniciales y(0)=0, y'(0)=1 y
// el mismo funcional (4) del slice 2. Los valores esperados son literales
// calculados por separado (Python), no repitiendo el bucle de fitness().
// -------------------------------------------------------------------------

static void test_fitness_caso1_residuo_del_paper() {
    // Ejemplo trabajado con `x` (token 11): parsv([11], [x,1,0]) = [x,1,0] =>
    //     residuo = x*0 + 1 - cos(x) = 1 - cos(x)
    //     penalizaciones = 10*(0-0)^2 + 10*(1-1)^2 = 0
    //     R = (1/99) * sum_i (1-cos(i/99))^2 = 0.04545624767792866
    // (literal calculado aparte con Python; el sumando no tiene una primitiva
    // discreta cerrada, pero si la integral exacta, que fija la escala).
    FunbObj f({11}, 0.0L, 1.0L, 1);
    check_ld("fitness_caso1(x) = Delta x*sum (1-cos x_i)^2",
             f.fitness(), 0.04545624767792866L, 1e-12L);
}

static void test_fitness_caso1_funcion_unaria() {
    // Segunda comprobacion con una funcion unaria, `sin(x)` (tokens 13,11):
    //     parsv(sin(x)) = [sin(x), cos(x), -sin(x)]
    //     residuo = x*(-sin(x)) + cos(x) - cos(x) = -x*sin(x)
    //     penalizaciones = 10*(sin 0 - 0)^2 + 10*(cos 0 - 1)^2 = 0
    //     R = (1/99) * sum_i (x_i*sin x_i)^2 = 0.1606370980396136 (Python).
    FunbObj f({13, 11}, 0.0L, 1.0L, 1);
    check_ld("fitness_caso1(sin(x)) = Delta x*sum (x_i sin x_i)^2",
             f.fitness(), 0.1606370980396136L, 1e-12L);
}

// -------------------------------------------------------------------------
// Slice 4 (issue #20, B6 de REVIEW.md): fitness() no muta al individuo y se
// cachea. Antes, un arbol sin x recibia hasta 4 escrituras aleatorias de 11
// dentro de fitness() y la primera llamada devolvia INFINITY mientras una
// segunda podia devolver otro valor: la evaluacion cambiaba al individuo.
// -------------------------------------------------------------------------

// Vector que no renderiza un arbol completo: `*` (19) con un solo operando
// deja el hueco "()" en el render, y fitness() debe rechazarlo con INFINITY.
static void test_fitness_arbol_incompleto_es_infinita() {
    FunbObj f({19, 11}, 0.0L, 1.0L);   // en Python: tokens = [19, 11] -> "(x)*()"
    check_bool("fitness() de un arbol incompleto es INFINITY",
               std::isinf(f.fitness()));
}

static void test_fitness_sin_x_no_muta_el_individuo() {
    // 20 constantes (misma longitud que un individuo real), sin token x (11).
    srand(20261013);   // fija el sorteo de la reparacion heredada
    std::vector<long double> sin_x(20, 3.0L);
    FunbObj f(sin_x, 0.0L, 1.0L);
    std::vector<long double> antes = f.vecvalue();

    long double r1 = f.fitness();
    long double r2 = f.fitness();

    check_bool("fitness() de un arbol sin x no toca el vector",
               f.vecvalue() == antes);
    check_bool("fitness() de un arbol sin x es INFINITY", std::isinf(r1));
    check_bool("fitness() repetido devuelve lo mismo", r2 == r1);
}

// -------------------------------------------------------------------------
// Slice issue #63: las hojas polinomiales x^2/x^3 (tokens 26/27) dependen de
// x igual que el token 11, asi que un arbol compacto como x^2+2 ya no puede
// rechazarse como "sin x". fitness() pregunta por cualquier hoja
// x-dependiente (is_x_dependent), no solo por la variable.
// -------------------------------------------------------------------------

static void test_fitness_hojas_polinomicas_dependen_de_x() {
    // x^2 en prefijo: {26}. Antes daba INFINITY por no contener el token 11.
    FunbObj f_x2({TOKEN_X2}, 0.0L, 1.0L);
    check_bool("fitness() de x^2 (solo token 26) es finita",
               !std::isinf(f_x2.fitness()));
    // x^3+x^2 compacta: {18, 27, 26}. Tampoco contiene el token 11.
    FunbObj f_suma({TOKEN_ADD, TOKEN_X3, TOKEN_X2}, 0.0L, 1.0L);
    check_bool("fitness() de x^3+x^2 (sin token 11) es finita",
               !std::isinf(f_suma.fitness()));
}

static void test_fitness_valido_es_estable() {
    // sin(x) (tokens 13,11): fitness finita, y dos llamadas deben devolver
    // exactamente lo mismo (la cache no cambia el resultado).
    FunbObj f({13, 11}, 0.0L, 1.0L);
    long double r1 = f.fitness();
    check_bool("fitness() de un arbol valido es estable",
               !std::isnan(r1) && f.fitness() == r1);
}

// La radicanda (x - 1/2)^2 - 1/10 es positiva en x=0 y x=1, pero negativa
// cerca de x=1/2. Asi la expresion es evaluable en los extremos y produce
// NaN en puntos interiores, que debe invalidar al individuo completo.
static void test_fitness_nan_interior_es_infinita() {
    // En Python seria una lista de enteros: sqrt((x - 1/2)**2 - 1/10).
    std::vector<long double> nan_interior = {
        17, 18, 21, 18, 11, 12, 20, 1, 2, 2, 12, 20, 1, 10
    };
    FunbObj f(nan_interior, 0.0L, 1.0L);
    check_bool("fitness() con NaN interior es INFINITY",
               std::isinf(f.fitness()));
}

// -------------------------------------------------------------------------
// Slice 5 (issues #10 y #27): la solucion aproximada del paper para el Caso 1,
//     y_s(x) = x*cos(x/(5*cos(cos 9)))   (ec. (20))
// en prefijo es {19,11,14,20,11,19,5,14,14,9}. Con el funcional (4) su valor
// es R = (1/99) * sum_i f_i^2 = 7.546733118983904e-06 (literal calculado
// aparte con Python), por debajo del umbral de convergencia 1e-5: el Caso 1
// es alcanzable con este alfabeto. Sin el factor Delta x valdria ~7.5e-4 y no
// convergeria; sin los cuadrados, ~1e-10, asi que el test fija ambas cosas.
// -------------------------------------------------------------------------

static void test_fitness_caso1_aprox_del_paper() {
    // y_s cumple y(0)=0 e y'(0)=1 exactamente => las penalizaciones son 0 y
    // toda la fitness viene de la integral del residuo al cuadrado.
    FunbObj f({19, 11, 14, 20, 11, 19, 5, 14, 14, 9}, 0.0L, 1.0L, 1);
    check_ld("fitness_caso1(ec. (20)) = 7.546733118983904e-06",
             f.fitness(), 7.546733118983904e-06L, 1e-12L);
    check_bool("la aproximacion del paper converge (<1e-5)",
               f.fitness() < 1e-5L);
}

// -------------------------------------------------------------------------
// Slice 6 (issue #61): el caso hiperbolico y'' - y = 0 con y(0)=1, y'(0)=0,
// cuya solucion exacta es cosh(x). Existe para que el solver pueda mostrar
// convergencia sobre una solucion hiperbolica (criterio de aceptacion del
// issue); los valores esperados son analiticos y salen de evaluar la ODE a
// mano, no de la implementacion.
// -------------------------------------------------------------------------

static void test_fitness_caso2_solucion_exacta() {
    // cosh(x) en prefijo: tokens {24, 11}. Residuo y''-y = cosh-cosh = 0 en
    // los 100 puntos de la rejilla y penalizaciones 10*(1-1)^2 + 10*(0-0)^2,
    // asi que R = 0 exacto. En Python seria `assert f.fitness() == 0`.
    FunbObj f({24, 11}, 0.0L, 1.0L, 2);
    check_ld("fitness_caso2(cosh(x)) = 0 (solucion exacta)",
             f.fitness(), 0.0L);
    check_bool("la solucion exacta del caso 2 converge (<1e-5)",
               f.fitness() < 1e-5L);
}

static void test_fitness_caso2_condiciones_iniciales() {
    // sinh(x) = tokens {23, 11}: cumple la ODE (sinh''-sinh = 0) pero no las
    // condiciones y(0)=1, y'(0)=0; cada una falla por 1, asi que
    // R = 10*1^2 + 10*1^2 = 20. Sin el caso 2 esta prueba valdria otra cosa,
    // de modo que fija la ecuacion y ambas condiciones a la vez.
    FunbObj f({23, 11}, 0.0L, 1.0L, 2);
    check_ld("fitness_caso2(sinh(x)) = 20 (falla ambas condiciones)",
             f.fitness(), 20.0L, 1e-12L);
}

// -------------------------------------------------------------------------
// Slice 7 (issue #102): las condiciones iniciales dejan de ser solo 0.0/1.0.
// El caso 6 (Legendre) declara y(0) = -1/2 y el caso 7 (Hermite) y(0) = -2,
// valores fraccionarios y negativos que los ternarios antiguos no podian
// expresar. La cota esperada se deriva a mano del funcional (4) con
// lambda = 10: con la expresion `x` (token 11, parsv en x=0 da [0,1,0]) la
// penalizacion de condiciones iniciales vale exactamente
// 10*(0-y0)^2 + 10*(1-dy0)^2 y el residual (cualquiera de los casos, presente
// o futuro) solo aporta terminos al cuadrado no negativos. En Python seria
// `assert f.fitness() >= 12.5` con la cota calculada aparte.
// -------------------------------------------------------------------------

static void test_fitness_caso6_condiciones_iniciales_fraccionarias() {
    // Caso 6: y(0) = -1/2, y'(0) = 0 => cota 10*(1/2)^2 + 10*1^2 = 12.5.
    // Con las condiciones anteriores (y(0)=1, y'(0)=1) la penalizacion valia
    // 10 y la fitness del mismo x quedaba por debajo de la cota, asi que el
    // assert distingue ambos juegos de condiciones iniciales.
    FunbObj f({11}, 0.0L, 1.0L, 6);
    const long double fit = f.fitness();
    check_bool("fitness_caso6(x) es finita (no INFINITY)", std::isfinite(fit));
    check_bool("fitness_caso6(x) >= 12.5 (y(0)=-1/2, y'(0)=0)", fit >= 12.5L);
}

static void test_fitness_caso7_condiciones_iniciales_negativas() {
    // Caso 7: y(0) = -2, y'(0) = 0 => cota 10*2^2 + 10*1^2 = 50. La cota
    // separa estas condiciones (negativas) de las (1,1) anteriores, con las
    // que la fitness del mismo x quedaba muy por debajo.
    FunbObj f({11}, 0.0L, 1.0L, 7);
    const long double fit = f.fitness();
    check_bool("fitness_caso7(x) es finita (no INFINITY)", std::isfinite(fit));
    check_bool("fitness_caso7(x) >= 50 (y(0)=-2, y'(0)=0)", fit >= 50.0L);
}

int main() {
    std::cout << "== test_grid: issues #7 (rejilla) y #27 (funcional de fitness) ==\n";
    test_x_rango_cantidad_de_puntos();
    test_x_rango_extremos_incluidos();
    test_x_rango_espaciado_uniforme();
    test_x_rango_triple_dual();
    test_fitness_potencias_y_pesos_del_paper();
    test_fitness_x_caso0_incluye_factor_deltax();
    test_fitness_caso1_residuo_del_paper();
    test_fitness_caso1_funcion_unaria();
    test_fitness_caso1_aprox_del_paper();
    test_fitness_caso2_solucion_exacta();
    test_fitness_caso2_condiciones_iniciales();
    test_fitness_caso6_condiciones_iniciales_fraccionarias();
    test_fitness_caso7_condiciones_iniciales_negativas();
    test_fitness_arbol_incompleto_es_infinita();
    test_fitness_sin_x_no_muta_el_individuo();
    test_fitness_hojas_polinomicas_dependen_de_x();
    test_fitness_valido_es_estable();
    test_fitness_nan_interior_es_infinita();
    std::cout << (g_fallos == 0 ? "OK" : "FALLARON") << ": "
              << (g_checks - g_fallos) << "/" << g_checks << " asserts\n";
    return g_fallos == 0 ? 0 : 1;  // como el exit code de pytest
}
