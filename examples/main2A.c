// Folgende Module der Low Level Library werden benötigt:

// + stm32f3xx_ll_rcc
// + stm32f3xx_ll_gpio
// + stm32f3xx_ll_tim
// + Stm32f3xx_ll_bus
// + stm32f3xx_ll_system.h


// Auf TUWEL finden Sie eine genau Anleitung, wie Sie die benötigten Module einbinden können.

// In diesem Beispiel werden wir nun einen Timer und Interrupt verwenden, um die LED zum Blinken zu bringen.
// Weil wir hier wirklich viel erklären müssen, ist die Anzahl an Kommentaren ein klein wenig größer....

/* Includes ------------------------------------------------------------------*/
#include "main.h"

int main(void)
{
    /*
    Auch hier können wir das Programm wieder in drei Schritte unterteilen:
          1. Konfiguration von GPIO Pin A.5 (gleich wie Beispiel 1C)
          2. Konfiguration von Timer und NVIC
          3. Programmieren des Interrupt Handlers
    */
 
    /*
     An dieser Stelle (Start der "main" Funktion) sind die Startup-Dateien bereits ausgeführt.
     Der Systemtakt und die einzelnen Bus-Takte sind also bereits konfiguriert.
     Die genauen Einstellungen sind in den Kommentaren der Startup-Datei "system_stm32f3xx.c" ersichtlich.
     In unserem Fall ist der Systemtakt 8MHz und auch der Takt der Peripheral Busse ist auf 8MHz gesetzt.
    */
    
    // Dies ist die GPIO Initialisierungs-Struktur, wie wir sie in Beispiel 1C kennengelernt haben.
    LL_GPIO_InitTypeDef GPIO_Initstructure; 
    
    // Ähnlich wie bei der GPIO, gibt es auch für den Timer und NVIC jeweils Strukturen für die Initialisierung.
    // Wir werden sie hier deklarieren und später verwenden.
    LL_TIM_InitTypeDef TIM_Init_Struct;

    
    /********************** Schritt 1: Konfiguration von Pin A.5 für die LED *************************/
    
    // Wir rufen die Funktion "LL_AHB1_GRP1_EnableClock" auf und übergeben den richtigen Parameter.
    // Der Name und der Aufbau dieser Funktion finder sich in der "um1786-description-of-stm32f3-hal-and-lowlayer-drivers-stmicroelectronics.pdf" Hilfsdatei.
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    
    // Nun befüllen wir zuerst die GPIO_Initstructure mit Standardwerten um sicher alle Felder mit sinnvollen Werten zu beschreiben
    LL_GPIO_StructInit(&GPIO_Initstructure);
    
    // Die folgenden Zeilen sind in Beispiel 1C bereits ausführlich erklärt worden.
    // Wir konfigurieren Pin A.5 als Push-Pull Ausgang ohne Pullup oder Pulldown Widerstand (Speed ist egal).
		
    GPIO_Initstructure.Mode = LL_GPIO_MODE_OUTPUT;
    GPIO_Initstructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    GPIO_Initstructure.Pin = LL_GPIO_PIN_5;
    GPIO_Initstructure.Pull = LL_GPIO_PULL_NO;
    GPIO_Initstructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    
    // Jetzt senden wir die befüllte Struktur an Port A.
    LL_GPIO_Init(GPIOA,&GPIO_Initstructure); 
    
    /*********************** Schritt 2: Timer und NVIC Konfigurieren *********************************************/ 
    
    // Gleich wie bei GPIO Port A müssen wir nun auch den Timer 2 (TIM2) mit Takt versorgen, bevor wir ihn konfigurieren können.
    // TIM2 ist am Advanced Peripheral Bus 1 angeschlossen und daher benötigen wir:
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);

    // Jetzt ist es an der Zeit den Timer zu konfigurieren. Dies geschieht durch die Funktion "LL_TIM_Init".
    // Diese Funktion konfiguriert die "Timer-Basis", also das Grundgerüst, welches für das einfache Zählen zuständig ist.
    // Als Übergabewert wird der gewünschte Timer (TIM2) und eine Struktur vom Typ "LL_TIM_InitTypeDef" erwartet.
    // Wir haben bereits eine Struktur mit dem Namen "TIM_Init_Struct" deklariert.
    // Nun hätten wir gerne, dass unser Timer im Sekundentakt einen Update Interrupt auslöst.
    // Daher müssen wir den Prescaler und das Autoreloadregister entsprechend befüllen.
    
    // In der TimeBaseInit Struktur kann man allerdings etwas mehr konfigurieren als nur diese beiden Werte. 
    // Da in unserem Fall aber alle Parameter außer der Prescaler und Autoreloadwert auf ihrem Default- (Reset-) Wert bleiben können, 
    // nützen wir die Gelegenheit und lernen eine neue praktische Low Level Library Funktion kennen: "LL_TIM_StructInit"
    // Der Funktion übergeben wir unsere Struktur und sie befüllt alle Elemente der Struktur mit den Default-Werten.
    // Mit einem Rechtsklick auf die Funktion im Code und dann der Auswahl "Goto Definition of ..." springen wir in die Funktion und sehen
    // auch schön, was die Default-Werte sind (wir könnten aber auch im Reference Manual nachsehen). Diesen Rechtsklick sollte man sich merken!
    // In diesem Fall wird der Timer ohne zusätzliche Clockdivision im Upcounting Modus konfiguriert und der Prescaler und ARR Wert
    // zunächst auf einen Standardwert gesetzt. (Der RepetitionCounter ist vorerst nicht wichtig für uns)
    // Solch eine "...StructInit" Funktion gibt es für die meisten Init-Strukturen (z.B. auch für unsere GPIO Struktur) und es ist daher
    // immer ratsam, mit dieser Funktion die jeweilige Strukur einmal zu initialisieren, damit kein Element vergessen wird. 
    // Wir hätten das daher auch bereits für unsere GPIO Struktur machen können. Da wir dort aber alle vorhandenen Elemente gesetzt haben, müssen 
    // wir die Funktion nicht unbedingt aufrufen. 
    // Wir merken uns also: Jedes Element der Struktur MUSS einen Wert zugewiesen bekommen. Sollen viele Elemente auf ihrem Default-Wert bleiben
	// dann bietet die jeweilige Init-Funktion eine praktische Möglichkeit, den Code zu reduzieren. Wir werden in Zukunft nur mehr dann solch eine
    // Init-Funktion verwenden, wenn wir nicht alle Elemente selber setzen.
	
    LL_TIM_StructInit(&TIM_Init_Struct);
    
    // Und jetzt passen wir die Parameter an, die wir aktiv verändern wollen: Prescaler und ARR Wert
    TIM_Init_Struct.Prescaler = 63999;            // Der Bustakt (64 MHz) wird durch den Prescaler (Registerwert + 1) dividiert.
                                                  // Wir dividieren den Takt also durch 64000 und erhalten damit 1kHz "Zählfrequenz".
    TIM_Init_Struct.Autoreload = 999;             // Der Timer zählt bis zu dem Autoreload Wert und löst einen Update Interrupt aus.
                                                  // Wir lassen ihn also bis 999 zählen, dann fängt er wieder bei 0 an.
                                                  // Daraus ergibt sich genau eine Sekunde zwischen den einzelnen Interrupts.
    
    // Jetzt übergeben wir die Struktur an den Timer 2:
    LL_TIM_Init(TIM2,&TIM_Init_Struct);
    
    
    // Nachdem wir nun Timer und GPIO konfiguriert haben, fehlt noch der NVIC.
    
    // Und hier brauchen wir ganz zu Beginn einen wichtigen Befehl: "NVIC_SetPriorityGrouping".
    // Diese Funktion ist nötig, um die richtige Anzahl an Bits für die Gruppenpriorität und Subgruppenpriorität zu setzen.
    // Im NVIC stehen für jeden Interrupt Channel für beide Prioritäten zusammen 4 Bits zur Verfügung und wir können uns aussuchen,
    // wie viele davon für die Gruppe und wie viele für die Subgruppe verwendet werden. Wir entscheiden uns hier für 2-2, also können wir 
    // jeweils die Level 0-3 einstellen. (Da wir in diesem Beispiel nur einen Interrupt haben, ist diese Wahl aber ziemlich egal)
    // Den dafür nötigen Übergabewert "priority_grouping" finden wir in der Datei "core_cm4.h" wenn wir
    //  wieder rechts auf die Funktion klicken und zu ihrer Definition springen. Dort ist ebenfalls ausführlich dokumentiert, wie die
    // Funktion funktioniert und welche Parameter man ihr übergeben kann.
    // Es ist jedenfalls sehr wichtig, diese Funktion VOR der Konfiguration der NVIC Channels aufzurufen, weil ansonsten die eingestellte
    // Gruppen- und Subgruppenpriorität nicht richtig konfiguriert wird! (Der Befehl kann in Zukunft daher gerne ganz oben im Code stehen)
    uint32_t priority_grouping = 5;
	NVIC_SetPriorityGrouping(priority_grouping);
    
    //http://www.ocfreaks.com/interrupt-priority-grouping-arm-cortex-m-nvic/
    
    
    
    
    // Jetzt können wir uns um die eigentliche Konfiguration des TIM2 Interrupt Channels kümmern.
    // Diese kann über die Funktion "NVIC_EncodePriority" und NVIC_SetPriority vorgenommen werden
    // Die Funktion NVIC_EncodePriority kodiert die gewünschte Priorität entsprechend der zuvor gewählten priority_grouping und es werden 3 Parameter übergeben
    // 1. Parameter: der zuvor definierte priority_grouping Wert übergeben
    // 2. Parameter: Die Preemption Priority 
    // 3. Parameter: Doe Subpriority 
    // Diese kodierte Priorität wird anschließend an die Funktion NVIC_SetPriority übergeben mit Angabe des Channels welche in der Datei "stm32f334x8.h" gefunden werden kann
    // In diesem Fall ist es "TIM2_IRQn":
    // -) "TIM2" da wir den Interrupt Channel von Timer 2 konfigurieren
    // -) "IRQ" steht für "Interrupt ReQuest"
    // -) "n" bedeutet, dass hier mehrere Interrupt Quellen zu einem Interrupt Channel zusammengefasst werden (siehe VO)
   	uint32_t encoded_priority = NVIC_EncodePriority(priority_grouping,0,0);
		NVIC_SetPriority(TIM2_IRQn, encoded_priority);
    
    
    // Anschließend wird mit NVIC_EnableIRQ der entsprechende Interrupt in der NVIC aktiviert
    NVIC_EnableIRQ(TIM2_IRQn);

    // Zurück zu Timer 2: Dieser ist zwar konfiguriert, er läuft aber noch nicht.
    // Wir müssen ihn also noch starten und das geschieht mit dem Befehl:
    LL_TIM_EnableCounter(TIM2); 

    // Wir haben schon den Interrupt Channel von TIM2 im NVIC aktiviert.
    // Jetzt müssen wir in der Konfiguration von TIM2 aber auch noch den "Update Interrupt" aktivieren.
    // Erst wenn im Timer der Update Interrupt und in der NVIC der zugehörige Channel aktiviert ist, wird der Interrupt auch ausgelöst.
    LL_TIM_EnableIT_UPDATE(TIM2);

    // Jetzt haben wir GPIO, Timer und NVIC fertig konfiguriert und den Timer gestartet.
    // Der Timer löst jede Sekunde einen Interrupt aus und dieser ruft den zugehörigen Interrupt Handler auf.
    // Diesen können wir nun so programmieren, dass die LED im Sekundentakt blinkt.
    // Der Interrupt Handler hat dabei einen speziellen Funktionsnamen.
    // Die Funktionsnamen der Interrupt Handler finden wir in der "startup_stm32f334x8.s" Datei, in unserem Fall ist das "TIM2_IRQHandler".
    // Die Funktion muss GENAU DIESEN Namen haben, inklusive gleicher Groß- und Kleinschreibung (case-sensitiv).

    // Warum betonen wir das so???
    // Der Interrupt Handler befindet sich an einer vorgegebenen Programmspeicher-Adresse.
    // An diese Adresse springt der Controller, wenn der Interrupt aufgerufen wird.
    // Der Funktionsname "TIM2_IRQHandler" ist nichts anderes als ein "Platzhalter" (eine Define-Anweisung) für diese Adresse.
    // Erstellen wir im Anschluss also eine Funktion mit diesem Namen, wird der zugehörige Code an die richtige Adresse im Speicher geschrieben
    // und der Code wird beim Auslösen des Interrupts korrekt ausgeführt.
    // Nennen wir die Funktion nur leicht anders, so erkennt der Kompiler den Funktionsnamen nicht mehr als Platzhalter einer Interrupt-Adresse.
    // Er hält die Funktion für eine "normale C-Funktion" und speichert ihren Inhalt an eine beliebige freie Stelle im Programmspeicher.
    // Wird nun der Interrupt Handler aufgerufen, springt der Controller an die Interrupt-Speicheradresse.
    // Doch unser programmierter "Interrupt-Code" steht wo anders im Speicher und das Programm funktioniert daher nicht wie gewünscht.
    // Es tritt aber WEDER eine WARNUNG noch ein FEHLER (ERROR) beim Kompilieren auf, weil die falsch genannte Funktion ja nicht verboten ist!
  
    //Controller geht in den Schlafzustand bei Beenden Des Hauptprogramms, die folgende Zeile deaktiviert dies
	/* Clear SLEEPONEXIT bit of Cortex System Control Register */
    CLEAR_BIT(SCB->SCR, ((uint32_t)SCB_SCR_SLEEPONEXIT_Msk));
    
    
    while (1)         // Endlosschleife
    {                 // Sie tut nichts AUSSER
    }                 // auf den Interrupt zu warten

}


/********************************** Schritt 3: Interrupt Handler von Timer 2 ******************************/ 

void TIM2_IRQHandler(void)
// Beachten Sie die korrekte Schreibweise "TIM2_IRQHandler".
{
    // Wenn ein TIM2 Interrupt ausgeöst wird, wird dieser Handler aufgerufen
    // Da es mehrere Interruptquellen gibt müssen wir zuerst prüfen ob der Update Interrupt aktiv ist
    // Dies gelingt mit der Funktion LL_TIM_IsActiveFlag_UPDATE
    // Wenn ein Update Interrupt der angegebenen Peripherie aktiv ist gibt diese Funktion 1 zurück
    if(LL_TIM_IsActiveFlag_UPDATE(TIM2) == 1)
    {		
        //Anschließend kann das entsprechende Flag mit der Funktion LL_TIM_ClearFlag_UPDATE zurückgesetzt werden
        // Dies ermöglicht ein erneutes setzen des Flags und somit den Aufruf des Interrupts
        // Bleibt es gesetzt, so wird dem NVIC dadurch nach der Abarbeitung des Interrupt Handlers sofort wieder signalisiert, dass der Interrupt ausgelöst wurde.
        // Der Interrupt Handler würde also sofort wieder aufgerufen werden ... und wieder ... und wieder ... (außer ein höher priorisierter Interrupt tritt auf).
		LL_TIM_ClearFlag_UPDATE(TIM2);
		// Anschließend kann der Pin A.5 mit dem Befehlt LL_GPIO_TogglePin getoggelt werden
		LL_GPIO_TogglePin(GPIOA,LL_GPIO_PIN_5);
    }
   
}
