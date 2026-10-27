// =========================================================================
// funbobj.h - Evaluacion de la fitness (issue #157).
//
// FunbObj y x_rango vivian dentro de parse_tree.cpp sin interfaz propia; este
// header los declara para que el solver y los tests enlacen el modulo sin
// recompilar el bucle DE completo. Para un desarrollador Python: header +
// .cpp son el modulo `funbobj.py`; la firma publica vive aqui y la
// implementacion en funbobj.cpp.
//
// La interfaz son el constructor, fitness(), represant(), vecvalue() y
// x_rango. El switch de residuales y el de condiciones iniciales por caso
// quedan dentro de funbobj.cpp: no son parte del contrato publico.
// =========================================================================

#ifndef FUNBOBJ_H
#define FUNBOBJ_H

#include <string>
#include <vector>

#include "dualfunctions.h"

// x_rango: construye la rejilla de evaluacion como `npoints` puntos
// igualmente espaciados en [start, stop], ambos extremos incluidos, cada uno
// empaquetado como el 2dual [x, 1, 0] (eq. (14) del paper: dx/dx = 1 y
// d2x/dx2 = 0, la convencion que espera parsv()). Precondicion: npoints >= 2
// (la comprueba un assert en la definicion). En Python seria
//     [start + (stop - start) * i / (npoints - 1) for i in range(npoints)]
std::vector<std::vector<long double>> x_rango(long double start,
                                              long double stop, int npoints);

class FunbObj{

    private:
    std::vector<ldouble> _vecval;
    str _repre;
    // _fresult es mutable porque fitness() es const pero necesita cachear el
    // resultado. En Python sería @cached_property: método const con estado interno.
    mutable ldouble _fresult;
    // Cache de la fitness ya calculada (B6 de REVIEW.md): los individuos se
    // reemplazan enteros en el bucle DE (`Population[j] = candidato`), nunca
    // se mutan, así que el valor cacheado no puede quedar obsoleto. Es mutable
    // porque fitness() es const pero debe poder actualizar la caché. En Python
    // serían `self._cache_valido = False` y `self._fresult`.
    mutable bool _cache_valido = false;
    std::vector<std::vector<ldouble>> _xrange;
    std::vector<long double> _xinitials = {0.0,1.0,0.0};
    // Factor de integración Delta x de la suma de Riemann de la ec. (4):
    // con los 100 puntos igualmente espaciados de [0,1] y ambos extremos
    // incluidos, el paso es (stop-start)/(npoints-1) = 1/99. En Python sería
    // `self._dx = (stop - start) / (len(self._xrange) - 1)`; long double no
    // tiene equivalente exacto en Python (es más ancho que float).
    long double _dx;
    // Caso del problema que mide esta fitness (lo elige ODE_CASE en main()):
    //   0 = juguete historico del prototipo: y'' + y = 0, y(0)=1, y'(0)=1
    //   1 = Caso 1 del paper, ecuacion (18): x*y'' + y' - cos(x) = 0,
    //       y(0)=0, y'(0)=1, con solucion exacta la integral del seno Si(x)
    //   2 = caso hiperbolico (issue #61): y'' - y = 0, y(0)=1, y'(0)=0,
    //       con solucion exacta cosh(x); la busqueda solo puede alcanzarlo
    //       desde que existe TOKEN_COSH.
    //   3 = oscilador amortiguado (issue #103): y'' + 2y' + 2y = 0,
    //       y(0)=1, y'(0)=-1, solucion e^(-x)*cos(x)
    //   4 = oscilador forzado (issue #103): y'' + y = e^x,
    //       y(0)=0, y'(0)=0, solucion (e^x - cos(x) - sin(x))/2
    //   5 = resonancia (issue #103): y'' - y = e^x,
    //       y(0)=0, y'(0)=0, solucion (2x*e^x - e^x + e^(-x))/4
    //   6..7 = casos Legendre/Hermite (issue #104): el parser ya los acepta
    //       (issue #102) y las condiciones iniciales por caso (incluidas
    //       y(0)=-1/2 de Legendre y y(0)=-2 de Hermite) se asignan en el
    //       switch de fitness(); sus residuales llegan con esos tickets.
    // Es el mismo patron que ODE_SEED: un numero de caso en vez de una bandera
    // por problema. En Python seria `self._caso = caso`.
    int _caso;

    public:
    // caso: selecciona la ODE que evalua fitness() (ver _caso). Tiene valor por
    // defecto 0 para que el resto del programa y los tests que ya construian
    // FunbObj con 3 argumentos sigan comportandose igual. En Python seria
    // `def __init__(self, vecval, start, stop, caso=0)`.
    // El constructor usa move semantics para el vector: std::move(vecval) evita
    // copiar el vector dos veces (una en el parámetro, otra en el miembro).
    // En Python no existe este detalle porque las listas siempre son referencias;
    // en C++ el move evita el overhead de copiar el vector completo.
    FunbObj(std::vector<ldouble> vecval, ldouble start, ldouble stop,
            int caso = 0);
    
    // create_random (issue #165): factory method que construye un Individual
    // aleatorio completo con la dimension y caso especificados. Implementa las
    // reglas de construccion: primer token operador (aridad > 0), sorteo
    // uniforme del resto, y reparacion x-dependence (inyecta TOKEN_X si no
    // existe). Es el unico camino de construccion aleatoria para el modulo
    // Individual, usado por createPopulation y los reinicios legacy/adaptive.
    // En Python seria `@classmethod def create_random(cls, dimension, caso)`.
    static FunbObj create_random(int dimension, int caso);
    // Accesores const (Con.2): represant() y vecvalue() solo leen el estado
    // del individuo; fitness() es const y cachea su resultado en los miembros
    // mutable de arriba. En Python serian @property y @cached_property.
    str represant() const;
    std::vector<ldouble> vecvalue() const;
    long double fitness() const;
};

#endif  // FUNBOBJ_H
