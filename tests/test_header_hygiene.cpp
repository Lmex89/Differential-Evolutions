// test_header_hygiene.cpp — test de tiempo de compilacion (issue #39)
//
// Verifica que dualfunctions.h y funbobj.h (issue #157) sean autocontenidos y
// que los parametros de solo-lectura sean const references. Es un test de
// compilacion pura: no enlaza con dualfunctions.cpp ni funbobj.cpp, solo
// verifica que las declaraciones sean correctas.
//
// Para un desarrollador Python: esto es como hacer `import modulo` sin
// llamar a ninguna funcion. Si el import falla (el header no incluye todo
// lo que usa), el test falla en tiempo de compilacion.

#include <type_traits>
#include <utility>

#include "dualfunctions.h"
#include "funbobj.h"

// Verificar que los tipos esten cualificados con std::
static_assert(sizeof(std::vector<ldouble>) > 0, "std::vector debe estar disponible");
static_assert(sizeof(str) > 0, "str (std::string) debe estar disponible");

// Verificar que las declaraciones acepten const references (solo declaracion,
// sin implementacion). Si el header declara parametros no-const, esto no
// compilara porque no podra pasar un const vector a un parametro no-const.
void test_const_correctness() {
    // Solo necesitamos que las declaraciones acepten const references.
    // No llamamos a las funciones, solo verificamos que las firmas sean
    // compatibles con const references usando punteros a funcion.
    
    // Funciones unarias: deben aceptar const std::vector<ldouble>&
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(sin2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(cos2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(tan2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(sinh2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(cosh2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(tanh2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(exp2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(log2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&)>(sqrt2dual);
    
    // Funciones binarias: deben aceptar dos const std::vector<ldouble>&
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&, const std::vector<ldouble>&)>(division2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&, const std::vector<ldouble>&)>(pow2dual);
    (void)static_cast<std::vector<ldouble>(*)(const std::vector<ldouble>&, const std::vector<ldouble>&)>(prod2dual);
    
    // parse_tree y OperF: deben aceptar const references
    (void)static_cast<str(*)(const std::vector<ldouble>&)>(parse_tree);
    (void)static_cast<std::vector<ldouble>(*)(ldouble, const std::vector<ldouble>&)>(OperF);
    
    // OperF2 mantiene su matriz no-const (contrato documentado de entrada/salida)
    (void)static_cast<std::vector<long double>(*)(ldouble, std::vector<std::vector<ldouble>>&, int)>(OperF2);
}

// FunbObj y x_rango (issue #157): el header del modulo de fitness expone el
// constructor, fitness(), represant(), vecvalue() y x_rango. Los decltype son
// operandos no evaluados: verifican las firmas (incluido el const) sin
// enlazar funbobj.cpp, como corresponde a un test de compilacion pura.
static_assert(std::is_constructible<FunbObj, std::vector<ldouble>, ldouble,
                                    ldouble, int>::value,
              "FunbObj debe aceptar (vecval, start, stop, caso)");
static_assert(std::is_constructible<FunbObj, std::vector<ldouble>, ldouble,
                                    ldouble>::value,
              "el caso debe tener valor por defecto 0");
static_assert(std::is_same<decltype(std::declval<const FunbObj&>().fitness()),
                           long double>::value,
              "fitness() debe ser const y devolver long double");
static_assert(std::is_same<decltype(std::declval<const FunbObj&>().represant()),
                           str>::value,
              "represant() debe ser const y devolver str");
static_assert(std::is_same<decltype(std::declval<const FunbObj&>().vecvalue()),
                           std::vector<ldouble>>::value,
              "vecvalue() debe ser const y devolver vector<ldouble>");
static_assert(std::is_same<decltype(x_rango(std::declval<ldouble>(),
                                            std::declval<ldouble>(),
                                            std::declval<int>())),
                           std::vector<std::vector<long double>>>::value,
              "x_rango debe devolver la rejilla de 2duales");

int main() {
    // No necesitamos hacer nada en runtime. Si este archivo compila, el
    // header es autocontenido y las firmas son correctas.
    return 0;
}
