/* C ABI of the FlowGrid core, consumed by the DTK (C++) UI.
 * Keep in sync with deepin/native/src/lib.rs. */
#ifndef FLOWGRID_CORE_CAPI_H
#define FLOWGRID_CORE_CAPI_H

#ifdef __cplusplus
extern "C" {
#endif

int   fg_core_start(const char *config_dir);
void  fg_core_stop(void);

/* Returns a JSON array string; release with fg_core_free_string. */
char *fg_core_poll_events(void);

char *fg_core_get_devices(void);          /* [{"id","name","platform","meta","latency","connected","transport"}] */
char *fg_core_get_latency(void);          /* "2.4ms" or "--" */
char *fg_core_get_connection_state(void); /* "Idle" / "Connected to <name>" */
char *fg_core_get_keymap_rules(void);     /* [{"from_key","to_key","context"}] */
char *fg_core_get_settings(void);         /* {"keyMapping":true,...} */

int   fg_core_set_setting(const char *key, int value);

void  fg_core_scan(void);
void  fg_core_connect(const char *device_id);
void  fg_core_disconnect(const char *device_id);
void  fg_core_remove_device(const char *device_id);

/* Host-side input capture (Synergy-style control of the remote device). */
int   fg_core_set_capturing(int enabled);
int   fg_core_get_capturing(void);

void  fg_core_free_string(char *s);

#ifdef __cplusplus
}
#endif

#endif /* FLOWGRID_CORE_CAPI_H */
