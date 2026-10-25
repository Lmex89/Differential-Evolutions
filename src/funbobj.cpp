// =========================================================================
// funbobj.cpp - Implementacion de la evaluacion de la fitness (issue #157).
//
// El cuerpo se movio sin cambios desde parse_tree.cpp: mismas formulas, misma
// cache por individuo y mismos switches de residuales y condiciones iniciales
// por caso. Para un desarrollador Python: este .cpp es el cuerpo del modulo
// `funbobj.py` y funbobj.h su interfaz publica.
// =========================================================================

#include "funbobj.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

#include "token.h"

using namespace std;

// x_rango: construye la rejilla de evaluacion como `npoints` puntos igualmente
// espaciados en [start, stop], ambos extremos incluidos. Antes el tercer
// parametro era un paso y el conteo salia de una division truncada
// (`int final = stop/step`): con paso 0.1 ese truncamiento daba 11 puntos en
// vez de los 100 que pide el paper (REVIEW C1). Ahora el conteo es explicito y
// cada punto se calcula con x_i = start + (stop-start)*i/(npoints-1), que en
// Python seria:
//     [start + (stop - start) * i / (npoints - 1) for i in range(npoints)]
// Cada punto se empaqueta como el 2dual [x, 1, 0] (eq. (14) del paper): x con
// dx/dx = 1 y d2x/dx2 = 0, la convencion que espera parsv().
vector<vector<long double>> x_rango(long double start,long double stop,int npoints){

    // Precondicion: npoints >= 2 para evitar division por cero en (npoints-1).
    // Con npoints < 2, la formula x_i = start + (stop-start)*i/(npoints-1)
    // divide por cero o genera un solo punto sin sentido. En Python seria
    // `assert npoints >= 2, "grid requires at least 2 points"`.
    assert(npoints >= 2);

    vector<vector<long double>> ans;

    for(int i = 0; i < npoints; i++){
        long double pos = start + (stop - start) * i / (npoints - 1);
        vector<long double> temp = {pos,1.0,0.0};
        ans.push_back(temp);
    }
    return ans;
}

FunbObj::FunbObj(vector<ldouble> vecval,ldouble start,ldouble stop, int caso)
: _vecval(std::move(vecval)), _caso(caso) {
    // Rejilla de evaluacion: los 100 puntos igualmente espaciados de [0,1]
    // que pide el paper (§5). En Python seria
    // `self._xrange = x_rango(start, stop, 100)`.
    _xrange = x_rango(start,stop,100);
    _repre = parse_tree(_vecval);
    _xrange[0] = _xinitials;
    // Delta x de la suma de Riemann que aproxima integral(f^2 dx) en la
    // ec. (4) del paper (seccion 5). x_rango siempre devuelve 100 puntos,
    // asi que el divisor (npoints-1) nunca es cero.
    _dx = (stop - start) / static_cast<long double>(_xrange.size() - 1);
}
// represant() es const porque no modifica el estado observable del objeto.
// En Python sería @property def represant(self): return self._repre.
string FunbObj::represant() const{
    return _repre;
}
// vecvalue() es const porque solo devuelve una copia del vector interno.
// En Python sería @property def vecvalue(self): return self._vecval.copy().
vector<ldouble> FunbObj::vecvalue() const{
    return _vecval;
}
// fitness() es const con caché mutable: el método es de solo lectura desde
// fuera, pero internamente puede actualizar _fresult y _cache_valido.
// En Python sería @cached_property: método const con estado interno cacheado.
long double FunbObj::fitness() const{
    // Cache por individuo: el bucle DE pide la fitness varias veces (la
    // comparacion del trial contra el objetivo y, en el barrido del mejor
    // de la generacion, dos por comparacion); recalcular la rejilla de 100
    // puntos en cada llamada era trabajo repetido. En Python seria
    // `if self._cache_valido: return self._fresult`.
    if (_cache_valido) return _fresult;

    ldouble res = 0 ;
    string fun = parse_tree(_vecval);

    std::size_t poss = fun.find("()");
    if (poss !=std::string::npos){
        _fresult  = INFINITY;
        _cache_valido = true;
        return _fresult ;
        }
// La expresion debe depender de x: basta cualquier hoja x-dependiente (la
// variable x o las hojas polinomiales x^2/x^3 del issue #63), no solo el
// token 11. En Python seria
// `if not any(is_x_dependent(int(t)) for t in self._vecval):`.
bool depende_de_x = std::any_of(_vecval.begin(), _vecval.end(),
                                [](ldouble t){ return is_x_dependent(static_cast<int>(t)); });
if (!depende_de_x){
    // Una expresion constante no es funcion de x: INFINITY sin tocar el
    // vector. Antes se escribian 11s al azar y luego se devolvia INFINITY
    // igual: la evaluacion mutaba al individuo y una segunda llamada
    // podia devolver otra cosa (B6 de REVIEW.md). Los productores
    // (vector_population, Mutation, Recombination) ya garantizan un token
    // x, asi que aqui solo se penaliza.
    _fresult  = INFINITY;
    _cache_valido = true;
    return _fresult ;
    }
    // Condicion inicial en x=0: [y(0), y'(0), y''(0)]. Antes se calculaba
    // tambien `b_end` (la misma evaluacion en x=1) y nunca se usaba; se
    // elimino (REVIEW C4, plan de correccion item 16).
    vector<ldouble> a_start = parsv(_vecval,_xrange[0]);
    // Cualquier componente NaN invalida el individuo completo. En Python seria
    // `any(math.isnan(componente) for componente in a_start)`; no hay una
    // conversion automatica de NaN a una excepcion en este codigo C++.
    if (isnan(a_start[0]) || isnan(a_start[1]) || isnan(a_start[2])){
        _fresult = INFINITY;
        _cache_valido = true;
        return _fresult;
    }

    // Hoist container size to avoid repeated casts in loop condition
    const int xrange_size = static_cast<int>(_xrange.size());
    for (int i = 0; i < xrange_size; i++){
        vector<ldouble> fitnessval(parsv(_vecval,_xrange[i]));
        //  cout << parse_tree(_vecval) << endl;

        //   cout << endl; cout << i << " " ;
        // cout <<" <<<<<"  << endl;
        // cin.get();

        // Cualquier componente NaN hace que todo el individuo sea no evaluable.
        // En Python equivale a `if any(math.isnan(c) for c in fitnessval):`
        // seguido de `return math.inf`; asi no existe una penalizacion finita
        // distinta para un punto sin informacion util.
        if (isnan(fitnessval[0]) || isnan(fitnessval[1]) || isnan(fitnessval[2])){
            _fresult = INFINITY;
            _cache_valido = true;
            return _fresult;
        }

        // Residuo de la ODE en el punto de la rejilla, elevado al cuadrado,
        // segun el caso (ver _caso). En Python seria `res += r**2`; la ec. (4)
        // del paper usa cuadrados, no potencias cuartas.
        if (_caso == 1){
            // Caso 1 del paper: x*y'' + y' - cos(x) = 0. Se evita
            // dividir por x=0, que es el primer punto de la rejilla.
            ldouble x = _xrange[i][0];
            res +=pow(x*fitnessval[2] + fitnessval[1] - cos(x),2) ;
        } else if (_caso == 2){
            // Caso 2 (issue #61): y'' - y = 0, la ecuacion hiperbolica. En
            // Python seria `res += (y2 - y)**2`.
            res +=pow(fitnessval[2] - fitnessval[0],2) ;
        } else if (_caso == 3){
            // Caso 3 (issue #103): oscilador amortiguado y'' + 2y' + 2y = 0.
            // En Python seria `res += (y2 + 2*y1 + 2*y0)**2`.
            res +=pow(fitnessval[2] + 2.0L*fitnessval[1] + 2.0L*fitnessval[0],2) ;
        } else if (_caso == 4){
            // Caso 4 (issue #103): oscilador forzado y'' + y = eˣ.
            // En Python seria `res += (y2 + y0 - exp(x))**2`.
            ldouble x = _xrange[i][0];
            res +=pow(fitnessval[2] + fitnessval[0] - exp(x),2) ;
        } else if (_caso == 5){
            // Caso 5 (issue #103): resonancia y'' - y = eˣ.
            // En Python seria `res += (y2 - y0 - exp(x))**2`.
            ldouble x = _xrange[i][0];
            res +=pow(fitnessval[2] - fitnessval[0] - exp(x),2) ;
        } else if (_caso == 6){
            // Caso 6 (issue #104): Legendre (1-x²)y'' - 2xy' + 6y = 0.
            // Primer caso con coeficientes variables: el residual usa x del
            // punto de la rejilla (_xrange[i][0]). En Python seria
            // `res += ((1 - x**2)*y2 - 2*x*y1 + 6*y0)**2`.
            ldouble x = _xrange[i][0];
            res +=pow((1.0L - x*x)*fitnessval[2] - 2.0L*x*fitnessval[1] + 6.0L*fitnessval[0],2) ;
        } else if (_caso == 7){
            // Caso 7 (issue #104): Hermite y'' - 2xy' + 4y = 0.
            // Segundo caso con coeficientes variables. En Python seria
            // `res += (y2 - 2*x*y1 + 4*y0)**2`.
            ldouble x = _xrange[i][0];
            res +=pow(fitnessval[2] - 2.0L*x*fitnessval[1] + 4.0L*fitnessval[0],2) ;
        } else {
            // Caso 0 (juguete del prototipo): y'' + y = 0.
            res +=pow(fitnessval[2] + fitnessval[0],2) ;
        }
    }
    // Integral de la ec. (4): los residuos al cuadrado suman una suma
    // de Riemann y el factor Delta x la convierte en la aproximacion
    // de integral(f^2 dx). Las penalizaciones de abajo quedan FUERA
    // de la integral. En Python seria `res *= self._dx`.
    res *= _dx;
    // Condiciones iniciales del caso, como terminos lambda*h^2 de la
    // ec. (4). El paper fija lambda1 = lambda2 = 10 (seccion 5) y h al
    // cuadrado; antes se usaban pesos 300/700 y potencias cuartas.
    // Issue #102: el switch reemplaza los ternarios que solo
    // distinguian 0.0L/1.0L para que cada caso declare valores
    // arbitrarios (fraccionarios como -1/2 o negativos como -2). En
    // Python seria `y0_objetivo, dy0_objetivo = IC_POR_CASO[self._caso]`,
    // con los valores por defecto del caso 0 si el caso no esta.
    ldouble y0_objetivo  = 1.0L;   // caso 0: y(0)=1 (solucion cos+sin)
    ldouble dy0_objetivo = 1.0L;   // caso 0: y'(0)=1
    switch (_caso){
        case 1:   // paper, ec. (18): x*y'' + y' - cos(x) = 0
            y0_objetivo  = 0.0L;
            dy0_objetivo = 1.0L;
            break;
        case 2:   // hiperbolico (issue #61): y'' - y = 0 => cosh(x)
            y0_objetivo  = 1.0L;
            dy0_objetivo = 0.0L;
            break;
        case 3:   // oscilador amortiguado (issue #103): y'' + 2y' + 2y = 0
            y0_objetivo  = 1.0L;
            dy0_objetivo = -1.0L;
            break;
        case 4:   // oscilador forzado (issue #103): y'' + y = eˣ
            y0_objetivo  = 0.0L;
            dy0_objetivo = 0.0L;
            break;
        case 5:   // resonancia (issue #103): y'' - y = eˣ
            y0_objetivo  = 0.0L;
            dy0_objetivo = 0.0L;
            break;
        case 6:   // Legendre (issue #104): y(0) = -1/2, y'(0) = 0
            y0_objetivo  = -0.5L;
            dy0_objetivo = 0.0L;
            break;
        case 7:   // Hermite (issue #104): y(0) = -2, y'(0) = 0
            y0_objetivo  = -2.0L;
            dy0_objetivo = 0.0L;
            break;
        default:  // caso 0: conserva (1,1)
            break;
    }
    res +=10*pow((a_start[0] - y0_objetivo),2) + 10*pow((a_start[1] - dy0_objetivo),2);
    // cout << "imprimimos res " << endl;
    // cout << res << endl;
    // cin.get();
    if (isnan(res)){
        _fresult = INFINITY;
        _cache_valido = true;
        return _fresult;
    }
    _fresult = res;
    _cache_valido = true;
    return _fresult;
}
