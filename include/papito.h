#ifndef PAPITO_H
#define PAPITO_H

#include <vector>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

// API simples usada pelo código principal
void papito_init();           // inicializa PAPI e carrega counters.in
void papito_start();          // começa a contagem
void papito_end();            // pára e imprime resultados (stdout)
void papito_finalize();       // final cleanup (opcional)

// Funções C++-style (apenas se incluir papito.h em C++ files)
#ifdef __cplusplus
}
#endif

#endif // PAPITO_H

