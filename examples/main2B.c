// Folgende Module der Low Level Library werden benötigt:

// + stm32f3xx_ll_rcc
// + stm32f3xx_ll_gpio
// + stm32f3xx_ll_tim
// + Stm32f3xx_ll_bus
// + stm32f3xx_ll_system.h

// In diesem Beispiel werden wir die LED mit einem PWM Signal zum Blinken bringen
// Im Gegensatz zu Beispiel 2A geschieht dies ohne Interrupts
// Wir werden stattdessen einen PWM Ausgang von TIM2 über die "Alternate Function" von GPIO Pin A.5 direkt an den Ausgang des Pins weiterleiten

/* Includes ------------------------------------------------------------------*/
#include "main.h"

int main(void)
{
    /*
    Hier werden wir das Porgramm in zwei Schritte unterteilen:
       1. Konfiguration von GPIO Pin A.5
       2. Konfiguration von Timer 2 (PWM)
    */
 
    /*
     An dieser Stelle (Start der "main" Funktion) sind die Startup-Dateien bereits ausgeführt
     Der Systemtakt und die einzelnen Bus-Takte sind also bereits konfiguriert
     Die genauen Einstellungen sind in den Kommentaren der Startup-Datei "system_stm32f3xx.c" ersichtlich
    */
    
    // Dies sind die Initialisierungs-Strukuren von GPIO und TIM2, wie wir sie bereits kennen
	LL_GPIO_InitTypeDef GPIO_Initstructure;
	LL_TIM_InitTypeDef TIM_Init_Struct;
	
    
    // Neben der "Basis-Timer-Konfiguration" müssen wir nun auch den für die PWM zuständigen Teil des TIM2 konfigurieren
    // Die PWM ist Teil des "Output Compare" Modus von TIM2 und daher benötigen wir zusätzlich eine "OCInit" Struktur
   LL_TIM_OC_InitTypeDef TIM_OC_Struct;

    
    /********************** Schritt 1: Konfiguration von Pin A.5 für die LED *************************/
    
    // Wir rufen die Funktion "RCC_AHBPeriphClockCmd" auf und aktivieren den Takt für GPIO Port A
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    
    // Dieses Mal konfigurieren wir den Pin NICHT als Ausgang sondern als "Alternate Function"
    // Die restlichen Parameter lassen wir gleich, schließlich soll der Pin immer noch ein Push-Pull Ausgang ohne Pullup- oder Pulldown Widerstand sein
    // Die Ausgangsstufe (Output Driver) des Pins wird nun aber nicht mehr von uns über das ODR Register angesteuert
    // Stattdessen verbinden wir sie direkt mit einem PWM Ausgang von TIM2 (= "Alternate Function" Modus)
    //Die meisten Pins des STM32 können jedoch für mehrere verschiedene "Alternate Functions" verwendet werden
    // Wir müssen also noch die korrekte AF auswählen, damit der Pin auch mit dem PWM Ausgang von TIM2 verbunden wird
    // Im Datasheet (PDF Seite 42-43) finden wir eine Tabelle, in der die möglichen AFs für alle Pins aufgelistet sind
    // Wir sehen in der Tabelle, dass Pin A.5 im AF-Modus unter anderem als "TIM2_CH1" verwendet werden kann und dazu als "AF1" konfiguriert werden muss
    // "TIM2_CH1" ist der interne Kanal 1 (Channel 1) von TIM2 (siehe VO bzw. Reference Manual)
    // Wenn wir das PWM Signal des Timers auf seinen internen Channel 1 legen (in Schritt 2) und den Pin A.5 als "AF1" konfigurieren, so wird die PWM an den Ausgang geleitet
    // Wir müssen den Pin A.5 also noch zusätzlich so konfigurieren, dass er im "AF1" Modus arbeitet
    
    LL_GPIO_StructInit(&GPIO_Initstructure);
    
    GPIO_Initstructure.Mode = LL_GPIO_MODE_ALTERNATE;         //Das ist neu
    GPIO_Initstructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_Initstructure.Pin = LL_GPIO_PIN_5;
    GPIO_Initstructure.Pull = LL_GPIO_PULL_NO;
    GPIO_Initstructure.Speed = LL_GPIO_SPEED_FREQ_HIGH; // Da wir keine hohen PWM Frequenzen erzeugen, ist der Speed hier immer noch egal
    GPIO_Initstructure.Alternate = LL_GPIO_AF_1;  // Dies ist ebenfalls neu und wählt die peripherie welche mit dem jeweiligen Pin verbunden werden soll
  
    // Jetzt senden wir die Struktur an Port A
    LL_GPIO_Init(GPIOA,&GPIO_Initstructure);
    

    
    
    /*********************** Schritt 2: Timer 2 (PWM) Konfigurieren *********************************************/
        
    // Auch hier müssen wir zu Beginn wieder den Takt für TIM2 aktivieren
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

    // Für die Konfiguration des PWM-Signals benötigen wir zwei Zeitinformationen
    // 1. Wie lange dauert ein Periodendauer
    // 2. Wie groß ist die Pulsweite
    
    // Die Periodendauer des PWM Signals wird durch das Autoreload Register (ARR) bestimmt
    // Der Timer zählt von 0 bis zu dem ARR Wert und fängt dann von vorne an -> Periodendauer wird durch Autoreload Wert bestimmt
    // Während des Zählens vergleicht der Timer den Zählstand mit dem Wert im Capture/Compare Register (CCR)
    // Wird dieser Wert überschritten, so toggelt der Ausgang -> Pulsweite wird durch den CCR Wert bestimmt (der CCR Wert muss also kleiner als der ARR Wert sein)
    
    // Wie in Beispiel 2A konfigurieren wir zu Beginn die Basis des TIM2
    LL_TIM_StructInit(&TIM_Init_Struct);
    TIM_Init_Struct.Prescaler = 63999;                     // Der Bustakt (64 MHz) wird durch den Prescaler (Registerwert + 1) dividiert
                                                           // Wir dividieren den Takt also durch 8000 und erhalten damit 1kHz "Zählfrequenz"
     TIM_Init_Struct.Autoreload = 999;                     // Der Timer zählt bis zu dem Autoreload Wert und fängt von vorne an
                                                           // Die Periodendauer unseres PWM Signals beträgt daher 1s
    
    // Jetzt übergeben wir die Struktur an den Timer 2
    LL_TIM_Init(TIM2,&TIM_Init_Struct);
    
    // Die Timer-Basis ist konfiguriert, jetzt müssen wir noch den PWM-Modus des Timers aktivieren und die Pulsweite einstellen
    // Dies geschieht mittels des Output Compare (OC) Modus des Timers und daher verwenden wir die "OCInit" Struktur
    // Auch hier initialisieren wir die Struktur wieder mit den Default-Werten, bevor wir die relevanten Parameter einstellen (die Funktion dazu heißt "TIM_OCStructInit").
	LL_TIM_OC_StructInit(&TIM_OC_Struct);
    TIM_OC_Struct.CompareValue=500;                                 // Pulsweite: Jener Wert, der in das CCR Register geschrieben wird (= 0.5s)
    TIM_OC_Struct.OCMode = LL_TIM_OCMODE_PWM1;                      // PWM Modus 1: PWM Channel ist "activ" solange der aktuelle Zählstand kleiner als der CCR-Wert ist
    TIM_OC_Struct.OCState = LL_TIM_OCSTATE_ENABLE;                  // PWM Ausgang soll aktiviert werden
    TIM_OC_Struct.OCPolarity = LL_TIM_OCPOLARITY_HIGH;              // Polarität des Ausgangs: Ist der Channel "active" so wird der Ausgang auf "high" gesetzt (LED leuchtet)
    
    // Wir haben die OCInit Struktur nun so befüllt, dass das gewünschte PWM Signal an einem internen Timer Channel erzeugt wird
    // Der Timer 2 besitzt jedoch mehrere interne Channels, die wir mit dieser Struktur konfigurieren können
    // Wir wissen aber bereits, dass Pin A.5 nur mit dem Channel 1 des Timers 2 verbunden werden kann
    // Daher müssen wir den Channel 1 von Timer 2 mit unserer Struktur konfigurieren
    // Dazu verwenden wir die Funktion "LL_TIM_OC_Init"  und geben den Channel über den 2en Parameter mit LL_TIM_CHANNEL_CH1 an 
    LL_TIM_OC_Init(TIM2,LL_TIM_CHANNEL_CH1,&TIM_OC_Struct);
    
    // Jetzt ist die Timer Basis und der internen Channel 1 von Timer 2 so konfiguriert, dass der Timer ein PWM Signal am internen Channel 1 ausgibt
    // Die Periodendauer ist 1s und die Pulsweite 0.5s
    // Außerdem haben wir den internen Channel 1 des Timers mit dem Ausgang von Pins A.5 verbunden
    // Die Konfiguration ist abgeschlossen und es fehlt nur mehr ein letzter Schritt:
    // Wir müssen den Timer starten und die LED beginnt zu blinken :-)
    // Wer ganz genau aufgepasst hat, wird auch bemerkt haben, dass die LED hier doppelt so schnell blinkt (sie toggelt ja alle 0.5s)
    LL_TIM_EnableCounter(TIM2);
    
    //Controller geht in den Schlafzustand bei Beenden Des Hauptprogramms, die folgende Zeile deaktiviert dies
	/* Set SLEEPONEXIT bit of Cortex System Control Register */
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPONEXIT_Msk));
    
    // Endlosschleife
    while (1);
    
    // Im Gegensatz zu Beispiel 2A ist die Endlosschleife dieses Mal aber eigentlich gar nicht nötig
    // Im Beispiel 2A musste die CPU aktiv bleiben, um jederzeit auf einen auftretenden Interrupt reagieren zu können
    // Das Blinken wird jetzt aber durch eine Hardware-PWM OHNE Zuhilfenahme des Kerns bewerkstelligt (es gibt ja auch keine Interrupts mehr)
    // Besonders deutlich wird dieser Umstand, wenn man im Debug-Modus nach der Initialisierung die Programmabarbeitung unterbricht (bzw. beendet):
    // Die LED blinkt dann immer noch :-)
    // Alternativ könnten wir auch die "while(1)" Schleife entfernen, dann beendet der Controller seine Arbeit nach der Konfiguration, die LED blinkt aber trotzdem :-)

}
