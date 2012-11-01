#include <EtherCard.h>

#include <string.h>

#define KEY "s3cr1t"

//#define NDEBUG 1
#undef NDEBUG

#define BUTTON_PIN (3)

/* digitalWrite(LED_PIN, LOW); --> turns on the LED */
//#define LED_PIN    (6) /* status led for nanode */
#define LED_PIN    (4)

#define BLINK_CLOCK (500) /* milliseconds */

#define DEBOUNCE_WAIT 200

static const uint8_t mac[] = "TEXINC" ; /* first byte should be even */

uint8_t Ethernet::buffer[1280];

static const prog_char server_name[] PROGMEM = "www.techinc.nl";
static const prog_char open_path[]   PROGMEM = "/space/?state=open&key=" KEY;
static const prog_char close_path[]  PROGMEM = "/space/?state=closed&key=" KEY;
static const prog_char check_path[]  PROGMEM = "/space/spacestate";

enum
{
	SPACE_UNKNOWN,
	SPACE_OPENING,
	SPACE_OPEN,
	SPACE_CLOSING,
	SPACE_CLOSED,

} space_state = SPACE_UNKNOWN;

enum
{
	NETWORK_ERROR,
	NETWORK_OK,

} network_state = NETWORK_ERROR;

enum
{
	BLINK_OFF  = 0x00,
	BLINK_ON   = 0xff,
	BLINK_FAST = 0x55,
	BLINK_SLOW = 0xf0,

} blink_mode = BLINK_FAST;

uint8_t button_state = LOW;
long button_debounce;

void try_init_network()
{
	network_state = NETWORK_ERROR;

	if (ether.begin(sizeof(Ethernet::buffer), mac) == 0)
	{
#ifdef NDEBUG
		Serial.println( "Failed to access Ethernet controller");
#endif
		return;
	}

#ifdef NDEBUG
	Serial.println( "Initialised Ethernet controller");
#endif

	if (!ether.dhcpSetup())
	{
#ifdef NDEBUG
		Serial.println("DHCP failed");
#endif
		return;
	}

#ifdef NDEBUG
	ether.printIp("IP:       ", ether.myip);
	ether.printIp("GATEWAY:  ", ether.gwip);
	ether.printIp("DNS:      ", ether.dnsip);
#endif

	if (!ether.dnsLookup(server_name))
	{
#ifdef NDEBUG
		Serial.println("DNS failed");
#endif
		return;
	}

#ifdef NDEBUG
	ether.printIp("SRV:      ", ether.hisip);
#endif

	network_state = NETWORK_OK;
	network_update_state();
}

static void network_update_callback(byte status, word off, word len)
{
	if (status != 0)
	{
#ifdef NDEBUG
		Serial.print("Error: ");
		Serial.println(status);
#endif
		return;
	}
	Ethernet::buffer[sizeof(Ethernet::buffer)-1] = 0;

	/* Scan for HTTP body */
	while ( (len >= 4) && (Ethernet::buffer[off] != '\0') )
	{
		if ( (Ethernet::buffer[off] == '\x0d') &&
		     (Ethernet::buffer[off+1] == '\x0a') &&
		     (Ethernet::buffer[off+2] == '\x0d') &&
		     (Ethernet::buffer[off+3] == '\x0a') )
			break;
		off++;
		len--;
	}

#ifdef NDEBUG
	Serial.println((const char*) Ethernet::buffer + off);
#endif

	if (strstr((const char*)Ethernet::buffer+off, "open"))
		space_state = SPACE_OPEN;
	else if (strstr((const char*)Ethernet::buffer+off, "closed"))
		space_state = SPACE_CLOSED;
	else
		space_state = SPACE_UNKNOWN;
}

static void network_update_state()
{
	const prog_char PROGMEM *path = check_path;

	if (space_state == SPACE_CLOSING)
		path = close_path;

	if (space_state == SPACE_OPENING)
		path = open_path;

#ifdef NDEBUG
	Serial.println("Checking/updating upstream state");
#endif

	ether.browseUrl(path, "", server_name, network_update_callback);
}

void button_press()
{
	switch (space_state)
	{
		case SPACE_UNKNOWN:
		case SPACE_CLOSING:
		case SPACE_CLOSED:
			space_state = SPACE_OPENING;
#ifdef NDEBUG
			Serial.println("OPENING");
#endif
			break;
		case SPACE_OPENING:
		case SPACE_OPEN:
			space_state = SPACE_CLOSING;
#ifdef NDEBUG
			Serial.println("CLOSING");
#endif
			break;
	}

	if ( network_state == NETWORK_OK )
		network_update_state();
}

void blink()
{
	static uint8_t bit=0;

	switch (space_state)
	{
		case SPACE_UNKNOWN:
			blink_mode = BLINK_SLOW;
			break;
		case SPACE_OPENING:
		case SPACE_CLOSING:
			blink_mode = BLINK_FAST;
			break;
		case SPACE_OPEN:
			blink_mode = BLINK_ON;
			break;
		case SPACE_CLOSED:
			blink_mode = BLINK_OFF;
			break;
	}

	if (network_state == NETWORK_ERROR)
		blink_mode = BLINK_SLOW;

	digitalWrite(LED_PIN, (blink_mode&(1<<bit)) ? LOW : HIGH);

	bit++;
	bit &= 0x7;
}

void setup()
{
#ifdef NDEBUG
	Serial.begin(57600);
	Serial.println("SPACEAPI (DEBUG MODE ON)");
#endif
	pinMode(BUTTON_PIN, INPUT);
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, HIGH);
	ether.begin(sizeof(Ethernet::buffer), mac);
	button_debounce = millis()-DEBOUNCE_WAIT-1;
}

void loop()
{
	uint8_t c;
	long cur=millis(), last;

	if ( network_state == NETWORK_ERROR )
		try_init_network();

	for ( c=0 ; c<255 ; c++ )
	{
		for ( last = cur ; cur-last < BLINK_CLOCK ; cur = millis() )
		{
			ether.packetLoop(ether.packetReceive());

			if ( (digitalRead(BUTTON_PIN) != button_state) &&
			     (cur-button_debounce > DEBOUNCE_WAIT) )
			{
				button_state = HIGH+LOW - button_state;
				button_debounce = cur;
				if (button_state == HIGH)
					button_press();
			}

		}
		blink();
	}
}

