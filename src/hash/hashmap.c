#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <cds/hash/hashmap.h>

/*
 * Fator de carga máximo: 75%
 *
 * Quando size / capacity > 0.75, a tabela é redimensionada.
 */
#define HASHMAP_LOAD_NUMERATOR 3
#define HASHMAP_LOAD_DENOMINATOR 4

/*
 * Duplica uma string.
 */
static char *hashmap_strdup(const char *source) {
    /*
     * Checagem defensiva: nunca desreferenciar um ponteiro NULL com strlen().
     */
    if (source == NULL) {
        return NULL;
    }

    /*
     * strlen() NÃO conta o '\0' final. Precisamos considerar isso separadamente abaixo.
     */
    const size_t length = strlen(source);

    /*
     * Protege contra overflow de inteiro: se length fosse SIZE_MAX, "length + 1" abaixo daria a volta
     * (overflow) e viraria 0, e malloc(0) poderia ter sucesso silenciosamente com um buffer de zero bytes,
     * causando um buffer overflow na chamada de memcpy() logo em seguida.
     */
    if (length == SIZE_MAX) {
        return NULL;
    }

    /*
     * "length + 1" reserva um byte extra para o terminador '\0' que strlen() não contou.
     */
    char *copy = malloc(length + 1);

    /*
     * malloc() pode falhar (ex: memória do sistema esgotada); sempre verificar antes de usar.
     */
    if (copy == NULL) {
        return NULL;
    }

    /*
     * Copia exatamente "length + 1" bytes: o conteúdo da string mais seu terminador '\0'. Usar o comprimento
     * já conhecido evita que memcpy() tenha que escanear a string de novo procurando o terminador,
     * diferente do que strcpy() faria.
     */
    memcpy(copy, source, length + 1);

    /*
     * Retorna a nova cópia, independente. Quem chamou agora é dono dessa memória e é responsável por
     * eventualmente liberá-la.
     */
    return copy;
}

/*
 * Função de hash DJB2.
 *
 * Converte uma string em um número.
 */
static unsigned long hashmap_hash(const char *key) {
    /*
     * Checagem defensiva: desreferenciar NULL abaixo causaria um crash. Todo chamador neste arquivo já
     * verifica NULL antes de chamar essa função, mas proteger aqui também evita depender dessa disciplina
     * sendo seguida em todo lugar, para sempre.
     */
    if (key == NULL) {
        return 0;
    }

    /*
     * Semente inicial, escolhida empiricamente por Daniel J. Bernstein (o "DJB" de DJB2) depois de testar
     * várias sementes em busca de boa distribuição. Não existe uma prova matemática mais profunda por trás
     * desse número exato; é um valor que funcionou bem na prática e se tornou o padrão de fato desse algoritmo.
     */
    unsigned long hash = 5381;

    /*
     * Percorre a string um caractere de cada vez até o terminador nulo, exatamente como qualquer
     * percorrimento de array/ponteiro em C.
     */
    while (*key != '\0') {
        /*
         * "char" pode ser com sinal ou sem sinal, dependendo da plataforma. Converter para "unsigned char"
         * garante que o valor sempre fique no intervalo 0-255, então o hash fica consistente entre
         * compiladores/plataformas diferentes, independente do sinal de char ali.
         */
        const unsigned char character = (unsigned char) *key;

        /*
         * O passo central do DJB2. "hash << 5" é "hash * 32" (deslocar bits à esquerda por 5 posições
         * multiplica por 2^5). Somar "hash" mais uma vez torna a expressão inteira equivalente a:
         *
         *     hash = hash * 33 + character;
         *
         * 33 é ímpar (coprimo com potências de 2), o que ajuda a espalhar bem os bits quando esse resultado
         * é depois reduzido com "% capacity" (capacity tipicamente é uma potência de 2). A forma com
         * deslocamento é como o algoritmo foi originalmente publicado; compiladores modernos otimizam
         * "hash * 33" para as mesmas instruções de qualquer jeito.
         *
         * "hash" é sem sinal, então assim que ultrapassa o valor máximo do seu tipo, ele silenciosamente dá
         * a volta (comportamento definido em C, diferente de overflow com sinal). Essa volta faz parte do que
         * dá ao hash seu efeito de embaralhamento ao longo de muitos caracteres.
         */
        hash = (hash << 5) + hash + character;

        /*
         * Avança para o próximo caractere (aritmética de ponteiro: como key é "char *", "+1" avança
         * exatamente um byte).
         */
        key++;
    }

    /*
     * A mesma string de entrada sempre produz o mesmo hash (determinismo), mas strings diferentes tendem a
     * produzir números bem diferentes, que é o que uma boa função de hash precisa fornecer.
     */
    return hash;
}

/*
 * Calcula o índice do bucket para uma chave.
 */
static size_t hashmap_get_index(const HashMap *map, const char *key) {
    /*
     * Checagens defensivas:
     *   - map == NULL causaria um crash em map->capacity abaixo.
     *   - capacity == 0 causaria uma divisão por zero no operador "%", que é comportamento indefinido para
     *      inteiros em C (tipicamente crasha o programa com SIGFPE).
     *
     * hashmap_create() já garante que capacity nunca é 0, mas checar aqui também evita depender que esse
     * invariante continue valendo para sempre em todo lugar que essa função possa ser chamada.
     */
    if (map == NULL || map->capacity == 0) {
        return 0;
    }

    /*
     * hashmap_hash(key) retorna um número grande, essencialmente arbitrário (até o alcance de unsigned long).
     * O "% map->capacity" encaixa esse número dentro de um índice de array válido: sempre entre 0 e
     * (capacity - 1), não importa quão grande era o hash original.
     *
     * Essa lógica fica centralizada aqui, em vez de repetida em hashmap_get/put/contains/remove, então existe
     * um único lugar para mudar como os índices são calculados, caso isso um dia precise mudar.
     */
    return hashmap_hash(key) % map->capacity;
}

/*
 * Cria uma HashMap.
 */
HashMap *hashmap_create(size_t initial_capacity) {
    /*
     * Capacidade padrão.
     *
     * Também evita que "capacity == 0" chegue até hashmap_get_index(), onde "hash % capacity" seria uma
     * divisão por zero (comportamento indefinido em C, tipicamente crashando com SIGFPE).
     */
    if (initial_capacity == 0) {
        initial_capacity = 16;
    }

    /*
     * Aloca espaço para a própria struct HashMap (o "bloco de controle": o ponteiro buckets, capacity e os
     * campos size) — não para nenhuma das entradas que serão inseridas depois, essas são alocadas
     * separadamente, uma de cada vez, dentro de hashmap_put().
     */
    HashMap *map = malloc(sizeof(HashMap));

    /*
     * malloc() pode falhar (ex: sistema sem memória). Sempre verificar antes de desreferenciar o ponteiro
     * retornado.
     */
    if (map == NULL) {
        return NULL;
    }

    /*
     * Aloca o array de buckets: "initial_capacity" posições, cada uma do tamanho de um único ponteiro
     * ("HashMapEntry *"), não do tamanho de uma entrada completa. Isso é propositalmente barato: os buckets
     * só precisam guardar o ENDEREÇO da sua primeira entrada (ou NULL), as entradas em si são alocadas sob
     * demanda conforme chaves são inseridas.
     *
     * calloc() é usado em vez de malloc() porque ele zera a memória. Isso é essencial aqui: todo bucket
     * precisa começar como NULL, significando "vazio". Com malloc(), cada posição conteria lixo indeterminado,
     * e a primeira busca/inserção tentaria seguir um ponteiro de lixo como se fosse uma entrada de verdade.
     */
    map->buckets = calloc(initial_capacity, sizeof(HashMapEntry *));

    /*
     * calloc() também pode falhar. Se falhar, a struct HashMap alocada acima ainda está lá na memória e
     * precisa ser liberada antes de desistir — senão seria um vazamento de memória (memória que está alocada
     * mas que nada nunca mais vai conseguir alcançar e liberar, já que "map" está prestes a ser perdido quando
     * essa função retornar NULL).
     */
    if (map->buckets == NULL) {
        free(map);
        return NULL;
    }

    /*
     * Registra quantos buckets existem, necessário depois por hashmap_get_index() para encaixar um hash em
     * um índice de array válido.
     */
    map->capacity = initial_capacity;

    /*
     * Nenhuma entrada foi inserida ainda.
     */
    map->size = 0;

    /*
     * Devolve uma HashMap totalmente inicializada e pronta para uso.
     */
    return map;
}

/*
 * Insere ou atualiza um elemento.
 */
int hashmap_put(HashMap *map, const char *key, void *value) {
    /*
     * Checagem defensiva: map e key são desreferenciados abaixo, então ambos precisam ser válidos. "value" é
     * propositalmente NÃO verificado: NULL é um valor legítimo para guardar (ex: "essa chave existe mas ainda
     * não tem dado associado").
     */
    if (map == NULL || key == NULL) {
        return -1;
    }

    /*
     * Encontra a qual bucket essa chave pertence, e pega a cabeça da lista ligada daquele bucket (NULL se o
     * bucket estiver vazio).
     */
    const size_t index = hashmap_get_index(map, key);
    HashMapEntry *entry = map->buckets[index];

    /*
     * Primeira passada: percorre a cadeia do bucket procurando essa chave exata. O índice só restringe EM QUAL
     * bucket procurar; múltiplas chaves diferentes podem compartilhar o mesmo bucket (colisão), então ainda
     * precisamos comparar as strings de verdade para achar um match real.
     */
    while (entry != NULL) {
        if (strcmp(entry->key, key) == 0) {
            /*
             * A chave já existe: só substitui o valor e para. Nenhuma memória nova é alocada, nenhuma entrada
             * duplicada é criada. Retorna 0, seguindo a convenção documentada dessa função
             * ("chave existente atualizada").
             */
            entry->value = value;
            return 0;
        }
        entry = entry->next;
    }

    /*
     * O loop terminou sem encontrar a chave: ela ainda não existe, então uma entrada nova precisa ser criada
     * e encadeada.
     */
    HashMapEntry *new_entry = malloc(sizeof(HashMapEntry));

    if (new_entry == NULL) {
        return -1;
    }

    /*
     * A HashMap faz sua própria cópia da chave (veja hashmap_strdup). Isso é essencial: "key" pode ser um
     * ponteiro para uma variável local de quem chamou, que deixa de ser válida assim que essa função retornar.
     * Sem copiar, entry->key se tornaria um ponteiro pendurado (dangling pointer).
     */
    char *key_copy = hashmap_strdup(key);

    if (key_copy == NULL) {
        /*
         * key_copy falhou, mas new_entry já tinha sido alocado acima. Precisa ser liberado aqui antes de
         * desistir, ou seria um vazamento: memória que está alocada mas que nada nunca vai conseguir alcançar
         * e liberar.
         */
        free(new_entry);
        return -1;
    }

    new_entry->key = key_copy;

    /*
     * Só o ponteiro é guardado, nunca uma cópia do que ele aponta. A HashMap não assume posse de "value" —
     * quem chamou continua responsável pelo tempo de vida dele (veja a documentação de hashmap_remove e
     * hashmap_destroy).
     */
    new_entry->value = value;

    /*
     * O clássico "inserir na cabeça" de lista ligada: a nova entrada aponta para o que era a primeira entrada
     * do bucket (ou NULL, se o bucket estava vazio), e então se torna a nova primeira entrada. Isso é O(1):
     * não precisa percorrer até o fim para inserir.
     */
    new_entry->next = map->buckets[index];
    map->buckets[index] = new_entry;

    /*
     * Mais um elemento agora existe em toda a tabela.
     */
    map->size++;

    /*
     * Seguindo a convenção documentada dessa função: 1 = nova chave inserida.
     */
    return 1;
}

/*
 * Busca um valor.
 */
void *hashmap_get(const HashMap *map, const char *key) {
    /*
     * Checagem defensiva: map e key são desreferenciados abaixo, nenhum dos dois pode ser NULL.
     */
    if (map == NULL || key == NULL) {
        return NULL;
    }

    /*
     * Encontra em qual bucket essa chave estaria, e pega a cabeça da lista ligada daquele bucket (NULL se o
     * bucket estiver vazio).
     */
    const size_t index = hashmap_get_index(map, key);
    const HashMapEntry *entry = map->buckets[index];

    /*
     * Percorre a cadeia de entradas desse bucket. O índice só restringe EM QUAL bucket procurar; outras
     * chaves podem ter colidido no mesmo bucket, então a chave de cada entrada ainda precisa ser comparada
     * para achar a certa.
     */
    while (entry != NULL) {
        if (strcmp(entry->key, key) == 0) {
            /*
             * Encontrou: retorna o valor guardado diretamente. Esse é só o ponteiro que foi originalmente
             * passado para hashmap_put — nenhuma cópia é feita ou retornada aqui.
             */
            return entry->value;
        }

        /*
         * Não é um match, avança para a próxima entrada da cadeia desse bucket (veja a explicação de
         * "deslocamento" de antes: nenhum dado se move, entry só passa a apontar para o próximo nó).
         */
        entry = entry->next;
    }

    /*
     * Percorreu a cadeia inteira (ou o bucket já estava vazio desde o início) sem achar um match: a chave não
     * existe.
     */
    return NULL;
}

/*
 * Verifica se uma chave existe.
 */
int hashmap_contains(const HashMap *map, const char *key) {
    /*
     * Checagem defensiva: map e key são desreferenciados abaixo, nenhum dos dois pode ser NULL. Retorna 0
     * ("não existe") em vez de algum código de erro, já que o tipo de retorno dessa função só tem espaço para
     * um resultado estilo booleano — não existe aqui um equivalente ao "-1 para erro" de hashmap_put.
     */
    if (map == NULL || key == NULL) {
        return 0;
    }

    /*
     * Mesma mecânica de busca de hashmap_get(): encontra o bucket, depois pega a cabeça da lista ligada
     * daquele bucket.
     */
    const size_t index = hashmap_get_index(map, key);
    const HashMapEntry *entry = map->buckets[index];

    /*
     * Percorre a cadeia comparando chaves, exatamente como hashmap_get() faz — exceto que essa função só
     * precisa saber QUE um match existe, não recuperar seu valor.
     */
    while (entry != NULL) {
        if (strcmp(entry->key, key) == 0) {
            return 1;
        }
        entry = entry->next;
    }

    /*
     * Cadeia esgotada (ou o bucket estava vazio) sem nenhum match.
     */
    return 0;
}

/*
 * Remove uma entrada.
 */
void *hashmap_remove(HashMap *map, const char *key) {
    /*
     * Checagem defensiva: ambos são desreferenciados abaixo.
     */
    if (map == NULL || key == NULL) {
        return NULL;
    }

    /*
     * Encontra o bucket, e pega sua primeira entrada.
     */
    const size_t index = hashmap_get_index(map, key);
    HashMapEntry *entry = map->buckets[index];

    /*
     * Diferente de hashmap_get()/hashmap_contains(), a remoção precisa lembrar do nó ANTERIOR enquanto
     * percorre a cadeia. Deletar um nó de uma lista ligada simples significa que o nó antes dele precisa ser
     * "religado" para pulá-lo — e a única forma de alcançar esse nó anterior depois é ter guardado um
     * ponteiro para ele ao longo do caminho. Começa como NULL, significando "ainda não vi nenhum nó anterior"
     * (ou seja, entry ainda é a cabeça do bucket).
     */
    HashMapEntry *previous = NULL;

    while (entry != NULL) {
        if (strcmp(entry->key, key) == 0) {
            /*
             * Encontrou a entrada a remover. Dois casos, dependendo se é a primeira entrada do bucket ou não:
             */
            if (previous == NULL) {
                /*
                 * É a primeira entrada: o próprio bucket precisa ser atualizado para pulá-la, apontando
                 * diretamente para o que vinha depois dela (possivelmente NULL, se era a única entrada
                 * desse bucket).
                 */
                map->buckets[index] = entry->next;
            } else {
                /*
                 * Está em algum lugar no meio (ou no fim): o "next" do nó anterior é o que precisa ser
                 * religado, não o bucket em si.
                 */
                previous->next = entry->next;
            }

            /*
             * Salva o valor ANTES de liberar a entrada. Essa ordem é essencial: uma vez que entry é liberado
             * abaixo, ler entry->value seria um use-after-free — a memória pode já estar reutilizada ou
             * inválida nesse ponto.
             */
            void *value = entry->value;

            /*
             * Libera a própria cópia da chave que a entrada tinha (alocada lá em hashmap_put via
             * hashmap_strdup), depois o próprio nó da entrada. O VALUE deliberadamente não é liberado aqui —
             * veja a documentação da própria função: a HashMap nunca foi dona dele, só o referenciava.
             */
            free(entry->key);
            free(entry);

            /*
             * Um elemento a menos em toda a tabela.
             */
            map->size--;

            /*
             * Devolve o valor removido para quem chamou, que agora é o único dono restante dele.
             */
            return value;
        }

        /*
         * Não é um match: antes de avançar, lembra esse nó como "previous" para a próxima iteração, depois
         * avança para o próximo nó da cadeia.
         */
        previous = entry;
        entry = entry->next;
    }

    /*
     * Percorreu a cadeia inteira sem encontrar a chave: nada para remover.
     */
    return NULL;
}

/*
 * Retorna a quantidade de elementos.
 */
size_t hashmap_size(const HashMap *map) {
    /*
     * Checagem defensiva: map->size causaria um crash em uma map NULL.
     */
    if (map == NULL) {
        return 0;
    }

    /*
     * O(1): só lê o contador mantido atualizado por hashmap_put() e hashmap_remove(), sem precisar percorrer
     * nada.
     */
    return map->size;
}

/*
 * Libera a HashMap.
 */
void hashmap_destroy(HashMap *map)
{
    /*
     * Checagem defensiva: map->capacity abaixo causaria um crash em NULL.
     */
    if (map == NULL) {
        return;
    }

    /*
     * Visita cada bucket, liberando a lista ligada dentro de cada um.
     */
    for (size_t i = 0; i < map->capacity; i++) {
        HashMapEntry *entry = map->buckets[i];

        while (entry != NULL) {
            /*
             * Salva "next" antes de liberar entry — ler isso depois de free(entry) seria use-after-free.
             */
            HashMapEntry *next = entry->next;

            free(entry->key);
            free(entry);

            entry = next;
        }
    }

    /*
     * Libera o próprio array de buckets, depois a struct. Valores nunca são liberados aqui — a HashMap nunca
     * foi dona deles.
     */
    free(map->buckets);
    free(map);
}