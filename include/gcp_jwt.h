#ifndef __GCP_JWT_H__
#define __GCP_JWT_H__

#include "stdint.h"
#include <stddef.h>

char *create_GCP_JWT(const char *projectId, const char *private_pem_key_start, size_t key_size);

#endif /* __JWT_H__ */
