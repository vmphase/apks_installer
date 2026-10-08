#ifndef INSTALL_H
#define INSTALL_H

#include "apks.h"

/*
 * Installs every embedded APK of `f` on the device.
 * (serial == NULL means "the only connected device").
 */
int install_apks(ApksFile *f, const char *serial);

#endif
