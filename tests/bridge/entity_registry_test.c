#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "entity_registry.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); return 1; } } while(0)

int main(void) {
    cc2x_entity_type entry;
    cc2x_entity_registry_init();
    CHECK(cc2x_entity_type_count() == 0);
    CHECK(cc2x_entity_type_register("mod_a", "example:drone", "vehicle", "example_drone") == CC2X_ENTITY_TYPE_OK);
    CHECK(cc2x_entity_type_register("mod_b", "example:drone", "vehicle", "other") == CC2X_ENTITY_TYPE_DUPLICATE);
    CHECK(cc2x_entity_type_register("mod_a", "not-namespaced", "vehicle", "example_drone") == CC2X_ENTITY_TYPE_INVALID);
    CHECK(cc2x_entity_type_count() == 1);
    CHECK(cc2x_entity_type_get(0, &entry));
    CHECK(!strcmp(entry.owner, "mod_a") && !strcmp(entry.id, "example:drone"));
    CHECK(!cc2x_entity_type_remove("mod_b", "example:drone"));
    CHECK(cc2x_entity_type_remove("mod_a", "example:drone"));
    CHECK(cc2x_entity_type_count() == 0);
    for (int i = 0; i < CC2X_ENTITY_TYPE_CAP; ++i) {
        char id[CC2X_ENTITY_TYPE_ID_CAP];
        snprintf(id, sizeof(id), "batch:type_%03d", i);
        CHECK(cc2x_entity_type_register("batch_mod", id, "vehicle", "stock_vehicle") == CC2X_ENTITY_TYPE_OK);
    }
    CHECK(cc2x_entity_type_count() == CC2X_ENTITY_TYPE_CAP);
    CHECK(cc2x_entity_type_register("batch_mod", "batch:overflow", "vehicle", "stock_vehicle") == CC2X_ENTITY_TYPE_FULL);
    CHECK(cc2x_entity_type_remove_owner("batch_mod") == CC2X_ENTITY_TYPE_CAP);
    CHECK(cc2x_entity_type_count() == 0);
    puts("PASS internal logic type registry: validation, global conflict, ownership removal, fixed capacity; no engine object created");
    return 0;
}
