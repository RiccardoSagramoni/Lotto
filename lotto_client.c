#include "lotto.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Indici dei comandi
#define C_HELP 0
#define C_SIGNUP 1
#define C_LOGIN 2
#define C_INVIA_GIOCATA 3
#define C_VEDI_GIOCATE 4
#define C_VEDI_ESTRAZIONE 5
#define C_VEDI_VINCITE 6
#define C_ESCI 7

#define BUFFER_SIZE 1024

//
// COMUNICAZIONE CLIENT-SERVER
//
/* Invia un comando al server
 * 
 * @socket descrittore della socket su cui inviare il messaggio
 * @tipo codice del tipo di messaggio client-to-server
 * @msg indirizzo al messaggio
 * @len lunghezza del messaggio
 * 
 * @return -1 in caso di errore, 0 altrimenti
 */
int inviaComando (const int socket, const uint8_t tipo, const void* msg, const size_t len)
{
	int ret;
	void* buffer;
	uint16_t len_buffer = len + sizeof(tipo), // lunghezza del buffer = lunghezza del messaggio + header (1 byte)
			network_len_buffer;	// contiene la lunghezza del buffer in formato network
	
	buffer = malloc(len_buffer);
	if (!buffer) {
		fprintf(stderr, "Impossibile allocare dinamicamente buffer\n");
		fflush(stderr);
		return -1;
	}
	
	// Genera il messaggio
	memcpy(buffer, &tipo, sizeof(tipo));
	memcpy(buffer + sizeof(tipo), msg, len);
	
	network_len_buffer = htons(len_buffer);
	
	// Invia dimensione messaggio
	ret = send(socket, &network_len_buffer, sizeof(network_len_buffer), 0);
	if (ret < 0) {
		perror("Impossibile inviare dimensione messaggio");
		free(buffer);
		return -1;
	}
	
	// Invia messaggio
	ret = send(socket, buffer, len_buffer, 0);
	if (ret < 0) {
		perror("Impossibile inviare messaggio");
		free(buffer);
		return -1;
	}
	
	free(buffer);
	
	return 0;
}

/* Attendi una risposta dal server
 * 
 * @socket socket su cui avviene la comunicazione
 * @msg puntatore al puntatore all'area di memoria dove verra' memorizzato dinamicamente il messaggio
 * 
 * @return la lunghezza del messaggio se l'operazione ha successo, -1 in caso di errore, 0 se il server chiude la connessione
 */
int attendiRisposta (const int socket, void** msg)
{
	int ret;
	uint16_t len;
	
	// Leggi lunghezza del messaggio
	ret = recv(socket, &len, sizeof(len), 0);
	if (ret < 0) {
		perror("Impossibile leggere lunghezza messaggio");
		return -1;
	}
	else if (ret == 0) {
		printf("Il server ha chiuso la connessione\n");
		return 0;
	}
	
	len = ntohs(len);
	
	*msg = malloc(len);
	if(!(*msg)) {
		fprintf(stderr, "Impossibile allocare dinamicamente il messaggio di risposta\n");
		fflush(stderr);
		return -1;
	}
	
	// Leggi messaggio
	ret = recv(socket, *msg, len, MSG_WAITALL);
	if (ret < 0) {
		perror("Impossibile ricevere il messaggio");
		return -1;
	}
	else if (ret == 0) {
		printf("Il server ha chiuso la connessione\n");
		return 0;
	}
	
	return len;
}

//
// STAMPA A VIDEO
//
/* Stampa i messaggi di aiuto all'utente per la corretta sintassi dei comandi.
 * 
 * @comando codice del comando di cui stampare il manuale, -1 per stamparli tutti
 */
void stampaAiuto(int comando)
{
	if (comando == C_HELP || comando == -1) {
		printf("1) !help <comando> --> mostra i dettagli di un comando\n");
	}
	if (comando == C_SIGNUP  || comando == -1) {
		printf("2) !signup <username> <password> --> crea un nuovo utente\n");
	}
	if (comando == C_LOGIN  || comando == -1) {
		printf("3) !login <username> <password> --> autentica un utente\n");
	}
	if (comando == C_INVIA_GIOCATA || comando == -1) {
		printf("4) !invia_giocata g --> invia una giocata g al server\n");
	}
	if (comando == C_VEDI_GIOCATE || comando == -1) {
		printf(	"5) !vedi_giocate tipo --> visualizza le giocate precedenti dove tipo = {0,1}\n"
				"                          e permette di visualizzare le giocate passate '0'\n"
				"                          oppure le giocate attive '1' (ancora non estratte)\n");
	}
	if (comando == C_VEDI_ESTRAZIONE || comando == -1) {
		printf(	"6) !vedi_estrazione <n> <ruota> --> mostra i numeri delle ultime n estrazioni\n"
				"                                    sulla ruota specificata\n");
	}
	if (comando == C_VEDI_VINCITE || comando == -1) {
		printf(	"7) !vedi_vincite --> mostra tutte le schedine vinte dall'utente\n");
	}
	if (comando == C_ESCI || comando == -1) {
		printf("8) !esci --> termina il client\n");
	}
	
	printf("\n");
	fflush(stdout);
}

/* Stampa messaggio di avvio dell'applicazione
 */
void stampaMessaggioAvvio()
{
	printf("\n***************************** GIOCO DEL LOTTO *****************************\n");
	printf("Sono disponibili i seguenti comandi:\n\n");

	stampaAiuto(-1);
}

/* Dato l'indice di una ruota, restituisce il nome della ruota sotto forma di stringa
 * 
 * @r indice della ruota
 * 
 * @return nome della ruota
 */
const char* convertiRuotaIntToString (int r)
{
	if (r == BARI) return S_BARI;
	if (r == CAGLIARI) return S_CAGLIARI;
	if (r == FIRENZE) return S_FIRENZE;
	if (r == GENOVA) return S_GENOVA;
	if (r == MILANO) return S_MILANO;
	if (r == NAPOLI) return S_NAPOLI;
	if (r == PALERMO) return S_PALERMO;
	if (r == ROMA) return S_ROMA;
	if (r == TORINO) return S_TORINO;
	if (r == VENEZIA) return S_VENEZIA;
	if (r == NAZIONALE) return S_NAZIONALE;
	
	return "";
}

/* Dati l'indice di una tipo di puntata e la modalita' di visualizzazione delle stringhe (minuscolo, maiuscolo...),
 * restituisce il nome della puntata (estratto, ambo...) secondo la modalita' scelta
 * 
 * @p tipo di puntata
 * @mode modalita' di visualizzazione
 * 
 * @return nome della puntata
 */
const char* getTipoDiPuntata (int p, int mode)
{
	if (mode == MINUSCOLO) {
		if (p == 0) return "estratto";
		if (p == 1) return "ambo";
		if (p == 2) return "terno";
		if (p == 3) return "quaterna";
		if (p == 4) return "cinquina";
	}
	else if (mode == CAPS_LOCK) {
		if (p == 0) return "ESTRATTO";
		if (p == 1) return "AMBO";
		if (p == 2) return "TERNO";
		if (p == 3) return "QUATERNA";
		if (p == 4) return "CINQUINA";
	}
	else if (mode == INIZIALE_MAIUSCOLA) {
		if (p == 0) return "Estratto";
		if (p == 1) return "Ambo";
		if (p == 2) return "Terno";
		if (p == 3) return "Quaterna";
		if (p == 4) return "Cinquina";
	}
	
	return NULL;
}

//
// VALIDAZIONE COMANDI
//
/* Converte un comando nel rispettivo codice identificativo
 * 
 * @str nome comando
 * 
 * @return codice identificativo del comando se esiste, -1 altrimenti
 */
int convertiComandoStringToInt (char* str)
{
	if (!strcmp(str, "help")) {
		return C_HELP;
	}
	if (!strcmp(str, "signup")) {
		return C_SIGNUP;
	}
	if (!strcmp(str, "login")) {
		return C_LOGIN;
	}
	if (!strcmp(str, "invia_giocata")) {
		return C_INVIA_GIOCATA;
	}
	if (!strcmp(str, "vedi_giocate")) {
		return C_VEDI_GIOCATE;
	}
	if (!strcmp(str, "vedi_estrazione")) {
		return C_VEDI_ESTRAZIONE;
	}
	if (!strcmp(str, "vedi_vincite")) {
		return C_VEDI_VINCITE;
	}
	if (!strcmp(str, "esci")) {
		return C_ESCI;
	}
	
	return -1;
}

/* Effettua il parse su un comando inserito dall'utente
 * (ovvero divide il comando nelle singole parole di cui e' composto).
 * 
 * @comando puntatore al comando su cui eseguire il parse
 * @len lunghezza del comando
 * @len_parsed puntatore alla variabile in cui memorizzare la lunghezza dell'array di stringhe restituito
 * 
 * @return array di stringhe (le parole del comando originale)
 */
char** parseComando (char* comando, size_t len, size_t* len_parsed)
{
	int quanteParole = 1;
	char delimiter = ' ';
	char* temp = NULL;
	char** parsed_comando = NULL;
	int i = 0;
	
	if (!comando) return 0;
	
	// Conta le parole
	for (i = 0; i < len; ++i) {
		if (comando[i] == delimiter) quanteParole++;
	}
	
	parsed_comando = (char**)malloc(quanteParole * sizeof(char*));
	if (!parsed_comando) {
		fprintf(stderr, "Impossibile allocare parsed_comando in memoria\n");
		fflush(stderr);
	}
	
	// Parse del comando
	for (i = 0; i < quanteParole; ++i) {
		temp = strsep(&comando, &delimiter);
		parsed_comando[i] = malloc(strlen(temp) + 1);
		if (!parsed_comando[i]) {
			perror("Impossibile allocare parola per parsed_comando");
			return NULL;
		}
		strcpy(parsed_comando[i], temp);
	}
	
	*len_parsed = (size_t)quanteParole;
	return parsed_comando;
}

/* Controlla se una data stringa corrisponde ad una ruota del lotto
 * 
 * @ruota nome della ruota
 * 
 * @return 1 la stringa e' una ruota, 0 altrimenti
 */
int controllaRuota (char* ruota)
{
	return (!strcmp(ruota, S_BARI) || !strcmp(ruota, S_CAGLIARI) || !strcmp(ruota, S_FIRENZE) ||
			!strcmp(ruota, S_GENOVA) || !strcmp(ruota, S_MILANO) || !strcmp(ruota, S_NAPOLI) || 
			!strcmp(ruota, S_PALERMO) || !strcmp(ruota, S_ROMA) || !strcmp(ruota, S_TORINO) || 
			!strcmp(ruota, S_VENEZIA) || !strcmp(ruota, S_NAZIONALE));
}

/* Controlla se una data stringa e' un numero intero
 * 
 * @numero stringa contenente il numero
 * 
 * @return 1 la stringa e' un numero intero minore di NUMERI_ESTRAIBILI, 0 altrimenti
 */
int controllaAsciiInt (char* numero) 
{
	int i, num;
	size_t len = strlen(numero);
	
	for (i = 0; i < len; ++i) {
		if (numero[i] < '0' || numero[i] > '9') {
			return 0;
		}
	}
	
	// Converte
	num = atoi(numero);
	return (num >= 1 && num <= NUMERI_ESTRAIBILI);
}

/* Controlla se una data stringa e' un numero reale
 * 
 * @numero stringa contenente il numero
 * 
 * @return 1 la stringa e' un numero reale, 0 altrimenti
 */
int controllaAsciiDouble (char* numero)
{
	int i, punto = 0;
	size_t len = strlen(numero);
	
	for (i = 0; i < len; ++i) {
		if (!punto && numero[i] == '.') {
			punto = 1;	// in un double, vi puo' essere solo un punto decimale
			continue;
		}
		if (numero[i] < '0' || numero[i] > '9') {
			return 0;
		}
	}
	return 1;
}

/* Prende il risultato del parse di un comando e verifica se la sintassi risulta corretta
 * 
 * @parsed_comando comando dopo il parse
 * @len lunghezza di parsed_comando
 * 
 * @return -1 se il comando non e' valido, l'indice del comando in caso contrario
 */
int validaComando (char** parsed_comando, size_t len)
{
	// Tutti i comandi iniziano con il punto esclamativo
	if (parsed_comando[0][0] != '!'){
		return -1;
	}
	// Comando help
	if (!strcmp(parsed_comando[0], "!help")) {
		if (len > 2) {
			return -1;
		}
		return C_HELP;
	}
	// Comando signup
	if (!strcmp(parsed_comando[0], "!signup")) {
		if (len != 3) {
			return -1;
		}
		return C_SIGNUP;
	}
	// Comando login
	if (!strcmp(parsed_comando[0], "!login")) {
		if (len != 3) {
			return -1;
		}
		return C_LOGIN;
	}
	// Comando invia_giocata
	if (!strcmp(parsed_comando[0], "!invia_giocata")) {
		// !invia_giocata -r <ruote> -n <numeri> -i <importi>
		int i, selezionateTutteLeRuote = 0;
		int inizioRuota = 2, inizioNumeri, inizioImporti;

		// RUOTE
		if (strcmp(parsed_comando[1], "-r") != 0) {
			return -1;
		}
		// Controlla che le stringhe siano ruote
		for (i = 0; (inizioRuota + i) < len &&
				strcmp(parsed_comando[inizioRuota + i], "-n") != 0; ++i) {	// esce se inizia la sezione dei numeri ("-n")
			// Se l'utente ha inserito l'opzione "-r tutte" non vi possono essere altre ruote,
			// quindi dovrebbe verificarsi la condizione di uscita n.2 (ovvero inizio sezione "-n")
			if (selezionateTutteLeRuote) {
				return -1;
			}
			if (!strcmp(parsed_comando[inizioRuota + i], "tutte")) {
				selezionateTutteLeRuote = 1;
				continue;
			}
			if (!controllaRuota(parsed_comando[inizioRuota + i])) {
				return -1;
			}
		}
		if (i == 0) {	// Non ci sono ruote
			return -1;
		}
		
		// NUMERI
		inizioNumeri = inizioRuota + i + 1; // salta "-n"
		for (i = 0; (inizioNumeri + i) < len 
				&& strcmp(parsed_comando[inizioNumeri + i], "-i") != 0; ++i) {
			if(!controllaAsciiInt(parsed_comando[inizioNumeri + i])) {
				return -1;
			}
		}
		if (i == 0) {	// Non ci sono numeri
			return -1;
		}
		if (i > QUANTITA_MASSIMA_NUMERI_SCHEDINA) {	// Troppi numeri
			printf("Non ci possono essere piu' di %i numeri nella schedina\n", QUANTITA_MASSIMA_NUMERI_SCHEDINA);
			return -1;
		}
		
		
		// IMPORTI
		inizioImporti = inizioNumeri + i+1; // salta "-i"
		
		for (i = 0; (inizioImporti + i) < len; ++i) {
			// Controlla che gli importi siano numeri double
			if(!controllaAsciiDouble(parsed_comando[inizioImporti + i])) {
				return -1;
			}
		}
		if (i == 0) {	// Non ci sono importi
			return -1;
		}
		
		return C_INVIA_GIOCATA;
	}
	
	// Comando vedi_giocate
	if (!strcmp(parsed_comando[0], "!vedi_giocate")) {
		// !vedi_giocate <tipo>
		if (len != 2) return -1;
		
		// <tipo> puo' essere solo '0' o '1'
		
		if (strlen(parsed_comando[1]) != 1 || (parsed_comando[1][0] != '0' && parsed_comando[1][0] != '1')) {
			return -1;
		}
		
		return C_VEDI_GIOCATE;
	}
	
	// Comando vedi_estrazione
	if (!strcmp(parsed_comando[0], "!vedi_estrazione")) {
		// !vedi_estrazione <n> <ruota (opzionale)>
		
		if (len > 3) return -1;
		
		// n: numero intero
		if (!controllaAsciiInt(parsed_comando[1])) {
			return -1;
		}
		
		if (len == 3 && !controllaRuota(parsed_comando[2])) {
			return -1;
		}
		
		return C_VEDI_ESTRAZIONE;
	}
	
	// Comando !vedi_vincite
	if (!strcmp(parsed_comando[0], "!vedi_vincite")) {
		// E' presente solo il comando, senza opzioni
		if (len != 1) return -1;
		else return C_VEDI_VINCITE;
	}
	
	// Comando !esci
	if (!strcmp(parsed_comando[0], "!esci")) {
		// E' presente solo il comando, senza opzioni
		if (len != 1) return -1;
		else return C_ESCI;
	}
	
	return -1;
}

//
// ESECUZIONE COMANDI
//
/* Esegue il comando help. 
 * Se non ci sono opzioni, stampa il manuale di tutti i comandi, altrimenti stampa il manuale del comando specificato
 * 
 * @parsed_comando comando dopo il parse
 * @len lunghezza di parsed_comando
 * 
 * @return -1 in caso di errore, 1 altrimenti
 */
int eseguiHelp (char** parsed_comando, const size_t len)
{
	int tipo;
	
	// Se il comando help non ha opzioni, stampa il manuale di tutti i comandi
	if (len == 1) {
		stampaAiuto(-1);
		return 1;
	}
	
	tipo = convertiComandoStringToInt(parsed_comando[1]);
	if (tipo < 0) {
		fprintf(stderr, "Errore: comando <%s>! non riconosciuto\n", parsed_comando[1]);
		fflush(stderr);
		return -1;
	}
	stampaAiuto(tipo);
	return 1;
}

/* Richiede in input un nuovo username all'utente e lo memorizza in *username
 * 
 * @username puntatore alla stringa dove memorizzare lo username
 * 
 * @return lunghezza dello username
 */
int richiediNuovoUsername (char** username)
{
	char* buffer;	// buffer di lettura
	char* temp;		// puntatore alla testa del buffer (usato con la strsep)
	char delimiter = ' ';	// delimitatore: non vi possono essere spazi in uno username
	int len;
	
	buffer = malloc(BUFFER_SIZE);
	fgets(buffer, BUFFER_SIZE, stdin);
	buffer[strlen(buffer)-1] = '\0';	// sovrascrive il newline
	
	temp = strsep(&buffer, &delimiter);	// estrai lo username (prima parola senza spazi)
	
	len = strlen(temp) + 1; // tiene conto del null terminator
	
	*username = malloc(len);
	strcpy(*username, temp);
	(*username)[len - 1] = '\0';
	
	free(buffer);
	return len;
}

/* Invia al server il comando di signup, che registra l'utente sul server.
 * Il comando NON effettua in automatico anche il login.
 * Il messaggio da inviare e' nel formato
 *		---------------------------------------------------
 *		|  username  |  ' ' (spazio)  |  password  | '\\0' |
 *		---------------------------------------------------
 * 
 * @socket descrittore del socket su cui comunicare
 * @parsed_comando comando dopo il parse
 * 
 * @return -1 in caso di errore, 0 se il server chiude la connessione, 1 altrimenti (anche se fallisce la signup)
 */
int eseguiSignup(const int socket, char** parsed_comando)
{
	int ret,
		exit = 0; // condizione di uscita: 1 se il server ha accettato lo username inviato, 0 altrimenti
	char* msg;
	size_t dim_msg, len_user, len_passw;
	
	// Calcola la lunghezza di utente, password e del messaggio completo
	len_user = strlen(parsed_comando[1]);
	len_passw = strlen(parsed_comando[2]);
	
	dim_msg = len_user + len_passw + 2;
	
	// Costruisce il messaggio
	msg = malloc(dim_msg);
	if (!msg) {
		perror("Memoria esaurita, signup fallita");
		return -1;
	}
	memcpy(msg, parsed_comando[1], len_user);
	msg[len_user] = ' ';
	memcpy(msg+len_user+1, parsed_comando[2], len_passw);
	msg[dim_msg - 1] = '\0';
	
	while (!exit) {
		ret = inviaComando(socket, SIGNUP, msg, dim_msg);
		free(msg);
		if (ret < 0) {
			return -1;
		}
	
		ret = attendiRisposta(socket, (void**)&msg);
		if (ret <= 0) return ret;
	
		dim_msg = ret;
		exit = 1;
		
		// Decodifica risposta del server
		if (msg[0] == ERR) {
			switch(msg[1]) {
				case LOGIN_GIA_EFFETTUATO:
					printf("Errore: L'utente ha gia' effettuato il login sul server. "
							"Per favore disconnettersi prima di registrare un nuovo account\n");
					break;
				case USERNAME_OCCUPATO:
					// Lo username scelto e' occupato --> ne viene richiesto un altro all'utente
					printf("Errore: Esiste gia' un utente con lo stesso username\n");
					printf("Per favore sceglierne un altro > ");
					fflush(stdout);
					free(msg);
					dim_msg = richiediNuovoUsername(&msg);
					exit = 0; // continua ad inviare lo username
					continue;
				default:
					printf("Errore sconosciuto\n");
			}
		}
		else if (msg[0] == DATI) {
			printf("Registrazione completata\n");
			break;
		}
		else {
			printf("Errore sconosciuto\n");
		}

		fflush(stdout);
		free(msg);
	}
	
	return 1;
}

/* Invia al server il comando di login.
 * Il messaggio da inviare e' nel formato
 *		------------------------------------------
 *		|  username  |  ' '  |  password  | '\\0' |
 *		------------------------------------------
 * 
 * @socket descrittore del socket su cui comunicare
 * @parsed_comando comando dopo il parse
 * @session_id indirizzo di memoria in cui memorizzare il session_id inviato dal server
 * 
 * @return -1 in caso di errore, 0 se il server chiude la connessione,
 *     1 se il login viene effettuato con successo, 2 se il comando login fallisce,
 *     3 se viene bloccato questo IP
 */
int eseguiLogin (const int socket, char** parsed_comando, char* session_id)
{
	int ret, return_value;
	char* msg;
	size_t dim_msg, len_user, len_passw;
	
	// Calcola lunghezza di username, password e del messaggio totoale
	len_user = strlen(parsed_comando[1]);	// username
	len_passw = strlen(parsed_comando[2]);	// password
	
	dim_msg = len_user + len_passw + 2;
	
	// Costruisce il messaggio
	msg = malloc(dim_msg);
	if (!msg) {
		perror("Memoria esaurita, login fallito");
		return -1;
	}
	memcpy(msg, parsed_comando[1], len_user);
	msg[len_user] = ' ';
	memcpy(msg + len_user + 1, parsed_comando[2], len_passw);
	msg[dim_msg - 1] = '\0';
	
	ret = inviaComando(socket, LOGIN, msg, dim_msg);
	free(msg);
	if (ret < 0) {
		return -1;
	}
	
	ret = attendiRisposta(socket, (void**)&msg);
	if (ret <= 0) return ret;
	
	dim_msg = ret;
	
	// Decodifica la risposta
	if (msg[0] == ERR) {
		switch(msg[1]) {
			case LOGIN_GIA_EFFETTUATO:
				printf("Errore: L'utente ha gia' effettuato il login sul server. "
						"Per favore disconnettersi prima di registrare un nuovo account\n");
				return_value = 2;
				break;
			case LOGIN_ERRATO:
				printf("Errore: username o password errati\n");
				return_value = 2;
				break;
			case TERZO_LOGIN_ERRATO:
				printf("Errore: terzo tentativo errato di accesso. IP bloccato per %i minuti\n", MINUTI_DI_BLOCCO_IP);
				return_value = 3;
				break;
			case IP_BLOCCATO:
				printf("Errore: il tuo IP e' temporanemente bloccato. Riprova piu' tardi\n");
				return_value = 2;
				break;
			default:
				printf("Errore sconosciuto\n");
				return_value = 2;
		}
	}
	else if (msg[0] == DATI) {
		printf("Login effettuato!\n");
		// Copia il session_id inviato dal server
		strcpy(session_id, msg + 1);
		return_value = 1;
	}
	else {
		printf("Messaggio sconosciuto\n");
		return_value = 2;
	}
	
	fflush(stdout);
	free(msg);
	
	return return_value;
}

/* Invia al server il comando invia_giocata. Il comando invia una schedina valida al server.
 * Il messaggio da inviare ha il seguente formato
 * ---------------------------------------------------------
 * | session_id (stringa + '\\0') | schedina (serializzata) |
 * ---------------------------------------------------------
 * 
 * @return -1 in caso di errore interno, 0 in caso di chiusura della connessione,
 *		1 in caso di successo, 2 in caso di fallimento
 */
int eseguiInviaGiocata (const int socket, char** parsed_comando, const size_t len, const char* session_id)
{
	int ret, i, 
		base_index = 2; // contiene l'indice dell'elemento di parsed_comando attualmente in analisi
	struct schedina sched;	// struttura che conterra' la schedina inserita dall'utente
	char* schedina_serializzata;
	char* messaggio;
	uint8_t* risposta;	// conterra' la risposta del server
	uint16_t len_schedina_serializzata;
	
	//
	// COSTRUISCI LA SCHEDINA
	//
	// Ruote
	if (!strcmp(parsed_comando[base_index], "tutte")) { // Inserisci tutte le ruote
		sched.quanteRuote = QUANTE_RUOTE;
		sched.ruote = malloc(sched.quanteRuote * sizeof(int));
		
		for (i = 0; i < QUANTE_RUOTE; ++i) {
			sched.ruote[i] = i;
		}
		
		base_index += 2;
	}
	else {
		// Conta le ruote
		i = 0;
		while (strcmp(parsed_comando[base_index + i], "-n")) {
			i++;
		}

		sched.quanteRuote = i;
		sched.ruote = malloc(sched.quanteRuote * sizeof(int));
		
		// Estrai le ruote dalla stringa
		for (i = 0; strcmp(parsed_comando[base_index + i], "-n"); ++i) {
			sched.ruote[i] = convertiRuotaStringToInt(parsed_comando[base_index + i]);
		}
		
		base_index += (sched.quanteRuote + 1);
	}
	
	
	
	// NUMERI GIOCATI
	// Conta i numeri
	// (in fase di convalida del comando abbiamo gia' testato che i numeri sono meno di QUANTITA_MASSIMA_NUMERI_SCHEDINA)
	i = 0;
	while (strcmp(parsed_comando[base_index + i], "-i")) {
		i++;
	}

	sched.quantiNumeri = i;
	sched.numeriGiocati = malloc(sched.quantiNumeri * sizeof(int));

	// Estrae i numeri giocati da parsed_comando
	for (i = 0; strcmp(parsed_comando[base_index + i], "-i"); ++i) {
		sched.numeriGiocati[i] = atoi(parsed_comando[base_index + i]);
	}
	
	base_index += (sched.quantiNumeri + 1);
	
	// IMPORTI
	i = 0;
	while (base_index + i < len) {
		i++;
	}
	
	sched.quantiImporti = i;
	sched.importi = malloc(sched.quantiImporti * sizeof(double));
	
	// Estrae gli importi da parsed_comando
	for (i = 0; base_index + i < len; ++i) {
		sched.importi[i] = atof(parsed_comando[base_index + i]);
	}
	
	// Serializza schedina
	schedina_serializzata = serializza_schedina_txt(sched, &len_schedina_serializzata);
	
	// Crea il messaggio
	messaggio = malloc(LUNGHEZZA_SESSION_ID + 1 + len_schedina_serializzata);
	strcpy(messaggio, session_id);
	memcpy(messaggio + LUNGHEZZA_SESSION_ID + 1, schedina_serializzata, len_schedina_serializzata);
	free(schedina_serializzata);

	ret = inviaComando(socket, C_INVIA_GIOCATA, messaggio, LUNGHEZZA_SESSION_ID + 1 + len_schedina_serializzata);
	
	free(messaggio);
	free(sched.ruote);
	free(sched.numeriGiocati);
	free(sched.importi);
	
	if (ret < 0) return -1;
	
	// La risposta del server, se positiva, e' un messaggio di tipo DATI contenente la stringa "OK"
	ret = attendiRisposta(socket, (void**)&risposta);
	if (ret <= 0) {
		return (ret == 0) ? 0 : -1;
	}
	
	if (risposta[0] != DATI) {
		fprintf(stderr, "Invio schedina fallito: errore del server\n");
		free(risposta);
		return 2;
	}
	
	printf("Schedina inviata correttamente\n");
	free(risposta);
	return 1;
}

/* Invia al server il comando di vedi_giocate <tipo>.
 * Il comando mostra le giocate effettuate dall'utente che sono gia' state estratte (se il tipo e' 0)
 * o che non sono state ancora estratte (se tipo e' 1)
 * Il messaggio da inviare e' nel formato
 *		------------------------------------------------
 *		| session_id (stringa + '\\0') | tipo (uint8_t) |
 *		------------------------------------------------
 * 
 * Il messaggio ricevuto dal server (se il comando ha avuto successo) e' nel formato
 *		--------------------------------------------------
 *		| DATI (uint8_t) | schedine serializzate | '\\0' |
 *		--------------------------------------------------
 *  
 * @socket descrittore del socket su cui comunicare
 * @parsed_comando comando dopo il parse
 * @session_id id di sessione da inviare
 * 
 * @return -1 in caso di errore, 0 se il server chiude la connessione,
 *     1 se il comando viene eseguito con successo, 2 se il comando fallisce
 */
int eseguiVediGiocate (const int socket, char** parsed_comando, const char* session_id)
{
	int ret, i, offset;
	uint8_t tipo;
	char msg[LUNGHEZZA_SESSION_ID + 1 + sizeof(tipo)];
	char* risposta;
	uint16_t lunghezza_risposta;
	
	// Prepara il messaggio di richiesta al server
	tipo = (uint8_t)atoi(parsed_comando[1]);
	memcpy(msg, session_id, LUNGHEZZA_SESSION_ID + 1);
	memcpy(msg + LUNGHEZZA_SESSION_ID + 1, &tipo, sizeof(tipo));
	
	ret = inviaComando(socket, VEDI_GIOCATE, msg, LUNGHEZZA_SESSION_ID + 1 + sizeof(tipo));
	if (ret < 0) return -1;
	
	ret = attendiRisposta(socket, (void**)&risposta);
	if (ret <= 0) {
		return (ret == 0) ? 0 : -1;
	}
	
	lunghezza_risposta = ret;
	
	// Decodifica tipo di risposta
	if (risposta[0] == ERR) {
		int return_value = 2;	// valore di ritorno
		switch ((uint8_t)risposta[1]){
			case FILE_VUOTO: // non stampa nulla, ma non restituisce errore
				printf("\n");
				return_value = 1;
				break;
				
			case MESSAGGIO_NON_COMPRENSIBILE:
				printf("Il comando e' errato\n");
				break;
				
			default:
				printf("Errore sconosciuto\n");
		}
		fflush(stdout);
		free(risposta);
		return return_value;
	}
	else if (risposta[0] != DATI) {
		printf("Errore: risposta del server non comprensibile\n");
		fflush(stdout);
		free(risposta);
		return 2;
	}
	
	// STAMPA SCHEDINA
	offset = 1;
	i = 1;
	while (offset < lunghezza_risposta - 1) { // non considerare il null terminator
		struct schedina temp;
		int quanti_byte_letti;
		int j;
		
		// Deserializza le schedine
		temp = deserializza_schedina_txt(risposta + offset, &quanti_byte_letti);
		offset += quanti_byte_letti;
		
		// Stampa schedina a video
		printf("%i) ", i);
		
		for (j = 0; j < temp.quanteRuote; ++j) {
			printf("%s ", convertiRuotaIntToString(temp.ruote[j]));
		}
		
		for (j = 0; j < temp.quantiNumeri; ++j) {
			printf("%i ", temp.numeriGiocati[j]);
		}
		
		for (j = temp.quantiImporti - 1; j >= 0; --j) {
			if (temp.importi[j] != 0) {
				printf("* %.2f %s ", temp.importi[j], getTipoDiPuntata(j, MINUSCOLO));
			}
		}
		
		printf("\n");
		
		// Dealloca campi di temp
		free(temp.ruote);
		free(temp.numeriGiocati);
		free(temp.importi);
		
		i++;
	}
	fflush(stdout);
	free(risposta);
	
	return 1;
}

/* Invia al server il comando di vedi_estrazione <n> <ruota>.
 * Mostra all'utente i risultati delle ultime n estrazioni sulla ruota specificata.
 * Se la ruota non e' specificata, mostra le ultime n estrazioni di tutte le ruote.
 * Il messaggio da inviare e' nel formato
 *		-----------------------------------------------------------------
 *		| session_id (stringa + '\\0') | n (uint32_t) | ruota (uint8_t) |
 *		-----------------------------------------------------------------
 *  
 * @socket descrittore del socket su cui comunicare
 * @parsed_comando comando dopo il parse
 * @len lunghezza di parsed_comando
 * @session_id id di sessione da inviare
 * 
 * @return -1 in caso di errore, 0 se il server chiude la connessione,
 *     1 se il comando viene eseguito con successo, 2 se il comando fallisce
 */
int eseguiVediEstrazione (const int socket, char** parsed_comando, const size_t len, const char* session_id)
{
	int ret, i, quante_ruote_stampate;
	uint32_t n, n_hton;	// parametro n del comando, rispettivamente in formato host e network
	uint8_t ruota;
	char msg[LUNGHEZZA_SESSION_ID + 1 + sizeof(n) + sizeof(ruota)];
	uint8_t* risposta;
	uint16_t lunghezza_risposta;
	
	// Lettura degli argomenti del comando
	n = (uint32_t)atoi(parsed_comando[1]);
	ruota = (len < 3) ? RUOTA_NON_SPECIFICATA : (uint8_t)convertiRuotaStringToInt(parsed_comando[2]);
	
	// Conversione di n in fomato network
	n_hton = htonl(n);
	
	// Costruzione del messaggio
	memcpy(msg, session_id, LUNGHEZZA_SESSION_ID + 1);
	memcpy(msg + LUNGHEZZA_SESSION_ID + 1, &n_hton, sizeof(n_hton));
	memcpy(msg + LUNGHEZZA_SESSION_ID + 1 + sizeof(n_hton), &ruota, sizeof(ruota));
	
	ret = inviaComando(socket, VEDI_ESTRAZIONE, msg, LUNGHEZZA_SESSION_ID + 1 + sizeof(n_hton) + sizeof(ruota));
	if (ret < 0) return -1;
	
	ret = attendiRisposta(socket, (void**)&risposta);
	if (ret <= 0) {
		return ret;
	}
	
	lunghezza_risposta = (uint16_t)ret;
	
	// Decodifica tipo di risposta
	if (risposta[0] == ERR) {
		switch ((uint8_t)risposta[1]){
			case FILE_VUOTO: // Non stampare nulla, ma non segnalare nemmeno errore al chiamante
				printf("\n");
				return 1;
			case MESSAGGIO_NON_COMPRENSIBILE:
				printf("Il comando e' errato\n");
				break;
			default:
				printf("Errore sconosciuto\n");
		}
		fflush(stdout);
		free(risposta);
		return 2;
	}
	else if (risposta[0] != DATI) {
		printf("Errore: risposta del server non comprensibile\n");
		fflush(stdout);
		free(risposta);
		return 2;
	}
	
	// STAMPA ESTRAZIONE
	i = 1;
	quante_ruote_stampate = 0;
	
	while (i < lunghezza_risposta) {
		int j;
		uint8_t ruota_da_stampare = (uint8_t)risposta[i];	// indice della ruota attualmente in esame
		const char* ruota_da_stampare_string = convertiRuotaIntToString((int)ruota_da_stampare); // nome della suddetta ruota
		
		printf("%s\t", ruota_da_stampare_string);
		
		// Siccome alcune stringhe sono piu' lunghe di un tab, questa printf e' necessaria per allineare
		// quelle meno corte a quelle piu' lunghe
		if (strlen(ruota_da_stampare_string) < 8) {
			printf("\t");
		}
		
		// Stampa tutti i numeri estratti
		for (j = 0; j < QUANTI_NUMERI_ESTRATTI; ++j) {
			// Estrae il numero da stampare e lo converte in formato host
			uint32_t numero_da_stampare_network = *(uint32_t*)(risposta + i + sizeof(uint8_t) + j * sizeof(uint32_t));
			uint32_t numero_da_stampare = ntohl(numero_da_stampare_network);
			
			printf("%2u\t", numero_da_stampare);
		}
		
		printf("\n");
		
		// Teniamo conto del numero delle ruote stampate cosicche', se l'utente ha richiesto
		// le estrazioni di tutte le ruote, inseriamo un newline aggiuntivo per separare i blocchi
		// di estrazioni distinti (ovvero andiamo a capo ogni volta che viene stampato un intero blocco di ruote,
		// ovvero QUANTE_RUOTE ruote)
		quante_ruote_stampate++;
		if (ruota == RUOTA_NON_SPECIFICATA && (quante_ruote_stampate % QUANTE_RUOTE) == 0) {
			printf("\n");
		}
		
		fflush(stdout);
		
		i += (QUANTI_NUMERI_ESTRATTI * sizeof(uint32_t) + sizeof(uint8_t));
	}
	
	printf("\n");
	fflush(stdout);
	
	free(risposta);
	return 1;
}

/* Stampa l'elenco delle vincite ricevuto dal server.
 * 
 * @risposta stringa contenente le vincite deserializzate
 * @lunghezza_risposta lunghezza della risposta 
 */
void stampaVincite (const char* risposta, const int lunghezza_riposta)
{
	int i, j, ret;
	int quanti_byte_letti = 0;	// "cursore" per l'analisi della stringa risposta
	
	// Variabili per la deserializzazione
	time_t timestamp;
	long int timestamp_int;
	int quante_ruote;
	unsigned int ruota;
	int quanti_numeri_vincenti;
	int numero_vincente;
	int quanti_importi_vinti;
	double importo_vinto;
	
	struct tm* timeinfo;	// struttura per stampare il timestamp in formato personalizzato
	double totale_vincite[QUANTI_TIPI_PREMIO]; // contiene il totale delle vincite per ogni tipo di puntata
	
	memset(totale_vincite, 0, sizeof(double) * QUANTI_TIPI_PREMIO);
	
	while (quanti_byte_letti < lunghezza_riposta - 1) {	// risposta e' terminata dal carattere '\0', che non va letto
		// Preleva il timestamp e il numero delle ruote
		sscanf(risposta + quanti_byte_letti, "%ld %i %n", &timestamp_int, &quante_ruote, &ret);
		timestamp = (time_t)timestamp_int;
		quanti_byte_letti += ret;

		// Stampa il timestamp
		timeinfo = localtime(&timestamp);
		printf("Estrazione del %02i-%02i-%4i ore %02i:%02i\n", timeinfo->tm_mday, timeinfo->tm_mon,
				timeinfo->tm_year + 1900, timeinfo->tm_hour, timeinfo->tm_min);

		// Stampa le ruote vincenti
		for (i = 0; i < quante_ruote; ++i) {
			sscanf(risposta + quanti_byte_letti, "%u %i %n", &ruota, &quanti_numeri_vincenti, &ret);
			quanti_byte_letti += ret;

			printf("%s\t", convertiRuotaIntToString(ruota));

			// Stampa i numeri vincenti
			for (j = 0; j < quanti_numeri_vincenti; ++j) {
				sscanf(risposta + quanti_byte_letti, "%i %n", &numero_vincente, &ret);
				quanti_byte_letti += ret;

				printf("%i ", numero_vincente);
			}

			printf("\t>>\t");

			sscanf(risposta + quanti_byte_letti, "%i %n", &quanti_importi_vinti, &ret);
			quanti_byte_letti += ret;

			// Stampale vincite in denaro
			for (j = 0; j < quanti_importi_vinti; ++j) {
				sscanf(risposta + quanti_byte_letti, "%lf %n", &importo_vinto, &ret);
				quanti_byte_letti += ret;

				printf("%s %.2lf  ", getTipoDiPuntata(j, INIZIALE_MAIUSCOLA), importo_vinto);

				totale_vincite[j] += importo_vinto;
			}

			printf("\n***********************************************\n");
			
			quanti_byte_letti++; // Ogni vincita e' separata da un carattere '|'
		}
	}
	
	printf("\n");
	
	// Stampa i totale delle vincite
	for (i = 0; i < QUANTI_TIPI_PREMIO; ++i) {
		printf("Vincite su %s: %.2lf\n", getTipoDiPuntata(i, CAPS_LOCK), totale_vincite[i]);
	}
	
	printf("\n");
}

/* Invia il comando !vedi_vincite. Il corpo del messaggio contiene soltanto il session_id
 * 
 * @socket socket su cui e' attiva la connessione con il server
 * @session_id stringa contenente il session_id dell'utente
 * 
 * @return -1 in caso di fallimento, 0 se il server chiude la connessione, 1 in caso di esito positivo del comando
 */
int eseguiVediVincite (const int socket, const char* session_id)
{
	int ret;
	char msg[LUNGHEZZA_SESSION_ID + 1]; // session_id + null terminator
	uint8_t* risposta;
	uint16_t lunghezza_risposta;
	
	// Crea il messaggio
	memcpy(msg, session_id, LUNGHEZZA_SESSION_ID + 1);
	
	ret = inviaComando(socket, VEDI_VINCITE, msg, LUNGHEZZA_SESSION_ID + 1);
	if (ret < 0) return -1;
	
	ret = attendiRisposta(socket, (void**)&risposta);
	if (ret <= 0) return ret;
	
	lunghezza_risposta = (uint16_t)ret;
	
	// Decodifica la risposta
	if (risposta[0] == ERR) {
		switch ((uint8_t)risposta[1]){
			case FILE_VUOTO: // Non stampare nulla, ma non segnalare nemmeno errore al chiamante
				printf("\n");
				return 1;
			case MESSAGGIO_NON_COMPRENSIBILE:
				printf("Il comando e' errato\n");
				break;
			default:
				printf("Errore sconosciuto\n");
		}
		fflush(stdout);
		free(risposta);
		return 2;
	}
	else if (risposta[0] != DATI) {
		printf("Errore: risposta del server non comprensibile\n");
		fflush(stdout);
		free(risposta);
		return 2;
	}
	
	stampaVincite((char*)(risposta + 1), (int)lunghezza_risposta - 1);
	return 1;
}

int main (int argc, char** argv)
{
	// Variabili per connessione TCP
	int client_socket;
	struct sockaddr_in server_addr;
	char IP_server[INET_ADDRSTRLEN]; // indirizzo IP del server in formato presentazione
	uint16_t porta_server;
	int disconnetti = 0;
	
	// Variabili di appoggio
	int ret, i;
	
	// Messaggi
	char buffer[BUFFER_SIZE];
	char** parsed_comando = NULL;
	size_t len_parsed_comando = 0;
	char session_id[LUNGHEZZA_SESSION_ID + 1];
	int loggato = 0;
	
	// Controlla che l'utente abbia rispettato il numero dei parametri in ingresso
	if (argc < 3) {
		fprintf(stderr, "Errore: il programma deve essere lanciato con i seguenti parametri\n");
		fprintf(stderr, "    ./lotto_client <IP server> <porta server>\n");
		fflush(stderr);
		exit(EXIT_FAILURE);
	}
	
	// Legge parametri inseriti da terminale
	strcpy(IP_server, argv[1]);
	porta_server = (uint16_t)atoi(argv[2]);
	
	// Apre la socket e la inizializza per la connessione con il server
	client_socket = socket(AF_INET, SOCK_STREAM, 0);
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(porta_server);
	inet_pton(AF_INET, IP_server, &server_addr.sin_addr.s_addr);
	
	// Connette la socket con il server
	ret = connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
	if (ret < 0) {
		perror("Errore in fase di connessione");
		exit(EXIT_FAILURE);
	}
	
	stampaMessaggioAvvio();
	
	// In loop, attende l'inserimento di un comando da terminale, ne controlla la sintassi 
	// e se corretto esegue le azioni associate al comando
	while (1) {
		int tipo_comando;
		
		memset(buffer, 0, BUFFER_SIZE);
		
		// Legge il messaggio
		printf("> "); fflush(stdout);
		fgets(buffer, BUFFER_SIZE, stdin);	// legge e copia l'intera riga (compreso il new line)
		buffer[strlen(buffer)-1] = '\0';	// sovrascrive il newline
		
		// Interpreta il comando facendone il parse e ne verifica la sintassi
		parsed_comando = parseComando(buffer, strlen(buffer), &len_parsed_comando);
		ret = validaComando(parsed_comando, len_parsed_comando);
		if (ret < 0) { // se la sintassi e' errata, stampa un messaggio di errore
			fprintf(stderr, "Errore: il comando non e' stato riconosciuto\n"
					"Per vedere la sintassi corretta di tutti i comandi, scrivere\n        !help\n\n");
			fflush(stderr);
			continue;
		}
		
		tipo_comando = ret;
		
		// Se l'utente ha gia' effettuato il login, non sono ammessi i comandi !login e !signup
		if (loggato && (tipo_comando == C_SIGNUP || tipo_comando == C_LOGIN)) {
			printf("Errore: L'utente ha gia' effettuato il login sul server. "
						"Per favore disconnettersi prima di accedere con un nuovo account\n");
			continue;
		}
		
		// Se l'utente non ha ancora effettuato il login, sono ammessi solo i comandi
		// !login, !signup, !help e !esci
		if (!loggato && tipo_comando != C_SIGNUP && tipo_comando != C_HELP 
					&& tipo_comando != C_LOGIN && tipo_comando != C_ESCI) {
			printf("Errore: L'utente deve prima effettuare il login\n");
			continue;
		}
		
		// Esegui il comando inserito
		switch (tipo_comando) {
			case C_HELP:
				ret = eseguiHelp(parsed_comando, len_parsed_comando);
				if (ret )
				break;
			case C_SIGNUP:
				ret = eseguiSignup(client_socket, parsed_comando);
				break;
			case C_LOGIN:
				ret = eseguiLogin(client_socket, parsed_comando, session_id);
				loggato = (ret == 1) ? 1 : 0;
				if (ret == 3) disconnetti = 1; //Disconnessione al terzo tentativo errato
				break;
			case C_INVIA_GIOCATA:
				ret = eseguiInviaGiocata(client_socket, parsed_comando, len_parsed_comando, session_id);
				break;
			case C_VEDI_GIOCATE:
				ret = eseguiVediGiocate(client_socket, parsed_comando, session_id);
				break;
			case C_VEDI_ESTRAZIONE:
				ret = eseguiVediEstrazione(client_socket, parsed_comando, len_parsed_comando, session_id);
				break;
			case C_VEDI_VINCITE:
				ret = eseguiVediVincite(client_socket, session_id);
				break;
			case C_ESCI:
				disconnetti = 1;
				break;
		}
		
		// Libera memoria dinamica allocata
		for (i = 0; i < len_parsed_comando; ++i) {
			free(parsed_comando[i]);
		}
		free(parsed_comando);
		
		// Controlla se bisogna chiudere l'applicazione, effettuando una disconnessione dal server
		// (se e' avvenuto un errore, se il server ha gia' chiuso la connessione o con il comando !esci)
		if (ret <= 0 || disconnetti){
			printf("Disconnessione\n"); 
			fflush(stdout);
			break;
		}
	}
	
	// CHIUSURA
	close(client_socket);
	
	return 0;
}
