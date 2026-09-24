#include "CloakController.h"

// Header-only helpers; this TU keeps the build graph stable if we grow the API.
namespace cloak {
int cloakingApiVersion() { return 1; }
} // namespace cloak
