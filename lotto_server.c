#include "lotto.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 2048

// Costanti temporali lato server {
	#define PERIODO_ESTRAZIONE 5
	#define SECONDI_IN_UN_MINUTO 60
// }

// Connessione TCP {
	#define LUNGHEZZA_BACKLOG 10
// }

// Sezione FILE {
	#define CARTELLA_FILES "./files"

	/* Formato record di FILE_UTENTI
	 * -----------------------------------------------------------------------
	 * |  utente (stringa)  | " " (char) |  password (stringa)  | " " (char) |
	 * -----------------------------------------------------------------------
	 */
	#define FILE_UTENTI CARTELLA_FILES"/utenti.txt"

	/* Formato record di FILE_CLIENT_BLOCCATI
	 * -----------------------------------------------
	 * |  ip address (in_addr)  | timestamp (time_t) |
	 * -----------------------------------------------
	 */
	#define FILE_CLIENT_BLOCCATI CARTELLA_FILES"/client_bloccati.bin"

	#define FILE_ESTRAZIONI CARTELLA_FILES"/estrazioni.bin"
	#define LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA   (sizeof(uint8_t)+QUANTI_NUMERI_ESTRATTI*sizeof(uint32_t))
	#define LUNGHEZZA_BLOCCO_ESTRAZIONE   (sizeof(time_t)+LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA*QUANTE_RUOTE)

	/* Formato record dei file schedina degli utenti
	 * -----------------------------------------------------------------------
	 * |  timestamp (time_t)  | ' ' | schedina serializzata (stringa) | '|'  |
	 * -----------------------------------------------------------------------
	 */

	/* Il file %utente%_schedine.bin ha uno header composto da due campi da 4 byte ciascuno.
	 * 
	 * Il primo campo e' l'offset  dell'insieme di schedine di tipo 1 (ovvero che non hanno subito un'estrazione.
	 * Questo campo viene aggiornato dalla routine di estrazione dei numeri
	 * 
	 * Il secondo campo e' l'offset dell'insieme di schedine (estratte o meno) su cui non e' stata verificata la vincite,
	 * ovvero che sono state inserite nel sistema DOPO l'ultima chiamata del comando !vedi_vincite da parte dell'utente.
	 * Questo campo viene aggiornato dalla ruotine eseguiVediVinci(...)
	 */
	#define LUNGHEZZA_HEADER_SCHEDINE_BIN 8
// }

int estrazione_completata = 0; // flag per viene settato alla notifica della conclusione di un'estrazione

//////////////////////////////////////////////
//			COMUNICAZIONE SU SOCKET			//
//////////////////////////////////////////////
/* Attende un messaggio da un client gia' connesso e scrive il messaggio nel buffer passato
 * 
 * @socket descrittore socket
 * @buffer indirizzo del buffer
 * @presentationClientAddress indirizzo IP del client in formato presentazione
 * 
 * @return -1 in caso di errore, 0 se il client chiude la connessione, la lunghezza del messaggio altrimenti
 */
int attendiMessaggioDalClient(const int socket, void** buffer, const char* presentationClientAddress)
{
	int ret;
	uint16_t len;

	// Ricevi la lunghezza del messaggio
	ret = recv(socket, &len, sizeof(len), MSG_WAITALL);
	if (ret < 0) {
		perror("Receive per la lunghezza del messaggio fallita");
		return -1;
	}
	if (ret == 0) { // Chiusura connessione da lato client
		return 0;
	}
	printf("Client %s, socket %d: ricevuta lunghezza messaggio\n", presentationClientAddress, socket);
	fflush(stdout);

	// Converti la lunghezza in formato host
	len = ntohs(len);

	*buffer = malloc(len);
	
	// Ricevi il messaggio
	ret = recv(socket, *buffer, len, MSG_WAITALL);
	if (ret < 0) {
		perror("Receive per il messaggio fallito");
		free(*buffer);
		*buffer = NULL;
		return -1;
	}
	if (ret == 0) { // Chiusura connessione da lato client
		return 0;
	}
	printf("Client %s, socket %d: ricevuto messaggio\n", presentationClientAddress, socket);
	fflush(stdout);

	return len;
}
/* Invia un messaggio su una connessione TCP aperta.
 * Il protocollo prevede che venga inviata prima la lunghezza del messaggio
 * 
 * @socket descrittore del socket attraverso cui inviare il messaggio
 * @msg indirizzo del messaggio da inviare
 * @lenmsg lunghezza del messaggio
 * 
 * @return -1 se fallisce, 1 altrimenti
 */
int invia (const int socket, void* msg, const uint16_t lenmsg)
{
	int ret;
	uint16_t len;
	
	len = htons(lenmsg);	// conversione in network format per ottenere architecture indipendence
	
	// Invio della lunghezza del messaggio
	ret = send(socket, &len, sizeof(len), 0);
	if (ret < 0) {
		perror("Errore in fase d'invio della lunghezza");
		return -1;
	}
	
	// Invio del messaggio
	ret = send(socket, msg, lenmsg, 0);
	if (ret < 0) {
		perror("Errore in fase d'invio del messaggio");
		return -1;
	}
	
	return 1;
}

/* Invia un messaggio contenente dati ad un client.
 * Il protocollo prevede di incapsulare il messaggio da inviare con un'intestazione di un byte
 * che segnala che il corpo contiene dati
 * 
 * @socket descrittore del socket su cui inviare il messaggio
 * @msg indirizzo del messaggio da inviare
 * @lenmsg lunghezza del messaggio
 * 
 * @return -1 se fallisce, 1 altrimenti
 */
int inviaDati (const int socket, const void* msg, const uint16_t lenmsg)
{
	uint8_t buffer[lenmsg + 1];

	buffer[0] = (uint8_t)DATI;
	memcpy(buffer + 1, msg, lenmsg);
	
	return invia(socket, buffer, lenmsg + 1);
}

/* Invia un messaggio di errore ad un client.
 * Il protocollo prevede di incapsulare il messaggio da inviare con un'intestazione di un byte
 * che segnala che il corpo contiene un messaggio di errore
 * 
 * @socket descrittore del socket su cui inviare il messaggio
 * @tipo tipo di errore (vedere sezione apposita in costanti.h)
 * 
 * @return -1 se fallisce, 1 altrimenti
 */
int inviaErrore (const int socket, const uint8_t tipo)
{
	const uint16_t lenmsg = 2;
	uint8_t buffer[lenmsg];

	buffer[0] = (uint8_t)ERR;
	buffer[1] = tipo;

	return invia(socket, buffer, lenmsg);
}

//////////////////////////////////////////////
//			FUNZIONI DI UTILITA'			//
//////////////////////////////////////////////
/* Confronta due interi secondo l'ordine crescente.
 * Utilizzata da qsort(...)
 */
int ordine_crescente (const void* a, const void* b)
{
	return (*(int*)a - *(int*)b);
}

/* Restituisce il maggiore tra gli interi a e b
 */
int max (int a, int b)
{
	return (a >= b) ? a : b;
}

/* Restituisce il minore tra gli interi a e b
 */
int min (int a, int b)
{
	return (a <= b) ? a : b;
}

/* Dato l'indice di una puntata (0: ESTRATTO, 1: AMBO...), restituisce il "moltiplicatore del premio",
 * ovvero il valore della vincita per ogni Euro giocato (supponendo che l'importo della giocata 
 * non venga suddiviso in sotto-importi). Rappresenta la funzione di conversione della Tabella 1
 * a pagina 2 delle specifiche di progetto.
 *
 * @p indice della vincita/puntata
 *
 * @return valore della vincita per ogni euro giocato
 */
double moltiplicatorePremio (int p)
{
	switch (p) {
		case 0: return PREMIO_SINGOLO;
		case 1: return PREMIO_AMBO;
		case 2: return PREMIO_TERNO;
		case 3: return PREMIO_QUATERNA;
		case 4: return PREMIO_CINQUINA;
		default: return 1;
	}
}

/* Calcola il coefficiente binomiale con parametri n e k
 * Formula originale --> ( n! ) / ( k! * (n-k)! )
 * che semplificanso diventa --> ( n * (n-1) * (n-2) * ... * (n-k+1) ) / ( k * (k-1) * ... * 2 * 1)
 */
double coefficienteBinomiale (unsigned int n, unsigned int k)
{
	unsigned int i;
	unsigned int sopra = 1, sotto = 1;
	
	if (k > n) return -1;
	
	for (i = 0; i < k; ++i) {
		sopra *= (n-i);
		sotto *= (i+1);
	}
	
	return ((double)sopra)/((double)sotto);
}

//////////////////////////////////////////////
//			SERVIZI PER L'UTENTE			//
//////////////////////////////////////////////
/* Genera una stringa di caratteri alfanumerici lunga LUNGHEZZA_SESSION_ID
 * @buffer indirizzo ad un array di caratteri lungo LUNGHEZZA_SESSION_ID + 1 (NULL terminator)
 */
void generaSessionId (char* buffer)
{
	int i;
	
	for (i = 0; i < LUNGHEZZA_SESSION_ID; ++i) {
		// Genera un numero casuale nell'intervallo [0, QUANTI_CARATTERI_ALFANUMERICI].
		// Ogni numero dell'intervallo e' in corrispondenza biunivoca con un elemento
		// dell'unione ordinata degli intervalli [0, 9]+[a,z]+[A,Z]
		int random = rand() % QUANTI_CARATTERI_ALFANUMERICI;
		
		if (random < QUANTE_CIFRE) {
			// E' una cifra
			buffer[i] = (char)(random + ASCII_PRIMA_CIFRA);
			continue;
		}
		
		random -= QUANTE_CIFRE;
		if (random < QUANTE_LETTERE) {
			// E' una lettera minuscola
			buffer[i] = (char)(random + ASCII_PRIMA_LETTERA_MINUSCOLA);
			continue;
		}
		
		random -= QUANTE_LETTERE;
		if (random < QUANTE_LETTERE) {
			// E' una lettera maiuscola
			buffer[i] = (char)(random + ASCII_PRIMA_LETTERA_MAIUSCOLA);
			continue;
		}
	}
	
	buffer[LUNGHEZZA_SESSION_ID] = '\0';
}

/* Cerca uno username dato nel file FILE_UTENTI
 * 
 * @username stringa che contiene lo username da cercare
 * @password stringa in cui scrivere la password dell'utente, se esiste.
 *	Se e' NULL, la funzione non copia la password (utile per signup)
 * 
 * @return 1 se lo username esiste, 0 se lo username non esiste, -1 in caso di errore
 */
int cercaUsername (char* username, char* password)
{
	FILE* fileUtenti;
	int ret;
	
	// Apertura in lettura del file FILE_UTENTI
	fileUtenti = fopen(FILE_UTENTI, "r");
	if (!fileUtenti) {
		perror("Impossibile aprire file");
		return -1;
	}
	
	// Ricerca di username e password sul file FILE_UTENTI
	while (1){
		char temp_password[512], temp_username[512];
		
		ret = fscanf(fileUtenti, "%s %s ", temp_username, temp_password);
		
		// End Of File, non e' stato trovato lo username
		if (ret <= 0 || ret == EOF) {
			fclose(fileUtenti);
			return 0;
		}
		
		// Confronta lo username trovato con quello dato
		if (strcmp(username, temp_username) == 0) {
			fclose(fileUtenti);
			if (password) {
				strcpy(password, temp_password);
			}
			return 1;
		}
	}
}

/* Effettua il login di un utente sul server.
 * Controlla che lo username esista e che la password sia corretta.
 * Da' ad un utente 3 possibilita' di inserire la password. Al terzo fallimento di accesso
 *		blocca per MINUTI_DI_BLOCCO_IP (30) minuti l'indirizzo IP di tale client.
 * Genera un session ID casuale e lo invia al client.
 * 
 * @socket descrittore del socket su cui avviene la comuncazione client-server
 * @clientAddr indirizzo del socket client
 * @msg indirizzo al messaggio applicativo nel seguente formato:
 *		---------------------------------------------------
 *		|  username  |  ' ' (spazio)  |  password  | '\\0' |
 *		---------------------------------------------------
 * @msgLen lunghezza di msg
 * @user puntatore in cui inserire l'indirizzo del nome utente allocato dinamicamente
 * @sessionId puntatore ad una stringa in cui memorizzare il sessionId generato
 * @accessiFalliti numero degli accessi falliti in questa connessione
 * 
 * @return 1 se il login ha successo, 0 se il login non ha successo per colpa del client, -1 in caso di errore
 */
int effettuaLogin (const int socket, const struct sockaddr_in clientAddr, char* msg, size_t msgLen,
					char** user, char* sessionId, uint8_t* accessiFalliti)
{
	int ret;
	
	// Variabili di appoggio per effettuare la decodifica del messaggio
	char utente[512], password[512], temp_password[512];
	char* temp = NULL;
	const char delimiter = ' ';
	
	// Variabili per navigazione file
	FILE* clientBloccati;
	struct stat info;
	
	// Tempo
	time_t current_time;
	
	time(&current_time);
	
	//
	// CONTROLLA SE IP E' BLOCCATO
	//	
	// Controlla dimensione file bloccato
	ret = stat(FILE_CLIENT_BLOCCATI, &info);
	
	if (info.st_size > 0) {
		// Apre in lettura il file, che verra' letto dall'ultima posizione verso la prima
		// (ovvero dal record piu' recente, al meno recente) in modo da ottimizzare la ricerca
		clientBloccati = fopen(FILE_CLIENT_BLOCCATI, "rb");
		if (!clientBloccati) {
			perror("Impossibile aprire file");
			return -1;
		}
		
		// Muovi il cursore fino all'ultimo "record" del file
		// (per il formato, vedere documentazione nella sezione FILE dell'area dei #define, inizio file sorgente)
		ret = fseek(clientBloccati, -(sizeof(struct in_addr) + sizeof(time_t)), SEEK_END);
		if (ret < 0) {
			perror("Impossibile raggiungere fine file");
			return -1;
		}
	
		// Legge il file al contrario
		while (1) {
			struct in_addr addr;
			time_t timestamp;
			int fine_file = 0;
			
			// Se abbiamo raggiunto la testa del file, allora questa e' l'ultima lettura
			if (ftell(clientBloccati) == 0) {
				fine_file = 1;
			}

			fread(&addr, sizeof(struct in_addr), 1, clientBloccati);
			fread(&timestamp, sizeof(time_t), 1, clientBloccati);

			// Se i record rimasti da leggere sono fuori dalla fascia temporale di blocco (MINUTI_DI_BLOCCO_IP minuti),
			// e' inutile continuare
			if (difftime(current_time, timestamp) > (MINUTI_DI_BLOCCO_IP * SECONDI_IN_UN_MINUTO)) {
				break;
			}

			// Controlla se l'IP del client e' attualmente bloccato
			if (addr.s_addr == clientAddr.sin_addr.s_addr) {
				inviaErrore(socket, IP_BLOCCATO);
				fclose(clientBloccati);
				return 0;
			}
			
			if (fine_file) {
				break;
			}
			
			// Sposta il cursore all'inizio del record precedente
			fseek(clientBloccati, -2*(sizeof(struct in_addr) + sizeof(time_t)), SEEK_CUR);
		}
		fclose(clientBloccati);
	}
	
	//
	// ESTRAZIONE DATI DAL MESSAGGIO
	//
	// Estrazione username
	temp = strsep(&msg, &delimiter);
	strcpy(utente, temp);
	
	// Estrazione password
	strcpy(password, msg);
	
	//
	// CONTROLLO USERNAME E PASSWORD
	//
	ret = cercaUsername(utente, temp_password);
	if (ret < 0) {
		return -1;
	}
	
	// Username o password sbagliati
	if (ret == 0 || strcmp(password, temp_password) != 0) {
		(*accessiFalliti)++;
		
		if (*accessiFalliti >= 3) {
			inviaErrore(socket, TERZO_LOGIN_ERRATO);

			// Blocca il client: scrive indirizzo IP e timestamp nel file FILE_CLIENT_BLOCCATI
			clientBloccati = fopen(FILE_CLIENT_BLOCCATI, "ab");
			if (!clientBloccati) {
				perror("Impossibile aprire file per bloccare client");
				return -1;
			}
			fwrite(&clientAddr.sin_addr, sizeof(clientAddr.sin_addr), 1, clientBloccati);
			fwrite(&current_time, sizeof(current_time), 1, clientBloccati);
			fclose(clientBloccati);
		}
		else {
			inviaErrore(socket, LOGIN_ERRATO);
		}
		return 0;
	}
	
	//
	// GENERA VARIABILI DI SESSIONE
	//
	// Memorizza utente
	*user = (char*)malloc(strlen(utente) + 1);
	if (*user == NULL) {
		perror("malloc fallita");
		return -1;
	}
	strcpy(*user, utente);
	
	generaSessionId(sessionId);
	
	// Invia sessionId al client
	inviaDati(socket, sessionId, LUNGHEZZA_SESSION_ID + 1);
	return 1;
}

/* Effettua la registazione di un utente.
 * Controlla che il nome utente scelta non esista gia'.
 * NON effettua il login automatico.
 * 
 * @socket descrittore del socket su cui avviene la comunicazione client-server
 * @presentationClientAddress indirizzo del socket client in formato presentazione
 * @msg indirizzo al messaggio applicativo nel seguente formato:
 *		---------------------------------------------------
 *		|  username  |  ' ' (spazio)  |  password  | '\0' |
 *		---------------------------------------------------
 * @msgLen lunghezza di msg
 * 
 * @return 1 se la registrazione ha successo, 0 se la registrazione fallisce per colpa del client,
 *    -1 in caso di errore interno o se il client chiude la connessione
 */
int effettuaSignup (const int socket, const char* presentationClientAddress, char* msg, const size_t msgLen)
{
	int ret;
	FILE* fileUtente;
	char messaggioAlClient[3];

	
	/* Struttura dei file di registro nomeutente_schedine.bin
	 * 
	 * I primi 8 byte costituiscono lo header (due campi da 4 byte).
	 * Il primo campo e' l'offset  dell'insieme di schedine di tipo 1 (ovvero che non hanno subito un'estrazione.
	 * Il secondo campo e' l'offset dell'insieme di schedine (estratte o meno) su cui non e' stata verificata la vincite,
	 * ovvero che sono state inserite nel sistema DOPO l'ultima chiamata del comando !vedi_vincite da parte dell'utente.
	 * 
	 * Ogni record contiene una schedina serializzata con un text protocol
	 */
	FILE* fileRegistro;
	char indirizzo_file_registro[128];
	uint32_t header_file_registro = LUNGHEZZA_HEADER_SCHEDINE_BIN; // alla creazione i due campi "puntano" alla fine del file
	
	// Variabili per parse del messaggio
	char utente[512], password[512];
	char* temp;
	char delimiter = ' ';
	
	//
	// ESTRAZIONE DATI DAL MESSAGGIO
	//
	// Estrazione username
	temp = strsep(&msg, &delimiter);
	strcpy(utente, temp);
	
	// Estrazione password
	strcpy(password, msg);
	
	while (1) { 
		char* messaggio = NULL;
		
		// CONTROLLA SE ESISTE GIA' LO USERNAME
		ret = cercaUsername(utente, NULL);
		if (ret < 0) {
			return -1;
		}
		if (ret == 0) {	// lo username e' unico
			break;
		}
		
		// Segnala al client che lo username scelto e' occupato e richiedine un altro (la password rimane la stessa)
		inviaErrore(socket, USERNAME_OCCUPATO);
		
		ret = attendiMessaggioDalClient(socket, (void**)&messaggio, presentationClientAddress);
		if (ret <= 0) return -1;
		
		temp = &messaggio[1];	// salta l'intestazione del messaggio
		strcpy(utente, temp);
	}
	
	//
	// REGISTRA UTENTE SU CLIENT
	//
	fileUtente = fopen(FILE_UTENTI, "a");
	if (!fileUtente) {
		perror("Impossibile aprire file utenti");
		return -1;
	}
	fprintf(fileUtente, "%s %s ", utente, password);
	fclose(fileUtente);
	
	// Creazione files di registro
	sprintf(indirizzo_file_registro, "%s/%s_schedine.bin", CARTELLA_FILES, utente);
	fileRegistro = fopen(indirizzo_file_registro, "wb");
	// Scrittura header:
	// 1) Offset del blocco delle schedine giocate di tipo 1 (non ancora estratte)
	fwrite(&header_file_registro, sizeof(header_file_registro), 1, fileRegistro);
	// 2) Offset del blocco delle schedine (estratte o meno) di cui non e' stata controllata la vincita
	fwrite(&header_file_registro, sizeof(header_file_registro), 1, fileRegistro);
	fclose(fileRegistro);

	sprintf(indirizzo_file_registro, "%s/%s_vincite.txt", CARTELLA_FILES, utente);
	fileRegistro = fopen(indirizzo_file_registro, "w");	// Crea il file senza scrivere nulla
	fclose(fileRegistro);
	
	strcpy(messaggioAlClient, "OK");
	inviaDati(socket, messaggioAlClient, 3);
	return 1;
}

/* Esegui il comando !invia_giocata <schedina>
 * 
 * @socket descrittore del socket su cui avviene la comuncazione client-server
 * @clientAddr indirizzo del socket client
 * @msg indirizzo al messaggio applicativo nel seguente formato:
 *		---------------------------------------------------------
 *		| session_id (stringa + '\\0') | schedina (serializzata) |
 *		---------------------------------------------------------
 * @msg_len lunghezza di msg
 * @user nome dell'utente
 * 
 * @return 1 se il comando ha successo, 0 se fallisce per colpa del client, -1 in caso di errore interno
 */
int eseguiInviaGiocata (const int socket, const char* msg, const size_t msg_len, char* user)
{
	char indirizzo_file_registro[128];
	const char messaggio_al_client[3] = "OK";
	
	// File
	FILE* file_registro;
	
	// Tempo
	time_t timestamp;	// servira' per determinare l'estrazione corrispondente alla schedina
	
	time(&timestamp);
	
	// Configurazione dell'indirizzo del file di registro
	sprintf(indirizzo_file_registro, "%s/%s_schedine.bin", CARTELLA_FILES, user);
	
	// Memorizzazione delle schedina serializzata con timestamp di ricezione
	file_registro = fopen(indirizzo_file_registro, "a");
	if (!file_registro) {
		perror("Impossibile aprire file schedine utente");
		inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	fprintf(file_registro, "%ld %s|", (long int)timestamp, msg + LUNGHEZZA_SESSION_ID + 1);
	fclose(file_registro);
	
	return inviaDati(socket, messaggio_al_client, strlen(messaggio_al_client)+1);
}

/* Esegui il comando !vedi_giocate <tipo>
 * Invia al client tutte le schedine del tipo <tipo>, serializzate
 * <tipo> 0: giocate relative a estrazioni gia' effettuate
 * <tipo> 1: giocate in attesa della prossima estrazione
 * 
 * @socket descrittore del socket su cui avviene la comuncazione client-server
 * @msg indirizzo al messaggio applicativo nel seguente formato:
 *		------------------------------------------------
 *		| session_id (stringa + '\\0') | tipo (uint8_t) |
 *		------------------------------------------------
 * @msg_len lunghezza di msg
 * @user nome dell'utente
 * 
 * @return 1 se il comando ha successo, 0 se fallisce per colpa del client, -1 in caso di errore interno
 */
int eseguiVediGiocate (const int socket, const char* msg, const size_t msg_len, char* user)
{
	int ret;
	uint8_t tipo;
	
	char* messaggio_al_client;
	
	// File
	FILE* file_registro;
	struct stat info;
	uint32_t offset, quanti_byte = 0, byte_da_inviare, byte_letti;
	char indirizzo_file[512];
	
	if (msg_len < LUNGHEZZA_SESSION_ID + 1 + sizeof(uint8_t)) {
		ret = inviaErrore(socket, MESSAGGIO_NON_COMPRENSIBILE);
		return (ret == -1) ? -1 : 0;
	}
	
	// Leggi tipo
	tipo = *(uint8_t*)(msg + LUNGHEZZA_SESSION_ID + 1);
	
	// Apre il file schedine in modalita' binaria
	sprintf(indirizzo_file, "%s/%s_schedine.bin", CARTELLA_FILES, user);
	file_registro = fopen(indirizzo_file, "rb");
	if (!file_registro) {
		perror("Impossibile aprire file schedine");
		inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	
	ret = stat(indirizzo_file, &info);
	if (ret < 0) {
		perror("Impossibile leggere dimensione file schedine");
		inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	
	// Inizializza offset e quanti_byte a seconda del tipo di schedine richiesto
	// e utilizzando il primo campo dello header (che contiene l'offset in cui inizia
	// la sezione delle schedine di tipo 1, ovver quelle non ancora estratte)
	if (tipo == 0) {
		offset = LUNGHEZZA_HEADER_SCHEDINE_BIN;
		// il primo campo dello header del file contiene l'offset da cui iniziano le schedine di tipo 1
		fread(&quanti_byte, sizeof(quanti_byte), 1, file_registro);
		quanti_byte -= LUNGHEZZA_HEADER_SCHEDINE_BIN;
	}
	else if (tipo == 1) {
		// lo header del file contiene l'offset da cui iniziano le schedine di tipo 1
		fread(&offset, sizeof(offset), 1, file_registro);
		quanti_byte = info.st_size - offset;
	}
	else {	// errore: il messaggio non e' comprensibile
		fclose(file_registro);
		ret = inviaErrore(socket, MESSAGGIO_NON_COMPRENSIBILE);
		return (ret == -1) ? -1 : 0;
	}

	fclose(file_registro);
	
	// Se non ci sono schedine, invia un alert di tipo FILE_VUOTO
	if (quanti_byte == 0) {
		ret = inviaErrore(socket, FILE_VUOTO);
		return (ret < 0) ? -1 : 1;
	}
	
	// Apri in lettura il file schedine come file di testo
	file_registro = fopen(indirizzo_file, "r");
	if (!file_registro) {
		perror("Impossibile aprire in lettura file schedine utente");
		inviaErrore(socket, MESSAGGIO_NON_COMPRENSIBILE);
		return -1;
	}
	messaggio_al_client = malloc(quanti_byte);
	memset(messaggio_al_client, 0, quanti_byte);
	
	// Posiziona il cursore all'inizio dell'area di lettura
	fseek(file_registro, offset, SEEK_SET);
	
	byte_da_inviare = 0;	// byte effettivamente da inviare (senza timestamp)
	byte_letti = 0;			// byte letti (contatore)
	while (byte_letti < quanti_byte) {
		int s_start, s_end; // Offset di inizio e fine della schedina relativamente al record
		int next; // Quanti byte compongono il record
		long int temp;
		
		// [Formato record] --> documentazione nella sezione FILE dell'area dei #define (inizio codice sorgente)
		fscanf(file_registro, "%ld %n%[^|]%n|%n", &temp, &s_start, messaggio_al_client + byte_da_inviare, &s_end, &next);

		byte_da_inviare += (s_end - s_start);
		byte_letti += next;
	}
	fclose(file_registro);
	
	messaggio_al_client[byte_da_inviare++] = '\0';
	
	ret = inviaDati(socket, messaggio_al_client, (uint16_t)byte_da_inviare);
	free(messaggio_al_client);
	return ret;
}

/* Esegui il comando !vedi_estrazione <n> <ruota>
 * Invia al client i numeri estratti nelle ultime <n> estrazioni, sulla ruota <ruota> ricevuta
 * Se la ruota non e' stata specificata, il server invia le informazioni di tutte le ruote.
 * 
 * @socket descrittore del socket su cui avviene la comuncazione client-server
 * @msg indirizzo al messaggio applicativo nel seguente formato:
 *		-----------------------------------------------------------------
 *		| session_id (stringa + '\\0') | n (uint32_t) | ruota (uint8_t) |
 *		-----------------------------------------------------------------
 * @msg_len lunghezza di msg
 * 
 * @return 1 se il comando ha successo, 0 se fallisce per colpa del client, -1 in caso di errore interno
 */
int eseguiVediEstrazione (const int socket, const char* msg, const size_t msg_len)
{
	int ret, i;
	uint32_t n;
	uint8_t ruota;
	size_t len_messaggio_al_client;	// lunghezza del messaggio al client
	uint16_t quanti_byte = 0;		// "cursore" per scrivere i dati sul messaggio al client
	
	uint8_t* messaggio_al_client;	// contiene i risultati delle ultime estrazioni in formato binario
	
	// File
	FILE* file_estrazione;
	struct stat info;
	
	// Controllo lunghezza messaggio
	if (msg_len < LUNGHEZZA_SESSION_ID + 1 + sizeof(uint32_t) + sizeof(uint8_t)) {
		ret = inviaErrore(socket, MESSAGGIO_NON_COMPRENSIBILE);
		return (ret == -1) ? -1 : 0;
	}
	
	// Lettura degli argomenti del comando
	n = *(uint32_t*)(msg + LUNGHEZZA_SESSION_ID + 1);
	n = ntohl(n);
	ruota = *(uint8_t*)(msg + LUNGHEZZA_SESSION_ID + 1 + sizeof(uint32_t));
	
	// Controllo di <n> e <ruota>
	if (n == 0 || (ruota != RUOTA_NON_SPECIFICATA && ruota >= QUANTE_RUOTE)) {
		ret = inviaErrore(socket, MESSAGGIO_NON_COMPRENSIBILE);
		return (ret == -1) ? -1 : 0;
	}
	
	// Creazione del messaggio.
	// Se la ruota non e' specificata, e' necessario inviare tutte le estrazioni di tutte le ruote
	len_messaggio_al_client = (size_t)(n * LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA * ((ruota == RUOTA_NON_SPECIFICATA) ? QUANTE_RUOTE : 1));
	messaggio_al_client = malloc(len_messaggio_al_client);
	memset(messaggio_al_client, 0, len_messaggio_al_client);
	
	// Apertura file in lettura
	file_estrazione = fopen(FILE_ESTRAZIONI, "rb");
	if (!file_estrazione) {
		ret = inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	ret = stat(FILE_ESTRAZIONI, &info);
	
	// Caso con ruota specificata
	if (ruota != RUOTA_NON_SPECIFICATA && info.st_size > 0) {
		// Raggiunge l'ultima estrazione.
		//
		/* Le estrazioni vengono memorizzate a blocchi di "ruote estratte" di dimensione LUNGHEZZA_RECORD_ESTRAZIONE.
		 * Un record di estrazione puo' essere scomposto nel timestamp e nelle 
		 * estrazioni vere e proprie (che formano un sottoblocco).
		 * La variabile ruota indica l'indice della ruota all'interno del sottoblocco delle estrazioni a partire dall'alto.
		 * 
		 * Percio' la distanza dal basso nel blocco di un estrazione singola e'
		 * (QUANTE_RUOTE - 1 - ruota) * LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA.
		 * 
		 * Inoltre, bisogna necessariamente tornare indietro di un estrazione singola
		 * poiche' all'apertura del file il cursore e' in cima al file e non in fondo.
		 */
		fseek(file_estrazione, -(LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA * (QUANTE_RUOTE - ruota)), SEEK_END);
		
		for (i = 0; i < n; ++i) {
			// Legge un record di estrazione della ruota
			fread(messaggio_al_client + quanti_byte, LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA, 1, file_estrazione);
			quanti_byte += LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA;

			// Controlla se siamo arrivati all'estrazione di testa
			if (ftell(file_estrazione) <= LUNGHEZZA_BLOCCO_ESTRAZIONE) {
				break;
			}
			
			// Porta il cursore indietro di un intero blocco
			fseek(file_estrazione, -LUNGHEZZA_BLOCCO_ESTRAZIONE - LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA, SEEK_CUR);
		}
	}
	// Caso con ruota non specificata
	else if (ruota == RUOTA_NON_SPECIFICATA && info.st_size > 0) {
		// Porta il cursore all'ultimo "blocco di record di estrazione"
		//
		/* Le estrazioni vengono memorizzate a blocchi di "ruote estratte" di dimensione LUNGHEZZA_RECORD_ESTRAZIONE.
		 */
		fseek(file_estrazione, -(LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA * QUANTE_RUOTE), SEEK_END);
		
		for (i = 0; i < n; ++i) {
			// Legge tutti i record dell'estrazione
			fread(messaggio_al_client + quanti_byte, LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA, QUANTE_RUOTE, file_estrazione);
			quanti_byte += LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA * QUANTE_RUOTE;
			
			// Controlla se siamo arrivati all'estrazione di testa
			if (ftell(file_estrazione) <= LUNGHEZZA_BLOCCO_ESTRAZIONE) {
				break;
			}
			
			// Porta il cursore all'estrazione precedente
			fseek(file_estrazione, -LUNGHEZZA_BLOCCO_ESTRAZIONE * 2 + sizeof(time_t), SEEK_CUR);
		}
	}
	else {
		// Il file e' vuoto. Il server lo notifica il client
		fclose(file_estrazione);
		free(messaggio_al_client);
		ret = inviaErrore(socket, FILE_VUOTO);
		return 1;
	}
	
	fclose(file_estrazione);
	
	// Conversione dell'estrazione in formato network
	
	i = 1; // salta il primo byte (tipo della ruota)
	
	while (i < quanti_byte) {
		int j;
		uint32_t numero;
		
		for (j = 0; j < QUANTI_NUMERI_ESTRATTI; ++j) {
			numero = *(uint32_t*)(messaggio_al_client + i + j * sizeof(uint32_t)); // recupero il numero estratto
			numero = htonl(numero);
			*(uint32_t*)(messaggio_al_client + i + j * sizeof(uint32_t)) = numero;
		}
		
		i += (QUANTI_NUMERI_ESTRATTI * sizeof(uint32_t) + 1);	// salta alla prossima estrazione, 
																// saltandone il primo byte (tipo della ruota, uint8_t)
	}
	
	// Invia dati al client
	ret = inviaDati(socket, messaggio_al_client, (uint16_t)quanti_byte);
	free(messaggio_al_client);
	return 1;
}

////////////////////////////////////////////////////////////////////////////
//						ESEGUI VEDI_VINCITE								////
////////////////////////////////////////////////////////////////////////////

/* Data una lista di schedine e un'estrazione completa (tutte le ruote),
 * elabora le vincite e le memorizza nel file file_vincite
 * 
 * @sched lista delle schedine da elaborare
 * @estrazioni_array estrazione da elaborare. Ogni estrazione deve avere i numeri ordinati in ordine crescente.
 * @timestamp timestamp dell'estrazione
 * @file_vincite puntatore descrittore del registro delle vincite su cui scrivere
 */
void elaboraVincitaSchedinaConEstrazione (struct schedina_list* sched, struct estrazione estrazioni_array[],
												time_t timestamp, FILE* file_vincite)
{
	int i;
	int quantiImporti = sched->s.quantiImporti,
		quantiNumeri = sched->s.quantiNumeri,
		quanteRuote = sched->s.quanteRuote;
	
	struct vincita vincita_temp;	// per memorizzare i dati temporanei durante l'elaborazione della vincita
	int ruote_vincenti = 0;			// numero di ruote con almeno una vincita
	
	/* distribuzione_premio[i] e' pari un fattore moltiplicativo del premio dovuto alla quantita' di numeri giocati.
	 * Per ogni tipo di evento vincinte (0 = ESTRATTO SINGOLO, 1 = AMBO ...), viene calcolato quante possibili combinazioni
	 * dell'evento vincente AL MASSIMO possono manifestarsi all'interno del set dei numeri scommessi.
	 * Il che significa, per la logica combinatoria, dover calcolare il coefficiente binomiale con
	 *	n = quantita' di numeri giocati
	 *	k = i + 1
	 */
	double distribuzione_premio[quantiImporti]; 
	
	for (i = 0; i < quantiImporti; ++i) {
		distribuzione_premio[i] = coefficienteBinomiale(quantiNumeri, i+1);
	}
	
	// Ordina la schedina
	qsort(sched->s.numeriGiocati, quantiNumeri, sizeof(int), ordine_crescente);
	
	// Costruzione di vincita_temp
	costruisci_vincita(&vincita_temp, timestamp, quanteRuote);
	
	// Cerca i numeri giocati per ogni ruota dell'estrazione
	for (i = 0; i < quanteRuote; ++i) {
		uint8_t ruota = sched->s.ruote[i];
		int index_scommessa = 0, index_estrazione = 0, j = 0; // indici per navigare i vettori
		
		int numeri_comuni[max(quantiNumeri, QUANTI_NUMERI_ESTRATTI)];	// contiene gli elementi comuni 
																		// tra i numeri estratti e quelli giocati
		int quanti_numeri_comuni = 0;
		
		vincita_temp.ruote[i] = 0; // valore di default a segnalare che la ruota di questo indice non e' vincente
		
		// Cerca elementi comuni tra il vettore dei numeri scommessi e il vettore dei numeri estratti
		// considerando che entrambi sono ordinati
		while (index_scommessa < quantiNumeri && index_estrazione < QUANTI_NUMERI_ESTRATTI) { 
			
			if (sched->s.numeriGiocati[index_scommessa] < estrazioni_array[ruota].numeri[index_estrazione]) {
				index_scommessa++; 
			}
			
			else if (estrazioni_array[ruota].numeri[index_estrazione] < sched->s.numeriGiocati[index_scommessa]) {
				index_estrazione++; 
			}
			
			else {
				numeri_comuni[quanti_numeri_comuni] = sched->s.numeriGiocati[index_scommessa];
				quanti_numeri_comuni++;
				index_estrazione++;
				index_scommessa++; 
			}
		}
		
		// Non ci sono stati numeri vincenti
		if (quanti_numeri_comuni == 0) {
			continue;
		}
		
		vincita_temp.ruote[i] = ruota; // la ruota in esame e' vincente
		ruote_vincenti++;
		
		// Inserisce i numeri vincenti nella struttura vincita_temp per la successiva memorizzazione su file
		vincita_temp.numeri_vincitori[i] = malloc(sizeof(int) * quanti_numeri_comuni);
		memcpy(vincita_temp.numeri_vincitori[i], numeri_comuni, sizeof(int) * quanti_numeri_comuni);
		vincita_temp.quanti_numeri_vincitori[i] = quanti_numeri_comuni;
		
		// Si usa il minimo come dimensione perche' se l'utente scommette per un'evento che richiede alpha numeri (es: terno, 3) 
		// e ci sono solo beta numeri comuni (con beta < alpha, es: ambo, 2),
		// allora sicuramente gli eventi con indice SUPERIORE a beta non si sono verificati.
		// Inoltre, se si verifica un evento di indice beta, tutti gli eventi di indice INFERIORE a beta
		// si verificano con una frequenza pari al coefficiente binomiale (es: se vinco il terno, vinco anche 3 ambi e 3 estratti)
		vincita_temp.quanti_importi_vinti[i] = min(quantiImporti, quanti_numeri_comuni);
		
		vincita_temp.importi_vinti[i] = malloc(vincita_temp.quanti_importi_vinti[i] * sizeof(double));
		
		for (j = 0; j < vincita_temp.quanti_importi_vinti[i]; ++j) {
			/* Formula usata:
			 * > coefficienteBinomiale(quanti_numeri_comuni, j+1)
			 *		--> frequenza dell'evento j (estratto, ambo, terno...) nel set dei numeri_comuni
			 * > moltiplicatorePremio(j)
			 *		--> moltiplicatore statico dovuto al tipo di scommessa (estratto, ambo, terno...)
			 * > distribuzione_premio[j] * quanteRuote
			 *		--> il premio base per ogni euro viene equamente ripartito su ogni ruota e viene diviso 
			 *			per la frequenza per cui quell'evento j si puo' verificare nel set di numeri giocati
			 */
			vincita_temp.importi_vinti[i][j] =
				coefficienteBinomiale(quanti_numeri_comuni, j+1) // frequenza dell'evento j+1 (estratto, ambo, terno...)
																 // nel set dei numeri_comuni
				* (moltiplicatorePremio(j) / ((double)(distribuzione_premio[j] * quanteRuote))); 
		}
	}
	
	if(ruote_vincenti == 0) {
		distruggi_vincita(&vincita_temp);
		return;
	}
	
	// Stampa la vincita (deserializzandola) su file in maniera ottimizzata (ovvero solo le informazioni delle ruote vincenti)
	fprintf(file_vincite, "%ld ", timestamp);
	fprintf(file_vincite, "%i ", ruote_vincenti);
	
	for (i = 0; i < quanteRuote; ++i) {
		int scorri = 0;
		
		// Se la ruota e' zero (valore di default), allora significa che non e' vincente
		if (vincita_temp.ruote[i] == 0) {
			continue;
		}
		
		fprintf(file_vincite, "%u ", (unsigned int)vincita_temp.ruote[i]); // Numero di ruota vincente
		fprintf(file_vincite, "%i ", vincita_temp.quanti_numeri_vincitori[i]);
		
		for (scorri = 0; scorri < vincita_temp.quanti_numeri_vincitori[i]; ++scorri) {
			fprintf(file_vincite, "%i ", vincita_temp.numeri_vincitori[i][scorri]);
		}
		
		fprintf(file_vincite, "%i ", vincita_temp.quanti_importi_vinti[i]);
		
		for (scorri = 0; scorri < vincita_temp.quanti_importi_vinti[i]; ++scorri) {
			fprintf(file_vincite, "%.2lf ", vincita_temp.importi_vinti[i][scorri]);
		}
		
		fprintf(file_vincite, "|");
	}
	
	distruggi_vincita(&vincita_temp);
}

/* Verifica se le schedine della lista <schedine>, giocate dall utente <user>, hanno vinto.
 * In caso positivo, scrive le vincite sul registro vincite dell'utente
 * 
 * @schedine lista delle schedine da convalidare (ovvero determinare se vittoriose o meno)
 * @user nome dell'utente
 */
void convalidaSchedineEstratte (struct schedina_list* schedine, const char* user)
{
	int i, ret;
	
	// Variabili per file
	FILE* file_estrazioni;	// aperto in modalita' lettura binaria
	
	FILE* file_vincite;		// aperto in modalita' lettura standard
	char indirizzo_file_vincite[512];
	
	// Timestamp usate durante l'analisi delle estrazione per individuare le schedine che vi appartengono
	time_t	time1 = 0,	// timestamp dell'estrazione analizzata appena in precedenza
			time2 = 0;	// timestamp dell'estrazione in analisi

	// Apertura file_estrazioni
	file_estrazioni = fopen(FILE_ESTRAZIONI, "rb");
	fseek(file_estrazioni, 0, SEEK_SET);
	
	// Apertura file_vincite pronti per la scrittura di nuove vincite
	sprintf(indirizzo_file_vincite, "%s/%s_vincite.txt", CARTELLA_FILES, user);
	file_vincite = fopen(indirizzo_file_vincite, "r+");
	fseek(file_vincite, 0, SEEK_END);
	
	// Esplora tutte le estrazioni per cercare l'estrazione corrispondente ad ogni schedina
	while (1) {
		struct estrazione estrazioni_array[QUANTE_RUOTE];
		int estrazione_prelevata_da_file = 0;
		
		// Legge timestamp di estrazione
		ret = fread(&time2, sizeof(time2), 1, file_estrazioni);
		
		// Controlla se e' stato raggiunta la fine del file
		if (ret <= 0) {
			break;
		}
		
		// Analizza le schedine
		while (schedine != NULL) {
			// Controlla la condizione che la prima schedina della lista NON afferisce all'estrazione in esame.
			// Siccome le schedine sono passate in ordine cronologico crescente,
			// se la prima non e' afferente all'estrazione in esame (ovvero e' temporalmente successiva),
			// non lo sono neanche le schedine successive.
			if (!((time1 == 0 && schedine->timestamp <= time2) ||
				(time1 != 0 && schedine->timestamp <= time2 && schedine->timestamp > time1))) {
				break;
			}
			
			// Per evitare letture inutili o doppie, l'estrazione viene prelevata dal file al massimo una volta,
			// esclusivamente nel caso in cui si abbia trovato una schedina afferente all'estrazione
			if (estrazione_prelevata_da_file == 0) {
				// Preleva i dati dell'estrazione in esame
				for (i = 0; i < QUANTE_RUOTE; ++i) {
					int j;
					// I numeri estratti sono stati memorizzati come uin32_t per ottenere una maggior
					// indipendenza dall'architettura del server e del client (dato che i dati sulle estrazioni
					// vengono scambiati con un protocollo in binario.
					// Tuttavia, per l'analisi delle schedine, ci servono interi con segno
					uint32_t numeri_estratti_temp[QUANTI_NUMERI_ESTRATTI];
					
					fread(&estrazioni_array[i].ruota, sizeof(uint8_t), 1, file_estrazioni);
					fread(numeri_estratti_temp, sizeof(int), QUANTI_NUMERI_ESTRATTI, file_estrazioni);

					// Converti i numeri estratti in interi
					for (j = 0; j < QUANTI_NUMERI_ESTRATTI; ++j) {
						estrazioni_array[i].numeri[j] = (int)numeri_estratti_temp[j];
					}
					
					// Ordina i numeri estratti
					qsort(estrazioni_array[i].numeri, QUANTI_NUMERI_ESTRATTI, sizeof(int), ordine_crescente);
				}
				
				estrazione_prelevata_da_file = 1;
			}
			
			// Elabora le vincite e le memorizza nel file
			elaboraVincitaSchedinaConEstrazione(schedine, estrazioni_array, time2, file_vincite);
			
			// Aggiorna variabili di appoggio
			schedine = schedine->next;
		}
		
		// Non ci sono piu' schedine da analizzare
		if (schedine == NULL) {
			break;
		}
		
		// Per mantenere consistente l'accesso al file, se non sono state trovate schedine afferente all'estrazione 
		// in esame (e di conseguenza non abbiamo letto su file l'estrazione), si porta in avanti il cursore
		// in modo che sia posizionato all'inizio della estrazione successiva
		if (estrazione_prelevata_da_file ==  0) {
			fseek(file_estrazioni, LUNGHEZZA_ESTRAZIONE_SINGOLA_RUOTA * QUANTE_RUOTE, SEEK_CUR);
		}
		
		time1 = time2;
	}
	
	fclose(file_vincite);
	fclose(file_estrazioni);
}

/* Invia l'intero contenuto del registro vincite al client
 * 
 * @socket descrittore del socket su cui avviene la comunicazione
 * @user nome utente
 * 
 * @return -1 in caso di fallimento, 1 altrimenti
 */
int inviaFileVincite (const int socket, const char* user)
{
	// Variabili per la gestione del file
	FILE* file_vincite;
	struct stat info;
	char indirizzo_file_vincite[512];
	
	// Variabili per il messaggio al client
	char* messaggio;
	uint16_t lenmsg;
	
	// Apri file vincite
	sprintf(indirizzo_file_vincite, "%s/%s_vincite.txt", CARTELLA_FILES, user);
	
	file_vincite = fopen(indirizzo_file_vincite, "rb");
	if (!file_vincite) {
		perror("inviaFileVincite(const int, const char*) fallita, impossibile aprire file vincite");
		inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	
	stat(indirizzo_file_vincite, &info);
	
	// Caso in cui non vi sono state vincite nel passato dell'utente
	if (info.st_size == 0) {
		return inviaErrore(socket, FILE_VUOTO);
	}
	
	// Copia il contenuto del file nel messaggio da inviare
	lenmsg = (uint16_t)(info.st_size) + 1; // carattere '/0' in fondo
	messaggio = malloc(lenmsg);
	memset(messaggio, 0, lenmsg);
	
	fread(messaggio, 1, lenmsg-1, file_vincite);
	
	return inviaDati(socket, messaggio, strlen(messaggio) + 1);
}

/* Esegui il comando !vedi_vincite
 * Controlla le ultime schedine giocate se hanno vinto, memorizza eventuali nuove vincite nel file utente relativo alle vincite
 * (che potrebbe contenere vincite passate) e ne invia il contenuto al client
 * 
 * @socket descrittore del socket su cui avviene la comuncazione client-server
 * @user nome dell'utente
 * 
 * @return 1 se il comando ha successo, 0 se fallisce per colpa del client, -1 in caso di errore interno
 */
int eseguiVediVincite (const int socket, const char* user)
{
	// Variabili per accesso ai file
	FILE* file_schedine;
	char indirizzo_file_schedine[512];
	
	struct schedina_list* schedine = NULL,	// lista delle schedine da analizzare
		* puntatore_schedina = NULL;		// puntatore di appoggio per la gestione della lista schedine
	
	// Variabili per memorizzare il valore dei campi dello header del file_schedine
	// (vedi inizio file sorgente, area #define, sezione FILE)
	uint32_t offset_schedine_non_estratte, offset_schedine_da_controllare;
	
	sprintf(indirizzo_file_schedine, "%s/%s_schedine.bin", CARTELLA_FILES, user);
	
	// Apertura in lettura e scrittura come file binario
	file_schedine = fopen(indirizzo_file_schedine, "rb+");
	if (!file_schedine) {
		perror("Impossibile aprire file schedine");
		inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	
	// Estrazione dello header
	fread(&offset_schedine_non_estratte, sizeof(offset_schedine_non_estratte), 1, file_schedine);
	fread(&offset_schedine_da_controllare, sizeof(offset_schedine_da_controllare), 1, file_schedine);
	
	// Aggiornamento del secondo campo dello header
	// (alla fine dell'iterazione di questa funzione, tutte le schedine attualmente
	// estratte ma non controllate, verranno controllate)
	fseek(file_schedine, -sizeof(offset_schedine_da_controllare), SEEK_CUR);
	fwrite(&offset_schedine_non_estratte, sizeof(offset_schedine_non_estratte), 1, file_schedine);
	
	fclose(file_schedine);
	
	if (offset_schedine_non_estratte == offset_schedine_da_controllare) {
		// Non ci sono schedine da controllare, quinid invia al client il contenuto del proprio file vincite
		return inviaFileVincite(socket, user);
	}
	
	// Apertura in lettura come file di testo
	file_schedine = fopen(indirizzo_file_schedine, "r");
	if (!file_schedine) {
		perror("Impossibile aprire file schedine");
		inviaErrore(socket, ERRORE_INTERNO_SERVER);
		return -1;
	}
	
	fseek(file_schedine, offset_schedine_da_controllare, SEEK_SET);
	
	// Estrazione da file di tutte le schedine
	while (ftell(file_schedine) < offset_schedine_non_estratte) {
		char buffer[BUFFER_SIZE];
		int start_buffer, end_buffer, quanti_byte; // variabili per calcolare la dimensione del buffer
		struct schedina_list* temp;
		long int temp_time;
		
		// Estrai schedina
		fscanf(file_schedine, "%ld %n%[^|]%n|", &temp_time, &start_buffer, buffer, &end_buffer);
		
		temp = malloc(sizeof(struct schedina_list));
		if (!temp) {
			perror("Memoria esaurita");
			fclose(file_schedine);
			inviaErrore(socket, ERRORE_INTERNO_SERVER);
			return -1;
		}
		
		quanti_byte = end_buffer - start_buffer;
		
		// Inizializza nuvo elemento schedina_list
		temp->s = deserializza_schedina_txt(buffer, &quanti_byte);
		temp->timestamp = (time_t)temp_time;
		temp->next = NULL;
		
		// Inserisci in coda (puntatore_schedina punta all'ultimo elemento della lista schedine)
		if (puntatore_schedina == NULL) {
			schedine = temp;
		}
		else {
			puntatore_schedina->next = temp;
		}
		puntatore_schedina = temp;
	}
	
	fclose(file_schedine);
	
	// Controlla le schedine estratte e memorizza le vincite su file
	convalidaSchedineEstratte(schedine, user);
	
	// Distruzione schedine dala memoria centrale
	while (schedine != NULL) {
		struct schedina_list* s = schedine;
		schedine = schedine->next;
		free(s);
	}
	
	// Invia al client il contenuto del proprio file vincite
	return inviaFileVincite(socket, user);
}



/* Gestisce le richieste inviate da una client
 * 
 *  FORMATO DEI MESSAGGI RICEVUTI, se il client ha gia' effettuto il login
 * ----------------------------------------------------------------
 * |  CODICE (1 byte)  |  SESSION_ID (LUNGHEZZA_SESSION_ID byte)  |
 * ----------------------------------------------------------------
 * |      ATTRIBUTI MESSAGGIO (opzionali e variabili)             |
 * ----------------------------------------------------------------
 * 
 *  FORMATO DEI MESSAGGI RICEVUTI, se il client NON ha ancora effettuto il login
 * --------------------------------------------
 * |  CODICE (1 byte)  |  ATTRIBUTI MESSAGGIO |
 * --------------------------------------------
 * 
 * @socket descrittore del socket su cui e' stata aperta la connessione
 * @clientAddress indirizzo del client
 */
void gestisciRichiesteClient (const int socket, const struct sockaddr_in clientAddress)
{
	int ret, 
		loggato = 0; // indica se l'utente ha gia' effettuato il comando di login con successo
	uint16_t len;
	char* buffer = NULL;
	char sessionId[LUNGHEZZA_SESSION_ID + 1],
		presentationClientAddress[INET_ADDRSTRLEN]; // IP del client in formato presentazione
	char* user = NULL;	// username dell'utente
	uint8_t accessiFalliti = 0;
	
	inet_ntop(AF_INET, &clientAddress.sin_addr.s_addr, presentationClientAddress, sizeof(presentationClientAddress));
	
	while (1) {
		uint8_t tipoRichiesta;
		
		// Dealloca buffer in caso non fosse stato deallocato (per esempio nel caso di una continue)
		if (buffer) {
			free(buffer);
			buffer = NULL;
		}
		
		ret = attendiMessaggioDalClient(socket, (void**)&buffer, presentationClientAddress);
		if (ret < 0) { // errore di trasmissione del client: riprovare
			continue;
		}
		else if (ret == 0) { // chiusura connessione
			break;
		}
		len = ret;

		// Estraggo byte d'intestazione
		tipoRichiesta = (uint8_t)buffer[0];
		
		// L'utente non e' loggato e tenta di eseguire azioni subordinate al login.
		if (!loggato && tipoRichiesta != SIGNUP && tipoRichiesta != LOGIN) {
			ret = inviaErrore(socket, LOGIN_NON_EFFETTUATO);
			if (ret < 0) {
				break;
			}
			continue;
		}
		
		if (loggato) {
			// L'utente e' gia' loggato e cerca di eseguire azioni di login o signup
			if (tipoRichiesta == SIGNUP || tipoRichiesta == LOGIN) {
				ret = inviaErrore(socket, LOGIN_GIA_EFFETTUATO);
				if (ret < 0) {
					break;
				}
				continue;
			}
			
			// Confronto session_id memorizzato e inviato
			if(strncmp(sessionId, buffer+1, LUNGHEZZA_SESSION_ID) != 0) {
				ret = inviaErrore(socket, SESSION_ID_ERRATO); 
				if (ret < 0) {
					break;
				}
				continue;
			}
		}
		
		// Gestione dei diversi tipi di richiesta
		switch (tipoRichiesta) {
			case LOGIN:
				printf("Client %s, socket %d: login iniziato\n", presentationClientAddress, socket);
				fflush(stdout);
				
				ret = effettuaLogin(socket, clientAddress, buffer + 1, len - 1, &user, sessionId, &accessiFalliti);
				if (ret < 0) { // si e' verificato un errore
					goto chiusura;
				}
				
				loggato = ret;
				if (!loggato && accessiFalliti >= 3) { // troppi accesi falliti
					goto chiusura;
				}
				
				printf("Client %s, socket %d: login %s\n", presentationClientAddress, socket, (loggato) ? "completato" : "fallito");
				break;
			
			case SIGNUP:
				printf("Client %s, socket %d: signup iniziata\n", presentationClientAddress, socket);
				fflush(stdout);
				
				ret = effettuaSignup(socket, presentationClientAddress, buffer + 1, len - 1);
				if (ret < 0) {
					goto chiusura;
				}
				
				printf("Client %s, socket %d: signup completata\n", presentationClientAddress, socket);
				fflush(stdout);
				break;
			
			case INVIA_GIOCATA:
				printf("Client %s, socket %d: invia_giocata iniziata\n", presentationClientAddress, socket);
				fflush(stdout);
				
				eseguiInviaGiocata(socket, buffer + 1, len - 1, user);
				if (ret < 0) goto chiusura;
				
				printf("Client %s, socket %d: invia_giocata ", presentationClientAddress, socket);
				if (ret > 0) printf("completata\n");
				else printf("fallita\n");
				fflush(stdout);

				break;
				
			case VEDI_GIOCATE:
				printf("Client %s, socket %d: vedi_giocate iniziata\n", presentationClientAddress, socket);
				fflush(stdout);
				
				ret = eseguiVediGiocate(socket, buffer + 1, len - 1, user);
				if (ret < 0) goto chiusura;
				
				printf("Client %s, socket %d: vedi_giocate ", presentationClientAddress, socket);
				if (ret > 0) printf("completata\n");
				else printf("fallita\n");
				fflush(stdout);
				
				break;
				
			case VEDI_ESTRAZIONE:
				printf("Client %s, socket %d: vedi_estrazione iniziata\n", presentationClientAddress, socket);
				fflush(stdout);
				
				ret = eseguiVediEstrazione(socket, buffer + 1, len - 1);

				if (ret < 0) goto chiusura;
				
				printf("Client %s, socket %d: vedi_estrazione ", presentationClientAddress, socket);
				if (ret > 0) printf("completata\n");
				else printf("fallita\n");
				fflush(stdout);
				break;
				
			case VEDI_VINCITE:
				printf("Client %s, socket %d: vedi_vincite iniziata\n", presentationClientAddress, socket);
				fflush(stdout);
				
				ret = eseguiVediVincite(socket, user);
				
				if (ret < 0) goto chiusura;
				
				printf("Client %s, socket %d: vedi_vincite ", presentationClientAddress, socket);
				if (ret > 0) printf("completata\n");
				else printf("fallita\n");
				fflush(stdout);
				break;
			
		}
	}
	
	// Chiusura connessione
chiusura:
	printf("Client %s, socket %d: chiusura connessione\n", presentationClientAddress, socket);
	fflush(stdout);
	
	// Dealloca variabili allocate dinamicamente
	if (user) {
		free(user);
	}
	if (buffer) {
		free(buffer);
	}
}

//////////////////////////////////////////////
//				ESTRAZIONE					//
//////////////////////////////////////////////
/* Handler del segnale SIGUSR1.
 * Blocca tutti i processi in modo da garantire l'esecuzione senza concorrenza dell'estrazione.
 * I processi rimangono in attesa del segnale SIGUSR2.
 */
void bloccaProcesso (int segnale) {
	while (!estrazione_completata) {
		sleep(1);
	}
	estrazione_completata = 0;
}

/* Handler del segnale SIGUSR2.
 * Non fa nulla. Il segnale ha come obiettivo di risvegliare i processi bloccati dal segnale SIGUSR1
 */
void risvegliaProcesso (int segnale) {
	estrazione_completata = 1;
	return;
}

/* Aggiorna gli header di tutte le schedine, dando per assunto come ipotesi iniziale 
 * che e' stata appena effettuata l'estrazione
 * 
 * @return -1 in caso di fallimento, 1 altrimenti
 */
int aggiornaHeaderSchedine ()
{
	struct dirent* de_files;	// puntatore alle directory entry (ovvero i file contenuti) della cartella files
	DIR* dr_files;				// puntatore alla directory files
	
	// Apre la cartella che contiene i registri
	dr_files = opendir(CARTELLA_FILES); 
	if (!dr_files) { 
		perror("Impossibile aprire la directory CARTELLA_FILES"); 
		return -1; 
	} 

	while ((de_files = readdir(dr_files)) != NULL) {
		
		// Esplora tutta la cartella alla ricerca dei registri delle schedine
		// (riconoscibili dal suffisso "_schedine.bin"
		if(strcmp(de_files->d_name + strlen(de_files->d_name) - 13, "_schedine.bin") == 0) {
			
			// Abbiamo trovato il file delle schedine di un utente!
			// Bisogna modificare lo header in modo che punti alla fine del file
			// (non esiste alcuna schedina che NON abbia subito un'estrazione)
			uint32_t new_header;
			FILE* file_schedina;
			char indirizzo_file[512];
			
			sprintf(indirizzo_file, "%s/%s", CARTELLA_FILES, de_files->d_name);
			file_schedina = fopen(indirizzo_file, "rb+");
			if (!file_schedina) {
				perror("Impossibile aprire file schedina");
				return -1;
			}
			
			// Modifica lo header in modo che punti alla fine del file
			fseek(file_schedina, 0, SEEK_END);
			new_header = (uint32_t)ftell(file_schedina);
			
			fseek(file_schedina, 0, SEEK_SET);			
			fwrite(&new_header, sizeof(new_header), 1, file_schedina);
			fclose(file_schedina);
		}
	}

	closedir(dr_files);   
	return 0;
}

/* Estrae 5 numeri casuali e unici per ognuna delle 11 ruote.
 * Inserisce i numeri estratti in FILE_ESTRAZIONI.
 * Aggiorna gli header di tutti i file schedine degli utenti in modo che tutte le schedine
 * registrate prima delll'estrazione vengano marcate come estratte.
 * 
 * In FILE_ESTRAZIONI ogni estrazione generale ha il seguente formato:
 *      ------------------------------------------------------------------
 *      | timestamp estrazione (time_t) | estrazioni delle singole ruote |
 *      ------------------------------------------------------------------
 * 
 * L'estrazione di una singola ruota ha il seguente formato:
 *      --------------------------------------------------------
 *      | codice ruota (uint8_t) | numeri estratti (5 uint32_t)|
 *      --------------------------------------------------------
 */
void effettuaEstrazione ()
{
	int ruota, i, ret;
	time_t timestamp;
	
	// Variabili per I/O su file
	FILE* file_estrazione;

	// Inizializza timestamp
	time(&timestamp);
	
	// Apri file in append mode
	file_estrazione = fopen(FILE_ESTRAZIONI, "ab");
	if (!file_estrazione) {
		perror("Impossibile aprire file di estrazione");
		return;
	}
	
	// Memorizza timestamp
	fwrite(&timestamp, sizeof(timestamp), 1, file_estrazione);
		
	// Estrae i numeri delle ruote ruote
	for (ruota = 0; ruota < QUANTE_RUOTE; ++ruota) {
		uint8_t codice_ruota = (uint8_t)ruota;
		uint32_t estrazione[QUANTI_NUMERI_ESTRATTI];
		memset(estrazione, 0, QUANTI_NUMERI_ESTRATTI);
		
		// Estrae QUANTI_NUMERI_ESTRATTI interi diversi tra loro
		for (i = 0; i < QUANTI_NUMERI_ESTRATTI; ++i) {
			int scorri;
			int booleanNumeroUnico; // indica se il numero attualmente estratto e' unico o meno
			
			do {
				booleanNumeroUnico = 1;
				estrazione[i] = (uint32_t)(rand() % NUMERI_ESTRAIBILI + 1);
				
				// Controlla l'unicita' del numero estratto
				for (scorri = 0; scorri < i; ++scorri) {
					if (estrazione[i] == estrazione[scorri]) {
						booleanNumeroUnico = 0;
						break;
					}
				}
			} while (booleanNumeroUnico == 0);
		}
		
		// Memorizza in un file l'estrazione
		fwrite(&codice_ruota, sizeof(codice_ruota), 1 , file_estrazione);
		fwrite(estrazione, sizeof(uint32_t), QUANTI_NUMERI_ESTRATTI, file_estrazione);
	}

	fclose(file_estrazione);
	
	ret = aggiornaHeaderSchedine();
	if (ret < 0) {
		printf("Routine di estrazione non completata\n");
		fflush(stdout);
		return;
	}
	
	printf("Estrazione effettuata\n");
	fflush(stdout);
}

//////////////////////////////////////////////
//					MAIN					//
//////////////////////////////////////////////
int main(int argc, char** argv)
{
	int periodoEstrazione = PERIODO_ESTRAZIONE;
	
	// Variabili per TCP server
	int listenerSocket, serverSocket;
	struct sockaddr_in serverAddress;
	uint16_t porta;
	
	// Variabili per TCP client
	struct sockaddr_in clientAddress;
	
	// Variabili per gestione processi
	pid_t pid, processo_estrazione;
	
	// Variabili di appoggio
	int ret;
	socklen_t addrLen;

	// Errore: manca il parametro <porta>
	if (argc < 2) {
		fprintf(stderr, "Errore: il programma deve essere lanciato con almeno il parametro <porta>\n");
		fflush(stderr);
		exit(EXIT_FAILURE);
	}

	// Lettura dei parametri inseriti da console
	porta = (uint16_t)atoi(argv[1]);
	
	if (argc >= 3) {
		periodoEstrazione = atoi(argv[2]);
		
		if (periodoEstrazione <= 0) {
			printf("Errore: inserire un periodo di estrazione > 0");
			exit(EXIT_FAILURE);
		}
	}
	
	processo_estrazione = fork();
	
	srand(time(NULL)); // inizializza algoritmo pseudorandomico
	
	if (processo_estrazione < 0) {
		perror("fork fallita");
		exit(EXIT_FAILURE);
	}
	else if (processo_estrazione == 0) { 
		time_t start_timer = 0, end_timer = 0;	// usati per calcolare il tempo di esecuzione della estrazione, 
												//da sottrarre al periodo di sleep
		
		// Ignora i segnali usati per la sospensione e riattivazione dei processi server
		signal(SIGUSR1, SIG_IGN);
		signal(SIGUSR2, SIG_IGN);
		
		while (1) {
			sleep(periodoEstrazione * SECONDI_IN_UN_MINUTO - (int)(end_timer - start_timer));
			
			time(&start_timer);	// calcola istante d'inizio dell'estrazione
			kill(0, SIGUSR1);	// blocca i processi
			effettuaEstrazione();
			kill(0, SIGUSR2);	// risveglia i processi	
			time(&end_timer);	// calcola istante di fine dell'estrazione
		}
		
		exit(0);
	}

	// Configurazione meccanismo di estrazione automatico.
	// L'estrazione viene effettuata da un processo ad hoc che periodicamente si risveglia
	// e invia un segnale di tipo SIGUSR1 a tutti gli altri processi del proprio gruppo.
	// Il segnale SIGUSR1 blocca il processo in attesa del segnale SIGUSR2, lanciato da processo_estrazione
	signal(SIGUSR1, bloccaProcesso);
	signal(SIGUSR2, risvegliaProcesso);
	
	// Configurazione socket di ascolto
	listenerSocket = socket(AF_INET, SOCK_STREAM, 0);
	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(porta);
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	
	memset(&clientAddress, 0, sizeof(clientAddress));
	
	// Effettua la bind del socket in ascolto
	ret = bind(listenerSocket, (struct sockaddr*)&serverAddress, sizeof(serverAddress));
	if (ret < 0) {
		perror("bind fallita");
		exit(EXIT_FAILURE);
	}
	printf("Bind(...) effettuata con successo\n");
	fflush(stdout);
	
	// Metti in ascolto il socket
	ret = listen(listenerSocket, LUNGHEZZA_BACKLOG);
	if (ret < 0) {
		perror("listen fallita");
		exit(EXIT_FAILURE);
	}
	printf("Socket in ascolto configurata correttamente\n");
	fflush(stdout);
	
	while (1) {
		char presentationAddress[INET_ADDRSTRLEN];
		addrLen = sizeof(clientAddress);
		
		// Accetta nuova connessione dal client
		serverSocket = accept(listenerSocket, (struct sockaddr*)&clientAddress, &addrLen);
		printf("CONNESSIONE ACCETTATA: client %s, socket %i\n",
				inet_ntop(AF_INET, &clientAddress.sin_addr.s_addr, presentationAddress, sizeof(presentationAddress)),
				serverSocket);
		fflush(stdout);
		
		pid = fork();
		
		// Errore nella fork()
		if (pid < 0) {
			perror("fork fallita");
			close(serverSocket);
			continue;
		}
		
		// Processo figlio che gestisce la richiesta del client
		if (pid == 0) {
			close(listenerSocket);
			gestisciRichiesteClient(serverSocket, clientAddress);
			close(serverSocket);
			exit(EXIT_SUCCESS);
		}
		
		// Processo padre
		close(serverSocket);
		
		// Termina tutti i processi figli zombie, cosi' da liberare memoria inutilizzata
		do {
			pid = waitpid(-1, NULL, WNOHANG); // non bloccante
		} while (pid != 0);
	}
	
	close(listenerSocket);
	return 0;
}

