// test_funbobj_const.cpp — tests de const-correctness para FunbObj (issue #42).
//
// Para un desarrollador Python: este test verifica que los accesores de
// FunbObj sean const (como `@property` en Python que no mutan el objeto).
// Un `const FunbObj` debe poder llamar `represant()`, `vecvalue()` y `fitness()`
// sin necesidad de const_cast. La caché de fitness debe funcionar incluso en
// objetos const (usando `mutable` internamente, como `@cached_property` en Python).
//
// Enlaza funbobj.cpp y dualfunctions.cpp (issue #158): FunbObj vive tras
// funbobj.h, asi que el test ya no compila el bucle DE ni necesita ODE_NO_MAIN.

#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <utility>

#include "dualfunctions.h"

// Interfaz publica del modulo de fitness (issue #157): constructor, fitness(),
// represant(), vecvalue() y x_rango.
#include "funbobj.h"

// -------------------------------------------------------------------------
// Arnés mínimo (mismo estilo que test_grid): contadores globales y helpers.
// -------------------------------------------------------------------------

static int g_checks = 0;
static int g_fallos = 0;

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

static void check_bool(const std::string& nombre, bool cond) {
    g_checks++;
    if (cond) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "\n";
        g_fallos++;
    }
}

static void check_str(const std::string& nombre, const std::string& got,
                      const std::string& esperado) {
    g_checks++;
    if (got == esperado) {
        std::cout << "PASS  " << nombre << "\n";
    } else {
        std::cout << "FAIL  " << nombre << "  (got=\"" << got
                  << "\" esperado=\"" << esperado << "\")\n";
        g_fallos++;
    }
}

// -------------------------------------------------------------------------
// Test 1: represant() debe ser const (issue #42).
// -------------------------------------------------------------------------

static void test_represant_const() {
    // Crear un FunbObj const. En Python sería `obj = FunbObj(...); obj_const = obj`.
    const FunbObj f({14, 11}, 0.0L, 1.0L);  // cos(x) en notación prefija: TOKEN_COS(14) TOKEN_X(11)
    // Llamar represant() en un objeto const. Si no es const, no compila.
    // En Python sería `assert obj_const.represant() == "cos(x)"`.
    std::string repr = f.represant();
    check_bool("represant() se puede llamar en const FunbObj", !repr.empty());
    check_str("represant() devuelve la expresión correcta en const",
              repr, "cos(x)");
}

// -------------------------------------------------------------------------
// Test 2: vecvalue() debe ser const (issue #42).
// -------------------------------------------------------------------------

static void test_vecvalue_const() {
    const FunbObj f({11, 14, 13}, 0.0L, 1.0L);  // cos(x) + sin(x)
    // Llamar vecvalue() en un objeto const. Si no es const, no compila.
    std::vector<long double> vec = f.vecvalue();
    check_int("vecvalue() devuelve el vector correcto en const",
              static_cast<int>(vec.size()), 3);
    // Verificar los valores: 11=TOKEN_X, 14=TOKEN_COS, 13=TOKEN_SIN
    check_bool("vecvalue() preserva los tokens originales",
               vec[0] == 11 && vec[1] == 14 && vec[2] == 13);
}

// -------------------------------------------------------------------------
// Test 3: fitness() debe ser const con caché mutable (issue #42).
// -------------------------------------------------------------------------

static void test_fitness_const() {
    const FunbObj f({13, 11}, 0.0L, 1.0L);  // sin(x): fitness = 10.0
    // Llamar fitness() en un objeto const. Si no es const, no compila.
    long double fit1 = f.fitness();
    check_ld("fitness() se puede llamar en const FunbObj",
             fit1, 10.0L, 1e-9L);
    
    // La caché debe funcionar: segunda llamada devuelve el mismo valor.
    long double fit2 = f.fitness();
    check_ld("fitness() con caché mutable devuelve el mismo valor en const",
             fit2, 10.0L, 1e-9L);
    check_bool("fitness() cachea el resultado en const",
               std::fabs(fit1 - fit2) < 1e-15L);
}

// -------------------------------------------------------------------------
// Test 4: fitness() cache funciona en objetos const (repeated calls).
// -------------------------------------------------------------------------

static void test_fitness_cache_const() {
    const FunbObj f({18, 14, 11, 13, 11}, 0.0L, 1.0L);  // cos(x)+sin(x): fitness=0
    // Primera llamada calcula.
    long double fit1 = f.fitness();
    check_ld("fitness() primera llamada en const = 0 (solución exacta)",
             fit1, 0.0L, 1e-9L);
    
    // Segunda llamada debe usar caché (sin recalcular).
    long double fit2 = f.fitness();
    check_ld("fitness() segunda llamada en const usa caché",
             fit2, 0.0L, 1e-9L);
    
    // Tercera llamada para asegurar.
    long double fit3 = f.fitness();
    check_ld("fitness() tercera llamada en const usa caché",
             fit3, 0.0L, 1e-9L);
}

// -------------------------------------------------------------------------
// Test 5: Constructor mueve el vector (semántica move) (issue #42).
// -------------------------------------------------------------------------

static void test_constructor_move_semantics() {
    // Crear un vector y pasarle con std::move.
    std::vector<long double> vec_original = {11, 14, 13};
    std::vector<long double> vec_copia = vec_original;  // guardar para comparar
    
    FunbObj f(std::move(vec_original), 0.0L, 1.0L);
    
    // El vector movido puede estar en estado válido pero indeterminado.
    // Lo importante es que FunbObj tenga los datos correctos.
    std::vector<long double> vec_recuperado = f.vecvalue();
    check_int("FunbObj tiene el vector correcto después de move",
              static_cast<int>(vec_recuperado.size()), 3);
    check_bool("FunbObj preserva los tokens después de move",
               vec_recuperado == vec_copia);
}

// -------------------------------------------------------------------------
// Test 6: Objetos const pueden inspeccionarse sin const_cast.
// -------------------------------------------------------------------------

static void test_const_inspection_no_cast() {
    // Función helper que toma const FunbObj& (no puede hacer const_cast).
    auto inspeccionar = [](const FunbObj& f) -> long double {
        // Todas estas llamadas deben compilar sin const_cast.
        std::string repr = f.represant();
        std::vector<long double> vec = f.vecvalue();
        long double fit = f.fitness();
        return fit;
    };
    
    const FunbObj f({13, 11}, 0.0L, 1.0L);  // sin(x)
    long double resultado = inspeccionar(f);
    check_ld("Función que toma const FunbObj& puede inspeccionar sin const_cast",
             resultado, 10.0L, 1e-9L);
}

// -------------------------------------------------------------------------
// Test 7: Mutación no afecta a objetos const (invariante de const).
// -------------------------------------------------------------------------

static void test_const_object_immutability() {
    FunbObj f1({13, 11}, 0.0L, 1.0L);  // sin(x)
    const FunbObj f2({13, 11}, 0.0L, 1.0L);  // mismo vector, pero const
    
    // Ambos deben dar la misma fitness.
    long double fit1 = f1.fitness();
    long double fit2 = f2.fitness();
    check_ld("FunbObj mutable y const con mismo vector dan misma fitness",
             fit1, fit2, 1e-15L);
    
    // Los accesores deben devolver lo mismo.
    check_str("represant() igual en mutable y const",
              f1.represant(), f2.represant());
    check_bool("vecvalue() igual en mutable y const",
               f1.vecvalue() == f2.vecvalue());
}

// -------------------------------------------------------------------------
// Main: ejecutar todos los tests.
// -------------------------------------------------------------------------

int main() {
    std::cout << "=== Tests de const-correctness para FunbObj (issue #42) ===\n\n";
    
    test_represant_const();
    test_vecvalue_const();
    test_fitness_const();
    test_fitness_cache_const();
    test_constructor_move_semantics();
    test_const_inspection_no_cast();
    test_const_object_immutability();
    
    std::cout << "\n=== Resumen ===\n";
    std::cout << "Total checks: " << g_checks << "\n";
    std::cout << "Passed: " << (g_checks - g_fallos) << "\n";
    std::cout << "Failed: " << g_fallos << "\n";
    
    if (g_fallos > 0) {
        std::cout << "\nFAIL: " << g_fallos << " test(s) failed\n";
        return 1;
    }
    
    std::cout << "\nOK: All tests passed\n";
    return 0;
}
