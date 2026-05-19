#include "main.h"

int main(){

	// Wir folgen den gleichen 3 Schritten wie bisher, um die LED zum Leuchten zu bringen:
	// 1ter Schritt: GPIO Port über den Advanced Hyper Bus (AHB) aktivieren
	// 2ter Schritt: Pin 5 (von GPIO A) als Push-Pull Ausgang konfigurieren
	// 3ter Schritt: '1' an den Ausgang legen, um die LED leuchten zu lassen
	
    // ... aber dieses Mal ist alles etwas anders ....
	
	
// 1. Schritt:
//*********
// Nach wie vor sehr einfach...
// Wir rufen die Funktion "LL_AHB1_GRP1_EnableClock" auf und übergeben die richtigen Parameter
// Woher wissen wir aber überhaupt, dass die Funktion so heißt?
// Weil es in der Hilfsdatei "um1786-description-of-stm32f3-hal-and-lowlayer-drivers-stmicroelectronics.pdf" steht und die Namen meist auch selbsterklärend sind
// Im Keil Editor kann man nun einfach beginnen "LL_AHB1..." einzutippen und Ctrl+Leertaste drücken
// Schon werden vom Compiler alle Funktionen und Define-Anweisung angezeigt, die mit dieser Zeichenfolge beginnen
// Wir wählen die richtige aus und öffnen eine Klammer, schon zeigt der Compiler wieder zwei Hinweise an:
// Wie viele Parameter werden erwartet und wie fangen deren Namen an
	
LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
	
// 1. Parameter: Welche Peripherie soll ausgewählt werden
	
// 2. Schritt:
// *********
// Die Funktion, die wir jetzt benötigen, heißt "LL_GPIO_Init()"

// Diese erwartet eine Struktur, welche alle nötigen Daten für die Initialisierung enthält
// Wir werden diese Struktur einfach "GPIO_InitStructure" nennen
// Sie muss vom Typ "LL_GPIO_InitTypeDef" sein (wie in der Hilfsdatei ersichtlich ist)
// Die entsprechende Typendefinition befindet sich in der eingebundenen Low Level Library

// Normalerweise stehen alle Deklarationen am Beginn einer Funktion (manche Kompiler verlangen das explizit)
// Für das bessere Verständnis platzieren wir die Deklaration der GPIO Struktur ausnahmsweise erst hier
// In Zukunft werden Deklarationen aber immer am Beginn der jeweiligen Funktion stehen 

LL_GPIO_InitTypeDef GPIO_InitStructure;

// Jetzt wird es Zeit, die Struktur mit den nötigen Daten zu füllen
// Auch hier hilft uns der Compiler wieder sehr
// Sobald wir die Struktur im Editor eingegeben und einen Punkt "." getippt haben, werden alle Elemente der Struktur angezeigt
// Wir wählen das erste ("Mode") aus und tippen "="
// Alles danach beginnt mit "LL_GPIO_MODE"
// Wieder zeigt uns der Compiler eine Liste aller möglichen Werte
// Da wir einen Ausgang ("OUTPUT") wollen, ergibt sich:

GPIO_InitStructure.Mode = LL_GPIO_MODE_OUTPUT;

// Jetzt wiederholen wir diese Schritte für alle anderen Elemente der Struktur ... Die Namen sind selbsterklärend ...
// Wichtig ist hier, dass wir auch wirklich für alle vorhandenen Elemente einen Wert setzen, selbst wenn uns ein Parameter egal ist 
// oder ohnehin der Resetwert hingehört ... Warum? Weil der Wert in der C-Strukur sonst nicht klar definiert ist und wir dann nicht
// wissen, wie unsere Peripherie konfiguriert wird (Meist wird die Struktur mit lauter 0er initialisiert, aber leider nicht immer!!!)

GPIO_InitStructure.OutputType = LL_GPIO_OUTPUT_PUSHPULL;     
GPIO_InitStructure.Pin = LL_GPIO_PIN_5;            
GPIO_InitStructure.Pull = LL_GPIO_PULL_NO;   // Wir benötigen keine Pull-Up oder Pull-Down Widerstände
GPIO_InitStructure.Speed = LL_GPIO_SPEED_FREQ_HIGH;  // Die Geschwindigkeit ist hier wirklich egal, wir drehen nur ein LED auf

// und jetzt ...
// senden wir die erstellten Konfigurationsdaten an Port A (GPIOA)
LL_GPIO_Init(GPIOA,&GPIO_InitStructure);
	
// Wir müssen nur beachten, dass wir nicht die Struktur selbst sondern nur einen Zeiger darauf übergeben können!

// 3ter Schritt:
// *********
// Jetzt nur noch Pin 5 auf '1' setzen. Glücklicherweise haben wir auch hierfür eine Low Level Funktion: "LL_GPIO_SetOutputPin"
// Diese benötigt keine Struktur sondern nur 2 Parameter: 
// Welcher GPIO und welcher Pin wird benötigt?

LL_GPIO_SetOutputPin(GPIOA,LL_GPIO_PIN_5);
	
// Geschafft! Willkommen in der Familie der Low Level Library Benutzer. Passen Sie auf sich auf :-)

while(1);	

}
