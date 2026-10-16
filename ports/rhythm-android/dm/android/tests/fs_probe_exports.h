/* Test-only discovery table. This game deliberately prunes unused HLE
 * registrations, while the filesystem unit probe also exercises those local
 * implementations. Expose them only in this CPU fixture, never in the host. */
#include "../../host/hle.h"
#undef HLE_LOCAL
#define HLE_LOCAL HLE_FORCE
