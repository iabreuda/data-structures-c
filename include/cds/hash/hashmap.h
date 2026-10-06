//
// Created by Igor on 29/07/2026.
//
#ifndef DATA_STRUCTURES_C_HASHMAP_H
#define DATA_STRUCTURES_C_HASHMAP_H

#include <stddef.h>

/*
 * Entrada interna da HashMap.
 *
 * Cada entrada guarda:
 *   - key:   a chave
 *   - value: o valor associado
 *   - next:  a próxima entrada do mesmo bucket
 */
typedef struct HashMapEntry {
    /*
     * Cópia própria da string da chave.
     * Um ponteiro porque strings em C são só endereços pro primeiro
     * caractere; isso permite chaves de qualquer tamanho, em vez de
     * um tamanho fixo.
     */
    char *key;

    /*
     * Ponteiro pro valor de quem chamou, de qualquer tipo.
     * "void *" é a forma de C ser genérico: como C não tem templates,
     * isso permite que a mesma HashMap guarde ints, structs, ou
     * qualquer outra coisa. A HashMap só guarda o endereço; ela nunca
     * é dona disso nem libera essa memória.
     */
    void *value;

    /*
     * Ponteiro pra próxima entrada do mesmo bucket (ou NULL se essa
     * for a última). Necessário pra tratamento de colisão: múltiplas
     * chaves que caem no mesmo bucket ficam encadeadas como uma
     * lista ligada.
     *
     * Atenção: precisa ser "struct HashMapEntry *", não
     * "HashMapEntry *". Nesse ponto o typedef ainda não terminou,
     * mas o nome da struct já é conhecido pelo compilador.
     *
     * Um ponteiro também é a única forma disso funcionar: embutir
     * "struct HashMapEntry next;" por valor faria a struct conter a
     * si mesma, um tamanho infinito que o compilador nunca
     * conseguiria resolver. Um ponteiro tem tamanho fixo independente
     * do que ele aponta, o que quebra essa recursão.
     */
    struct HashMapEntry *next;
} HashMapEntry;

/*
 * Estrutura principal da HashMap.
 */
typedef struct HashMap {
    /*
     * Ponteiro pro primeiro elemento de um array de cabeças de
     * bucket. Cada elemento é, ele mesmo, um "HashMapEntry *": a
     * cabeça da lista ligada daquele bucket, ou NULL se o bucket
     * estiver vazio.
     *
     * Essa indireção dupla ("HashMapEntry **") é o que permite que
     * cada bucket, de forma independente, esteja vazio ou guarde uma
     * cadeia de qualquer tamanho, sem dar a cada bucket o tamanho
     * fixo de uma entrada completa.
     */
    HashMapEntry **buckets;

    /*
     * Número de buckets no array acima (o tamanho fixo da tabela,
     * não o número de elementos guardados). Usado pra encaixar um
     * valor de hash num índice válido via "hash % capacity".
     */
    size_t capacity;

    /*
     * Número de pares chave-valor de fato guardados, somando todos
     * os buckets. Mantido como um contador incremental, pra que
     * hashmap_size() seja O(1) em vez de precisar percorrer cada
     * bucket e contar.
     */
    size_t size;
} HashMap;

/*
 * Cria uma nova HashMap.
 *
 * initial_capacity: número inicial de buckets.
 *
 * Retorna:
 *   - um ponteiro para a HashMap;
 *   - NULL se ocorrer um erro de alocação.
 */
HashMap *hashmap_create(size_t initial_capacity);

/*
 * Insere ou atualiza um elemento.
 *
 * Valores de retorno:
 *   1  = nova chave inserida;
 *   0  = chave existente atualizada;
 *   -1 = erro.
 */
int hashmap_put(HashMap *map, const char *key, void *value);

/*
 * Busca o valor associado a uma chave.
 *
 * Retorna:
 *   - um ponteiro para o valor;
 *   - NULL se a chave não existir.
 */
void *hashmap_get(const HashMap *map, const char *key);

/*
 * Verifica se uma chave existe.
 *
 * Retorna:
 *   1 = existe;
 *   0 = não existe.
 */
int hashmap_contains(const HashMap *map, const char *key);

/*
 * Remove uma entrada.
 *
 * Retorna:
 *   - o valor removido;
 *   - NULL se a chave não existir.
 *
 * A HashMap libera a chave e a entrada,
 * mas não libera o valor.
 */
void *hashmap_remove(HashMap *map, const char *key);

/*
 * Retorna a quantidade de elementos.
 */
size_t hashmap_size(const HashMap *map);

/*
 * Libera a HashMap inteira.
 *
 * Os valores não são liberados.
 */
void hashmap_destroy(HashMap *map);

#endif