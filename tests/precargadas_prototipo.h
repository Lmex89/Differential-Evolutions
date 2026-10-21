// precargadas_prototipo.h — copia de referencia (oraculo) de las cinco
// semillas trigonometricas escritas a mano del prototipo, que
// src/parse_tree.cpp::semillas_precargadas() conserva como tramo precargado
// del catalogo warm start (issue #144).
//
// La duplicacion de literales respecto de semillas_precargadas() es
// deliberada: este archivo es la fuente de verdad INDEPENDIENTE con la que
// los tests comprueban que el tramo precargado no se altero, y a proposito
// no incluye a parse_tree.cpp (un oraculo que lee del codigo que vigila no
// vigila nada). Si el contenido historico cambia de verdad (un issue
// explicito), actualiza este archivo y semillas_precargadas() juntos, en el
// mismo commit; un test fallara si solo se actualiza uno.
#pragma once

#include <vector>

#include "dualfunctions.h"  // ldouble
#include "token.h"          // TOKEN_ADD, TOKEN_COS, ...

// precargadas_del_prototipo: los cinco vectores de 20 tokens tal como los
// inyectaba el prototipo antes de existir el catalogo por caso. En Python
// seria una constante de modulo `PRECARGADAS_REFERENCIA = [[...], ...]`.
static const std::vector<std::vector<ldouble>>& precargadas_del_prototipo() {
    static const std::vector<std::vector<ldouble>> semillas = {
        {TOKEN_ADD, TOKEN_SIN, TOKEN_SQRT, TOKEN_X, TOKEN_COS, TOKEN_COS,
         TOKEN_X, TOKEN_COS, TOKEN_COS, TOKEN_COS, TOKEN_SIN, TOKEN_COS,
         TOKEN_COS, TOKEN_COS, TOKEN_COS, TOKEN_COS, TOKEN_COS, TOKEN_COS,
         TOKEN_COS, TOKEN_X},                                             // uno
        {TOKEN_ADD, TOKEN_COS, TOKEN_SIN, TOKEN_SQRT, TOKEN_X, TOKEN_COS,
         TOKEN_DIV, TOKEN_CONST1, TOKEN_CONST5, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1},          // dos
        {TOKEN_ADD, TOKEN_SIN, TOKEN_X, TOKEN_SIN, TOKEN_SIN, TOKEN_COS,
         TOKEN_COS, TOKEN_COS, TOKEN_X, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1},          // tres
        {TOKEN_ADD, TOKEN_COS, TOKEN_X, TOKEN_SIN, TOKEN_SIN, TOKEN_X,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1},          // cuatro
        {TOKEN_ADD, TOKEN_SIN, TOKEN_SIN, TOKEN_SIN, TOKEN_X, TOKEN_SIN,
         TOKEN_X, TOKEN_X, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1,
         TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1, TOKEN_CONST1}           // c5
    };
    return semillas;
}
