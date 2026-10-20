/* Host device identity only; production SDK is not shadowed. */
#pragma once
struct device { int id; const void *config; };
const struct device *device_get_binding(const char *name);
