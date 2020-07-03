#ifndef COSTANTI_H
#define COSTANTI_H

// Costanti temporali comuni {
#define MINUTI_DI_BLOCCO_IP 30
// }

// Codici messaggi: ServerToClient {
#define ERR		0x00
#define DATI	0x01
// }

// Codici messaggi: ClientToServer {
#define SIGNUP			0x01
#define LOGIN			0x02
#define INVIA_GIOCATA	0x03
#define VEDI_GIOCATE	0x04
#define VEDI_ESTRAZIONE	0x05
#define VEDI_VINCITE	0x06
#define ESCI			0x07
// }

// Codici errori {
#define LOGIN_NON_EFFETTUATO	0x00
#define LOGIN_GIA_EFFETTUATO	0x01
#define SESSION_ID_ERRATO		0x02
#define LOGIN_ERRATO			0x03
#define TERZO_LOGIN_ERRATO		0x04
#define IP_BLOCCATO				0x05
#define USERNAME_OCCUPATO		0x06
#define FILE_VUOTO				0x07

#define ERRORE_INTERNO_SERVER		0xFE
#define MESSAGGIO_NON_COMPRENSIBILE	0xFF
// }

// Applicazione {
#define QUANTE_RUOTE 11
#define QUANTI_NUMERI_ESTRATTI 5
#define NUMERI_ESTRAIBILI 90
#define QUANTITA_MASSIMA_NUMERI_SCHEDINA 10

#define LUNGHEZZA_SESSION_ID 10
#define QUANTE_CIFRE 10
#define QUANTE_LETTERE 26
#define QUANTI_CARATTERI_ALFANUMERICI QUANTE_LETTERE*2+QUANTE_CIFRE
#define ASCII_PRIMA_CIFRA 48
#define ASCII_PRIMA_LETTERA_MAIUSCOLA 65
#define ASCII_PRIMA_LETTERA_MINUSCOLA 97

	// Ruote (uint8_t){
#define BARI		0x00
#define CAGLIARI	0x01 
#define FIRENZE		0x02
#define GENOVA		0x03
#define MILANO		0x04
#define NAPOLI		0x05
#define PALERMO		0x06
#define ROMA		0x07
#define TORINO		0x08
#define VENEZIA		0x09
#define NAZIONALE	0x0A
#define RUOTA_NON_SPECIFICATA 0xFF
	// }

	// Ruote (stringhe){
#define S_BARI		"bari"
#define S_CAGLIARI	"cagliari"
#define S_FIRENZE	"firenze"
#define S_GENOVA	"genova"
#define S_MILANO	"milano"
#define S_NAPOLI	"napoli"
#define S_PALERMO	"palermo"
#define S_ROMA		"roma"
#define S_TORINO	"torino"
#define S_VENEZIA	"venezia"
#define S_NAZIONALE	"nazionale"
	// }

	// Premi {
#define QUANTI_TIPI_PREMIO 5
#define PREMIO_SINGOLO	11.23
#define PREMIO_AMBO		250.0
#define PREMIO_TERNO	4500.0
#define PREMIO_QUATERNA 120000.0
#define PREMIO_CINQUINA	6000000.0

#define MINUSCOLO 0
#define CAPS_LOCK 1
#define INIZIALE_MAIUSCOLA 2
	// }
// }



#endif	// COSTANTI_H
