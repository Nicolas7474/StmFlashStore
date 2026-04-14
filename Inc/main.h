
#include <stdint.h>


typedef enum {
    EE_PAGE_0 = 0,
    EE_PAGE_1 = 1
} ee_page_t;

typedef enum {
    EE_SPEED = 0x0001,
    EE_MODE  = 0x0002,
    EE_TEMP  = 0x0003
} ee_var_id_t;

/* ================= PUBLIC API ================= */

void ee_init(void); // Initialize EEPROM emulation

int ee_write(ee_var_id_t id, const void *data, uint16_t len); // Write variable

int ee_read(uint16_t id, void *out, uint16_t max_len); // Read variable

uint16_t ee_get_erase_count(ee_page_t page);

uint32_t ee_get_free_space(void);
