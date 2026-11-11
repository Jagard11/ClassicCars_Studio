#ifndef STUDIO_RUNTIME_VERSIONS_H
#define STUDIO_RUNTIME_VERSIONS_H

#include "json.h"

/* JSON array of libraries this process actually loads, each with the version
 * string that library reports. A library that fails to report a version is
 * included with an "error" field. Returns NULL only when allocation fails. */
JsonValue *studio_runtime_versions(void);

#endif
