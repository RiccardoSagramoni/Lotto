#ifndef LOTTO_H
#define LOTTO_H

#include "costanti.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//////////////////////////////////////////////
//				STRUTTURE DATI				//
//////////////////////////////////////////////
/* Struttura che contiene i dati una schedina
 */
struct schedina {
	int* ruote;
	int quanteRuote;
	int* numeriGiocati;
	int quantiNumeri;
	double* importi;
	int quantiImporti;
};

/* Struttura utilizzata per creare e gestire una lista di schedine
 */
struct schedina_list {
	struct schedina s;
	time_t timestamp;	// timestamp di registrazione della schedina
	struct schedina_list* next;
};

/* Struttura che mantiene i dati relativi all'estrazione di una singola ruota
 */
struct estrazione {
	uint8_t ruota;
	int numeri[QUANTI_NUMERI_ESTRATTI];
};

/* Struttura che mantiene i dati di una vincita
 */
struct vincita {
	time_t timestamp;
	uint8_t* ruote;
	int** numeri_vincitori;			// per ogni ruota i numeri vincenti
	int* quanti_numeri_vincitori;	// per ogni ruota quanti sono i numeri vincenti
	double** importi_vinti;			// per ogni ruota quali sono gli importi vinti
	int* quanti_importi_vinti;		// per ogni ruota quanti sono gli importi vinti
	int quante_ruote;
};


//////////////////////////////////////////////////
//				FUNZIONI DI UTILITY				//
//////////////////////////////////////////////////

/* Alloca in memoria dinamica e inizializza i vari campi di struttura di tipo vincita
 * 
 * @vin struttura GIA' allocata, di cui bisogna allocare i campi
 * @time valore del campo timestamp
 * @quanteRuote valore del campo quante_ruote. Parametro da cui dipende la grandezza dei campi allocati dinamicamente
 */
void costruisci_vincita (struct vincita* vin, time_t time, int quanteRuote);

/* Distrugge i campi allocati dinamicamente di una struttura vincita
 * 
 * @vin struttura di cui bisogna deallocare i campi (la variabile NON sara' deallocata)
 */
void distruggi_vincita (struct vincita* vin);

//
// FUNZIONI DI SERIALIZZAZIONE E DESERIALIZZAZIONE
//
/* Serializza la struttura schedina
 * 
 * @sched schedina da serializzare
 * @len puntatore alla variabile che conterra' la lunghezza della schedina serializzata
 * 
 * @return puntatore che conterra' la stringa della schedina serializzata
 */
char* serializza_schedina_txt (struct schedina sched, uint16_t* len);

/* Deserializza una schedina
 * 
 * @str stringa da deserializzare
 * @quanti_byte_letti quanti byte sono stati deserializzati
 * 
 * @return schedina deserializzata
 */
struct schedina deserializza_schedina_txt (char* str, int* quanti_byte_letti);

//
// FUNZIONI DI CONVERSIONE
//
/* Converte il nome di una ruota in un indice numerico
 * 
 * @str nome da convertire
 * 
 * @return codice numerico se la stringa e' riconosciuta, -1 altrimenti
 */
int convertiRuotaStringToInt (char* str);

/* Inserisce un elemento schedina_list in coda ad una lista
 * 
 * @lista lista degli elementi
 * @elem elemento da inserire
 */
void inserisci_lista_schedina (struct schedina_list** lista, struct schedina_list* elem);

#endif	// LOTTO_H
