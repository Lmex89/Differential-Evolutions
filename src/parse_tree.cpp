#include <iostream>
#include <array>
#include <vector>
#include <ctime>
#include <cmath>
#include <cstring>
#include <cassert>
#include <climits>
#include <cstdlib>
#include <algorithm>
#include <functional>
#include <utility>
#include <sstream>
#include <iomanip>
#include "dualfunctions.h"
#include "funbobj.h"
#include "semilla.h"
#include "salida.h"
#include "token.h"
#include "restart_lifecycle.h"

using namespace std;

// ---------------------------------------------------------------------------
// Configuracion de la busqueda (issue #28).
//
// Los valores por defecto son los del Caso 1 del paper (REVIEW.md C5-C7):
// 100 individuos, dimension 20 y 5000 generaciones. Cada uno se puede
// sobreescribir con una variable de entorno, igual que ODE_SEED (issue #4):
// ODE_POP, ODE_DIM, ODE_GEN y ODE_WARM_START. Para un desarrollador Python:
// ConfigBusqueda seria un `@dataclass` con defaults y resolver_config su
// `from_env()`, funcion pura para poder probarla sin tocar el entorno.
// ---------------------------------------------------------------------------

// Valores por defecto (Caso 1 del paper, REVIEW.md C5-C7).
constexpr int POBLACION_DEFECTO = 100;
constexpr int DIMENSION_DEFECTO = 20;
constexpr int GENERACIONES_DEFECTO = 5000;

// Minimos aceptados. DE/rand/1/bin necesita tres individuos base distintos
// del objetivo (indices_distintos), asi que por debajo de 4 la busqueda
// degenera; 1 es la dimension minima de un arbol y 1 el tope minimo de
// generaciones.
constexpr int POBLACION_MINIMA = 4;
constexpr int DIMENSION_MINIMA = 1;
constexpr int GENERACIONES_MINIMAS = 1;

// ConfigBusqueda: los parametros de la corrida en una estructura en vez de
// argumentos sueltos (regla I.23 de docs/cpp-standards.md: firmas cortas).
// Los `= valor` son los defaults, como en un dataclass de Python.
// Solo lleva parametros de busqueda (poblacion, dimension, generaciones, warm_start).
// Los parametros de reinicio viven en ConfigReinicio (issue #166).
struct ConfigBusqueda {
    int poblacion = POBLACION_DEFECTO;
    int dimension = DIMENSION_DEFECTO;
    int max_generaciones = GENERACIONES_DEFECTO;
    bool warm_start = false;
};

// config_entera: lee una variable de entorno ya obtenida con getenv() (en
// Python seria el string de os.environ.get(nombre), o None) y devuelve su
// valor entero solo si representa un numero >= `minimo`; si falta, no es
// numerica o queda fuera de rango, devuelve el respaldo. Es la regla de
// ODE_SEED: una variable invalida se ignora, no rompe la corrida.
int config_entera(const char* valor_env, int respaldo, int minimo) {
    if (valor_env == nullptr) return respaldo;

    // strtol interpreta el texto como entero en base 10 (como int() de
    // Python) y deja en `fin` la posicion del primer caracter no consumido.
    char* fin = nullptr;
    long valor = strtol(valor_env, &fin, 10);

    // Se exige que TODO el texto sea el numero ("17abc" no vale): es el
    // `except ValueError` de Python. La cota superior evita que un valor
    // enorme desborde al convertirlo a int.
    if (fin == valor_env || *fin != '\0') return respaldo;
    if (valor < static_cast<long>(minimo) || valor > static_cast<long>(INT_MAX)) return respaldo;

    return static_cast<int>(valor);
}

// config_decimal: lee una variable de entorno y devuelve su valor long double
// solo si representa un numero valido; si falta o no es numerica, devuelve el
// respaldo. Es el equivalente de config_entera para numeros de punto flotante.
// En Python seria `float(valor_env)` con manejo de excepciones.
long double config_decimal(const char* valor_env, long double respaldo) {
    if (valor_env == nullptr) return respaldo;

    // strtold interpreta el texto como long double (como float() de Python)
    // y deja en `fin` la posicion del primer caracter no consumido.
    char* fin = nullptr;
    long double valor = strtold(valor_env, &fin);

    // Se exige que TODO el texto sea el numero, igual que config_entera.
    if (fin == valor_env || *fin != '\0') return respaldo;

    return valor;
}

// config_bandera: lee una bandera de entorno: "1" activa, "0" desactiva y
// cualquier otro valor (o la ausencia) devuelve el respaldo. En Python seria
// `{"1": True, "0": False}.get(valor, respaldo)`.
bool config_bandera(const char* valor_env, bool respaldo) {
    if (valor_env == nullptr) return respaldo;
    if (strcmp(valor_env, "1") == 0) return true;
    if (strcmp(valor_env, "0") == 0) return false;
    return respaldo;
}

// config_modo_reinicio: lee el modo de reinicio (issue #126). "adaptive"
// activa el modo nuevo; cualquier otro valor (o la ausencia) devuelve legacy.
// En Python seria `{"adaptive": ModoReinicio.ADAPTIVE}.get(valor, LEGACY)`.
ModoReinicio config_modo_reinicio(const char* valor_env) {
    if (valor_env == nullptr) return ModoReinicio::legacy;
    if (strcmp(valor_env, "adaptive") == 0) return ModoReinicio::adaptive;
    return ModoReinicio::legacy;
}

// nombre_modo_reinicio: texto del modo para la cabecera del log. En Python
// seria `modo.name.lower()`.
const char* nombre_modo_reinicio(ModoReinicio modo) {
    return modo == ModoReinicio::adaptive ? "adaptive" : "legacy";
}

// defaults_por_caso: devuelve los defaults de (poblacion, dimension,
// generaciones) segun el caso ODE. El paper (Mex et al. 2015) tunnea estos
// parametros por caso: los casos 0-2 usan defaults originales (100/20/5000),
// mientras que los casos 3-5 requieren busquedas mas agresivas porque sus
// soluciones exactas tienen 6/11/15 tokens. En Python seria un dict de
// defaults por caso.
struct DefaultsCaso {
    int poblacion;
    int dimension;
    int generaciones;
};

DefaultsCaso defaults_por_caso(int caso) {
    // Casos 0-2: soluciones cortas (2-5 tokens), defaults del paper.
    // Casos 3-5: soluciones mas largas (6-15 tokens), necesitan mas
    // poblacion, dimension y generaciones para converger a J < 1e-5.
    if (caso >= 3 && caso <= 5) {
        return {200, 25, 10000};
    }
    return {POBLACION_DEFECTO, DIMENSION_DEFECTO, GENERACIONES_DEFECTO};
}

// resolver_config: arma la configuracion efectiva de la corrida a partir de
// los valores crudos de ODE_POP/ODE_DIM/ODE_GEN/ODE_WARM_START/ODE_STALE_LIMIT/
// ODE_RESTART_FRACTION y, desde el issue #126, ODE_RESTART_MODE/
// ODE_RESTART_MIN_FRACTION/ODE_RESTART_CAP/ODE_RESTART_MUTATION_SHARE/
// ODE_DIV_THRESHOLD. main() lee el entorno con getenv() y se los pasa
// (por eso es una funcion pura y testeable, igual que resolver_semilla).
// Los defaults por caso (paper practice) son el fallback para pop/dim/gen.
// Los parametros nuevos van al final con default nullptr: los llamadores que
// no los pasan (tests viejos incluidos) compilan sin cambios.
// Issue #166: devuelve ambas configuraciones (busqueda y reinicio) por separado.
void resolver_config(const char* pop_env, const char* dim_env,
                     const char* gen_env, const char* warm_env,
                     int caso,
                     const char* stale_env,
                     const char* fraction_env,
                     const char* restart_mode_env,
                     const char* restart_min_fraction_env,
                     const char* restart_cap_env,
                     const char* restart_mutation_share_env,
                     const char* div_threshold_env,
                     ConfigBusqueda& config_busqueda,
                     ConfigReinicio& config_reinicio) {
    DefaultsCaso defaults = defaults_por_caso(caso);
    
    // Configuracion de busqueda (poblacion, dimension, generaciones, warm_start).
    config_busqueda.poblacion = config_entera(pop_env, defaults.poblacion, POBLACION_MINIMA);
    config_busqueda.dimension = config_entera(dim_env, defaults.dimension, DIMENSION_MINIMA);
    config_busqueda.max_generaciones = config_entera(gen_env, defaults.generaciones, GENERACIONES_MINIMAS);

    // El warm start ya no se limita a la dimension historica de 20 tokens:
    // cada entrada del catalogo se rellena con constantes 1 hasta la dimension
    // configurada (issue #142). Asi los casos 3-5 (dimensiones 25/30) tambien
    // pueden sembrarse y Mutation()/Recombination() siguen emparejando genes
    // por posicion (B5 de REVIEW.md).
    config_busqueda.warm_start = config_bandera(warm_env, false);

    // Configuracion de reinicio (issue #166): solo hechos de stale, modo,
    // escalacion, mutation-share y Diversity.
    config_reinicio.stale_limit = config_entera(stale_env, 500, 1);
    config_reinicio.restart_fraction = config_decimal(fraction_env, 0.8L);
    config_reinicio.restart_mode = config_modo_reinicio(restart_mode_env);
    config_reinicio.restart_min_fraction = config_decimal(restart_min_fraction_env, 0.4L);
    config_reinicio.restart_cap = config_entera(restart_cap_env, 3, 1);
    config_reinicio.restart_mutation_share = config_decimal(restart_mutation_share_env, 0.5L);
    config_reinicio.div_threshold = config_decimal(div_threshold_env, 0.05L);
}


// Tamano de los tres tramos del catalogo warm start: cinco bases por caso
// (issue #143), cinco semillas precargadas del prototipo (issue #144) y
// quince bloques genericos compartidos (issue #145). Son `size_t` a proposito:
// en los tests se comparan contra indices y tamanos de vector, y asi toda la
// aritmetica de indices queda sin signo (ES.100). En Python serian constantes
// de modulo `NUM_BASES_POR_CASO = 5`, `NUM_PRECARGADAS = 5` y
// `NUM_BLOQUES_GENERICOS = 15`.
constexpr size_t NUM_BASES_POR_CASO = 5;
constexpr size_t NUM_PRECARGADAS = 5;
constexpr size_t NUM_BLOQUES_GENERICOS = 15;

// EntradaCatalogo: referencia no propietaria a una entrada del catalogo warm
// start. `std::reference_wrapper` es el equivalente C++ de una referencia
// Python a una lista: copiar la entrada copia la referencia, no la lista. En
// Python el catalogo seria `{caso: [bases..., *PRECARGADAS, *GENERICOS]}`,
// donde los tramos compartidos son las mismas listas en todos los casos.
// No transfiere propiedad (I.11): el vector referenciado vive en un `static`
// de `semillas_precargadas()`, `bloques_genericos()` o `bases_por_caso`.
using EntradaCatalogo = std::reference_wrapper<const vector<ldouble>>;

// semillas_precargadas: las cinco semillas trigonometricas escritas a mano del
// prototipo (issue #144). Se conservan con sus 20 tokens originales en vez de
// recortarlas a su expresion prefija minima: el contrato del issue es que su
// contenido no cambie. createPopulation las rellena con constantes 1 solo si la
// dimension configurada supera 20. En Python seria una constante de modulo
// `PRECARGADAS = [[...], ..., [...]]`.
const vector<vector<ldouble>>& semillas_precargadas(){
    // `static const` se construye una sola vez, como una constante de modulo
    // en Python, y vive mientras dure el proceso.
    static const vector<vector<ldouble>> semillas = {
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
    // El tramo precargado mide exactamente NUM_PRECARGADAS (issue #144): si
    // el contenido historico se alterara, este assert delata el drift antes
    // que el oraculo de tests/precargadas_prototipo.h. En Python seria
    // `assert len(PRECARGADAS) == NUM_PRECARGADAS`.
    assert(semillas.size() == NUM_PRECARGADAS);
    return semillas;
}

// bloques_genericos (issue #145): quince bloques de expresion bien formados
// compartidos entre todos los casos. Ocupan los indices 10-24 del catalogo y
// cada caso los referencia sin copiarlos: son las mismas instancias en los
// ocho catalogos. Son expresiones prefijas minimas, todas dependen de x y
// evaluan a fitness finita en cualquier caso. En Python seria una constante
// de modulo `GENERIC_BLOCKS = [[...], ..., [...]]`.
const vector<vector<ldouble>>& bloques_genericos(){
    // `static const` se construye una sola vez, como una constante de modulo
    // en Python, y vive mientras dure el proceso. Cada vector es la expresion
    // prefija minima del bloque; createPopulation la rellena con constantes 1
    // hasta la dimension configurada.
    static const vector<vector<ldouble>> genericos = {
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
    // El tramo generico mide exactamente NUM_BLOQUES_GENERICOS (issue #145):
    // si la lista se alterara, este assert delata el drift. En Python seria
    // `assert len(GENERIC_BLOCKS) == NUM_BLOQUES_GENERICOS`.
    assert(genericos.size() == NUM_BLOQUES_GENERICOS);
    return genericos;
}

// catalogo_semillas: entradas warm start del caso, en el orden en que se
// inyectan (bases del caso, luego las precargadas compartidas y, desde #145,
// los bloques genericos). Las bases son expresiones prefijas minimas; las
// precargadas conservan sus 20 tokens historicos. Cada entrada es una vista
// (`EntradaCatalogo`), no una copia: los tramos compartidos son las mismas
// instancias en todos los casos. createPopulation rellena cada entrada con
// constantes 1 hasta la dimension configurada. En Python seria un dict
// `CATALOGO = {0: [refs...], ..., 7: [refs...]}` y
// `CATALOGO.get(caso, [])`.
const vector<EntradaCatalogo>& catalogo_semillas(int caso){
    // Issue #143: cinco bases por caso. La primera es la solucion exacta (o la
    // familia de la serie truncada en el caso 1); las otras cuatro son bloques
    // cercanos de la misma familia. El ticket #144 agrega despues las semillas
    // precargadas; #145 agrega los bloques genericos.
    static constexpr int NUM_CASOS = 8;
    static const vector<vector<vector<ldouble>>> bases_por_caso = {
        // Caso 0: y'' + y = 0, y(0)=1, y'(0)=1 -> cos(x) + sin(x).
        {
            {TOKEN_ADD, TOKEN_COS, TOKEN_X, TOKEN_SIN, TOKEN_X},
            {TOKEN_COS, TOKEN_X},
            {TOKEN_SIN, TOKEN_X},
            {TOKEN_ADD, TOKEN_CONST1, TOKEN_X},
            {TOKEN_ADD, TOKEN_ADD, TOKEN_CONST1, TOKEN_X,
             TOKEN_NEG, TOKEN_DIV, TOKEN_X2, TOKEN_CONST2}
        },
        // Caso 1: x*y'' + y' - cos(x) = 0, y(0)=0, y'(0)=1 -> Si(x). El
        // alfabeto no tiene integral, asi que las bases son la serie truncada
        // x - x^3/18 + x^5/600 y variantes equivalentes.
        {
            {TOKEN_X},
            {TOKEN_ADD, TOKEN_X, TOKEN_NEG, TOKEN_DIV, TOKEN_X3,
             TOKEN_MUL, TOKEN_CONST3, TOKEN_CONST6},
            {TOKEN_ADD, TOKEN_ADD, TOKEN_X, TOKEN_NEG, TOKEN_DIV, TOKEN_X3,
             TOKEN_MUL, TOKEN_CONST3, TOKEN_CONST6,
             TOKEN_DIV, TOKEN_MUL, TOKEN_X3, TOKEN_X2,
             TOKEN_MUL, TOKEN_MUL, TOKEN_CONST6, TOKEN_CONST10, TOKEN_CONST10},
            {TOKEN_MUL, TOKEN_X, TOKEN_ADD, TOKEN_CONST1, TOKEN_NEG,
             TOKEN_DIV, TOKEN_X2, TOKEN_MUL, TOKEN_CONST3, TOKEN_CONST6},
            {TOKEN_ADD, TOKEN_X, TOKEN_NEG, TOKEN_DIV, TOKEN_POW, TOKEN_X,
             TOKEN_CONST3, TOKEN_MUL, TOKEN_CONST3, TOKEN_CONST6}
        },
        // Caso 2: y'' - y = 0, y(0)=1, y'(0)=0 -> cosh(x).
        {
            {TOKEN_COSH, TOKEN_X},
            {TOKEN_DIV, TOKEN_ADD, TOKEN_EXP, TOKEN_X,
             TOKEN_EXP, TOKEN_NEG, TOKEN_X, TOKEN_CONST2},
            {TOKEN_ADD, TOKEN_CONST1, TOKEN_DIV, TOKEN_X2, TOKEN_CONST2},
            {TOKEN_EXP, TOKEN_X},
            {TOKEN_ADD, TOKEN_ADD, TOKEN_CONST1, TOKEN_DIV, TOKEN_X2,
             TOKEN_CONST2, TOKEN_DIV, TOKEN_MUL, TOKEN_X2, TOKEN_X2,
             TOKEN_MUL, TOKEN_CONST4, TOKEN_CONST6}
        },
        // Caso 3: y'' + 2y' + 2y = 0, y(0)=1, y'(0)=-1 -> e^(-x)*cos(x).
        {
            {TOKEN_MUL, TOKEN_EXP, TOKEN_NEG, TOKEN_X, TOKEN_COS, TOKEN_X},
            {TOKEN_MUL, TOKEN_EXP, TOKEN_NEG, TOKEN_X, TOKEN_SIN, TOKEN_X},
            {TOKEN_EXP, TOKEN_NEG, TOKEN_X},
            {TOKEN_MUL, TOKEN_EXP, TOKEN_NEG, TOKEN_X,
             TOKEN_ADD, TOKEN_COS, TOKEN_X, TOKEN_NEG, TOKEN_SIN, TOKEN_X},
            {TOKEN_COS, TOKEN_X}
        },
        // Caso 4: y'' + y = e^x, y(0)=y'(0)=0
        // -> (e^x - cos(x) - sin(x)) / 2.
        {
            {TOKEN_DIV, TOKEN_ADD, TOKEN_EXP, TOKEN_X, TOKEN_NEG,
             TOKEN_ADD, TOKEN_COS, TOKEN_X, TOKEN_SIN, TOKEN_X,
             TOKEN_CONST2},
            {TOKEN_DIV, TOKEN_EXP, TOKEN_X, TOKEN_CONST2},
            {TOKEN_DIV, TOKEN_ADD, TOKEN_EXP, TOKEN_X, TOKEN_NEG,
             TOKEN_CONST1, TOKEN_CONST2},
            {TOKEN_DIV, TOKEN_NEG, TOKEN_ADD, TOKEN_COS, TOKEN_X,
             TOKEN_SIN, TOKEN_X, TOKEN_CONST2},
            {TOKEN_ADD, TOKEN_EXP, TOKEN_X, TOKEN_NEG, TOKEN_COS, TOKEN_X}
        },
        // Caso 5: y'' - y = e^x, y(0)=y'(0)=0
        // -> (2*x*e^x - e^x + e^(-x)) / 4.
        {
            {TOKEN_DIV, TOKEN_ADD, TOKEN_MUL, TOKEN_EXP, TOKEN_X,
             TOKEN_ADD, TOKEN_MUL, TOKEN_CONST2, TOKEN_X,
             TOKEN_NEG, TOKEN_CONST1,
             TOKEN_EXP, TOKEN_NEG, TOKEN_X, TOKEN_CONST4},
            {TOKEN_DIV, TOKEN_MUL, TOKEN_X, TOKEN_EXP, TOKEN_X,
             TOKEN_CONST2},
            {TOKEN_MUL, TOKEN_X, TOKEN_EXP, TOKEN_X},
            {TOKEN_SINH, TOKEN_X},
            {TOKEN_ADD, TOKEN_DIV, TOKEN_MUL, TOKEN_X, TOKEN_EXP, TOKEN_X,
             TOKEN_CONST2, TOKEN_NEG, TOKEN_DIV, TOKEN_EXP, TOKEN_X,
             TOKEN_CONST4}
        },
        // Caso 6: (1-x^2)*y'' - 2x*y' + 6y = 0, y(0)=-1/2, y'(0)=0
        // -> P2(x) = (3*x^2-1)/2.
        {
            {TOKEN_DIV, TOKEN_ADD, TOKEN_MUL, TOKEN_CONST3,
             TOKEN_X2, TOKEN_NEG, TOKEN_CONST1, TOKEN_CONST2},
            {TOKEN_DIV, TOKEN_MUL, TOKEN_CONST3, TOKEN_X2, TOKEN_CONST2},
            {TOKEN_ADD, TOKEN_MUL, TOKEN_CONST3, TOKEN_X2,
             TOKEN_NEG, TOKEN_CONST1},
            {TOKEN_ADD, TOKEN_X2, TOKEN_NEG, TOKEN_DIV,
             TOKEN_CONST1, TOKEN_CONST2},
            {TOKEN_X2}
        },
        // Caso 7: y'' - 2x*y' + 4y = 0, y(0)=-2, y'(0)=0
        // -> H2(x) = 4*x^2-2.
        {
            {TOKEN_ADD, TOKEN_MUL, TOKEN_CONST4, TOKEN_X2,
             TOKEN_NEG, TOKEN_CONST2},
            {TOKEN_MUL, TOKEN_CONST2, TOKEN_ADD, TOKEN_MUL, TOKEN_CONST2,
             TOKEN_X2, TOKEN_NEG, TOKEN_CONST1},
            {TOKEN_MUL, TOKEN_CONST4, TOKEN_X2},
            {TOKEN_ADD, TOKEN_MUL, TOKEN_CONST2, TOKEN_X2,
             TOKEN_NEG, TOKEN_CONST2},
            {TOKEN_ADD, TOKEN_MUL, TOKEN_CONST4, TOKEN_POW, TOKEN_X,
             TOKEN_CONST2, TOKEN_NEG, TOKEN_CONST2}
        }
    };
    // catalogo: une cada tramo de bases con el tramo precargado compartido y
    // el tramo generico compartido. Las entradas son vistas no propietarias:
    // las bases viven en bases_por_caso y los tramos compartidos en sus
    // funciones estaticas, de modo que los quince genericos son las mismas
    // instancias en los ocho casos (issue #145). El lambda invocado al momento
    // (`[](){...}()`) es el idioma C++ para inicializar una constante con
    // codigo; en Python seria una comprension de diccionario que guarda
    // referencias a las mismas listas:
    // `{caso: [*bases, *PRECARGADAS, *GENERICOS] for caso, bases in ...}`.
    static const vector<vector<EntradaCatalogo>> catalogo = []{
        vector<vector<EntradaCatalogo>> entradas_por_caso;
        entradas_por_caso.reserve(bases_por_caso.size());
        for (const auto& bases : bases_por_caso) {
            // Cada caso aporta exactamente NUM_BASES_POR_CASO bases (#143):
            // el catalogo se arma por yuxtaposicion de referencias y un caso
            // con un tramo de distinto largo romperia los indices 0-4 que
            // asumen los tests.
            assert(bases.size() == NUM_BASES_POR_CASO);
            vector<EntradaCatalogo> entradas;
            entradas.reserve(NUM_BASES_POR_CASO + NUM_PRECARGADAS +
                             NUM_BLOQUES_GENERICOS);
            for (const auto& base : bases) entradas.emplace_back(base);
            for (const auto& precargada : semillas_precargadas()) {
                entradas.emplace_back(precargada);
            }
            for (const auto& generico : bloques_genericos()) {
                entradas.emplace_back(generico);
            }
            entradas_por_caso.push_back(std::move(entradas));
        }
        return entradas_por_caso;
    }();
    static const vector<EntradaCatalogo> sin_entradas;

    if (caso >= 0 && caso < NUM_CASOS) return catalogo[caso];
    return sin_entradas;
}

// cuota_semillas (issue #146): cuantas entradas del catalogo se inyectan en
// una poblacion de `poblacion` individuos: min(ceil(0.05 * poblacion),
// tamano_catalogo). `(poblacion + 19) / 20` es ceil(poblacion / 20) en
// aritmetica entera (el +19 empuja el residuo al siguiente entero), sin
// depender de <cmath>. Con el catalogo actual de 25 entradas: pop=100 -> 5,
// pop=200 -> 10, pop=300 -> 15, pop=400 -> 20, pop=500 -> 25 y de pop=500
// en adelante el tope es el catalogo completo.
// En Python seria `min(math.ceil(0.05 * poblacion), len(catalogo))`.
int cuota_semillas(int poblacion, int tamano_catalogo){
    const int sin_tope = (poblacion + 19) / 20;
    return std::min(sin_tope, tamano_catalogo);
}

// semilla_valida (issue #147): predicado que decide si una entrada del
// catalogo puede inyectarse como individuo. Debe caber en la dimension (si no,
// no se puede rellenar sin truncar el arbol), renderizar sin huecos, contener
// al menos una hoja x-dependiente y tener fitness finita para el caso activo.
// En Python seria:
//     def semilla_valida(entrada, dimension, caso):
//         if len(entrada) > dimension: return False
//         semilla = entrada + [TOKEN_CONST1] * (dimension - len(entrada))
//         individuo = FunbObj(semilla, 0, 1, caso)
//         return ("()" not in individuo.represant()
//                 and any(is_x_dependent(t) for t in semilla)
//                 and math.isfinite(individuo.fitness()))
bool semilla_valida(const vector<ldouble>& entrada, int dimension, int caso){
    if (static_cast<int>(entrada.size()) > dimension) return false;

    vector<ldouble> semilla = entrada;
    semilla.resize(dimension, TOKEN_CONST1);

    const bool depende_de_x = std::any_of(
        semilla.begin(), semilla.end(),
        [](ldouble token){ return is_x_dependent(static_cast<int>(token)); });
    if (!depende_de_x) return false;

    FunbObj individuo(std::move(semilla), 0, 1, caso);
    return individuo.represant().find("()") == string::npos &&
           std::isfinite(individuo.fitness());
}

// createPopulation: llena `Population` (que entra vacia) con exactamente
// config.poblacion individuos de config.dimension tokens. Es la inicializacion
// del paper: aleatoria uniforme (ecuacion (6)) salvo las entradas del catalogo
// warm start, que solo entran con la bandera activa. Devuelve cuantas entradas
// del catalogo se inyectaron de verdad (el `warm_seeds=` del log).
//
// La variante con catalogo explicito permite probar la garantia de runtime con
// entradas sinteticas (issue #147) sin tocar el catalogo real; main() usa la
// sobrecarga de tres argumentos, que pasa `catalogo_semillas(caso)`. En Python
// seria `createPopulation(poblacion, config, caso, CATALOGO.get(caso, []))`
// con un parametro opcional.
int createPopulation(vector<FunbObj>& Population, const ConfigBusqueda& config,
                     int caso, const vector<EntradaCatalogo>& catalogo){

    // Precondiciones del inicializador de poblacion:
    // 1. La poblacion entra vacia y se llena con push_back (en Python seria
    //    `assert not poblacion`). Activa porque no se compila con -DNDEBUG.
    // 2. poblacion >= 1: una solicitud de longitud cero no tiene sentido en el
    //    bucle DE (necesitamos al menos un individuo para evaluar fitness). En
    //    Python seria `assert config.poblacion >= 1, "population must have at
    //    least 1 individual"`.
    assert(Population.empty());
    assert(config.poblacion >= 1);

    // Warm start: cada entrada del catalogo del caso es una expresion prefija
    // minima; se rellena con constantes 1 hasta la dimension configurada. El
    // relleno final no altera la evaluacion porque el interprete consume el
    // arbol desde el inicio y las hojas sobrantes no participan (mismo patron
    // que las semillas historicas).
    //
    // La garantia de runtime (issue #147) valida cada intento antes de
    // conservarlo: `semilla_valida` exige que la entrada quepa en la dimension,
    // renderice sin huecos, dependa de x y tenga fitness finita para el caso.
    // Un intento rechazado no se inyecta: su lugar lo ocupa un individuo
    // uniforme para que la poblacion conserve exactamente config.poblacion.
    // La cuota del 5% (issue #146) cuenta INTENTOS, no semillas aceptadas:
    // min(ceil(0.05 * poblacion), tamano del catalogo). Asi una entrada rota
    // no permite que una entrada posterior fuera de la cuota se cuele en el
    // censo; `semillas_inyectadas` solo cuenta las validas.
    int semillas_inyectadas = 0;
    if (config.warm_start){
        const int cuota = cuota_semillas(config.poblacion,
                                         static_cast<int>(catalogo.size()));
        for (size_t i = 0;
             i < catalogo.size() &&
             static_cast<int>(i) < cuota &&
             static_cast<int>(Population.size()) < config.poblacion;
             i++){
            const vector<ldouble>& entrada = catalogo[i].get();
            if (semilla_valida(entrada, config.dimension, caso)){
                vector<ldouble> semilla = entrada;
                semilla.resize(config.dimension, TOKEN_CONST1);
                // `caso` se propaga a cada FunbObj: los tokens no cambian
                // entre casos, solo la ODE que mide fitness(). En Python seria
                // `FunbObj(semilla, 0, 1, caso)`.
                Population.push_back(FunbObj(std::move(semilla), 0, 1, caso));
                semillas_inyectadas++;
            } else {
                Population.push_back(FunbObj::create_random(config.dimension, caso));
            }
        }
    }

    // El resto (o la poblacion entera sin warm start) es aleatorio: tantos
    // individuos nuevos como falten para llegar a config.poblacion, sin el
    // off-by-one de REVIEW C5.
    while (static_cast<int>(Population.size()) < config.poblacion){
        Population.push_back(FunbObj::create_random(config.dimension, caso));
    }

    return semillas_inyectadas;
}

// Sobrecarga de produccion: inyecta el catalogo del caso. En Python seria el
// valor por defecto `catalogo=None` resuelto a `CATALOGO.get(caso, [])`.
int createPopulation(vector<FunbObj>& Population, const ConfigBusqueda& config, int caso){
    return createPopulation(Population, config, caso, catalogo_semillas(caso));
}

// -------------------------------------------------------------------------
// Diversity y reinicio ahora en src/restart_lifecycle.{h,cpp} (issue #166).
// El módulo expone: ResumenDiversidad, resumir_diversidad(), 
// dispara_reinicio_por_diversidad(), ControlReinicio, DisparadorReinicio,
// DecisionReinicio, EscaladorReinicio, ModoReinicio.
// -------------------------------------------------------------------------

// parte_entera: cuantos elementos representa la fraccion `fraccion` de un
// total `total`, truncando hacia abajo. Es el int(f*n) que el reinicio usa
// tanto para la fraccion de reemplazo como para la cuota de mutacion, en un
// solo lugar. En Python seria `int(fraccion * total)`.
int parte_entera(long double fraccion, int total) {
    return static_cast<int>(fraccion * total);
}

// reiniciar_poblacion: mecanismo de escape de minimos locales (issue #82).
// Reemplaza el 80% de la poblacion con individuos aleatorios frescos para
// inyectar diversidad, pero antes restaura el mejor historico `record` en el
// indice reservado `indice_record` y no vuelve a tocar ese indice: asi el
// reinicio no puede perder el record (elitismo real). Devuelve cuantos
// individuos se reemplazaron, que es el campo `replaced=` del log `restart |`.
// Issue #166: ahora acepta ambas configuraciones (busqueda y reinicio) por separado.
// En Python seria
// `reemplazados = reiniciar_poblacion(poblacion, indice, record, config_busqueda, config_reinicio, caso)`.
int reiniciar_poblacion(vector<FunbObj>& poblacion, int indice_record,
                        const vector<ldouble>& record,
                        const ConfigBusqueda& config_busqueda,
                        const ConfigReinicio& config_reinicio, int caso) {
    // Precondicion: el indice reservado apunta dentro de la poblacion (evita
    // el acceso fuera de rango si un refactor cambia el calculo de `mejor`).
    // En Python seria `assert 0 <= indice_record < len(poblacion)`.
    assert(indice_record >= 0 && indice_record < static_cast<int>(poblacion.size()));

    // Elitismo: el record entra al indice reservado ANTES del barrido, para
    // que el bucle (que salta `indice_record`) lo deje intacto. Un `record`
    // vacio significa que la corrida nunca encontro un fitness finito: no hay
    // nada que restaurar y el indice queda como estaba.
    if (!record.empty()){
        poblacion[indice_record] = FunbObj(record, 0, 1, caso);
    }

    // La fraccion de reemplazo viene de config_reinicio.restart_fraction (issue #108),
    // configurable via ODE_RESTART_FRACTION. Antes era hardcoded 0.8.
    const int tam_poblacion = static_cast<int>(poblacion.size());
    const int cantidad_reemplazar = parte_entera(config_reinicio.restart_fraction, tam_poblacion);
    int reemplazados = 0;
    for (int j = 0; j < tam_poblacion && reemplazados < cantidad_reemplazar; j++){
        if (j != indice_record){
            poblacion[j] = FunbObj::create_random(config_busqueda.dimension, caso);
            reemplazados++;
        }
    }
    return reemplazados;
}

// ParametrosReinicioAdaptativo: opciones de un reinicio adaptativo puntual
// (issue #127). Es un struct para no seguir sumando argumentos sueltos a la
// rutina (regla I.23 de docs/cpp-standards.md: `FunbObj(vecval, start, stop,
// caso)` ya esta en el limite). `fraccion` y `cuota_mutacion` viajan por
// reinicio porque el issue #128 las escala entre reinicios consecutivos;
// `dimension` y `caso` los comparte toda la corrida. Los cuatro campos son
// obligatorios (sin `= valor`): unos defaults aqui duplicarian los de
// ConfigBusqueda/resolver_config (regla ES.45) y todo llamador los pasa.
struct ParametrosReinicioAdaptativo {
    long double fraccion;        // parte de la poblacion que se reemplaza
    long double cuota_mutacion;  // parte de los reemplazos que muta elites
    int dimension;               // longitud de los uniformes nuevos
    int caso;                    // ODE que evalua fitness()
};

// Cuantos tokens puede tocar una mutacion puntual: entre 1 y 2 (issue #127).
constexpr int CAMBIOS_PUNTUALES_MIN = 1;
constexpr int CAMBIOS_PUNTUALES_MAX = 2;

// mutacion_puntual_en: nucleo puro del refinamiento local (issue #127): copia
// `base` y escribe `token` en `posicion`. Sin rand(), como mutacion_rand1: la
// sustitucion se puede probar con valores a mano, y el sorteo de posiciones y
// tokens vive en su driver mutacion_puntual. En Python seria
// `copia = list(base); copia[posicion] = token; return copia`.
vector<ldouble> mutacion_puntual_en(const vector<ldouble>& base, int posicion, int token){
    vector<ldouble> copia(base);
    copia[posicion] = token;
    return copia;
}

// mutacion_puntual: driver aleatorio: copia `base` (el vector de un elite) y
// sustituye 1 o 2 posiciones al azar por tokens nuevos distintos de los que
// habia, de modo que el cambio sea exactamente de 1-2 tokens y la longitud se
// conserve. Es la mutacion de refinamiento local del reinicio adaptativo
// (issue #127), no la mutacion DE/rand/1 del paper. En Python seria
// `copia = list(base); posiciones = random.sample(range(len(copia)), 1 + random.randint(0, 1)); ...`.
vector<ldouble> mutacion_puntual(const vector<ldouble>& base){
    const int n = static_cast<int>(base.size());
    if (n == 0) return base;

    // 1 o 2 cambios, nunca mas posiciones que tokens tiene el vector.
    int cambios = CAMBIOS_PUNTUALES_MIN +
                  rand() % (CAMBIOS_PUNTUALES_MAX - CAMBIOS_PUNTUALES_MIN + 1);
    if (cambios > n) cambios = n;

    // Primera posicion al azar y token nuevo distinto del anterior. El
    // do-while es como el de indices_distintos: el alfabeto tiene 29 tokens
    // (token.h), asi que el reintento termina (no hay bucle infinito posible).
    // Los tokens viven como long double en el vector (el tipo comun de la
    // representacion); cada token es un codigo entero, de ahi el static_cast
    // al compararlos.
    vector<ldouble> copia(base);
    const int primera = rand() % n;
    int nuevo = 0;
    do { nuevo = random_token(); } while (nuevo == static_cast<int>(copia[primera]));
    copia = mutacion_puntual_en(copia, primera, nuevo);

    if (cambios == CAMBIOS_PUNTUALES_MAX){
        // Segunda posicion distinta de la primera: se sortea sobre n-1 y se
        // corre un lugar si cayo a la derecha de la primera (evita el bucle
        // de rechazo y consume exactamente un rand()).
        int segunda = rand() % (n - 1);
        if (segunda >= primera) segunda++;
        do { nuevo = random_token(); } while (nuevo == static_cast<int>(copia[segunda]));
        copia = mutacion_puntual_en(copia, segunda, nuevo);
    }

    return copia;
}

// reiniciar_poblacion_adaptativo: reinicio por estancamiento del modo
// adaptativo (issue #127). A diferencia del legacy, ordena la poblacion por
// fitness y reemplaza SOLO a los peores `fraccion`, de modo que los
// sobrevivientes son los individuos realmente mas aptos y ninguno de ellos se
// toca. Devuelve cuantos lugares se reemplazaron (el campo `replaced=` del log
// `restart |`; los que inyecto el record incluidos), asi que
// `kept = tam - devuelto` cuenta sobrevivientes intactos. `parametros` trae la
// fraccion, la cuota de mutacion, la longitud de los individuos uniformes y el
// caso de la ODE (ver ParametrosReinicioAdaptativo). El reinicio legacy
// conserva su firma y su comportamiento.
// En Python seria
// `reemplazados = reiniciar_poblacion_adaptativo(poblacion, record, parametros)`.
int reiniciar_poblacion_adaptativo(vector<FunbObj>& poblacion,
                                   const vector<ldouble>& record,
                                   const ParametrosReinicioAdaptativo& parametros) {
    const int tam = static_cast<int>(poblacion.size());

    // Orden estable por fitness (menor = mejor). stable_sort conserva el orden
    // original en los empates: con la misma semilla se eligen los mismos
    // indices. En Python seria
    // `orden = sorted(range(tam), key=lambda i: poblacion[i].fitness())`.
    vector<int> orden(tam);
    for (int i = 0; i < tam; i++) orden[i] = i;
    stable_sort(orden.begin(), orden.end(), [&poblacion](int a, int b){
        return poblacion[a].fitness() < poblacion[b].fitness();
    });

    // Cuantos se reemplazan: la fraccion truncada igual que en el reinicio
    // legacy (parte_entera), pero se deja siempre al menos un sobreviviente
    // (el mejor/record) aunque `parametros.fraccion` llegue a 1.
    int cantidad = parte_entera(parametros.fraccion, tam);
    if (cantidad > tam - 1) cantidad = tam - 1;
    if (cantidad < 0) cantidad = 0;
    int conservados = tam - cantidad;

    // El record (mejor historico) sobrevive siempre. Si su vector ya esta
    // entre los conservados no se toca (no se duplica); si no, ocupa el
    // primero de los lugares que se reemplazan, de modo que ningun
    // sobreviviente se toca y `kept=`/`replaced=` describen exactamente lo que
    // paso (el lugar del record cuenta como reemplazo). Un record vacio (la
    // corrida nunca encontro fitness finito) no se inyecta. En Python seria
    // `inyectar = record and not any(p.vecvalue() == record for p in conservados)`.
    bool record_presente = false;
    if (!record.empty()){
        for (int k = 0; k < conservados && !record_presente; k++){
            if (poblacion[orden[k]].vecvalue() == record) record_presente = true;
        }
    }
    const bool inyectar = !record.empty() && !record_presente && tam > 0;

    // Borde: si la fraccion trunco a cero no hay lugar de reemplazo, pero el
    // record ausente necesita uno; se le abre el del peor y esa sustitucion
    // cuenta como reemplazo (el log no puede informar replaced=0 y a la vez
    // haber cambiado un individuo).
    if (inyectar && cantidad == 0){
        cantidad = 1;
        conservados = tam - cantidad;
    }

    // Composicion hibrida (issue #127): el record (si hay que inyectarlo)
    // ocupa el primer lugar reemplazado; despues van las mutaciones puntuales
    // de 1-2 tokens de un sobreviviente (refinamiento local alrededor de las
    // elites) en la cuota `parametros.cuota_mutacion` y los uniformes nuevos
    // (exploracion) completan el resto. El reparto se trunca igual que la
    // fraccion. En Python seria
    // `mutaciones = int(cuota_mutacion * (cantidad - inyectar))`.
    const int plazas_record = inyectar ? 1 : 0;
    const int mutaciones = parte_entera(parametros.cuota_mutacion,
                                        cantidad - plazas_record);
    for (int k = conservados; k < tam; k++){
        const int lugar = k - conservados;
        if (lugar < plazas_record){
            poblacion[orden[k]] = FunbObj(record, 0, 1, parametros.caso);
        } else if (lugar - plazas_record < mutaciones){
            const vector<ldouble> elite =
                poblacion[orden[rand() % conservados]].vecvalue();
            poblacion[orden[k]] = FunbObj(mutacion_puntual(elite), 0, 1, parametros.caso);
        } else {
            poblacion[orden[k]] = FunbObj::create_random(parametros.dimension, parametros.caso);
        }
    }

    return cantidad;
}


// mutacion_rand1: nucleo de la mutacion DE/rand/1/bin, ecuacion (7) del paper:
//
//     v = x_r1 + F * (x_r2 - x_r3)
//
// Es una funcion *pura* (sin rand(), sin estado): por eso se puede testear con
// valores a mano. En Python seria `def mutacion_rand1(r1, r2, r3, F): return
// r1 + F * (r2 - r3)`. Se separa de Mutation() para poder probar la formula
// sola, sin depender del sorteo de F.
ldouble mutacion_rand1(ldouble r1, ldouble r2, ldouble r3, ldouble F){
    return r1 + F * (r2 - r3);
}

// token_valido: pasa el valor real de la ecuacion (7) a un token entero del
// alfabeto. Redondea al entero mas cercano y recorta entre TOKEN_MIN y
// TOKENS_ALFABETO, cuyo valor superior coincide con TOKEN_MAX porque el
// alfabeto empieza en 1.
//
// Detalle C++ sin equivalente directo en Python: se devuelve `int` y no
// `long double` porque un token es un codigo, no una cantidad; el redondeo se
// hace con floor(v + 0.5) (equivalente a round() pero sin la ambiguedad de
// round() con los .5 negativos) y el recorte con dos `if` en vez de min/max
// para que quede explicito. En Python seria:
//     def token_valido(v): return min(TOKENS_ALFABETO, max(TOKEN_MIN, floor(v + 0.5)))
int token_valido(ldouble v){
    ldouble redondeado = floor(v + 0.5L);
    if (redondeado < TOKEN_MIN) return TOKEN_MIN;
    if (redondeado > TOKENS_ALFABETO) return TOKENS_ALFABETO;
    return static_cast<int>(redondeado);
}

// indices_distintos: sortea tres indices de poblacion distintos entre si y
// distintos de `excluir` (el individuo objetivo). Son los x_r1, x_r2, x_r3 de
// la ecuacion (7): el paper exige individuos distintos para que la diferencia
// (x_r2 - x_r3) no sea siempre cero.
//
// Los parametros de salida (`int& r1, ...`) se reemplazaron por un valor
// devuelto (F.20/F.21): en Python seria `return r1, r2, r3`, y el llamador lo
// desempaqueta con un structured binding, `r1, r2, r3 = indices_distintos(...)`.
// El sorteo se hace por rechazo: se tira rand() hasta que no toque un indice
// repetido. Con poblaciones de ~275 individuos el bucle casi nunca repite.
array<int, 3> indices_distintos(int tam, int excluir){
    // Con menos de 4 individuos no existen 3 indices distintos del objetivo;
    // se devuelven los que haya para no quedarse en un bucle infinito (solo
    // puede pasar si alguien achica la poblacion a mano).
    if (tam < 4) {
        return {0, (tam > 1) ? 1 : 0, (tam > 2) ? 2 : 0};
    }
    // El orden de los sorteos (r1, r2, r3) es parte del contrato de neutralidad
    // del flujo de rand(): no reordenar ni cambiar el numero de tiradas.
    array<int, 3> indices{};
    do { indices[0] = rand() % tam; } while (indices[0] == excluir);
    do { indices[1] = rand() % tam; } while (indices[1] == excluir || indices[1] == indices[0]);
    do { indices[2] = rand() % tam; } while (indices[2] == excluir || indices[2] == indices[0] || indices[2] == indices[1]);
    return indices;
}

// ---------------------------------------------------------------------------
// Operadores DE/rand/1/bin (issue #5).
// ---------------------------------------------------------------------------

// Mutation: version DE/rand/1/bin con dither, ecuacion (7) del paper. Aplica,
// token a token, v = x_r1 + F*(x_r2 - x_r3), donde x_r1/x_r2/x_r3 son los tres
// individuos base que recibe (el llamador garantiza que son distintos) y F es
// un unico aleatorio en [0,1] por llamada (parte "dither": F cambia en cada
// generacion, no es una constante del algoritmo). El real resultante se
// convierte a un token valido con token_valido().
//
// Nota C++ para un desarrollador Python: los tres parametros entran por
// referencia constante (const&: sin copiar el vector, igual que pasar una
// lista a una funcion en Python) y la funcion devuelve un vector nuevo; el
// const& declara ademas que los padres son de solo lectura (nadie los muta).
vector<ldouble> Mutation(const vector<ldouble>& x, const vector<ldouble>& y, const vector<ldouble>& z){

    // Se recorre hasta el menor de los tres tamanos: si dos vectores midieran
    // distinto (p. ej. un seed mal escrito, B5 de REVIEW.md), leer y[i]/z[i]
    // de mas seria una lectura fuera de rango. En Python seria
    // `n = min(len(x), len(y), len(z))` (aqui `min` es std::min, de <algorithm>).
    const int n = static_cast<int>(min(x.size(), min(y.size(), z.size())));

    // Se parte de una copia de x: asi, si x es mas largo que y/z, los tokens
    // sobrantes sobreviven intactos en vez de quedar indefinidos.
    vector<ldouble> res(x);

    // Dither: un rand() por llamada. rand()/RAND_MAX da un real en [0,1]
    // (rand() devuelve enteros en [0,RAND_MAX]); en Python seria
    // `F = random.random()`.
    ldouble F = static_cast<ldouble>(rand()) / static_cast<ldouble>(RAND_MAX);

    for(int i = 0 ; i < n; i++ ){
        res[i] = token_valido(mutacion_rand1(x[i], y[i], z[i], F));
    }

    // Reparación de colapso: si el primer token quedó como hoja (aridad 0), la
    // expresión completa colapsaría a un solo token. Se reemplaza con un
    // operador aleatorio (aridad > 0). En Python sería
    // `if arity(res[0]) == 0: res[0] = random_operator_token()`.
    if (res.size() > 0 && arity(static_cast<int>(res[0])) == 0){
        res[0] = random_operator_token();
    }

    // Reparacion heredada: la fitness exige alguna hoja que dependa de x
    // (variable o polinomial, issue #63), asi que si la mutacion borro el
    // token x se fuerza uno en una posicion al azar (un solo token; antes se
    // intentaban 3 posiciones con un rango dudoso). Es una regla propia de
    // este prototipo, no del paper.
    if (res.size() > 0 && std::find(res.begin(), res.end(),
                                    static_cast<ldouble>(TOKEN_X)) == res.end()){
        res[rand() % res.size()] = TOKEN_X;
    }

    return res;
}

// ---------------------------------------------------------------------------
// Cruce binomial (issue #6), ecuacion (8) del paper.
// ---------------------------------------------------------------------------

// gen_binomial: decision *pura* del cruce para un gen, ecuacion (8): con
// probabilidad Cr el trial hereda el gen del mutante (v) y con probabilidad
// 1-Cr el del objetivo (x). La tirada `r` (en [0,1]) llega como parametro para
// poder probar la regla con valores a mano, sin rand(); Recombination() es
// quien sortea un r por gen. En Python seria:
//     def gen_binomial(objetivo, mutante, r, Cr): return mutante if r < Cr else objetivo
// La comparacion es estricta (r < Cr), igual que en el enunciado del issue.
ldouble gen_binomial(ldouble objetivo, ldouble mutante, ldouble r, ldouble Cr){
    return (r < Cr) ? mutante : objetivo;
}

// Recombination: cruce binomial del paper, ecuacion (8). Construye el vector
// trial mezclando, gen a gen, el individuo objetivo (x) con el mutante (v):
// con probabilidad Cr el gen viene del mutante y en caso contrario del
// objetivo. Ademas el indice j_rand se fuerza al mutante para que el trial
// nunca sea una copia exacta del objetivo (asi lo pide el paper; REVIEW D2).
//
// Nota C++ para un desarrollador Python: los padres entran por referencia
// constante (no se copian) y el resultado se devuelve por valor; la funcion no
// modifica a nadie, a diferencia de la version anterior que intercambiaba
// genes "in place" entre dos vectores.
vector<ldouble> Recombination(const vector<ldouble>& objetivo, const vector<ldouble>& mutante, ldouble Cr){

    // Se recorre hasta el menor de los dos tamanos: leer mutante[i] de mas seria
    // lectura fuera de rango. En Python seria
    // `n = min(len(objetivo), len(mutante))`.
    const int n = static_cast<int>(min(objetivo.size(), mutante.size()));

    // Se parte de una copia del objetivo: si el objetivo es mas largo que el
    // mutante, sus tokens sobrantes sobreviven intactos.
    vector<ldouble> trial(objetivo);

    // Con vectores vacios no hay genes que cruzar (ni j_rand que sortear).
    if (n <= 0) return trial;

    // j_rand: indice sorteado que siempre toma el gen del mutante.
    // `rand() % n` es `random.randrange(n)` en Python.
    int j_rand = rand() % n;

    for (int i = 0; i < n; i++){
        // Un rand() por gen, como el `if rand() < Cr` del issue: rand()/RAND_MAX
        // da un real en [0,1] (random.random()).
        ldouble r = static_cast<ldouble>(rand()) / static_cast<ldouble>(RAND_MAX);
        if (i == j_rand){
            trial[i] = mutante[i];
        } else {
            trial[i] = gen_binomial(objetivo[i], mutante[i], r, Cr);
        }
    }

    // Reparación de colapso: si el primer token quedó como hoja (aridad 0), la
    // expresión completa colapsaría a un solo token. Se reemplaza con un
    // operador aleatorio (aridad > 0). En Python sería
    // `if arity(trial[0]) == 0: trial[0] = random_operator_token()`.
    if (trial.size() > 0 && arity(static_cast<int>(trial[0])) == 0){
        trial[0] = random_operator_token();
    }

    // Reparacion heredada (igual que en Mutation()): la fitness exige alguna
    // hoja que dependa de x (variable o polinomial, issue #63), asi que si el
    // cruce borro el token x se fuerza uno en una posicion al azar. Es una
    // regla propia de este prototipo, no del paper.
    if (std::find(trial.begin(), trial.end(),
                                      static_cast<ldouble>(TOKEN_X)) == trial.end()){
        trial[rand() % trial.size()] = TOKEN_X;
    }

    return trial;
}

// Guarda de compilacion para los tests: `-DODE_NO_MAIN` descarta este bloque
// (y con el, main()) al compilar parse_tree.cpp dentro de un test. En Python no
// existe nada parecido: un modulo no "contiene" el entry point, asi que no hay
// que apagarlo; importarlo simplemente no ejecuta la busqueda.
#ifndef ODE_NO_MAIN

// ===========================================================================
// Log de main(): impresion con formato profesional.
//
// Cada linea sigue el patron por defecto de spdlog, la libreria de logging mas
// usada en C++ (timestamp + nivel + mensaje), pero implementado con iostreams
// de la STL: el repo no tiene gestor de paquetes, asi que no se agrega ninguna
// dependencia externa. Forma de cada linea:
//
//     [  0.12s] [INFO ] de_run_start | seed=42 case=0 population=276 ...
//
// - "[  0.12s]": tiempo de CPU desde el arranque, ancho fijo 6 y 2 decimales.
//   En Python seria f"{t:6.2f}s" con t = time.process_time() - t0.
// - "[INFO ]"/"[WARN ]": nivel del mensaje, siempre 5 caracteres (con relleno)
//   para que los mensajes empiecen en la misma columna; spdlog usa etiquetas
//   como "[info]" y "[warning]".
// - Mensaje: "evento | clave=valor ..." (logging estructurado): legible por
//   una persona y parseable con split() desde un script; en Python seria
//   f"gen={gen} best={fit:.6e}" (una f-string).
//
// Contrato con scripts/ab_experiment.sh: ese script extrae las claves seed,
// gen, generations, best, expr, vector, status y elapsed_cpu_s de estas
// lineas; renombrar una clave obliga a actualizar el awk del script.
//
// C++ Core Guidelines (F.3): se imprime '\n' y un flush explicito
// (cout.flush(), el sys.stdout.flush() de Python) en vez de std::endl. Aqui el
// flush es deliberado, no incidental: la salida se consume en vivo (`tail`) y
// un `timeout` puede matar el proceso en cualquier momento, asi que cada linea
// de main se vacia al emitirse (igual que el sink de consola de spdlog); endl
// queda evitado en el resto del programa, donde nadie lo necesita. '\n' +
// flush() es exactamente lo que endl hace por dentro.
// ===========================================================================

// Instante de arranque para el timestamp de cada linea. `static` a nivel de
// archivo significa visibilidad solo dentro de este .cpp (en Python no hay un
// equivalente exacto: es como un nombre con guion bajo a nivel de modulo, pero
// impuesto por el compilador).
static clock_t g_log_t0 = 0;

// fmt_fijo: numero con `decimales` decimales fijos, como f"{v:.2f}" en Python.
// ostringstream es el buffer de texto de la STL (la f-string de C++): se arma
// el string con el formato pedido por <iomanip> y .str() lo devuelve, sin
// tocar el estado global de cout.
string fmt_fijo(ldouble v, int decimales){
    ostringstream os;
    os << fixed << setprecision(decimales) << v;
    return os.str();
}

// fmt_ciencia: notacion cientifica con 6 decimales (f"{v:.6e}" en Python), el
// formato de la fitness: valores muy chicos o INFINITY, que sale "inf" igual
// que en Python.
string fmt_ciencia(ldouble v){
    ostringstream os;
    os << scientific << setprecision(6) << v;
    return os.str();
}

// fmt_segundos: segundos con 2 decimales y ancho 6 (f"{v:6.2f}" en Python).
// Solo para el timestamp que abre cada linea.
string fmt_segundos(ldouble v){
    ostringstream os;
    os << fixed << setprecision(2) << setw(6) << v;
    return os.str();
}

// fmt_vector: el individuo como "[21,11,14,20]"
// ("[" + ",".join(str(int(t)) for t in v) + "]" en Python). Sin espacios a
// proposito: asi el vector es UN solo campo clave=valor y la linea se puede
// partir con split(). El parametro entra por referencia constante (const&),
// que en C++ evita copiar el vector entero, como pasar una lista en Python
// sin copiarla.
string fmt_vector(const vector<ldouble>& v){
    ostringstream os;
    os << "[";
    for (size_t i = 0; i < v.size(); i++){
        if (i > 0) os << ",";
        os << static_cast<long long>(v[i]);  // los tokens son enteros TOKEN_MIN..TOKEN_MAX
    }
    os << "]";
    return os.str();
}

// segundos_cpu: CPU transcurrida desde g_log_t0 (time.process_time() - t0 en
// Python). clock() mide tiempo de CPU del proceso, no tiempo de pared.
ldouble segundos_cpu(){
    return static_cast<ldouble>(clock() - g_log_t0) / CLOCKS_PER_SEC;
}

// log_linea: imprime "[<t>s] [<NIVEL>] " y devuelve cout para que el llamador
// complete el mensaje y cierre con '\n'; en Python seria logging.info(f"...").
// Devolver el stream (ostream&) es el idioma de C++ para encadenar `<<`: el
// llamador sigue escribiendo campos sobre el mismo renglon. El nivel llega con
// el relleno ya puesto ("INFO ").
ostream& log_linea(const char* nivel){
    cout << '[' << fmt_segundos(segundos_cpu()) << "s] [" << nivel << "] ";
    return cout;
}

// Atajos por nivel, como logging.info y logging.warning de Python.
ostream& log_info(){
    return log_linea("INFO ");
}

ostream& log_warn(){
    return log_linea("WARN ");
}

int main() {

    // Arranque del reloj de CPU: alimenta el timestamp de cada linea y el
    // tiempo total del resumen (en Python: t0 = time.process_time()).
    g_log_t0 = clock();

    // Destino de la salida (issue #112): por defecto todo va a ode-solver.log;
    // ODE_LOG=terminal lo devuelve a stdout y ODE_LOG_FILE cambia la ruta. La
    // redireccion se instala UNA vez, antes de la primera linea de log, y el
    // RAII la deshace al salir. En Python seria `with redireccion: ...`.
    const ConfigSalida config_salida =
        resolver_salida(getenv("ODE_LOG"), getenv("ODE_LOG_FILE"));
    RedireccionSalida redireccion(config_salida);
    if (redireccion.fallo_apertura()) {
        // La ruta no se pudo abrir: se avisa por la terminal (cout sigue
        // intacto) y la corrida continua ahi, sin perder resultados.
        log_warn() << "log_open_failed | path=" << config_salida.ruta << '\n';
        cout.flush();
    }

    // Semilla controlada por ODE_SEED (issue #4): con ODE_SEED=42 todas las
    // corridas salen identicas (mismo fitness, mismo vector), que es lo que
    // exige el experimento A/B. Sin la variable se usa el reloj, como antes.
    // En Python seria: random.seed(os.environ.get("ODE_SEED")).
    long semilla = resolver_semilla(getenv("ODE_SEED"), static_cast<long>(time(nullptr)));
    srand(static_cast<unsigned>(semilla));

    // Caso del problema, controlado por ODE_CASE (mismo patron que ODE_SEED):
    //   0 (o ausente) -> juguete historico y'' + y = 0,  y(0)=y'(0)=1
    //   1            -> Caso 1 del paper: x*y'' + y' - cos(x) = 0 (ec. (18)),
    //                   y(0)=0, y'(0)=1, solucion exacta Si(x)
    //   2            -> caso hiperbolico (issue #61): y'' - y = 0,
    //                   y(0)=1, y'(0)=0, solucion exacta cosh(x)
    //   3            -> oscilador amortiguado (issue #103): y'' + 2y' + 2y = 0,
    //                   y(0)=1, y'(0)=-1, solucion e^(-x)*cos(x)
    //   4            -> oscilador forzado (issue #103): y'' + y = e^x,
    //                   y(0)=0, y'(0)=0, solucion (e^x - cos(x) - sin(x))/2
    //   5            -> resonancia (issue #103): y'' - y = e^x,
    //                   y(0)=0, y'(0)=0, solucion (2x*e^x - e^x + e^(-x))/4
    //   6..7         -> casos Legendre/Hermite (issue #104): el parser ya los
    //                   acepta (issue #102) y cada uno lleva sus condiciones
    //                   iniciales en fitness(); sus residuales se implementan
    //                   en los tickets siguientes.
    // En Python seria `caso = int(os.environ.get("ODE_CASE", "0"))` (aqui
    // atoi() convierte el string del entorno a entero, como int()); cualquier
    // valor fuera de 0..7 cae de vuelta al caso 0, igual que el resto de ODE_*.
    int caso = 0;
    const char* caso_env = getenv("ODE_CASE");
    if (caso_env != nullptr){
        const int caso_pedido = atoi(caso_env);
        if (caso_pedido >= 0 && caso_pedido <= 7){
            caso = caso_pedido;
        }
    }

    // Configuracion de la busqueda (issue #28): poblacion, dimension y tope de
    // generaciones del Caso 1 del paper (REVIEW C5-C7), cada uno sobreescribible
    // con ODE_POP/ODE_DIM/ODE_GEN; el warm start (el catalogo por caso) se
    // activa con ODE_WARM_START=1 y por defecto la poblacion es aleatoria
    // uniforme, como pide la ecuacion (6) del paper. En Python seria
    // `config = ConfigBusqueda.from_env()` leyendo os.environ.
    // Issue #166: resolver_config devuelve ambas configuraciones por separado.
    // ConfigBusqueda lleva los parametros de busqueda; ConfigReinicio lleva
    // solo los hechos de reinicio (stale, modo, escalacion, mutation-share, Diversity).
    ConfigBusqueda config_busqueda;
    ConfigReinicio config_reinicio;
    resolver_config(getenv("ODE_POP"), getenv("ODE_DIM"),
                    getenv("ODE_GEN"), getenv("ODE_WARM_START"),
                    caso,
                    getenv("ODE_STALE_LIMIT"),
                    getenv("ODE_RESTART_FRACTION"),
                    getenv("ODE_RESTART_MODE"),
                    getenv("ODE_RESTART_MIN_FRACTION"),
                    getenv("ODE_RESTART_CAP"),
                    getenv("ODE_RESTART_MUTATION_SHARE"),
                    getenv("ODE_DIV_THRESHOLD"),
                    config_busqueda,
                    config_reinicio);

    // Una sola poblacion (issue #6): antes habia dos (Population1/Population2)
    // que se cruzaban entre si; DE/rand/1/bin (paper) evoluciona una unica
    // poblacion generacion a generacion.
    vector<FunbObj> Population;
    // createPopulation devuelve cuantas entradas del catalogo warm start se
    // inyectaron de verdad; el resto de la poblacion es aleatoria.
    const int semillas_warm = createPopulation(Population, config_busqueda, caso);

    // Cr: probabilidad de heredar el gen del mutante en el cruce binomial
    // (eq. (8)). Antes era `int Cr = 85`, un umbral comparado contra el tamano
    // de poblacion; el paper (§5, REVIEW D9) usa 0.2.
    constexpr ldouble Cr = 0.2;
    constexpr long double eps = 0.00001;
    long double Bestfit = INFINITY;
    vector<ldouble> Bestvect;
    int gen = 0;
    bool flag = true;
    const int max_gen = config_busqueda.max_generaciones;  // tope del bucle (ODE_GEN)
    string estado = "max_generations";  // pasa a "converged" si llega a eps
    
    // Mecanismo de escape de mínimos locales: si la búsqueda no mejora en
    // stale_limit generaciones, se reinicia el restart_fraction de la población
    // manteniendo el mejor individuo. Esto inyecta diversidad fresca sin perder
    // el mejor hallazgo. configurable via ODE_STALE_LIMIT y ODE_RESTART_FRACTION.
    const int stale_limit = config_reinicio.stale_limit;

    // ControlReinicio (issue #139): el ciclo de vida vive en un solo objeto.
    // main() ya no lleva su propio contador de estancamiento ni su escalador;
    // le pasa a decidir() los hechos de la generacion y obedece la decision.
    // En Python seria `control_reinicio = ControlReinicio(config_reinicio)`.
    ControlReinicio control_reinicio(config_reinicio);

    // Cabecera de la corrida (reemplaza el banner de ASCII art): una sola
    // linea con toda la configuracion en clave=valor. `population=` informa el
    // tamano real de la poblacion ya creada; `warm_start=` es el valor efectivo
    // y `warm_seeds=` cuantas entradas del catalogo se inyectaron de verdad.
    // `stale_limit` y `restart_fraction` son los parametros de reinicio
    // (issue #108), configurables via ODE_STALE_LIMIT y ODE_RESTART_FRACTION.
    // `restart_mode` es el modo efectivo resuelto por resolver_config
    // (issue #126) y el bucle lo usa para elegir entre el reinicio legacy y
    // el adaptativo escalado de #128.
    log_info() << "de_run_start | seed=" << semilla
               << " case=" << caso
               << " population=" << Population.size()
               << " dimension=" << config_busqueda.dimension
               << " warm_start=" << (config_busqueda.warm_start ? 1 : 0)
               << " warm_seeds=" << semillas_warm
               << " cr=" << fmt_fijo(Cr, 2)
               << " fitness_target=" << fmt_ciencia(eps)
               << " max_generations=" << max_gen
               << " stale_limit=" << stale_limit
               << " restart_fraction=" << fmt_fijo(config_reinicio.restart_fraction, 2)
               << " restart_mode=" << nombre_modo_reinicio(config_reinicio.restart_mode) << '\n';

    // Descripcion legible del caso: el dato maquina es el campo `case=` de
    // arriba; este renglon es la explicacion y va como texto libre (sin
    // clave=valor) para no confundir al parser del script A/B.
    log_info() << "ode_case | " << caso << ": ";
    if (caso == 1){
        cout << "paper, ec. (18): x*y'' + y' - cos(x) = 0, y(0)=0, y'(0)=1";
    } else if (caso == 2){
        cout << "hiperbolico (issue #61): y'' - y = 0, y(0)=1, y'(0)=0, solucion cosh(x)";
    } else if (caso == 3){
        cout << "oscilador amortiguado (issue #103): y'' + 2y' + 2y = 0, y(0)=1, y'(0)=-1, solucion e^(-x)*cos(x)";
    } else if (caso == 4){
        cout << "oscilador forzado (issue #103): y'' + y = e^x, y(0)=0, y'(0)=0, solucion (e^x - cos(x) - sin(x))/2";
    } else if (caso == 5){
        cout << "resonancia (issue #103): y'' - y = e^x, y(0)=0, y'(0)=0, solucion (2x*e^x - e^x + e^(-x))/4";
    } else if (caso == 6){
        cout << "Legendre (issue #104): (1-x²)y'' - 2xy' + 6y = 0, y(0)=-1/2, y'(0)=0, solucion P₂(x)=(3x²-1)/2";
    } else if (caso == 7){
        cout << "Hermite (issue #104): y'' - 2xy' + 4y = 0, y(0)=-2, y'(0)=0, solucion H₂(x)=4x²-2";
    } else {
        cout << "juguete: y'' + y = 0, y(0)=1, y'(0)=1";
    }
    cout << '\n';
    cout.flush();

    while(flag){

        // La generacion se cuenta 1-based desde el principio: asi el campo
        // `generations=` del resumen final cuenta las generaciones corridas
        // (antes el contador incrementaba al final y el ultimo avance impreso
        // era 100001).
        gen++;

        // Una generacion de DE/rand/1/bin: por cada individuo objetivo se
        // sortean los tres individuos base (distintos entre si, eq. (7)), se
        // muta, se cruza (eq. (8)) y se selecciona el mejor de los dos.
        const int tam_poblacion = static_cast<int>(Population.size());
        for (int j = 0; j < tam_poblacion; j++){

            // Structured binding: desempaqueta el array devuelto por el
            // sorteo (Python: `r1, r2, r3 = indices_distintos(...)`).
            auto [r1, r2, r3] = indices_distintos(tam_poblacion, j);
            vector<ldouble> mutante(Mutation(Population[r1].vecvalue(), Population[r2].vecvalue(), Population[r3].vecvalue()));
            vector<ldouble> trial(Recombination(Population[j].vecvalue(), mutante, Cr));

            // Seleccion DE estandar (greedy, uno a uno): el trial reemplaza al
            // objetivo solo si su fitness es estrictamente menor. Es el
            // `if fitness(trial) < fitness(objetivo): poblacion[j] = trial`
            // clasico de Python; ya no hay elitismo por sorting ni
            // cross-pollination entre poblaciones.
            FunbObj candidato(trial, 0, 1, caso);
            if (candidato.fitness() < Population[j].fitness()){
                Population[j] = candidato;
            }
        }

        // Mejor individuo de la generacion (la fitness mas baja). Se busca con
        // un barrido simple en vez de ordenar la poblacion entera.
        int mejor = 0;
        for (int j = 1; j < tam_poblacion; j++){
            if (Population[j].fitness() < Population[mejor].fitness()){
                mejor = j;
            }
        }
        ldouble BestCurrent = Population[mejor].fitness();

        // Avance de la generacion: mejor fitness de la generacion, record
        // historico y su expresion. Con flush: aunque es la linea de alta
        // frecuencia (hasta 100000), tiene que verse en vivo (`tail`) y
        // sobrevivir si un `timeout` mata el proceso (ver la cabecera).
        log_info() << "gen=" << gen << " best=" << fmt_ciencia(BestCurrent)
                   << " best_ever=" << fmt_ciencia(Bestfit)
                   << " expr=" << Population[mejor].represant() << '\n';
        cout.flush();

        // `nuevo_record` es el hecho que consume el ciclo de vida: actualiza
        // el record historico y resetea la ventana y la escalera (issue #139).
        // En Python seria `nuevo_record = best_current < best_ever`.
        const bool nuevo_record = BestCurrent < Bestfit;
        if (nuevo_record){
            Bestfit = BestCurrent;
            Bestvect = Population[mejor].vecvalue();
            // Record nuevo: una sola linea (antes eran cinco con adornos) con
            // su flush, para que quede en el log aunque un `timeout` mate el
            // proceso a mitad de corrida.
            log_info() << "new_best | gen=" << gen << " best=" << fmt_ciencia(Bestfit)
                       << " expr=" << parse_tree(Bestvect)
                       << " vector=" << fmt_vector(Bestvect) << '\n';
            cout.flush();
        }

        // El resumen de diversidad solo se calcula en adaptive (issue #129):
        // en legacy no se evalua y la corrida historica sigue bit-reproducible.
        // La muestra es la poblacion de esta generacion; fitness() ya esta
        // cacheada por la seleccion y el barrido del mejor. En Python seria
        // `resumen = resumir_diversidad([ind.fitness() for ind in poblacion])`.
        ResumenDiversidad resumen;
        if (config_reinicio.restart_mode == ModoReinicio::adaptive){
            vector<ldouble> fitnesses;
            fitnesses.reserve(Population.size());
            for (const FunbObj& individuo : Population){
                fitnesses.push_back(individuo.fitness());
            }
            resumen = resumir_diversidad(fitnesses);
        }

        // El ciclo de vida decide (issue #139): stale tiene prioridad sobre
        // diversity y cualquier reinicio resetea la ventana de estancamiento.
        // En Python seria
        // `decision = control_reinicio.decidir(nuevo_record, resumen)`.
        const DecisionReinicio decision =
            control_reinicio.decidir(nuevo_record, resumen);

        // El modo lo elige ODE_RESTART_MODE: `legacy` conserva el reinicio
        // historico y `adaptive` usa el reinicio elitista con composicion
        // hibrida (issue #127).
        if (decision.hay_reinicio()){
            int reemplazados = 0;
            const long double fraccion = decision.parametros.fraccion;
            string detalle_restart;
            if (config_reinicio.restart_mode == ModoReinicio::adaptive){
                // La escalera de #128 ya decidio la fraccion y la cuota de
                // este reinicio: 0.4 + 0.2 * consecutivos hasta el tope, y
                // refinamiento-only tras ODE_RESTART_CAP reinicios sin mejora.
                const ParametrosReinicioAdaptativo parametros{
                    decision.parametros.fraccion,
                    decision.parametros.cuota_mutacion,
                    config_busqueda.dimension, caso};
                reemplazados = reiniciar_poblacion_adaptativo(
                    Population, Bestvect, parametros);
                detalle_restart = " mutation_share=" +
                                  fmt_fijo(decision.parametros.cuota_mutacion, 2);
            } else {
                // `mejor` es el indice del mejor de la generacion actual (el
                // que el reemplazo respeta); ademas el helper restaura alli el
                // record historico `Bestvect`, porque en una corrida estancada
                // el mejor del momento no es el record (issue #82).
                reemplazados = reiniciar_poblacion(Population, mejor, Bestvect, config_busqueda, config_reinicio, caso);
            }
            // Una sola linea `restart |` para ambos modos: `trigger=` y
            // `fraction=` dejan auditable que disparo el reinicio y con que
            // fuerza, `kept=` cuenta los sobrevivientes intactos (issue #127)
            // y `mutation_share=` registra la cuota escalada en adaptive
            // (issue #128). En adaptive el trigger distingue estancamiento de
            // colapso de diversidad (issue #129). Las claves viejas (`gen`,
            // `stagnant_gens`, `replaced`) conservan nombre y orden por
            // compatibilidad con los consumidores del log, que leen por clave
            // (`clave=valor`). `stagnant_gens=` sale de la decision porque la
            // ventana ya quedo reseteada dentro de decidir().
            const int conservados = static_cast<int>(Population.size()) - reemplazados;
            const char* trigger =
                decision.disparador == DisparadorReinicio::diversity
                    ? "diversity" : "stale";
            log_info() << "restart | gen=" << gen
                       << " stagnant_gens=" << decision.generaciones_sin_mejora
                       << " replaced=" << reemplazados
                       << " fraction=" << fmt_fijo(fraccion, 2)
                       << detalle_restart
                       << " trigger=" << trigger
                       << " kept=" << conservados << '\n';
            cout.flush();
        }

        if ( Bestfit < eps){
            estado = "converged";
            flag = false;
        }

        if(gen >= max_gen){
            flag = false;
        }
    }

    // Resultado final: la solucion en una linea con su flush. Solo si hubo
    // algun individuo finito (Bestvect vacio = toda la poblacion dio fitness
    // infinita; no hay nada que mostrar).
    if (!Bestvect.empty()){
        log_info() << "solution | best=" << fmt_ciencia(Bestfit)
                   << " expr=" << parse_tree(Bestvect)
                   << " vector=" << fmt_vector(Bestvect) << '\n';
    } else {
        log_warn() << "no_solution | best=" << fmt_ciencia(Bestfit) << '\n';
    }
    cout.flush();

    // Si el bucle se corto por el tope de generaciones no se alcanzo la
    // tolerancia: es una advertencia, no una solucion.
    if (estado != "converged"){
        log_warn() << "no_convergence | reason=" << estado
                   << " generations=" << gen
                   << " fitness_target=" << fmt_ciencia(eps) << '\n';
    }
    cout.flush();

    // Resumen de la corrida: estado, generaciones, record y tiempo de CPU
    // (el viejo "Execution Time", ahora como campo estructurado).
    log_info() << "de_run_end | status=" << estado
               << " generations=" << gen
               << " best=" << fmt_ciencia(Bestfit)
               << " elapsed_cpu_s=" << fmt_fijo(segundos_cpu(), 2) << '\n';
    cout.flush();

    // Firma del autor (issue #44), conservada del prototipo original pero ahora
    // como un evento estructurado mas: nivel INFO y flush explicito, para que
    // el consumidor del log no vea lineas sueltas. `author=` es un campo nuevo
    // que el awk de scripts/ab_experiment.sh no parsea (solo mira seed, gen,
    // generations, best, expr, vector, status y elapsed_cpu_s). En Python
    // seria logging.info("signature | author=Lmex").
    log_info() << "signature | author=Lmex" << '\n';
    cout.flush();

    return 0;
}
#endif // ODE_NO_MAIN
