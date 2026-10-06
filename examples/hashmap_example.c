#include <stdio.h>
#include <stdlib.h>

#include <cds/hash/hashmap.h>

int main(void)
{
    int age;

    HashMap *map = hashmap_create(16);

    if (map == NULL) {
        fprintf(stderr, "Could not create HashMap.\n");
        return EXIT_FAILURE;
    }

    age = 36;

    /*
     * Insert:
     *
     * "age" -> address of age
     */
    if (hashmap_put(map, "age", &age) == -1) {
        fprintf(stderr, "Could not insert value.\n");
        hashmap_destroy(map);
        return EXIT_FAILURE;
    }

    /*
     * Look up the value.
     */
    const int *result = hashmap_get(map, "age");

    if (result != NULL) {
        printf("Age: %d\n", *result);
    }

    printf("Size: %lu\n", (unsigned long) hashmap_size(map));

    hashmap_destroy(map);

    return EXIT_SUCCESS;
}