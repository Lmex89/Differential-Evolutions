// test_population.cpp — tests del semillero de la poblacion (issue #11) y de
// la configuracion de la busqueda (issue #28).
//
// Para un desarrollador Python: este archivo es como un modulo `test_*.py` de
// pytest, pero sin framework: cada `test_*()` imprime PASS/FAIL y `main()` al
// final hace de resumen (seria el `pytest.main()` + exit code). El binario
// termina con codigo != 0 si algo fallo, igual que pytest.
//
// Se compila con -DODE_NO_MAIN (apaga el main() del programa y deja el de este
// archivo) y junto con dualfunctions.cpp y semilla.cpp; parse_tree.cpp se
// incluye mas abajo en vez de enlazarse, porque FunbObj no tiene header (mismo
// patron que test_grid.cpp: ver el comentario del include).
// En Python no existe "dos main() en un mismo binario": aqui el preprocesador
// `#ifndef ODE_NO_MAIN` del programa deja al compilador elegir que main() incluir.

#include <iostream>
#include <vector>
#include <string>
#include <cstdlib>
#include <array>

#include "dualfunctions.h"  // ahora autocontenido: incluye <vector>/<string> por si mismo
#include "token.h"          // expone el contrato publico del rango del alfabeto
#include "precargadas_prototipo.h"  // oraculo independiente del tramo precargado (#144)
#include "genericos_esperados.h"    // oraculo independiente del tramo generico (#145)

// En vez de declarar prototipos sueltos, este test incluye parse_tree.cpp
// entero: createPopulation y FunbObj viven ahi sin header. En Python seria
// `from parse_tree import createPopulation, FunbObj`: importar el modulo real.
// El main() del programa queda fuera por -DODE_NO_MAIN (ver el Makefile).
#include "parse_tree.cpp"

// -------------------------------------------------------------------------
// Arnes minimo (mismo estilo que test_dual/test_grid): contadores globales y
// helpers check_* que imprimen PASS/FAIL.
// -------------------------------------------------------------------------

static int g_checks = 0;   // total de asserts ejecutados
static int g_fallos = 0;   // asserts que fallaron

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

// check_int: para igualdades entre enteros; imprime el valor recibido cuando
// falla, como el `assert got == esperado, f"...{got}..."` de pytest.
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

// -------------------------------------------------------------------------
// Slice 1 (issue #11): todo individuo del semillero mide lo mismo — 20 tokens.
// B5 de REVIEW.md: el seed `dos` tenia 21 y `Mutation()` leia fuera de rango
// cuando ese individuo entraba como operando x. La invariante de diseno es que
// la poblacion inicial es uniforme: los 5 individuos escritos a mano y los
// aleatorios de vector_population(20) miden 20 tokens.
// -------------------------------------------------------------------------

static void test_createPopulation_tamano_uniforme() {
    // srand fijo: createPopulation sortea los individuos aleatorios con rand();
    // fijar la semilla hace el test reproducible (en Python seria
    // `random.seed(20261013)`), igual que en los tests de mutacion/cruce.
    srand(20261013);

    // Configuracion por defecto del Caso 1 del paper (issue #28): dimension 20.
    ConfigBusqueda config;
    config.poblacion = 10;

    // `poblacion` entra por referencia y createPopulation la llena con
    // push_back (en Python seria `poblacion = []` pasada por referencia).
    vector<FunbObj> poblacion;
    createPopulation(poblacion, config, 0);

    check_bool("createPopulation produce individuos", !poblacion.empty());

    // vecvalue() es la via publica de lectura; _vecval es privado (Python no
    // tiene esta distincion: todo seria publico).
    bool todos_20 = true;
    for (size_t i = 0; i < poblacion.size(); i++) {
        if (poblacion[i].vecvalue().size() != 20) todos_20 = false;
    }
    check_bool("todos los individuos tienen 20 tokens (dimension del paper)", todos_20);
}

// -------------------------------------------------------------------------
// Slice 2 (issue #28): createPopulation crea EXACTAMENTE los individuos que
// pide config.poblacion. Antes el bucle aleatorio era `i < tam - 4` con 5
// semillas fijas, asi que una poblacion de 10 salia de 11 individuos (el
// off-by-one que reporta REVIEW C5: 5 + tam - 4 = tam + 1).
// -------------------------------------------------------------------------

static void test_createPopulation_cantidad_exacta() {
    srand(20261013);

    ConfigBusqueda config;
    config.poblacion = 10;

    vector<FunbObj> poblacion;
    createPopulation(poblacion, config, 0);

    // 10 es un literal independiente de la implementacion: el contrato es
    // "tantos individuos como pidas", no "los que salgan de la formula".
    check_bool("createPopulation(10) crea exactamente 10 individuos",
               poblacion.size() == 10);
}

// -------------------------------------------------------------------------
// Slice 3 (issue #28): la dimension sale de la configuracion, no de un 20
// fijo. Con ODE_DIM=12 la poblacion entera debe medir 12 tokens y seguir
// usando solo el alfabeto TOKEN_MIN..TOKENS_ALFABETO (el criterio de
// "individuos validos" de #28).
// -------------------------------------------------------------------------
static void test_createPopulation_respeta_la_dimension() {
    srand(20261013);

    ConfigBusqueda config;
    config.poblacion = 8;
    config.dimension = 12;

    vector<FunbObj> poblacion;
    createPopulation(poblacion, config, 0);

    bool todos_12 = true;
    bool tokens_validos = true;
    for (size_t i = 0; i < poblacion.size(); i++) {
        const vector<ldouble>& v = poblacion[i].vecvalue();
        if (v.size() != 12) todos_12 = false;
        for (size_t j = 0; j < v.size(); j++) {
            if (v[j] < TOKEN_MIN || v[j] > TOKENS_ALFABETO) tokens_validos = false;
        }
    }
    check_bool("cada individuo mide la dimension pedida (12)", todos_12);
    check_bool("todos los tokens estan en el alfabeto TOKEN_MIN..TOKENS_ALFABETO",
               tokens_validos);
}

// -------------------------------------------------------------------------
// Slice 4 (issues #28 y #142): el warm start es opt-in. Por defecto la
// poblacion es aleatoria uniforme (ecuacion (6) del paper, REVIEW D6); con
// ODE_WARM_START=1 entran las entradas del catalogo del caso.
// -------------------------------------------------------------------------

static void test_warm_start_opt_in() {
    // Sin la bandera no entra ninguna entrada del catalogo. srand fijo hace el
    // sorteo deterministico, asi que la comprobacion no es probabilistica.
    const vector<ldouble> base = {
        TOKEN_DIV, TOKEN_ADD, TOKEN_MUL, TOKEN_CONST3,
        TOKEN_X2, TOKEN_NEG, TOKEN_CONST1, TOKEN_CONST2};
    vector<ldouble> legendre = base;
    legendre.resize(20, TOKEN_CONST1);

    srand(20261013);
    ConfigBusqueda frio;
    frio.poblacion = 10;
    vector<FunbObj> sin_semillas;
    const int sembradas = createPopulation(sin_semillas, frio, 6);

    check_int("sin warm start no se inyectan semillas del catalogo",
              sembradas, 0);
    check_bool("sin warm start la poblacion conserva su tamano",
               sin_semillas.size() == 10);

    bool ninguna = true;
    for (size_t i = 0; i < sin_semillas.size(); i++) {
        if (sin_semillas[i].vecvalue() == legendre) ninguna = false;
    }
    check_bool("sin warm start no aparece la base del catalogo", ninguna);
}

// -------------------------------------------------------------------------
// Slice 4b (issues #142-#145): el catalogo warm start es por caso y todos los
// casos 0..7 aportan veinticinco entradas. Las primeras cinco son las bases:
// la primera es la solucion exacta (o, en el caso 1, la familia de la serie
// truncada de Si(x)) y las otras cuatro son bloques cercanos de la misma
// familia. Las cinco siguientes son las semillas precargadas del prototipo,
// con su contenido intacto. Las ultimas quince son los bloques genericos
// compartidos (issue #145). Cada entrada debe ser una expresion prefija
// completa y depender de x; las bases ademas evaluan a fitness finita.
// -------------------------------------------------------------------------

// fitness_de_base: evalua una entrada minima del catalogo con la ODE del caso.
// En Python seria `FunbObj(base, 0, 1, caso).fitness()`.
static long double fitness_de_base(const vector<ldouble>& base, int caso) {
    FunbObj individuo(base, 0.0L, 1.0L, caso);
    return individuo.fitness();
}

// Total del catalogo: bases del caso + precargadas del prototipo + genericos
// compartidos. Las constantes llegan de parse_tree.cpp (incluido arriba), la
// misma fuente que la produccion usa para armar el catalogo.
constexpr size_t TOTAL_CATALOGO = NUM_BASES_POR_CASO + NUM_PRECARGADAS + NUM_BLOQUES_GENERICOS;

// validar_catalogo: verifica que todas las entradas de un caso (bases,
// precargadas y genericos) rendericen completas y dependan de x; ademas, que
// las cinco bases tengan fitness finita. En Python seria
// `assert all(valida(b) for b in catalogo)` mas las comprobaciones de forma.
static void validar_catalogo(const vector<EntradaCatalogo>& catalogo, int caso) {
    bool completas = true;
    bool dependen_de_x = true;
    bool bases_finitas = true;
    size_t indice = 0;
    for (const EntradaCatalogo& entrada : catalogo) {
        const vector<ldouble>& base = entrada.get();
        FunbObj individuo(base, 0.0L, 1.0L, caso);
        if (individuo.represant().find("()") != string::npos) {
            completas = false;
        }
        bool tiene_x = false;
        for (const auto& token : base) {
            if (is_x_dependent(static_cast<int>(token))) {
                tiene_x = true;
            }
        }
        if (!tiene_x) dependen_de_x = false;
        // Solo las bases (#143) estan garantizadas finitas. Las precargadas
        // son el contenido historico intacto (#144); algunas usan SQRT(X) y
        // pueden dar NaN fuera de x >= 0. El filtro de validez y el relleno
        // aleatorio de esos huecos llegan en #147.
        if (indice < NUM_BASES_POR_CASO && !std::isfinite(individuo.fitness())) {
            bases_finitas = false;
        }
        indice++;
    }
    const string etiqueta = "caso " + std::to_string(caso) + ": ";
    check_bool(etiqueta + "las " + std::to_string(TOTAL_CATALOGO) +
                   " entradas renderizan completas",
               completas);
    check_bool(etiqueta + "las " + std::to_string(TOTAL_CATALOGO) +
                   " entradas dependen de x",
               dependen_de_x);
    check_bool(etiqueta + "las bases tienen fitness finita", bases_finitas);
}

static void test_catalogo_semillas_por_caso() {
    const std::array<int, 8> casos = {0, 1, 2, 3, 4, 5, 6, 7};

    for (const int caso : casos) {
        const string etiqueta = "caso " + std::to_string(caso) + ": ";
        const vector<EntradaCatalogo>& catalogo = catalogo_semillas(caso);

        check_int(etiqueta + "el catalogo tiene " +
                      std::to_string(TOTAL_CATALOGO) + " entradas (" +
                      std::to_string(NUM_BASES_POR_CASO) + " bases + " +
                      std::to_string(NUM_PRECARGADAS) + " precargadas + " +
                      std::to_string(NUM_BLOQUES_GENERICOS) + " genericos)",
                  static_cast<int>(catalogo.size()),
                  static_cast<int>(TOTAL_CATALOGO));

        validar_catalogo(catalogo, caso);

        // El tramo precargado ocupa los indices 5-9 y conserva exactamente
        // los tokens del prototipo (issue #144).
        const vector<vector<ldouble>>& legado = precargadas_del_prototipo();
        bool precargadas_intactas =
            catalogo.size() >= NUM_BASES_POR_CASO + legado.size();
        for (size_t i = 0;
             i < legado.size() && NUM_BASES_POR_CASO + i < catalogo.size();
             i++) {
            if (catalogo[NUM_BASES_POR_CASO + i].get() != legado[i]) {
                precargadas_intactas = false;
            }
        }
        check_bool(etiqueta +
                       "las cinco precargadas (indices 5-9) conservan el contenido del prototipo",
                   precargadas_intactas);

        if (caso == 1) {
            // Si(x) no esta en el alfabeto: la mejor entrada es la serie
            // truncada de tres terminos x - x^3/18 + x^5/600. El limite es
            // holgado porque la serie solo aproxima la integral exacta.
            long double mejor = INFINITY;
            for (size_t i = 0; i < NUM_BASES_POR_CASO && i < catalogo.size(); i++) {
                const long double fit = fitness_de_base(catalogo[i].get(), caso);
                if (fit < mejor) mejor = fit;
            }
            check_bool(etiqueta +
                           "la mejor entrada expresable tiene fitness < 1e-3",
                       mejor < 1e-3L);
        } else {
            // La primera base es la solucion exacta de la ODE del caso.
            check_bool(etiqueta +
                           "la base exacta (primera) tiene fitness < 1e-6",
                       !catalogo.empty() &&
                           fitness_de_base(catalogo[0].get(), caso) < 1e-6L);
        }
    }

    // El test Legendre previo se conserva: su base exacta sigue siendo
    // DIV(ADD(MUL(3, x^2), NEG(1)), 2) = (3x^2-1)/2.
    const vector<ldouble> legendre = {
        TOKEN_DIV, TOKEN_ADD, TOKEN_MUL, TOKEN_CONST3,
        TOKEN_X2, TOKEN_NEG, TOKEN_CONST1, TOKEN_CONST2};
    const vector<EntradaCatalogo>& caso6 = catalogo_semillas(6);
    check_bool("la base exacta del caso 6 sigue siendo (3x^2-1)/2",
               !caso6.empty() && caso6[0].get() == legendre);

    // Un caso fuera de 0..7 no inventa bases: el contrato del catalogo es
    // explicito y no depende del clamp de main().
    check_bool("un caso fuera de 0..7 no inventa bases",
               catalogo_semillas(8).empty());
}

// genericos_compartidos: comprueba que el tramo generico de un catalogo son
// las mismas instancias que bloques_genericos(), en los indices 10-24. En
// Python seria `all(catalogo[i] is GENERICOS[i - 10] for i in ...)`.
static bool genericos_compartidos(const vector<EntradaCatalogo>& catalogo) {
    const vector<vector<ldouble>>& genericos = bloques_genericos();
    if (catalogo.size() < NUM_BASES_POR_CASO + NUM_PRECARGADAS + genericos.size()) {
        return false;
    }
    for (size_t i = 0; i < genericos.size(); i++) {
        const size_t indice = NUM_BASES_POR_CASO + NUM_PRECARGADAS + i;
        if (&catalogo[indice].get() != &genericos[i]) return false;
    }
    return true;
}

// Issue #145: el tier generico (indices 10-24) es compartido entre todos los
// casos. Verificamos el contenido exacto contra un oraculo independiente, que
// los ocho catalogos referencian las mismas instancias y que su fitness es
// finita en todos los casos.
static void test_genericos_compartidos_entre_casos() {
    const std::array<int, 8> casos = {0, 1, 2, 3, 4, 5, 6, 7};
    const vector<vector<ldouble>>& genericos = bloques_genericos();
    const vector<vector<ldouble>>& esperados = genericos_del_issue_145();

    // En Python seria `assert genericos == GENERICOS_REFERENCIA`.
    bool contenido_exacto = genericos.size() == esperados.size();
    for (size_t i = 0; i < genericos.size() && i < esperados.size(); i++) {
        if (genericos[i] != esperados[i]) contenido_exacto = false;
    }
    check_bool("los " + std::to_string(NUM_BLOQUES_GENERICOS) +
                   " genericos conservan el contenido exacto del issue #145",
               contenido_exacto);

    // En Python seria
    // `assert all(catalogo_semillas(c)[10 + i] is GENERICOS[i]
    //            for c in casos for i in range(15))`.
    bool compartidos = true;
    for (const int caso : casos) {
        if (!genericos_compartidos(catalogo_semillas(caso))) {
            compartidos = false;
        }
    }
    check_bool("los " + std::to_string(NUM_BLOQUES_GENERICOS) +
                   " genericos son las mismas instancias en los ocho casos",
               compartidos);

    // En Python seria
    // `assert all(math.isfinite(fitness(g, c)) for g in genericos for c in casos)`.
    bool finitos = true;
    for (const auto& generico : genericos) {
        for (const int caso : casos) {
            if (!std::isfinite(fitness_de_base(generico, caso))) {
                finitos = false;
            }
        }
    }
    check_bool("los " + std::to_string(NUM_BLOQUES_GENERICOS) +
                   " genericos evaluan a fitness finita en todos los casos",
               finitos);
}

// -------------------------------------------------------------------------
// Slice 4c (issues #142-#146): createPopulation inyecta las entradas del
// catalogo del caso al frente de la poblacion, rellenas con constantes 1 hasta
// la dimension configurada. El orden es bases (0-4), precargadas (5-9) y
// genericos (10-24). La inyeccion respeta la cuota del 5% (issue #146) y no
// consume RNG: la cola aleatoria coincide con la de una poblacion fria de la
// misma semilla.
// -------------------------------------------------------------------------

static void test_warm_start_inyecta_el_catalogo_del_caso() {
    const std::array<int, 8> casos = {0, 1, 2, 3, 4, 5, 6, 7};
    const std::array<int, 3> dimensiones = {20, 25, 30};
    // El issue #145 completa el catalogo a 25 entradas (5 bases + 5 precargadas
    // + 15 genericos). Con pop=500, la cuota del 5% es ceil(0.05*500)=25, lo
    // que permite inyectar el catalogo completo y la cabecera debe reportar
    // warm_seeds=25.
    constexpr int POBLACION_CATALOGO_COMPLETO = 500;

    for (const int caso : casos) {
        const vector<EntradaCatalogo>& catalogo = catalogo_semillas(caso);
        const vector<vector<ldouble>>& legado = precargadas_del_prototipo();
        const vector<vector<ldouble>>& genericos = bloques_genericos();

        for (const int dimension : dimensiones) {
            // Semilla distinta por (caso, dimension): el test no depende de
            // que todos los sorteos aleatorios coincidan.
            srand(20261022 + caso * 100 + dimension);

            ConfigBusqueda config;
            config.poblacion = POBLACION_CATALOGO_COMPLETO;
            config.dimension = dimension;
            config.warm_start = true;

            vector<FunbObj> poblacion;
            const int sembradas = createPopulation(poblacion, config, caso);

            const string etiqueta = "caso " + std::to_string(caso) +
                                    " dim " + std::to_string(dimension) + ": ";
            check_int(etiqueta + "inyecta las " + std::to_string(TOTAL_CATALOGO) +
                          " semillas del catalogo completo",
                      sembradas, static_cast<int>(TOTAL_CATALOGO));
            check_bool(etiqueta + "conserva el tamano de la poblacion",
                       poblacion.size() == POBLACION_CATALOGO_COMPLETO);

            // Las cinco bases ocupan los indices 0-4, en el orden del catalogo
            // y rellenas con constantes 1 hasta la dimension configurada.
            bool bases_al_frente = true;
            for (size_t i = 0; i < NUM_BASES_POR_CASO && i < catalogo.size(); i++) {
                vector<ldouble> esperada = catalogo[i].get();
                esperada.resize(dimension, TOKEN_CONST1);
                if (poblacion[i].vecvalue() != esperada) bases_al_frente = false;
            }
            check_bool(etiqueta + "las cinco bases ocupan los indices 0-4",
                       bases_al_frente);

            // Las precargadas ocupan los indices 5-9, tambien en orden y con
            // el mismo relleno. Se comparan contra los literales del
            // prototipo, no contra el catalogo, para detectar una alteracion
            // del tramo precargado.
            bool precargadas_despues = true;
            for (size_t i = 0;
                 i < legado.size() && NUM_BASES_POR_CASO + i < poblacion.size();
                 i++) {
                vector<ldouble> esperada = legado[i];
                esperada.resize(dimension, TOKEN_CONST1);
                if (poblacion[NUM_BASES_POR_CASO + i].vecvalue() != esperada) {
                    precargadas_despues = false;
                }
            }
            check_bool(etiqueta + "las cinco precargadas ocupan los indices 5-9",
                       precargadas_despues);

            // Los genericos ocupan los indices 10-24, tambien en orden y con
            // el mismo relleno. Se comparan contra los bloques genericos
            // compartidos para verificar que el tramo generico sea correcto.
            bool genericos_despues = true;
            for (size_t i = 0;
                 i < genericos.size() && NUM_BASES_POR_CASO + NUM_PRECARGADAS + i < poblacion.size();
                 i++) {
                vector<ldouble> esperada = genericos[i];
                esperada.resize(dimension, TOKEN_CONST1);
                if (poblacion[NUM_BASES_POR_CASO + NUM_PRECARGADAS + i].vecvalue() != esperada) {
                    genericos_despues = false;
                }
            }
            check_bool(etiqueta + "los " +
                           std::to_string(NUM_BLOQUES_GENERICOS) +
                           " genericos ocupan los indices 10-24",
                       genericos_despues);

            // Las semillas inyectadas deben renderizar como expresiones
            // prefijas completas (sin huecos de aridad). En Python seria
            // `assert all("()" not in repr(p) for p in poblacion[:25])`.
            bool completas = true;
            for (size_t i = 0; i < TOTAL_CATALOGO && i < poblacion.size(); i++) {
                if (poblacion[i].represant().find("()") != string::npos) {
                    completas = false;
                }
            }
            check_bool(etiqueta + "las " + std::to_string(TOTAL_CATALOGO) +
                           " semillas inyectadas renderizan completas",
                       completas);
        }

    }

    // Sin consumo de RNG: con la misma semilla, la cola aleatoria de una
    // corrida caliente es la poblacion fria desplazada por las semillas
    // realmente inyectadas. La cuota del 5% (issue #146) deja pasar una sola
    // semilla con poblacion 20 (ceil(0.05*20)=1), asi que el desplazamiento
    // es de uno.
    srand(20261023);
    ConfigBusqueda caliente;
    caliente.poblacion = 20;
    caliente.dimension = 20;
    caliente.warm_start = true;
    vector<FunbObj> con_semilla;
    const size_t semillas =
        static_cast<size_t>(createPopulation(con_semilla, caliente, 6));

    srand(20261023);
    ConfigBusqueda frio;
    frio.poblacion = 20;
    frio.dimension = 20;
    vector<FunbObj> sin_semilla;
    createPopulation(sin_semilla, frio, 6);

    bool cola_igual = true;
    for (size_t i = 0; i + semillas < con_semilla.size(); i++) {
        if (con_semilla[i + semillas].vecvalue() != sin_semilla[i].vecvalue()) {
            cola_igual = false;
        }
    }
    check_bool("la inyeccion no consume RNG (la cola aleatoria coincide)",
               cola_igual);

    // Un caso sin entradas en el catalogo no siembra nada, aunque el warm
    // start este activo.
    srand(20261024);
    ConfigBusqueda otro;
    otro.poblacion = 10;
    otro.dimension = 25;
    otro.warm_start = true;
    vector<FunbObj> poblacion_otro;
    const int sembradas_otro = createPopulation(poblacion_otro, otro, 8);
    check_int("un caso sin catalogo no inyecta semillas", sembradas_otro, 0);
    check_bool("un caso sin catalogo conserva el tamano de la poblacion",
               poblacion_otro.size() == 10);
}

// -------------------------------------------------------------------------
// Slice 4d (issue #146): la cuota de inyeccion es
// min(ceil(0.05 * poblacion), tamano del catalogo). Con el catalogo actual
// de 25 entradas la cuota llega al tope con pop=500; las comprobaciones con
// tope diez conservan el escenario historico del issue #146 (catalogo de
// diez entradas) para verificar que el tope se aplica.
// -------------------------------------------------------------------------

static void test_cuota_semillas() {
    // Escenario historico del issue #146: tope con un catalogo de diez
    // entradas. En Python serian `CATALOGO_HISTORICO = 10` y
    // `CATALOGO_ACTUAL = len(CATALOGO)`.
    constexpr int CATALOGO_HISTORICO = 10;
    constexpr int CATALOGO_ACTUAL = static_cast<int>(TOTAL_CATALOGO);

    // Poblaciones del issue #146 contra el catalogo historico de diez.
    check_int("cuota pop=4: ceil(0.05*4)=1", cuota_semillas(4, CATALOGO_HISTORICO), 1);
    check_int("cuota pop=100: ceil(0.05*100)=5",
              cuota_semillas(100, CATALOGO_HISTORICO), 5);
    check_int("cuota pop=200: ceil(0.05*200)=10",
              cuota_semillas(200, CATALOGO_HISTORICO), 10);
    check_int("cuota pop=300 con catalogo de " +
                  std::to_string(CATALOGO_HISTORICO) + ": tope 10",
              cuota_semillas(300, CATALOGO_HISTORICO), CATALOGO_HISTORICO);
    check_int("cuota pop=500 con catalogo de " +
                  std::to_string(CATALOGO_HISTORICO) + ": tope 10",
              cuota_semillas(500, CATALOGO_HISTORICO), CATALOGO_HISTORICO);
    check_int("cuota pop=800 con catalogo de " +
                  std::to_string(CATALOGO_HISTORICO) + ": tope 10",
              cuota_semillas(800, CATALOGO_HISTORICO), CATALOGO_HISTORICO);

    // Con el catalogo actual de 25 la cuota es el ceil puro: pop=300 -> 15
    // y pop>=500 -> 25.
    check_int("cuota pop=300 sin tope: 15", cuota_semillas(300, CATALOGO_ACTUAL), 15);
    check_int("cuota pop=500 sin tope: 25",
              cuota_semillas(500, CATALOGO_ACTUAL), CATALOGO_ACTUAL);
    check_int("cuota pop=800 con catalogo actual: tope 25",
              cuota_semillas(800, CATALOGO_ACTUAL), CATALOGO_ACTUAL);
}

static void test_warm_start_cuota_del_5_por_ciento() {
    // Con pop=100 (el default del paper) la cuota es 5: entran solo las cinco
    // bases y se restaura el warm_seeds=5 del comportamiento previo a #144.
    // Con pop=4 entra una sola semilla (ceil(0.05*4)=1).
    srand(20261025);
    ConfigBusqueda config_100;
    config_100.poblacion = 100;
    config_100.dimension = 20;
    config_100.warm_start = true;
    vector<FunbObj> poblacion_100;
    const int sembradas_100 = createPopulation(poblacion_100, config_100, 0);
    check_int("pop=100: la cuota inyecta 5 semillas (warm_seeds=5 legacy)",
              sembradas_100, 5);
    check_bool("pop=100: la poblacion conserva su tamano",
               poblacion_100.size() == 100);

    srand(20261026);
    ConfigBusqueda config_4;
    config_4.poblacion = 4;
    config_4.dimension = 20;
    config_4.warm_start = true;
    vector<FunbObj> poblacion_4;
    const int sembradas_4 = createPopulation(poblacion_4, config_4, 0);
    check_int("pop=4: la cuota inyecta 1 semilla", sembradas_4, 1);
    check_bool("pop=4: la poblacion conserva su tamano", poblacion_4.size() == 4);
}

// -------------------------------------------------------------------------
// Slice 5 (issue #28): la reproducibilidad empieza en la poblacion inicial: la
// misma semilla de rand() debe producir exactamente los mismos individuos
// (criterion 3 de #28).
// -------------------------------------------------------------------------
static void test_createPopulation_reproducible() {
    ConfigBusqueda config;
    config.poblacion = 10;

    srand(20261013);
    vector<FunbObj> primera;
    createPopulation(primera, config, 0);

    srand(20261013);
    vector<FunbObj> segunda;
    createPopulation(segunda, config, 0);

    bool iguales = primera.size() == segunda.size();
    for (size_t i = 0; i < primera.size() && iguales; i++) {
        if (primera[i].vecvalue() != segunda[i].vecvalue()) iguales = false;
    }
    check_bool("la misma semilla produce la misma poblacion inicial", iguales);
}

// -------------------------------------------------------------------------
// Slice 6 (issue #28): configuracion de la busqueda. Sin variables de
// entorno, los valores por defecto son los del Caso 1 del paper (REVIEW
// C5-C7): 100 individuos, dimension 20 y 5000 generaciones. En Python esto
// seria un `@dataclass` con defaults, construido por una funcion pura que
// recibe los `os.environ.get(...)` ya leidos.
// -------------------------------------------------------------------------

static void test_config_defaults() {
    // Mismo patron que resolver_semilla: la funcion recibe los getenv() ya
    // resueltos (nullptr = variable ausente) para poder probarse sin tocar el
    // entorno del proceso.
    ConfigBusqueda config = resolver_config(nullptr, nullptr, nullptr, nullptr);

    // Los literales 100/20/5000 vienen del paper (Caso 1), no de la
    // implementacion: son la fuente de verdad independiente del test.
    check_int("poblacion por defecto = 100 (Caso 1 del paper)", config.poblacion, 100);
    check_int("dimension por defecto = 20 (Caso 1 del paper)", config.dimension, 20);
    check_int("generaciones por defecto = 5000 (Caso 1 del paper)",
              config.max_generaciones, 5000);
    check_bool("warm start desactivado por defecto", !config.warm_start);
}

static void test_config_overrides() {
    // Cada variable de entorno pisa su default (equivalente a leer
    // os.environ["ODE_POP"], etc.). "0" en la bandera desactiva el warm start.
    ConfigBusqueda config = resolver_config("50", "30", "77", "0");

    check_int("ODE_POP=50 sobreescribe la poblacion", config.poblacion, 50);
    check_int("ODE_DIM=30 sobreescribe la dimension", config.dimension, 30);
    check_int("ODE_GEN=77 sobreescribe las generaciones", config.max_generaciones, 77);
    check_bool("ODE_WARM_START=0 desactiva el warm start", !config.warm_start);

    // Con la dimension por defecto (20) el warm start si se acepta.
    ConfigBusqueda caliente = resolver_config(nullptr, nullptr, nullptr, "1");
    check_bool("ODE_WARM_START=1 activa el warm start con dimension 20",
               caliente.warm_start);
}

static void test_config_invalida_usa_respaldo() {
    // "abc" y "" no son numeros, "0" esta por debajo del minimo de
    // generaciones y "tal vez" no es una bandera: cada valor invalido cae a su
    // respaldo, la misma regla que usa resolver_semilla con ODE_SEED.
    ConfigBusqueda config = resolver_config("abc", "", "0", "tal vez");

    check_int("ODE_POP invalida usa el default 100", config.poblacion, 100);
    check_int("ODE_DIM invalida usa el default 20", config.dimension, 20);
    check_int("ODE_GEN invalida usa el default 5000", config.max_generaciones, 5000);
    check_bool("ODE_WARM_START invalida usa el default (apagado)",
               !config.warm_start);

    // Una poblacion menor que 4 no puede elegir 3 individuos base distintos
    // del objetivo (indices_distintos): el minimo aceptado es 4.
    check_int("ODE_POP=3 queda por debajo del minimo y usa el default",
              resolver_config("3", nullptr, nullptr, nullptr).poblacion, 100);
    check_int("ODE_POP=4 es aceptable",
              resolver_config("4", nullptr, nullptr, nullptr).poblacion, 4);

    // "17abc" no es un entero puro: se rechaza entero en vez de quedarse con 17.
    check_int("un texto a medias no es un entero valido",
              resolver_config("17abc", nullptr, nullptr, nullptr).poblacion, 100);
}

static void test_warm_start_cualquier_dimension() {
    // El warm start ya no depende de que la dimension sea 20: las entradas del
    // catalogo se rellenan con constantes 1 hasta la dimension configurada.
    ConfigBusqueda config = resolver_config(nullptr, "30", nullptr, "1");

    check_int("la dimension pedida se conserva (30)", config.dimension, 30);
    check_bool("ODE_WARM_START=1 se respeta con ODE_DIM=30", config.warm_start);

    ConfigBusqueda dim25 = resolver_config(nullptr, "25", nullptr, "1");
    check_bool("ODE_WARM_START=1 se respeta con ODE_DIM=25", dim25.warm_start);
}

// -------------------------------------------------------------------------
// Slice 7 (issue #82): reinicio por estancamiento. El reinicio reemplaza el
// 80% de la poblacion para inyectar diversidad, pero el indice reservado
// conserva el mejor historico: antes el `FunbObj` del record se construia y
// se descartaba, y el unico indice que sobrevivia era el mejor de la
// generacion actual (no el record).
// -------------------------------------------------------------------------

static void test_reinicio_reemplaza_el_80_por_ciento() {
    srand(20261013);

    // 15 individuos: el 80% es 12, el mismo criterio que el log real
    // `restart | ... replaced=120` con una poblacion de 150.
    ConfigBusqueda config;
    config.poblacion = 15;

    vector<FunbObj> poblacion;
    createPopulation(poblacion, config, 0);

    // Copia de los vectores originales para contar cuantos cambian.
    vector<vector<ldouble>> antes;
    for (size_t i = 0; i < poblacion.size(); i++){
        antes.push_back(poblacion[i].vecvalue());
    }

    const int indice_record = 3;
    const vector<ldouble> record = poblacion[indice_record].vecvalue();

    const int reemplazados = reiniciar_poblacion(poblacion, indice_record, record, config, 0);

    check_int("el reinicio reemplaza el 80% de la poblacion (12 de 15)", reemplazados, 12);
    check_bool("el reinicio conserva el tamano de la poblacion", poblacion.size() == 15);

    // Con el record igual al vector del indice reservado, solo deben cambiar
    // los reemplazados. El srand fijo hace deterministico el sorteo, asi que
    // la comprobacion no es probabilistica.
    int cambiados = 0;
    for (size_t i = 0; i < poblacion.size(); i++){
        if (poblacion[i].vecvalue() != antes[i]) cambiados++;
    }
    check_int("solo los reemplazados cambian de vector", cambiados, reemplazados);
}

static void test_reinicio_restaura_el_record() {
    srand(20261013);

    ConfigBusqueda config;
    config.poblacion = 10;
    vector<FunbObj> poblacion;
    createPopulation(poblacion, config, 0);

    // Record simulado: un vector fijo distinto del individuo que hoy ocupa el
    // indice reservado (en una corrida estancada el mejor de la generacion no
    // es el record, que suele vivir en otro indice). Son tokens validos.
    const vector<ldouble> record = {18, 13, 17, 11};
    const int indice_record = 4;
    check_bool("el record simulado no es el individuo del indice reservado",
               poblacion[indice_record].vecvalue() != record);

    reiniciar_poblacion(poblacion, indice_record, record, config, 0);

    check_bool("el indice reservado contiene exactamente el record restaurado",
               poblacion[indice_record].vecvalue() == record);
}

static void test_reinicio_sin_record_no_toca_el_indice_reservado() {
    srand(20261013);

    ConfigBusqueda config;
    config.poblacion = 10;
    vector<FunbObj> poblacion;
    createPopulation(poblacion, config, 0);

    // `sin_record` simula la corrida en la que nunca hubo un fitness finito:
    // el helper no debe restaurar nada ni tocar el indice reservado.
    const int indice_record = 5;
    const vector<ldouble> original = poblacion[indice_record].vecvalue();
    const vector<ldouble> sin_record;

    const int reemplazados = reiniciar_poblacion(poblacion, indice_record, sin_record, config, 0);

    check_int("sin record el reinicio sigue reemplazando el 80% (8 de 10)", reemplazados, 8);
    check_bool("sin record el indice reservado queda intacto",
               poblacion[indice_record].vecvalue() == original);
}

// -------------------------------------------------------------------------
// Slice 8 (issue #127): reinicio adaptativo. A diferencia del legacy, no
// reemplaza posiciones arbitrarias del arreglo: ordena por fitness y
// reemplaza solo a los peores, asi que los sobrevivientes son los individuos
// realmente mas aptos. El reinicio legacy de arriba queda intacto.
// -------------------------------------------------------------------------

// poblacion_fixture: arma individuos {TOKEN_MUL, TOKEN_CONSTk, TOKEN_X}
// (notacion prefija: `19 k 11` = k*x) para las constantes recibidas. Para el
// caso 0, F(k*x) = integral(k*x)^2 + 10*(k-1)^2 (las demas penalizaciones de
// la ec. (4) son cero porque y(0)=0 y la de y'(0)=k la fija la constante),
// que crece estrictamente con k: el fixture tiene un orden de fitness
// conocido a mano, sin recalcular nada con la implementacion. En Python seria
// `[FunbObj([19, k, 11], 0, 1, 0) for k in constantes]`.
static vector<FunbObj> poblacion_fixture(const vector<int>& constantes) {
    vector<FunbObj> poblacion;
    for (size_t i = 0; i < constantes.size(); i++){
        const int token_constante = TOKEN_CONST1 + (constantes[i] - 1);
        poblacion.push_back(FunbObj({static_cast<ldouble>(TOKEN_MUL),
                                     static_cast<ldouble>(token_constante),
                                     static_cast<ldouble>(TOKEN_X)}, 0, 1, 0));
    }
    return poblacion;
}

// copia_vectores: fotografia de los vectores antes del reinicio, para saber
// quien sobrevivio intacto. En Python seria
// `antes = [ind.vecvalue() for ind in poblacion]`.
static vector<vector<ldouble>> copia_vectores(const vector<FunbObj>& poblacion) {
    vector<vector<ldouble>> copia;
    for (size_t i = 0; i < poblacion.size(); i++){
        copia.push_back(poblacion[i].vecvalue());
    }
    return copia;
}

static void test_reinicio_adaptativo_conserva_los_mejores() {
    srand(20261017);

    // La poblacion baraja los k a proposito: los mas aptos no estan en las
    // primeras posiciones (ese es el quirk que el modo legacy conserva).
    vector<FunbObj> poblacion = poblacion_fixture({3, 1, 5, 2, 4});
    const vector<vector<ldouble>> antes = copia_vectores(poblacion);

    // Premisa del fixture: la fitness real respeta k=1 < k=2 < ... < k=5
    // (F(k) = k^2/3 + 10 + 10*(k-1)^2, calculada a mano). Si esto falla, el
    // resto del test no probaria nada.
    check_bool("fixture: F(k=1) < F(k=2) < ... < F(k=5)",
               poblacion[1].fitness() < poblacion[3].fitness() &&
               poblacion[3].fitness() < poblacion[0].fitness() &&
               poblacion[0].fitness() < poblacion[4].fitness() &&
               poblacion[4].fitness() < poblacion[2].fitness());

    // Record = vector del mejor historico: el k=1 (indice 1).
    const vector<ldouble> record = poblacion[1].vecvalue();

    const int reemplazados =
        reiniciar_poblacion_adaptativo(poblacion, record, {0.4L, 0.5L, 3, 0});

    check_int("el reinicio adaptativo reemplaza el 40% (2 de 5)", reemplazados, 2);
    check_bool("conserva el tamano de la poblacion", poblacion.size() == 5);

    // Sobreviven los 3 mas aptos (k=1, k=2, k=3), sin cambios.
    check_bool("sobrevive intacto el k=1 (indice 1)",
               poblacion[1].vecvalue() == antes[1]);
    check_bool("sobrevive intacto el k=2 (indice 3)",
               poblacion[3].vecvalue() == antes[3]);
    check_bool("sobrevive intacto el k=3 (indice 0)",
               poblacion[0].vecvalue() == antes[0]);

    // Se reemplazan exactamente los peores (k=4 en el indice 4 y k=5 en el
    // indice 2): ninguna posicion arbitraria se toca.
    check_bool("se reemplaza el k=4 (segundo peor, indice 4)",
               poblacion[4].vecvalue() != antes[4]);
    check_bool("se reemplaza el k=5 (peor, indice 2)",
               poblacion[2].vecvalue() != antes[2]);

    check_bool("el record sigue presente en la poblacion",
               poblacion[1].vecvalue() == record);
}

static void test_reinicio_adaptativo_inyecta_el_record() {
    srand(20261018);

    // Fixture {2, 1, 4, 3}: el orden por fitness es k=1 (indice 1), k=2
    // (indice 0), k=3 (indice 3), k=4 (indice 2). Con fraccion 0.5 se
    // conservan 2 y se reemplazan 2.
    vector<FunbObj> poblacion = poblacion_fixture({2, 1, 4, 3});
    const vector<vector<ldouble>> antes = copia_vectores(poblacion);

    // Record simulado que NO esta en la poblacion (el caso real de una corrida
    // estancada donde el record historico no ocupa el indice reservado): la
    // expresion 1+x, mas apta que cualquier k*x del fixture.
    const vector<ldouble> record = {static_cast<ldouble>(TOKEN_ADD),
                                    static_cast<ldouble>(TOKEN_CONST1),
                                    static_cast<ldouble>(TOKEN_X)};

    const int reemplazados =
        reiniciar_poblacion_adaptativo(poblacion, record, {0.5L, 0.5L, 3, 0});

    check_int("inyecta el record reemplazando 2 de 4", reemplazados, 2);

    bool record_presente = false;
    for (size_t i = 0; i < poblacion.size(); i++){
        if (poblacion[i].vecvalue() == record) record_presente = true;
    }
    check_bool("el record ausente entra a la poblacion durante el reinicio",
               record_presente);

    // La inyeccion no sacrifica a ningun sobreviviente: los dos conservados
    // (k=1 en el indice 1 y k=2 en el indice 0) quedan intactos y el record
    // ocupa el primer lugar reemplazado (k=3, el mejor de los dos peores).
    check_bool("el mejor real (k=1, indice 1) sobrevive intacto",
               poblacion[1].vecvalue() == antes[1]);
    check_bool("el k=2 (indice 0) tambien sobrevive intacto",
               poblacion[0].vecvalue() == antes[0]);
    check_bool("el record ocupa el primer lugar reemplazado (indice 3)",
               poblacion[3].vecvalue() == record);
    // El resto de los reemplazados sigue siendo el peor (k=4).
    check_bool("se reemplaza el k=4 (peor, indice 2)",
               poblacion[2].vecvalue() != antes[2]);

    // Un record vacio (la corrida nunca encontro fitness finito) no inyecta
    // nada: los sobrevivientes quedan exactamente como estaban.
    srand(20261018);
    vector<FunbObj> otra = poblacion_fixture({2, 1, 4, 3});
    const vector<vector<ldouble>> antes_otra = copia_vectores(otra);
    const vector<ldouble> sin_record;

    reiniciar_poblacion_adaptativo(otra, sin_record, {0.5L, 0.5L, 3, 0});

    check_bool("sin record, los 2 sobrevivientes quedan intactos",
               otra[1].vecvalue() == antes_otra[1] &&
               otra[0].vecvalue() == antes_otra[0]);
}

// -------------------------------------------------------------------------
// Slice 9 (issue #127): composicion hibrida de los reemplazos. Una parte son
// mutaciones puntuales de 1-2 tokens de un elite (los sobrevivientes) y el
// resto individuos uniformes nuevos, segun ODE_RESTART_MUTATION_SHARE.
// -------------------------------------------------------------------------

// distancia_hamming: posiciones en que difieren dos vectores. En Python seria
// `sum(1 for a, b in zip(x, y) if a != b)`.
static int distancia_hamming(const vector<ldouble>& a, const vector<ldouble>& b) {
    const size_t n = (a.size() < b.size()) ? a.size() : b.size();
    int distancia = 0;
    for (size_t i = 0; i < n; i++){
        if (a[i] != b[i]) distancia++;
    }
    return distancia;
}

// distancia_minima_a: minima distancia Hamming entre `valor` y los vectores
// de los indices `candidatos`; -1 si la lista esta vacia. En Python seria
// `min((distancia_hamming(valor, poblacion[i].vecvalue()) for i in candidatos), default=-1)`.
static int distancia_minima_a(const vector<ldouble>& valor,
                              const vector<FunbObj>& poblacion,
                              const vector<int>& candidatos) {
    int minima = -1;
    for (size_t s = 0; s < candidatos.size(); s++){
        const int d = distancia_hamming(valor, poblacion[candidatos[s]].vecvalue());
        if (minima < 0 || d < minima) minima = d;
    }
    return minima;
}

static void test_reinicio_adaptativo_compone_reemplazos_hibridos() {
    ConfigBusqueda config;
    config.poblacion = 10;
    config.dimension = 20;

    // Tres escenarios con la misma poblacion base y la misma semilla: el
    // unico cambio es la cuota de mutacion. 0.5 se parte 2+2, 1.0 es todo
    // mutacion de elite y 0.0 todo uniforme: asi la composicion se verifica
    // sin depender de clasificar a ojo un sorteo probabilistico.
    // En Python seria una lista de dicts o namedtuples.
    struct Escenario {
        long double share;
        int mutaciones_esperadas;
        const char* nombre;
    };
    const Escenario escenarios[] = {
        {0.5L, 2, "share 0.5"},
        {1.0L, 4, "share 1.0"},
        {0.0L, 0, "share 0.0"},
    };

    const size_t n_escenarios = sizeof(escenarios) / sizeof(escenarios[0]);
    for (size_t esc = 0; esc < n_escenarios; esc++){
        const Escenario& e = escenarios[esc];
        srand(20261019);

        vector<FunbObj> poblacion;
        createPopulation(poblacion, config, 0);
        const vector<vector<ldouble>> antes = copia_vectores(poblacion);
        const vector<ldouble> record;  // sin record: aca importa la composicion

        const int reemplazados = reiniciar_poblacion_adaptativo(
            poblacion, record, {0.4L, e.share, config.dimension, 0});

        // Sobreviviente = vector intacto; reemplazado = vector cambiado.
        vector<int> sobrevivientes_idx;
        vector<int> reemplazados_idx;
        bool longitudes_ok = true;
        for (size_t i = 0; i < poblacion.size(); i++){
            if (poblacion[i].vecvalue() == antes[i]){
                sobrevivientes_idx.push_back(static_cast<int>(i));
            } else {
                reemplazados_idx.push_back(static_cast<int>(i));
            }
            if (poblacion[i].vecvalue().size() != static_cast<size_t>(config.dimension)){
                longitudes_ok = false;
            }
        }

        // Composicion: cada reemplazo es una mutacion de elite (a 1-2 tokens
        // de un sobreviviente) o un uniforme fresco (a >= 3 de todos). Los dos
        // predicados son distintos a proposito: un reemplazo que no cambiara
        // nada (distancia 0) no cae en ninguno y delata el error.
        int mutaciones = 0;
        int uniformes = 0;
        for (size_t r = 0; r < reemplazados_idx.size(); r++){
            const int minima = distancia_minima_a(
                poblacion[reemplazados_idx[r]].vecvalue(), poblacion, sobrevivientes_idx);
            if (minima >= 1 && minima <= 2) mutaciones++;
            if (minima >= 3) uniformes++;
        }

        const string etiqueta = string(e.nombre) + ": ";
        check_int(etiqueta + "reemplaza 4 de 10", reemplazados, 4);
        check_bool(etiqueta + "todos los reemplazos conservan la dimension",
                   longitudes_ok);
        check_int(etiqueta + "mutaciones de elite esperadas", mutaciones,
                  e.mutaciones_esperadas);
        check_int(etiqueta + "uniformes frescos (el resto)", uniformes,
                  reemplazados - e.mutaciones_esperadas);
    }
}

// -------------------------------------------------------------------------
// Slice 10 (issue #127, revision): cobertura directa del nucleo puro de la
// mutacion puntual (sin sorteo, con valores a mano), del driver aleatorio y
// del borde en que el record ausente no tiene cupo de reemplazo.
// -------------------------------------------------------------------------

static void test_parte_entera_trunca_hacia_abajo() {
    check_int("parte_entera(0.8, 100) = 80", parte_entera(0.8L, 100), 80);
    check_int("parte_entera(0.4, 5) = 2", parte_entera(0.4L, 5), 2);
    check_int("parte_entera trunca hacia abajo: 0.99 de 3 = 2",
              parte_entera(0.99L, 3), 2);
    check_int("parte_entera(0, n) = 0", parte_entera(0.0L, 7), 0);
}

static void test_mutacion_puntual_cambia_uno_o_dos_tokens() {
    srand(20261020);

    // Nucleo puro: una sustitucion explicita cambia exactamente una posicion
    // y conserva la longitud, sin sorteo (como mutacion_rand1).
    const vector<ldouble> base = {TOKEN_MUL, TOKEN_CONST1, TOKEN_X,
                                  TOKEN_SIN, TOKEN_CONST2};
    const vector<ldouble> sustitucion = mutacion_puntual_en(base, 3, TOKEN_COS);
    check_int("mutacion puntual directa: cambia exactamente 1 token",
              distancia_hamming(sustitucion, base), 1);
    check_int("mutacion puntual directa: conserva la longitud",
              static_cast<int>(sustitucion.size()),
              static_cast<int>(base.size()));
    check_bool("mutacion puntual directa: escribe el token pedido en la posicion pedida",
               sustitucion[3] == static_cast<ldouble>(TOKEN_COS) &&
               sustitucion[0] == base[0] && sustitucion[1] == base[1] &&
               sustitucion[2] == base[2] && sustitucion[4] == base[4]);

    // Driver: 40 sorteos con la misma semilla; siempre 1-2 cambios y la
    // longitud intacta. El minimo arranca en el maximo posible y el maximo en
    // 0: son cotas, no centinelas.
    int minimo = static_cast<int>(base.size());
    int maximo = 0;
    bool longitudes_ok = true;
    for (int i = 0; i < 40; i++){
        const vector<ldouble> mutado = mutacion_puntual(base);
        const int d = distancia_hamming(mutado, base);
        if (d < minimo) minimo = d;
        if (d > maximo) maximo = d;
        if (mutado.size() != base.size()) longitudes_ok = false;
    }
    check_bool("mutacion puntual aleatoria: siempre conserva la longitud",
               longitudes_ok);
    check_int("mutacion puntual aleatoria: el minimo de cambios es 1", minimo, 1);
    check_int("mutacion puntual aleatoria: el maximo de cambios es 2", maximo, 2);

    // Un vector de un solo token no puede elegir segunda posicion: cambia
    // exactamente 1 (el clamp evita el rand() % 0).
    const vector<ldouble> un_token = {TOKEN_X};
    const vector<ldouble> mutado_uno = mutacion_puntual(un_token);
    check_int("mutacion puntual aleatoria con 1 token: exactamente 1 cambio",
              distancia_hamming(mutado_uno, un_token), 1);
}

static void test_reinicio_adaptativo_inyecta_el_record_sin_cupo() {
    srand(20261021);

    // Poblacion de 2 con fraccion 0.4: int(0.4*2) = 0 lugares de reemplazo.
    // El record ausente necesita uno, asi que desplaza al peor (k=2, indice 0)
    // y esa sustitucion cuenta como reemplazo: el log informara replaced=1,
    // kept=1 y ningun sobreviviente cambia de vector.
    vector<FunbObj> poblacion = poblacion_fixture({2, 1});
    const vector<vector<ldouble>> antes = copia_vectores(poblacion);
    const vector<ldouble> record = {static_cast<ldouble>(TOKEN_ADD),
                                    static_cast<ldouble>(TOKEN_CONST1),
                                    static_cast<ldouble>(TOKEN_X)};

    const int reemplazados =
        reiniciar_poblacion_adaptativo(poblacion, record, {0.4L, 0.5L, 3, 0});

    check_int("sin cupo de reemplazo, el record desplaza al peor (1)",
              reemplazados, 1);
    check_bool("el unico sobreviviente (k=1, indice 1) queda intacto",
               poblacion[1].vecvalue() == antes[1]);
    check_bool("el record ausente entra igual a la poblacion",
               poblacion[0].vecvalue() == record);
}

int main() {
    std::cout << "== test_population: issues #11 y #28 (semillero y configuracion) ==\n";
    test_createPopulation_tamano_uniforme();
    test_createPopulation_cantidad_exacta();
    test_createPopulation_respeta_la_dimension();
    test_warm_start_opt_in();
    test_catalogo_semillas_por_caso();
    test_genericos_compartidos_entre_casos();
    test_warm_start_inyecta_el_catalogo_del_caso();
    test_cuota_semillas();
    test_warm_start_cuota_del_5_por_ciento();
    test_createPopulation_reproducible();
    test_config_defaults();
    test_config_overrides();
    test_config_invalida_usa_respaldo();
    test_warm_start_cualquier_dimension();
    test_reinicio_reemplaza_el_80_por_ciento();
    test_reinicio_restaura_el_record();
    test_reinicio_sin_record_no_toca_el_indice_reservado();
    test_reinicio_adaptativo_conserva_los_mejores();
    test_reinicio_adaptativo_inyecta_el_record();
    test_reinicio_adaptativo_compone_reemplazos_hibridos();
    test_parte_entera_trunca_hacia_abajo();
    test_mutacion_puntual_cambia_uno_o_dos_tokens();
    test_reinicio_adaptativo_inyecta_el_record_sin_cupo();
    std::cout << (g_fallos == 0 ? "OK" : "FALLARON") << ": "
              << (g_checks - g_fallos) << "/" << g_checks << " asserts\n";
    return g_fallos == 0 ? 0 : 1;  // como el exit code de pytest
}
