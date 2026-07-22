// Pre-production physical CAN smoke: RT publishes high MCP2515 + low TWAI; SYS publishes low TWAI.
#include <cstdio>
#include "driver/spi_master.h"
#include "driver/twai.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#ifndef BOARD_ROLE
#define BOARD_ROLE 0
#endif
namespace {
constexpr bool kRt = BOARD_ROLE == 0;
constexpr uint32_t kLowId = kRt ? 0x100 : 0x200;
constexpr uint32_t kHighId = 0x110;
constexpr int kHost = SPI2_HOST;
constexpr uint8_t RESET=0xC0, READ=0x03, WRITE=0x02, MODIFY=0x05;
constexpr uint8_t CANSTAT=0x0E, CANCTRL=0x0F, CNF3=0x28, CNF2=0x29, CNF1=0x2A;
constexpr uint8_t TXCTRL=0x30, TXSIDH=0x31, TXSIDL=0x32, TXDLC=0x35, TXD0=0x36;
spi_device_handle_t spi=nullptr;
bool xfer(const uint8_t* tx,uint8_t* rx,size_t n){spi_transaction_t t{};t.length=n*8;t.tx_buffer=tx;t.rx_buffer=rx;return spi_device_transmit(spi,&t)==ESP_OK;}
bool wr(uint8_t r,uint8_t v){uint8_t b[3]={WRITE,r,v};return xfer(b,nullptr,3);}
bool rd(uint8_t r,uint8_t& v){uint8_t b[3]={READ,r,0},o[3]{};if(!xfer(b,o,3))return false;v=o[2];return true;}
bool mod(uint8_t r,uint8_t m,uint8_t v){uint8_t b[4]={MODIFY,r,m,v};return xfer(b,nullptr,4);}
bool low_init(){twai_general_config_t g=TWAI_GENERAL_CONFIG_DEFAULT_V2(0,GPIO_NUM_5,GPIO_NUM_4,TWAI_MODE_NORMAL);g.tx_queue_len=16;g.rx_queue_len=32;twai_timing_config_t t{};t.quanta_resolution_hz=8'000'000;t.tseg_1=11;t.tseg_2=4;t.sjw=2;twai_filter_config_t f=TWAI_FILTER_CONFIG_ACCEPT_ALL();return twai_driver_install(&g,&t,&f)==ESP_OK&&twai_start()==ESP_OK;}
bool high_init(){spi_bus_config_t b{};b.mosi_io_num=16;b.miso_io_num=17;b.sclk_io_num=15;b.quadwp_io_num=-1;b.quadhd_io_num=-1;if(spi_bus_initialize(static_cast<spi_host_device_t>(kHost),&b,SPI_DMA_DISABLED)!=ESP_OK)return false;spi_device_interface_config_t d{};d.mode=0;d.clock_speed_hz=1'000'000;d.spics_io_num=18;d.queue_size=1;if(spi_bus_add_device(static_cast<spi_host_device_t>(kHost),&d,&spi)!=ESP_OK)return false;uint8_t reset=RESET,stat=0;if(!xfer(&reset,nullptr,1))return false;vTaskDelay(pdMS_TO_TICKS(20));if(!rd(CANSTAT,stat)||stat==0||stat==0xFF)return false;return wr(CNF1,0)&&wr(CNF2,0x91)&&wr(CNF3,0x01)&&mod(CANCTRL,0xE0,0);}
void low_send(uint16_t s){twai_message_t m{};m.identifier=kLowId;m.data_length_code=4;m.data[0]=kRt?'R':'S';m.data[1]=kRt?'T':'Y';m.data[2]=s>>8;m.data[3]=s;twai_transmit(&m,pdMS_TO_TICKS(20));}
void high_send(uint16_t s){if(!wr(TXCTRL,0)||!wr(TXSIDH,kHighId>>3)||!wr(TXSIDL,(kHighId&7)<<5)||!wr(TXDLC,4)||!wr(TXD0,'R')||!wr(TXD0+1,'H')||!wr(TXD0+2,s>>8)||!wr(TXD0+3,s)){return;}mod(TXCTRL,0x08,0x08);}
}
extern "C" void app_main(){bool low=low_init(),high=!kRt||high_init();printf("DUAL_CAN role=%s low=%s high=%s\\n",kRt?"RT":"SYS",low?"OK":"FAIL",high?"OK":"FAIL/N-A");for(uint16_t s=0;;++s){if(low)low_send(s);if(kRt&&high)high_send(s);if(!(s%10))printf("DUAL_CAN seq=%u\\n",s);vTaskDelay(pdMS_TO_TICKS(200));}}