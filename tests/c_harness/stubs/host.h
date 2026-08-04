/* Host-harness stub for src/smk/host.h — captures sends instead of
 * driving USB. The real smk/report.c calls host_keyboard_send() and
 * host_nkro_send() (the same names it calls on target). */
#ifndef HARNESS_HOST_H
#define HARNESS_HOST_H

#include "report.h"

#include <stdint.h>

void host_keyboard_send(__xdata report_keyboard_t *report);
void host_nkro_send(__xdata report_nkro_t *report);
void host_system_send(uint16_t usage);
void host_consumer_send(uint16_t usage);

#endif
