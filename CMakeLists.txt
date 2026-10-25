# CMakeLists.txt del prototipo ODE (issue #48: layout estandar de C++).
#
# Para un desarrollador Python: este archivo es el equivalente CMake del
# pyproject.toml y el unico punto de entrada del build. Los fuentes viven en
# src/ y los tests en tests/; el Makefile de la raiz quedo obsoleto con la
# reorganizacion (su retiro es el issue #53). Los comandos son:
#
#   cmake -B build            # configurar       (como leer pyproject.toml)
#   cmake --build build       # compilar         (como `python -m build`)
#   ctest --test-dir build    # correr los tests (como `pytest`)
#
# El ejecutable se llama build/ode-solver (antes main.exe).

cmake_minimum_required(VERSION 3.16)

project(ODE CXX)

# Mismos flags que el Makefile: CXXFLAGS := -std=c++17 -Wall -O3.
# CMAKE_CXX_EXTENSIONS OFF pide -std=c++17 estricto (no -std=gnu++17), igual
# que la variable CXXFLAGS del Makefile.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -O3)

# ---------------------------------------------------------------------------
# Ejecutable principal: los mismos fuentes que $(SRCS) del Makefile, ahora en
# src/. El objetivo se llama ode-solver (issue #48) y en Linux CMake no anade
# sufijo, asi que el binario queda en build/ode-solver.
# parse_tree_bckp.cpp queda fuera a proposito: ahora vive en docs/legacy/ y
# tambien define main(); enlazarlo daria un simbolo duplicado.
# ---------------------------------------------------------------------------
add_executable(ode-solver
    src/dualfunctions.cpp
    src/funbobj.cpp
    src/semilla.cpp
    src/salida.cpp
    src/parse_tree.cpp
)

# ---------------------------------------------------------------------------
# Tests: los mismos 9 binarios de `make test`, ahora en tests/. Cada test
# define su propio main() y devuelve codigo distinto de cero si falla algun
# assert (el equivalente al exit code de pytest). Todos necesitan src/ en el
# include path porque los headers (dualfunctions.h, semilla.h, token.h) ya no
# resuelven junto al archivo que los incluye.
# ---------------------------------------------------------------------------

enable_testing()

# test_dual (issue #2): numeros duales; enlaza solo dualfunctions.cpp.
add_executable(test_dual src/dualfunctions.cpp tests/test_dual.cpp)
target_include_directories(test_dual PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_dual COMMAND test_dual)

# test_semilla (issue #4): resolucion de ODE_SEED; solo semilla.cpp.
add_executable(test_semilla src/semilla.cpp tests/test_semilla.cpp)
target_include_directories(test_semilla PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_semilla COMMAND test_semilla)

# test_salida (issue #112): resolucion de ODE_LOG/ODE_LOG_FILE; solo salida.cpp.
add_executable(test_salida src/salida.cpp tests/test_salida.cpp)
target_include_directories(test_salida PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_salida COMMAND test_salida)

# test_mutation (issue #5), test_crossover (issue #6) y test_token (issue #31)
# enlazan parse_tree.cpp (ahi viven Mutation y Recombination) y, con el, sus
# dependencias, entre ellas funbobj.cpp (FunbObj y x_rango, issue #157).
# -DODE_NO_MAIN descarta el main() del programa para que el unico main() de
# cada binario sea el del test (misma tecnica que el Makefile).
foreach(nombre IN ITEMS test_mutation test_crossover test_token)
    add_executable(${nombre}
        src/semilla.cpp
        src/dualfunctions.cpp
        src/funbobj.cpp
        src/parse_tree.cpp
        tests/${nombre}.cpp
    )
    target_compile_definitions(${nombre} PRIVATE ODE_NO_MAIN)
    target_include_directories(${nombre} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME ${nombre} COMMAND ${nombre})
endforeach()

# test_funbobj_const (issue #42), test_grid (issue #7), test_population
# (issue #11), test_restart_escalation (issues #127/#128),
# test_diversity_trigger (issue #129), test_restart_lifecycle (issue #138) y
# el resto incluyen parse_tree.cpp desde su propio .cpp (el bucle DE y el
# reinicio); FunbObj y x_rango viven ahora en funbobj.cpp (issue #157), que se
# enlaza aparte. parse_tree.cpp NO se compila aparte o habria simbolos
# duplicados. Necesitan src/ en el include path (asi resuelven tanto el .cpp
# incluido como sus headers) y -DODE_NO_MAIN para que gane el main() del test.
foreach(nombre IN ITEMS test_funbobj_const test_grid test_population test_ode_cases test_per_case_config test_restart_escalation test_diversity_trigger test_restart_lifecycle)
    add_executable(${nombre}
        src/semilla.cpp
        src/dualfunctions.cpp
        src/funbobj.cpp
        tests/${nombre}.cpp
    )
    target_compile_definitions(${nombre} PRIVATE ODE_NO_MAIN)
    target_include_directories(${nombre} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME ${nombre} COMMAND ${nombre})
endforeach()

# test_header_hygiene (issue #39): test de tiempo de compilacion; si el
# binario compila, el test pasa. Incluye dualfunctions.h, que ahora vive en
# src/.
add_executable(test_header_hygiene tests/test_header_hygiene.cpp)
target_include_directories(test_header_hygiene PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_test(NAME test_header_hygiene COMMAND test_header_hygiene)
