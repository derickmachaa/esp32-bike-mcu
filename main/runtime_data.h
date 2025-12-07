#ifndef RUNTIME_DATA_H
#define RUNTIME_DATA_H

extern char lastCommand;
extern bool BMCU_TAILLIGHT_ENABLE;

// This are for writing and reading permanent storage
extern int8_t storage_read_char(void); // this reads data for booting purposes
extern void storage_write_char(char mychar);

extern void send_brake_signal(void);
extern void send_normal_signal(void);
extern void send_off_signal(void);
extern void send_right_signal(void);
extern void send_left_signal(void);
#endif
