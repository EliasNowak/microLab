// Folgende Module der Low Level Library werden benötigt:
// + stm32f3xx_ll_bus
// + stm32f3xx_ll_gpio
// + stm32f3xx_ll_usart
// + stm32f3xx_ll_rcc
// + stm32f3xx_ll_system

#include "main.h"

// Hilfsfunktion: Ein einzelnes Zeichen über USART2 senden
void USART2_SendChar(uint8_t ch)
{
    // Warten bis das Transmit Data Register leer ist (TXE Flag)
    while (!LL_USART_IsActiveFlag_TXE(USART2));
    // Zeichen in das Transmit Data Register schreiben
    LL_USART_TransmitData8(USART2, ch);
}

// Hilfsfunktion: Einen String über USART2 senden
void USART2_SendString(const char *str)
{
    while (*str)
    {
        USART2_SendChar(*str++);
    }
}

// Hilfsfunktion: Ein Zeichen über USART2 empfangen (blockierend)
uint8_t USART2_ReceiveChar(void)
{
    // Warten bis Daten im Receive Data Register vorhanden sind (RXNE Flag)
    while (!LL_USART_IsActiveFlag_RXNE(USART2));
    // Empfangenes Zeichen zurückgeben
    return LL_USART_ReceiveData8(USART2);
}

int main(void)
{
    /******************** Schritt 1: GPIO Konfiguration für USART2 (PA2=TX, PA3=RX) ********************/

    LL_GPIO_InitTypeDef GPIO_InitStructure;

    // Takt für GPIO Port A aktivieren
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);

    // PA2 (TX) und PA3 (RX) als Alternate Function konfigurieren
    // Auf dem STM32F334 ist USART2 an AF7 für PA2 und PA3 verfügbar
    LL_GPIO_StructInit(&GPIO_InitStructure);
    GPIO_InitStructure.Pin = LL_GPIO_PIN_2 | LL_GPIO_PIN_3;
    GPIO_InitStructure.Mode = LL_GPIO_MODE_ALTERNATE;
    GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_InitStructure.Pull = LL_GPIO_PULL_UP;
    GPIO_InitStructure.Alternate = LL_GPIO_AF_7;

    LL_GPIO_Init(GPIOA, &GPIO_InitStructure);

    /******************** Schritt 2: USART2 Konfiguration (115200 Baud, 8N1) ********************/

    LL_USART_InitTypeDef USART_InitStructure;

    // Takt für USART2 aktivieren (USART2 hängt am APB1 Bus)
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_USART2);

    // USART2 zuerst deaktivieren, bevor wir konfigurieren
    LL_USART_Disable(USART2);

    // USART2 Konfiguration
    LL_USART_StructInit(&USART_InitStructure);
    USART_InitStructure.BaudRate = 115200;
    USART_InitStructure.DataWidth = LL_USART_DATAWIDTH_8B;
    USART_InitStructure.StopBits = LL_USART_STOPBITS_1;
    USART_InitStructure.Parity = LL_USART_PARITY_NONE;
    USART_InitStructure.TransferDirection = LL_USART_DIRECTION_TX_RX;
    USART_InitStructure.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
    USART_InitStructure.OverSampling = LL_USART_OVERSAMPLING_16;

    LL_USART_Init(USART2, &USART_InitStructure);

    // USART2 aktivieren
    LL_USART_Enable(USART2);

    // Warten bis USART2 bereit ist (TEACK und REACK Flags)
    // Dies ist auf dem STM32F3 notwendig, bevor Daten gesendet oder empfangen werden können
    while (!LL_USART_IsActiveFlag_TEACK(USART2));
    while (!LL_USART_IsActiveFlag_REACK(USART2));

    /******************** Schritt 3: UART Kommunikation ********************/

    // Begrüßungsnachricht senden
    USART2_SendString("USART2 bereit (115200 Baud, 8N1)\r\n");

    // Echo-Schleife: Empfangene Zeichen werden zurückgesendet
    while (1)
    {
        uint8_t received = USART2_ReceiveChar();
        USART2_SendChar(received);
    }
}
