#include "lotto.h"

/* Alloca in memoria dinamica e inizializza i vari campi di struttura di tipo vincita
 * 
 * @vin struttura GIA' allocata, di cui bisogna allocare i campi
 * @time valore del campo timestamp
 * @quanteRuote valore del campo quante_ruote. Parametro da cui dipende la grandezza dei campi allocati dinamicamente
 */
void costruisci_vincita (struct vincita* vin, time_t time, int quanteRuote)
{
	vin->timestamp = time;
	vin->quante_ruote = quanteRuote;
	
	vin->ruote = malloc(sizeof(uint8_t) * quanteRuote);
	
	vin->numeri_vincitori = malloc(sizeof(int*) * quanteRuote);
	memset(vin->numeri_vincitori, 0, (sizeof(int*) * quanteRuote));
	
	vin->importi_vinti = malloc(sizeof(double*) * quanteRuote);
	memset(vin->importi_vinti, 0, (sizeof(double*) * quanteRuote));
	
	vin->quanti_numeri_vincitori = malloc(sizeof(int) * quanteRuote);
	memset(vin->quanti_numeri_vincitori, 0, sizeof(int) * quanteRuote);
	
	vin->quanti_importi_vinti = malloc(sizeof(int) * quanteRuote);
	memset(vin->quanti_importi_vinti, 0, sizeof(int) * quanteRuote);
}

/* Distrugge i campi allocati dinamicamente di una struttura vincita
 * 
 * @vin struttura di cui bisogna deallocare i campi (NON sara' deallocata)
 */
void distruggi_vincita (struct vincita* vin)
{
	int i;
	
	for (i = 0; i < vin->quante_ruote; ++i) {
		free(vin->numeri_vincitori[i]);
		free(vin->importi_vinti[i]);
	}
	
	free(vin->quanti_importi_vinti);
	free(vin->quanti_numeri_vincitori);
	free(vin->ruote);
}

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
char* serializza_schedina_txt (struct schedina sched, uint16_t* len)
{
	int i, char_scritti;
	size_t contatore = 0;
	char* schedina_serializzata = NULL;
	char buffer[2048];
	
	sprintf(buffer, "%i %n", sched.quanteRuote, &char_scritti);
	contatore += char_scritti;
	
	for (i = 0; i < sched.quanteRuote; ++i) {
		sprintf(buffer + contatore, "%i %n", sched.ruote[i], &char_scritti);
		contatore += char_scritti;
	}
	
	sprintf(buffer + contatore, "%i %n", sched.quantiNumeri, &char_scritti);
	contatore += char_scritti;
	
	for (i = 0; i < sched.quantiNumeri; ++i) {
		sprintf(buffer + contatore, "%i %n", sched.numeriGiocati[i], &char_scritti);
		contatore += char_scritti;
	}
	
	sprintf(buffer+contatore, "%i %n", sched.quantiImporti, &char_scritti);
	contatore += char_scritti;
	
	for (i = 0; i < sched.quantiImporti; ++i) {
		sprintf(buffer+contatore, "%.2lf %n", sched.importi[i], &char_scritti);
		contatore += char_scritti;
	}
	contatore++; // includi null terminator
	
	schedina_serializzata = malloc(contatore);
	if (!schedina_serializzata) {
		fprintf(stderr, "Impossibile allocare in memoria dinamica la schedina serializzata\n");
		fflush(stderr);
		return NULL;
	}
	
	memcpy(schedina_serializzata, buffer, contatore);
	*len = (uint16_t)contatore;
	
	return schedina_serializzata;

}

/* Deserializza una schedina
 * 
 * @str stringa da deserializzare
 * @quanti_byte_letti quanti byte sono stati deserializzati
 * 
 * @return schedina deserializzata
 */
struct schedina deserializza_schedina_txt (char* str, int* quanti_byte_letti)
{
	int i, contatore = 0, char_letti;
	struct schedina sched;
	
	sscanf(str + contatore, "%i %n", &sched.quanteRuote, &char_letti);
	contatore += char_letti;
	
	sched.ruote = malloc(sched.quanteRuote * sizeof(int));
	
	for (i = 0; i < sched.quanteRuote; ++i) {
		sscanf(str + contatore, "%i %n", &sched.ruote[i], &char_letti);
		contatore += char_letti;
	}
	
	sscanf(str + contatore, "%i %n", &sched.quantiNumeri, &char_letti);
	contatore += char_letti;
	
	sched.numeriGiocati = malloc(sched.quantiNumeri * sizeof(int));
	
	for (i = 0; i < sched.quantiNumeri; ++i) {
		sscanf(str + contatore, "%i %n", &sched.numeriGiocati[i], &char_letti);
		contatore += char_letti;
	}
	
	sscanf(str + contatore, "%i %n", &sched.quantiImporti, &char_letti);
	contatore += char_letti;
	
	sched.importi = malloc(sched.quantiImporti * sizeof(double));
	
	for (i = 0; i < sched.quantiImporti; ++i) {
		sscanf(str + contatore, "%lf %n", &sched.importi[i], &char_letti);
		contatore += char_letti;
	}
	
	*quanti_byte_letti = contatore;
	
	return sched;
}

//
// FUNZIONI DI CONVERSIONE
//
/* Converte il nome di una ruota in un indice numerico
 * 
 * @str nome da convertire
 * 
 * @return codice numerico se la stringa e' riconosciuta, -1 altrimenti
 */
int convertiRuotaStringToInt (char* str)
{
	if (!strcmp(str, S_BARI)) return BARI;
	if (!strcmp(str, S_CAGLIARI)) return CAGLIARI;
	if (!strcmp(str, S_FIRENZE)) return FIRENZE;
	if (!strcmp(str, S_GENOVA)) return GENOVA;
	if (!strcmp(str, S_MILANO)) return MILANO;
	if (!strcmp(str, S_NAPOLI)) return NAPOLI;
	if (!strcmp(str, S_PALERMO)) return PALERMO;
	if (!strcmp(str, S_ROMA)) return ROMA;
	if (!strcmp(str, S_TORINO)) return TORINO;
	if (!strcmp(str, S_VENEZIA)) return VENEZIA;
	if (!strcmp(str, S_NAZIONALE)) return NAZIONALE;
	return -1;
}

/* Inserisce un elemento schedina_list in coda ad una lista, utilizzando la tecnica del doppio puntatore
 * 
 * @lista lista degli elementi
 * @elem elemento da inserire
 */
void inserisci_lista_schedina (struct schedina_list** lista, struct schedina_list* elem)
{
	struct schedina_list* p1, * p2;
	
	p1 = *lista;
	p2 = NULL;
	
	elem->next = NULL;

	while (p1 != NULL) {
		p2 = p1;
		p1 = p1->next;
	}
	
	if (p2 == NULL) {
		*lista = elem;
	}
	else {
		p2->next = elem;
	}
}
