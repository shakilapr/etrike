/*
 * driver/spi_master.h — Host stub for ESP-IDF SPI master.
 *
 * The MCP2515 CAN controller uses SPI.  On the host the MCP2515
 * is replaced by a direct VirtualCanBus route, so SPI is a no-op.
 */
#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

/* ── types ─────────────────────────────────────────────────────── */
typedef int spi_host_device_t;
#define SPI2_HOST 2
#define SPI3_HOST 3

struct spi_bus_config_t {
    int mosi_io_num, miso_io_num, sclk_io_num;
    int quadwp_io_num, quadhd_io_num;
    int max_transfer_sz;
    int intr_flags;
};

struct spi_device_interface_config_t {
    int         command_bits, address_bits, dummy_bits;
    int         mode;
    int         clock_speed_hz;
    int         spics_io_num;
    int         queue_size;
    int         flags;
    int         cs_ena_pretrans, cs_ena_posttrans;
};

typedef void* spi_device_handle_t;

struct spi_transaction_t {
    uint32_t flags;
    uint8_t  command;
    uint8_t  address;
    uint32_t length;
    uint32_t rxlength;
    void*    user;
    union {
        const void* tx_buffer;
        void*       rx_buffer;
    };
};

#define SPI_MASTER_FREQ_8M     (8 * 1000 * 1000)
#define SPI_MASTER_FREQ_10M    (10 * 1000 * 1000)
#define SPI_DMA_DISABLED       0
#define SPI_TRANS_MODE_DIO     0
#define SPI_DEVICE_HALFDUPLEX  0

/* ── API stubs (no-op) ─────────────────────────────────────────── */
inline int spi_bus_initialize(spi_host_device_t, const spi_bus_config_t*, int) { return 0; }
inline int spi_bus_add_device(spi_host_device_t, const spi_device_interface_config_t*, spi_device_handle_t*) { return 0; }
inline int spi_device_transmit(spi_device_handle_t, spi_transaction_t*) { return 0; }

#ifdef __cplusplus
}
#endif
