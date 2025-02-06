#ifndef SHARED_H
#define SHARED_H

#include <stdbool.h>

// Globale Variable
extern bool commissioned;

// Funktionen
void save_commissioned_status(bool status);
bool load_commissioned_status();

#endif // SHARED_H

