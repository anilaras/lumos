#ifndef LUMOS_BRIGHTNESS_H
#define LUMOS_BRIGHTNESS_H

/* Call before starting other threads. Returns the number of usable outputs. */
int brightness_init(const char *backlight_dir, int verbose);
/* Apply one percentage to all outputs, with a tolerance in percentage points. */
void brightness_apply(double percent, double tolerance);

#endif
