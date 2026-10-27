// =========================================================================
// test_individual_random.cpp - Tests para la construcción aleatoria de
// Individuals en el módulo FunbObj (issue #165).
//
// El seam bajo test es `FunbObj::create_random(int dimension, int caso)`:
// factory method que construye un Individual aleatorio completo con:
// - Primer token operador (aridad > 0) para evitar colapso de expresiones
// - Sorteo uniforme del resto de tokens
// - Reparación x-dependence (inyecta TOKEN_X si no existe)
// =========================================================================

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "funbobj.h"
#include "token.h"

// check_bool: helper de aserción. En Python sería `assert condición, mensaje`.
// La diferencia es que C++ imprime el mensaje y continúa (no lanza excepción),
// de modo que el test reporta todos los fallos, no solo el primero.
static int checks_passed = 0;
static int checks_failed = 0;

static void check_bool(const std::string& msg, bool ok) {
    if (ok) {
        checks_passed++;
    } else {
        checks_failed++;
        std::cerr << "FAIL: " << msg << "\n";
    }
}

// -------------------------------------------------------------------------
// Slice 1: create_random produce un FunbObj con la dimensión pedida.
// -------------------------------------------------------------------------
static void test_create_random_dimension_correcta() {
    std::srand(20261026);

    FunbObj ind = FunbObj::create_random(20, 1);
    const std::vector<long double>& tokens = ind.vecvalue();

    check_bool("create_random(20) produce 20 tokens", tokens.size() == 20);
    check_bool("create_random(15) produce 15 tokens",
               FunbObj::create_random(15, 1).vecvalue().size() == 15);
    check_bool("create_random(1) produce 1 token",
               FunbObj::create_random(1, 1).vecvalue().size() == 1);
}

// -------------------------------------------------------------------------
// Slice 2: el primer token es operador (aridad > 0).
// -------------------------------------------------------------------------
static void test_create_random_primer_token_operador() {
    std::srand(20261026);

    // 50 individuos: todos deben empezar con operador.
    bool todos_operadores = true;
    for (int i = 0; i < 50; i++) {
        FunbObj ind = FunbObj::create_random(20, 1);
        const std::vector<long double>& tokens = ind.vecvalue();
        int primer = static_cast<int>(tokens[0]);
        if (arity(primer) == 0) {
            todos_operadores = false;
            break;
        }
    }
    check_bool("los 50 individuos empiezan con operador (aridad > 0)",
               todos_operadores);
}

// -------------------------------------------------------------------------
// Slice 3: todos los tokens están en el alfabeto válido.
// -------------------------------------------------------------------------
static void test_create_random_tokens_en_alfabeto() {
    std::srand(20261026);

    bool todos_validos = true;
    for (int i = 0; i < 30; i++) {
        FunbObj ind = FunbObj::create_random(20, 1);
        const std::vector<long double>& tokens = ind.vecvalue();
        for (size_t j = 0; j < tokens.size(); j++) {
            int token = static_cast<int>(tokens[j]);
            if (!is_valid(token)) {
                todos_validos = false;
                break;
            }
        }
        if (!todos_validos) break;
    }
    check_bool("todos los tokens están en TOKEN_MIN..TOKEN_MAX", todos_validos);
}

// -------------------------------------------------------------------------
// Slice 4: el individuo depende de x (reparación x-dependence).
// -------------------------------------------------------------------------
static void test_create_random_depende_de_x() {
    std::srand(20261026);

    // 50 individuos: todos deben tener al menos un token x-dependiente.
    bool todos_dependen = true;
    for (int i = 0; i < 50; i++) {
        FunbObj ind = FunbObj::create_random(20, 1);
        const std::vector<long double>& tokens = ind.vecvalue();
        bool depende = false;
        for (size_t j = 0; j < tokens.size(); j++) {
            int token = static_cast<int>(tokens[j]);
            if (is_x_dependent(token)) {
                depende = true;
                break;
            }
        }
        if (!depende) {
            todos_dependen = false;
            break;
        }
    }
    check_bool("los 50 individuos dependen de x (TOKEN_X, TOKEN_X2, o TOKEN_X3)",
               todos_dependen);
}

// -------------------------------------------------------------------------
// Slice 5: el individuo es evaluable (represant sin huecos "()").
// -------------------------------------------------------------------------
static void test_create_random_representacion_completa() {
    std::srand(20261026);

    // 30 individuos: ninguno debe tener huecos "()" en su representación.
    bool todos_completos = true;
    for (int i = 0; i < 30; i++) {
        FunbObj ind = FunbObj::create_random(20, 1);
        std::string repr = ind.represant();
        if (repr.find("()") != std::string::npos) {
            todos_completos = false;
            break;
        }
    }
    check_bool("los 30 individuos tienen representación completa (sin huecos)",
               todos_completos);
}

// -------------------------------------------------------------------------
// Slice 6: el individuo tiene fitness finita o INFINITY (evaluable).
// -------------------------------------------------------------------------
static void test_create_random_fitness_evaluable() {
    std::srand(20261026);

    // 20 individuos: todos deben tener fitness finita o INFINITY (no NaN).
    bool todos_evaluables = true;
    for (int i = 0; i < 20; i++) {
        FunbObj ind = FunbObj::create_random(20, 1);
        long double fitness = ind.fitness();
        if (std::isnan(fitness)) {
            todos_evaluables = false;
            break;
        }
    }
    check_bool("los 20 individuos tienen fitness evaluable (finita o INFINITY)",
               todos_evaluables);
}

// -------------------------------------------------------------------------
// Slice 7: create_random funciona con diferentes casos ODE.
// -------------------------------------------------------------------------
static void test_create_random_multiples_casos() {
    std::srand(20261026);

    bool todos_validos = true;
    for (int caso = 0; caso <= 5; caso++) {
        FunbObj ind = FunbObj::create_random(20, caso);
        const std::vector<long double>& tokens = ind.vecvalue();
        if (tokens.size() != 20) {
            todos_validos = false;
            break;
        }
        int primer = static_cast<int>(tokens[0]);
        if (arity(primer) == 0) {
            todos_validos = false;
            break;
        }
    }
    check_bool("create_random funciona con casos 0-5", todos_validos);
}

// -------------------------------------------------------------------------
// Slice 8: create_random es reproducible con srand fijo.
// -------------------------------------------------------------------------
static void test_create_random_reproducible() {
    std::srand(20261026);
    FunbObj ind1 = FunbObj::create_random(20, 1);

    std::srand(20261026);
    FunbObj ind2 = FunbObj::create_random(20, 1);

    const std::vector<long double>& v1 = ind1.vecvalue();
    const std::vector<long double>& v2 = ind2.vecvalue();

    bool iguales = (v1.size() == v2.size());
    if (iguales) {
        for (size_t i = 0; i < v1.size(); i++) {
            if (v1[i] != v2[i]) {
                iguales = false;
                break;
            }
        }
    }
    check_bool("create_random es reproducible con srand fijo", iguales);
}

int main() {
    test_create_random_dimension_correcta();
    test_create_random_primer_token_operador();
    test_create_random_tokens_en_alfabeto();
    test_create_random_depende_de_x();
    test_create_random_representacion_completa();
    test_create_random_fitness_evaluable();
    test_create_random_multiples_casos();
    test_create_random_reproducible();

    if (checks_failed == 0) {
        std::cout << "test_individual_random: PASS | " << checks_passed
                  << " checks passed\n";
        return 0;
    } else {
        std::cerr << "test_individual_random: FAIL | " << checks_failed
                  << " checks failed, " << checks_passed << " passed\n";
        return 1;
    }
}
