/************************************************************************************
*	STM32F469 EEPROM Emulation Library 						 	Nicolas Prata 04/2026
*
*	A lightweight Flash Translation Layer for STM32F469 that abstracts internal Flash
* 	sectors into a persistent storage area for user data and configuration.
*************************************************************************************/

/*  Recommendations: in STM32F469NIHX_FLASH.ld modify the Flash size and add a new EEPROM space storage element.
    Example:
			FLASH  (rx) : ORIGIN = 0x8000000,  LENGTH = 1792K // (1792 = 2048 - 256)
		 //	EEPROM (rw) : ORIGIN = 0x081C0000, LENGTH = 256K  // optional, space is reserved just by shrinking FLASH

 	 	 And in main.c define the new sectors: PAGE0_ADDR, PAGE1_ADDR

 	************************************************************************************
 	Data Structure in a sector (page):

	[ PAGE HEADER ].[ HEADER ][ DATA ][ PADDING ].[ HEADER ][ DATA ][ PADDING ].[ HEADER ][ DATA ][ PADDING ].....[ FREE ]

	[ PAGE HEADER ] 	+0x00   2 byte   Page State (uint16_t) - Example: PAGE_VALID
						+0x02   2 byte   Unused / padding (0xFFFF)
	------------------------------------------------------------------------------------
	A page is a sequence of records. A record is structured as follows:

	[ HEADER ]  The first record in sector starts at addr = page + 4
	 	 	 	16 bytes total (see EE_Header struct)
				Example: DEADBEEF 0002 0002 11111111 0005 AAAA

	[ DATA ]	Payload for the id (EE_Header->id)

	[ PADDING ] Align to 4 bytes

	End of data detection: [ FREE ] 0xFFFFFFFF → end marker = empty flash
	------------------------------------------------------------------------------------

	Update behavior: when writing same ID: old record → remains, new record → appended
 	Index points to latest occurrence (ee_index[EE_MAX_ID]), older records with same id are ignored.

	When a sector is full, we switch to the other page, which is erased once the transfer of data is completed.
	The erase count is tracked within the record header to prevent excessive wear on specific flash sectors.
	The page header is restored immediately after the data have been moved.
	After transfer: new page: → only latest records (compacted, only one record per id)
					old page: → 100% erased

	Check the Public API section in main.h to discover the user functions.
	The ee_read() and ee_write() functions are designed for type-agnostic data handling.
	Data is processed as raw bytes → any data type can be passed as long as the correct memory size is specified (use sizeof() operator).
	Portability ⚠️ On STM32: little-endian.
 */


#include <stm32f469xx.h>
#include "main.h"
#include "myConfig.h"
#include "myTimers.h"
#include <stdint.h>
#include <string.h>
#include <stddef.h>

/* ================= CONFIG ================= */

#define PAGE0_ADDR  0x081C0000U   // Flash RAM - sector 22
#define PAGE1_ADDR  0x081E0000U   // Flash RAM - sector 23
#define PAGE_SIZE   0x20000U      // 128 KB

#define FLASH_SECTOR_22   26 	  // sectors 12–23 → bank 2 (offset encoding)
#define FLASH_SECTOR_23   27

#define PAGE_ERASED   0xFFFF
#define PAGE_RECEIVE  0xEEEE // only 1 → 0 works writing Flash
#define PAGE_VALID    0xCCCC

#define EE_MAGIC       0xDEADBEEFU 	// check validity of header
#define EE_ERASE_MAGIC 0xA5A5A5A5U

#define EE_STATE_EMPTY    0xFFFF 	// same as Flash erased value
#define EE_STATE_WRITING  0xBBBB
#define EE_STATE_COMMITTED  0xAAAA 	// easier to visualize on debugger than 0x0000

#define EE_MAX_ID   256   // direct mapping index

#define ERASE_LOG_OFFSET 0x10

/* ================= STRUCTURES ================= */

typedef struct __attribute__((packed))
{
	uint32_t magic;
	uint16_t id;
	uint16_t length;
	uint32_t timestamp;   // optional metadata
	uint16_t erase_count;   //
	uint16_t state;
} EE_Header;


/* ============= STATIC DECLARATIONS ============ */

/* low-level flash */
static void flash_wait(void);
static void flash_unlock(void);
static void flash_lock(void);
static void flash_erase_sector(uint32_t sector);
static void flash_write_halfword(uint32_t addr, uint16_t data);
static void flash_write_word(uint32_t addr, uint32_t data);
static void flash_write_block(uint32_t addr, const uint8_t *data, uint32_t len);


/* internal "EEPROM" logic */
static uint32_t ee_get_active_page(void);
static uint32_t ee_find_free(uint32_t page);
static int ee_is_header_valid(EE_Header *hdr);

static uint16_t ee_get_erase_count_internal(uint32_t page);
static void ee_build_index(void);
static void ee_write_to_page(uint32_t page, uint16_t id, const void *data, uint16_t len);
static void ee_page_transfer(void); // Trigger page transfer (when full)

/* =============== PV ================= */

static uint32_t ee_index[EE_MAX_ID]; // RAM index: id → flash addr
uint16_t ee_global_erase_count; // keep track of the number of erase

/* ================= FLASH LOW LEVEL ================= */

static void flash_wait(void)
{
	while (FLASH->SR & FLASH_SR_BSY);
}

static void flash_unlock(void)
{ 	/* After reset, write is not allowed in the flash control register (FLASH_CR)
	  to protect the flash	memory against possible unwanted operations */
	FLASH->KEYR = 0x45670123;
	FLASH->KEYR = 0xCDEF89AB;
}

static void flash_lock(void)
{
	FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_erase_sector(uint32_t sector)
{
	flash_wait();

	FLASH->SR |= FLASH_SR_PGSERR | FLASH_SR_PGPERR | FLASH_SR_PGAERR | FLASH_SR_WRPERR; // Clear previous errors
	FLASH->CR &= ~FLASH_CR_SNB;
	FLASH->CR |= (sector << FLASH_CR_SNB_Pos);
	FLASH->CR |= FLASH_CR_SER; // Sector Erase activated
	FLASH->CR |= FLASH_CR_STRT;

	flash_wait();

	FLASH->CR &= ~FLASH_CR_SER;
}

static void flash_write_halfword(uint32_t addr, uint16_t data)
{
	flash_wait();

	FLASH->CR &= ~FLASH_CR_PSIZE;
	FLASH->CR |= FLASH_CR_PSIZE_0;

	FLASH->CR |= FLASH_CR_PG;

	*(volatile uint16_t*)addr = data;

	flash_wait();

	FLASH->CR &= ~FLASH_CR_PG;
}

static void flash_write_word(uint32_t addr, uint32_t data)
{
	flash_wait();

	FLASH->CR &= ~FLASH_CR_PSIZE;
	FLASH->CR |= FLASH_CR_PSIZE_1;

	FLASH->CR |= FLASH_CR_PG;

	*(volatile uint32_t*)addr = data;

	flash_wait();

	FLASH->CR &= ~FLASH_CR_PG;
}

static void flash_write_block(uint32_t addr, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i += 2)
    {
    	uint16_t half;
    	// check the last pair of bytes (last iteration of i)
    	if (i + 1 < len)
    		half = data[i] | (data[i + 1] << 8); // normal, 2 bytes available (little-endian)
    	else
    		// if (i + 1 >= len : end of loop, no second byte → must pad
    		half = data[i] | 0xFF00;   // padding: odd length (no second byte, only 1 byte left)

    	// If len is odd → crash :  uint16_t half = data[i] | (data[i + 1] << 8);  // little endian
    	flash_write_halfword(addr + i, half);
    }
}

/* ================= PAGE ================= */

static uint32_t ee_get_active_page(void)
{
	uint16_t p0 = *(uint16_t*)PAGE0_ADDR;
	uint16_t p1 = *(uint16_t*)PAGE1_ADDR;

	if (p0 == PAGE_VALID) return PAGE0_ADDR;
	if (p1 == PAGE_VALID) return PAGE1_ADDR;

	// fallback - TODO weak logic to improve
	return PAGE0_ADDR;
}

/* ================= ERASE COUNT ================= */

static uint16_t ee_get_erase_count_internal(uint32_t page)
{
    uint32_t addr = page + 4;
    uint16_t last = 0;

    while (addr < page + PAGE_SIZE)
    {
        EE_Header *hdr = (EE_Header*)addr;

        // stop if empty / invalid
        if (hdr->magic != EE_MAGIC)
            break;

        // optional: check state
        if (hdr->state == EE_STATE_COMMITTED)
        {
            last = hdr->erase_count;
        }

        // move to next record
        uint32_t size = sizeof(EE_Header);

        // align data length to 4 bytes
        uint32_t data_len = (hdr->length + 3) & ~3;

        addr += size + data_len;
    }

    return last;
}

uint16_t ee_get_erase_count(ee_page_t page)
{
	uint32_t addr = (page == EE_PAGE_0) ? PAGE0_ADDR : PAGE1_ADDR;
    return ee_get_erase_count_internal(addr);
}



/* ================= HEADER VALIDATION ================= */

static int ee_is_header_valid(EE_Header *hdr)
{
	if (hdr->magic != EE_MAGIC)
		return 0;

	if (hdr->state != EE_STATE_COMMITTED &&
			hdr->state != EE_STATE_WRITING)
		return 0;

	if (hdr->length == 0 || hdr->length > 512)
		return 0;

	return 1;
}

/* ================= INDEX BUILD (SELF-REPAIR) ================= */

static void ee_build_index(void)
{
	uint32_t page = ee_get_active_page();
	uint32_t addr = page + 4;

	memset(ee_index, 0xFF, sizeof(ee_index));

	while (addr < page + PAGE_SIZE)
	{
	    if (addr + sizeof(EE_Header) > page + PAGE_SIZE)  // boundary check
	        break;

	    if (addr % 4 != 0)	 // alignment check
	    {
	        addr += 2;
	        continue;
	    }

	    EE_Header *hdr = (EE_Header*)addr;

	    if (hdr->magic == 0xFFFFFFFF)  // check erased first
	        break;

	    if (hdr->id >= EE_MAX_ID)
	        continue;

	    if (!ee_is_header_valid(hdr))
	    {
	        addr += 4;   // safer resync
	        continue;
	    }

	    if (hdr->state == EE_STATE_COMMITTED)
	    {
	        ee_index[hdr->id] = addr;
	    }

	    uint32_t data_len = (hdr->length + 3) & ~3;
	    addr += sizeof(EE_Header) + data_len;
	}
}

/* ================= FIND FREE ================= */

static uint32_t ee_find_free(uint32_t page)
{
	uint32_t addr = page + 4; // Start just after page header (first 4 bytes reserved for page state)

	while (addr < page + PAGE_SIZE)   // Scan the page until the end
	{
		EE_Header *hdr = (EE_Header*)addr;   // Interpret current address as a header

		if (hdr->magic == 0xFFFFFFFF)
			return addr;  // If magic is erased (0xFFFFFFFF), this is free space

		// Align data length to 4 bytes (padding for Flash alignment)
		// +3 → ensures we "overflow" into next block and & ~3 → clears last 2 bits → multiple of 4
		uint32_t data_len = (hdr->length + 3) & ~3; // round UP to next multiple of 4
		addr += sizeof(EE_Header) + data_len;  // Move to next record: header + aligned data
	}

	return 0;   // No space left in page !
}

/* =============== GET FREE SPACE =============== */

uint32_t ee_get_free_space(void)
{
	// Return the remaining contiguous space
    uint32_t page = ee_get_active_page();
    uint32_t free_addr = ee_find_free(page);

    if (free_addr == 0)
        return 0;

    return (page + PAGE_SIZE) - free_addr;
}

/* ================ WRITE TO PAGE ================ */

static void ee_write_to_page(uint32_t page, uint16_t id, const void *data, uint16_t len)
{
	// Function only valid when flash is unlocked, we are inside ee_page_transfer & target page is known and correct
	uint32_t addr = ee_find_free(page);

	EE_Header hdr = {
			.magic = EE_MAGIC,
			.id = id,
			.length = len,
			.timestamp = 0,
			.erase_count = ee_global_erase_count,
			.state = EE_STATE_WRITING
	};

	flash_write_block(addr, (uint8_t*)&hdr, sizeof(hdr));
	flash_write_block(addr + sizeof(hdr), data, len);

	uint16_t valid = EE_STATE_COMMITTED;
	flash_write_halfword(addr + offsetof(EE_Header, state), valid); // VALID is a bitwise subset of WRITING (1 → 0 is allowed)
}


/* ================= WRITE ================= */

int ee_write(ee_var_id_t id, const void *data, uint16_t len)
{
    flash_unlock();

    for (int attempt = 0; attempt < 2; attempt++)  // max 1 retry
    {
        uint32_t page = ee_get_active_page();
        uint32_t addr = ee_find_free(page);

        if (addr != 0)
        {
            // normal write
            EE_Header hdr = {
                .magic = EE_MAGIC,
                .id = id,
                .length = len,
                .timestamp = 0x11111111, // dummy time
				.erase_count = ee_global_erase_count,
                .state = EE_STATE_WRITING
            };

            flash_write_block(addr, (uint8_t*)&hdr, sizeof(hdr));
            flash_write_block(addr + sizeof(hdr), data, len);

            uint16_t valid = EE_STATE_COMMITTED;
            flash_write_halfword(addr + offsetof(EE_Header, state), valid);

            // update the id index with the address of the last updated value (the only meaningful one)
            ee_index[id] = addr;

            flash_lock();
            return 1; // success
        }

        // no space → perform page transfer once
        if (attempt == 0)
        {
            flash_lock();          // clean state before transfer
            ee_page_transfer();    // ⚠️ may take time
            flash_unlock();        // reopen for retry
        }
    }

    flash_lock();
    return 0; // still failed (very unlikely unless config broken)
}

/* ================= READ ================= */

int ee_read(uint16_t id, void *out, uint16_t max_len)
{
	uint32_t addr = ee_index[id];

	if (addr == 0xFFFFFFFF)
		return 0;

	EE_Header *hdr = (EE_Header*)addr;

	memcpy(out,
			(uint8_t*)(addr + sizeof(EE_Header)),
			(hdr->length < max_len) ? hdr->length : max_len);

	return 1;
}

/* ================= PAGE TRANSFER ================= */

static void ee_page_transfer(void)
{
	flash_unlock();

	uint32_t oldPage = ee_get_active_page();
	uint32_t newPage = (oldPage == PAGE0_ADDR) ? PAGE1_ADDR : PAGE0_ADDR;

	// mark new page
	flash_write_word(newPage, ((uint32_t)0xFFFF << 16) | PAGE_RECEIVE);

	ee_global_erase_count++; // not inside flash_erase_sector() to allow first commit to have the correct erase count

	// copy all indexed variables
	for (uint16_t id = 0; id < EE_MAX_ID; id++)
	{
		if (ee_index[id] != 0xFFFFFFFF)
		{
			EE_Header *old = (EE_Header*)ee_index[id];

			ee_write_to_page(newPage,
			                 id,
			                 (uint8_t*)(ee_index[id] + sizeof(EE_Header)), //data situad after the header
			                 old->length);
		}
	}

	// activate new page
	flash_write_word(newPage, ((uint32_t)0xFFFF << 16) | PAGE_VALID); // explicitly set the upper 16 bits (PAGE_VALID is only on 16bits)

	// erase old
	if (oldPage == PAGE0_ADDR)
		flash_erase_sector(FLASH_SECTOR_22);
	else
		flash_erase_sector(FLASH_SECTOR_23);

	flash_lock();

	ee_build_index();
}

/* ================= INIT ================= */

void ee_init(void)
{
	uint16_t p0 = *(uint16_t*)PAGE0_ADDR;
	uint16_t p1 = *(uint16_t*)PAGE1_ADDR;
	if (p0 != PAGE_VALID && p1 != PAGE_VALID)
	{
	    flash_unlock();

	    flash_erase_sector(FLASH_SECTOR_22);
	    flash_write_word(PAGE0_ADDR, ((uint32_t)0xFFFF << 16) | PAGE_VALID); // explicitly set the upper 16 bits (PAGE_VALID is only on 16bits)

	    flash_lock();
	}
	ee_build_index();

	ee_global_erase_count = ee_get_erase_count(ee_get_active_page());
}


uint16_t buf_mode[5]; // globals for Debugger visualization
uint16_t buf_speed[1];
uint16_t buf_temp[1];

int main (void) {

	SysClockConfig();
	GPIO_Config();
	InterruptGPIO_Config();
	SysTick_Init();

	GPIOD->ODR^=GPIO_ODR_OD4; //orange
	GPIOD->ODR^=GPIO_ODR_OD5; // red
	GPIOG->ODR^=GPIO_ODR_OD6; // green

	ee_init();
	NBdelay_ms(50);
	uint16_t mode[5] = {1, 2, 3, 4, 5};
	uint16_t speed = 1234;
	uint16_t temp = 32;

	NBdelay_ms(100);


	if(!ee_write(EE_MODE, &mode, sizeof(mode)))
	{
		GPIOD->ODR^=GPIO_ODR_OD5; // red
	}
	if(!ee_write(EE_SPEED, &speed, sizeof(speed)))
	{
		GPIOG->ODR^=GPIO_ODR_OD6; // green
	}
	if(!ee_write(EE_TEMP, &temp, sizeof(temp)))
	{
		GPIOD->ODR^=GPIO_ODR_OD4; //orange
	}

	ee_read(EE_MODE, buf_mode, 10);
	ee_read(EE_SPEED, buf_speed,2);
	ee_read(EE_TEMP, buf_temp, 2);
}


