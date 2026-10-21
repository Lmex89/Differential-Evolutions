// genericos_esperados.h — copia de referencia (oraculo) de los quince bloques
// genericos compartidos del catalogo warm start (issue #145).
//
// La duplicacion de literales respecto de bloques_genericos() es deliberada:
// este archivo es la fuente de verdad INDEPENDIENTE con la que los tests
// comprueban que el tramo generico no se altero, y a proposito no incluye a
// parse_tree.cpp (un oraculo que lee del codigo que vigila no vigila nada).
// Si el contenido del issue #145 cambia de verdad, actualiza este archivo y
// bloques_genericos() juntos, en el mismo commit; un test fallara si solo se
// actualiza uno.
#pragma once

#include <vector>

#include "dualfunctions.h"  // ldouble
#include "token.h"          // TOKEN_ADD, TOKEN_SIN, ...

// genericos_del_issue_145: los quince bloques tal como los enumera el issue
// #145, en su orden. En Python seria una constante de modulo
// `GENERICOS_REFERENCIA = [[...], ...]`.
static const std::vector<std::vector<ldouble>>& genericos_del_issue_145() {
    // `static const` se construye una sola vez y vive mientras dure el
    // proceso, igual que una constante de modulo en Python.
    static const std::vector<std::vector<ldouble>> genericos = {
        {TOKEN_X},                                              // x
        {TOKEN_X2},                                             // x^2
        {TOKEN_X3},                                             // x^3
        {TOKEN_ADD, TOKEN_CONST1, TOKEN_X},                     // 1 + x
        {TOKEN_ADD, TOKEN_ADD, TOKEN_CONST1, TOKEN_X,           // 1 + x + x^2/2
         TOKEN_DIV, TOKEN_X2, TOKEN_CONST2},
        {TOKEN_SIN, TOKEN_X},                                   // sin(x)
        {TOKEN_COS, TOKEN_X},                                   // cos(x)
        {TOKEN_ADD, TOKEN_SIN, TOKEN_X, TOKEN_COS, TOKEN_X},    // sin(x) + cos(x)
        {TOKEN_EXP, TOKEN_X},                                   // e^x
        {TOKEN_MUL, TOKEN_X, TOKEN_EXP, TOKEN_X},               // x * e^x
        {TOKEN_SINH, TOKEN_X},                                  // sinh(x)
        {TOKEN_COSH, TOKEN_X},                                  // cosh(x)
        {TOKEN_ADD, TOKEN_X, TOKEN_NEG, TOKEN_DIV, TOKEN_X3,    // x - x^3/6
         TOKEN_CONST6},
        {TOKEN_ADD, TOKEN_CONST1, TOKEN_NEG, TOKEN_DIV, TOKEN_X2, TOKEN_CONST2},  // 1 - x^2/2
        {TOKEN_EXP, TOKEN_NEG, TOKEN_X}                         // e^(-x)
    };
    return genericos;
}
