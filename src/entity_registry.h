#ifndef CC2X_ENTITY_REGISTRY_INTERNAL_H
#define CC2X_ENTITY_REGISTRY_INTERNAL_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal descriptor registry only. It is not an engine object registry. */
enum { CC2X_ENTITY_TYPE_ID_CAP = 97, CC2X_ENTITY_OWNER_CAP = 97, CC2X_ENTITY_LABEL_CAP = 97, CC2X_ENTITY_TYPE_CAP = 256 };
typedef struct {
    char owner[CC2X_ENTITY_OWNER_CAP];
    char id[CC2X_ENTITY_TYPE_ID_CAP];
    char base[CC2X_ENTITY_LABEL_CAP];
    char definition[CC2X_ENTITY_LABEL_CAP];
} cc2x_entity_type;
typedef enum {
    CC2X_ENTITY_TYPE_OK = 0,
    CC2X_ENTITY_TYPE_INVALID = 1,
    CC2X_ENTITY_TYPE_DUPLICATE = 2,
    CC2X_ENTITY_TYPE_FULL = 3,
    CC2X_ENTITY_TYPE_NOT_OWNER = 4
} cc2x_entity_type_result;

void cc2x_entity_registry_init(void);
cc2x_entity_type_result cc2x_entity_type_register(const char *owner, const char *id, const char *base, const char *definition);
int cc2x_entity_type_count(void);
int cc2x_entity_type_get(int index, cc2x_entity_type *out);
int cc2x_entity_type_remove(const char *owner, const char *id);
int cc2x_entity_type_remove_owner(const char *owner);

#ifdef __cplusplus
}
#endif

#endif
