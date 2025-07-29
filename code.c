#include "contiki.h"
#include "contiki-lib.h"
#include "contiki-net.h"
#include <string.h>
#define DEBUG DEBUG_PRINT
#include "net/ip/uip-debug.h"
#include "dev/watchdog.h"
#include "dev/leds.h"
#include "net/rpl/rpl.h"
#include "dev/leds.h"
#include "sls.h"	
/*---------------------------------------------------------------------------*/
#define UIP_IP_BUF   ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF  ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
#define MAX_PAYLOAD_LEN 120
/* Temperature simulation defines */
#define TEMP_MIN 20
#define TEMP_MAX 35
#define TEMP_STEP 1
/*---------------------------------------------------------------------------*/
static struct uip_udp_conn *server_conn;
static char buf[MAX_PAYLOAD_LEN];
static uint16_t len;
/* SLS define */
static struct led_struct_t led_db;
static struct net_struct_t net_db;
static char str_reply[50];
static char str_cmd[10];
static char str_arg[10];
static char str_rx[MAX_PAYLOAD_LEN];
/* Temperature simulation variables */
static int16_t current_temp = 25;  // Start at 25°C
static int8_t temp_direction = 1;  // 1 for increasing, -1 for decreasing
static uint8_t temp_simulation_enabled = 0; // Flag to control simulation
static uint8_t red_led_state = 0; // Independent red LED control (0=OFF, 1=ON)
static 	radio_value_t aux;
/* define prototype of function call */
static void get_radio_parameter(void);
static void init_default_parameters(void);
static void reset_parameters(void);
static void simulate_temperature(void);
static int16_t get_temp(void);
static 	char *p;
/*---------------------------------------------------------------------------*/
PROCESS(udp_echo_server_process, "UDP echo server process");
AUTOSTART_PROCESSES(&udp_echo_server_process);
/*---------------------------------------------------------------------------*/
/* Temperature simulation function */
static void simulate_temperature(void) {
    if (!temp_simulation_enabled) return;
    
    // Update temperature based on direction
    current_temp += (temp_direction * TEMP_STEP);

    // Change direction at boundaries
    if (current_temp >= TEMP_MAX) {
        temp_direction = -1;
        current_temp = TEMP_MAX;
    } else if (current_temp <= TEMP_MIN) {
        temp_direction = 1;
        current_temp = TEMP_MIN;
    }
    
    // Temperature simulation does NOT control LED anymore
    // LED is controlled independently via commands
    
    // Print for Cooja console output
    printf("[Node %u] Temp: %d°C (Red LED controlled independently)\n", 
           linkaddr_node_addr.u8[0], current_temp);
}

/* Get current temperature */
static int16_t get_temp(void) {
    return current_temp;
}

/*---------------------------------------------------------------------------*/
static void tcpip_handler(void) {

 	memset(buf, 0, MAX_PAYLOAD_LEN);
 	if(uip_newdata()) {
    	leds_on(LEDS_BLUE); // Use blue for network activity indication
    	len = uip_datalen();
   		memcpy(buf, uip_appdata, len);
    	PRINTF("Received from [");
    	PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
    	PRINTF("]:%u\n", UIP_HTONS(UIP_UDP_BUF->srcport));
		PRINTF("%u bytes DATA: %s; ",len, buf);
		
    	uip_ipaddr_copy(&server_conn->ripaddr, &UIP_IP_BUF->srcipaddr);
    	server_conn->rport = UIP_UDP_BUF->srcport;

		get_radio_parameter();
		reset_parameters();
		
		strcpy(str_rx,buf);
		if (SLS_CC2538DK_HW)
			sscanf(str_rx,"%s %s",str_cmd, str_arg);
		else { /* used for SKY */
    		PRINTF("str_rx = %s", str_rx);
  			p = strtok (str_rx," ");  
			if (p != NULL) {
				strcpy(str_cmd,p);
    			p = strtok (NULL, " ,");
				if (p != NULL) {strcpy(str_arg,p);}			
			}
		}
		
		PRINTF("CMD = %s ARG = %s\n",str_cmd, str_arg);		
		
		/* Original LED commands - now control red LED independently */
		if (strstr(str_cmd,SLS_LED_ON)!=NULL) {
			PRINTF ("Execute CMD = %s\n",SLS_LED_ON);
			leds_on(LEDS_RED);  // Control red LED
			red_led_state = 1;  // Update state tracking
			sprintf(str_reply, "Red LED turned ON");
			led_db.status = LED_ON;
		}
		else if (strstr(str_cmd, SLS_LED_OFF)!=NULL) {
			PRINTF ("Execute CMD = %s\n",SLS_LED_OFF);
			leds_off(LEDS_RED); // Control red LED
			red_led_state = 0;  // Update state tracking
			sprintf(str_reply, "Red LED turned OFF");
			led_db.status = LED_OFF;
		}
		else if (strstr(str_cmd, SLS_LED_ALL_ON)!=NULL) {
			PRINTF ("Execute CMD = %s\n",SLS_LED_ALL_ON);
			leds_on(LEDS_GREEN);
			leds_on(LEDS_RED);
			leds_on(LEDS_BLUE);
			red_led_state = 1; // Update red LED state tracking
			sprintf(str_reply, "All LEDs turned ON");
		}
		else if (strstr(str_cmd, SLS_LED_DIM)!=NULL) {
			PRINTF ("Execute CMD = %s to value %s\n",SLS_LED_DIM, str_arg);
			leds_toggle(LEDS_BLUE);
			sprintf(str_reply, "Blue LED toggled, dim = %s", str_arg);
			led_db.status = LED_DIM;
			led_db.dim = atoi(str_arg);
		}
		
		/* Temperature control commands */
		else if (strstr(str_cmd, "TEMP_SIM_ON")!=NULL) {
			PRINTF ("Execute CMD = TEMP_SIM_ON\n");
			temp_simulation_enabled = 1;
			sprintf(str_reply, "Temperature simulation started");
		}
		else if (strstr(str_cmd, "TEMP_SIM_OFF")!=NULL) {
			PRINTF ("Execute CMD = TEMP_SIM_OFF\n");
			temp_simulation_enabled = 0;
			// Do NOT turn off red LED - it's controlled independently
			sprintf(str_reply, "Temperature simulation stopped");
		}
		else if (strstr(str_cmd, "SET_TEMP")!=NULL) {
			PRINTF ("Execute CMD = SET_TEMP to value %s\n", str_arg);
			int new_temp = atoi(str_arg);
			if (new_temp >= TEMP_MIN && new_temp <= TEMP_MAX) {
				current_temp = new_temp;
				// Temperature does NOT control LED anymore
				sprintf(str_reply, "Temperature set to %d°C", current_temp);
			} else {
				sprintf(str_reply, "Invalid temperature. Range: %d-%d°C", TEMP_MIN, TEMP_MAX);
			}
		}
		else if (strstr(str_cmd, "GET_TEMP")!=NULL) {
			PRINTF ("Execute CMD = GET_TEMP\n");
			int16_t temp = get_temp();
			sprintf(str_reply, "Current temperature: %d°C", temp);
		}
		
		/* Status query commands */
		else if (strstr(str_cmd, SLS_GET_LED_STATUS)!=NULL) {
			sprintf(str_reply, "LED: id=%u;power=%u;temp=%d;dim=%u;status=0x%02X", 
					led_db.id, led_db.power, led_db.temperature, led_db.dim, led_db.status);
		}		
		else if (strstr(str_cmd, SLS_GET_NW_STATUS)!=NULL) {
			sprintf(str_reply, "Network: ch=%u;rssi=%ddBm;lqi=%u;tx_pwr=%ddBm;panid=0x%02X", 
					net_db.channel, net_db.rssi, net_db.lqi, net_db.tx_power, net_db.panid);
		}
		else if (strstr(str_cmd, "GET_TEMP_STATUS")!=NULL) {
			sprintf(str_reply, "[Node %u] Temp:%d°C;sim=%s;red_led=%s", 
					linkaddr_node_addr.u8[0], current_temp, 
					temp_simulation_enabled ? "ON" : "OFF",
					red_led_state ? "ON" : "OFF");
		}
		else {
			reset_parameters();
			sprintf(str_reply,"Unknown cmd. Use: led_on, led_off, GET_TEMP, TEMP_SIM_ON/OFF");
		}
		
		PRINTF("str_reply=%s \n",str_reply);

		/* echo back to sender */	
    	PRINTF("Echo back to [");
    	PRINT6ADDR(&UIP_IP_BUF->srcipaddr);
    	PRINTF("]:%u %u bytes\n", UIP_HTONS(UIP_UDP_BUF->srcport), strlen(str_reply));
    	uip_udp_packet_send(server_conn, str_reply, strlen(str_reply));
    	uip_create_unspecified(&server_conn->ripaddr);
    	server_conn->rport = 0;
 	}
	leds_off(LEDS_BLUE);
 	return;
}

static void reset_parameters(void) {
	memset(&str_cmd[0], 0, sizeof(str_cmd));
	memset(&str_arg[0], 0, sizeof(str_arg));
	memset(&str_reply[0], 0, sizeof(str_reply));
}

/*---------------------------------------------------------------------------*/
static void get_radio_parameter() {
	NETSTACK_RADIO.get_value(RADIO_PARAM_CHANNEL, &aux);
	net_db.channel = (unsigned int) aux;

	aux = packetbuf_attr(PACKETBUF_ATTR_RSSI);
	net_db.rssi = (int8_t)aux;

	aux = packetbuf_attr(PACKETBUF_ATTR_LINK_QUALITY);
	net_db.lqi = aux;

	NETSTACK_RADIO.get_value(RADIO_PARAM_TXPOWER, &aux);
	net_db.tx_power = aux;
}
/*---------------------------------------------------------------------------*/
static void init_default_parameters(void) {
	led_db.id		= 0x20;				
	led_db.power	= 120;
	led_db.temperature = current_temp; // Initialize with current temperature
	led_db.dim		= 80;
	led_db.status	= LED_ON; 
	net_db.panid 	= SLS_PAN_ID;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(udp_echo_server_process, ev, data){
	static struct etimer timer;
	PROCESS_BEGIN();
	PRINTF("Initialization....\n");
	init_default_parameters();
	PRINTF("Starting UDP echo server with temperature simulation\n");
	PRINTF("Node ID: %u\n", linkaddr_node_addr.u8[0]);
	PRINTF("Available commands:\n");
	PRINTF("  TEMP_SIM_ON  - Start temperature simulation\n");
	PRINTF("  TEMP_SIM_OFF - Stop temperature simulation\n");
	PRINTF("  SET_TEMP <val> - Set temperature (%d-%d°C)\n", TEMP_MIN, TEMP_MAX);
	PRINTF("  GET_TEMP     - Get current temperature\n");
	PRINTF("  GET_TEMP_STATUS - Get temperature and simulation status\n");

	server_conn = udp_new(NULL, UIP_HTONS(0), NULL);
	udp_bind(server_conn, UIP_HTONS(3000));

	PRINTF("Listen port: 3000, TTL=%u\n", server_conn->ttl);
	PRINTF("Node ID: %u\n", linkaddr_node_addr.u8[0]);
	// Set timer for temperature simulation (every 3 seconds for Cooja)
	etimer_set(&timer, CLOCK_SECOND * 3);
	while(1) {
    	PROCESS_YIELD();
    	if(ev == tcpip_event) {
      		tcpip_handler();
    	}
    	else if(etimer_expired(&timer)) {
    		// Update temperature simulation
    		simulate_temperature();
    		led_db.temperature = current_temp; // Update LED database
    		etimer_set(&timer, CLOCK_SECOND * 3); // Reset timer for Cooja
    	}
  	}

	PROCESS_END();
}
/*---------------------------------------------------------------------------*/
