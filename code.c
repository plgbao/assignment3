#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "dev/leds.h"
#include <stdio.h>
#include <string.h>

// Cấu hình mạng
#define BROADCAST_PORT 129
#define MAX_NEIGHBORS 5
#define PRINT_INTERVAL (CLOCK_SECOND * 10)

/*---------------------------------------------------------------------------*/
PROCESS(example_broadcast_process, "Neighbor Statistics");
AUTOSTART_PROCESSES(&example_broadcast_process);
/*---------------------------------------------------------------------------*/

typedef struct {
    rimeaddr_t addr;
    uint16_t tx_count;
    uint16_t rx_count;
    int16_t rssi;
    double prr;
    clock_time_t last_seen;
} neighbor_info_t;

static neighbor_info_t neighbors[MAX_NEIGHBORS];
static uint8_t neighbor_count = 0;
static uint16_t my_tx_count = 0;

/*---------------------------------------------------------------------------*/
static void print_neighbor_table() {
    printf("\n=== Neighbor Table === Time: %lu\n", clock_seconds());
    printf("Node ID | TX | RX | RSSI | PRR %% | Age\n");
    printf("----------------------------------------\n");
    
    uint8_t i;
    for (i = 0; i < neighbor_count; i++) {
        neighbor_info_t *n = &neighbors[i];
        int age = (clock_time() - n->last_seen) / CLOCK_SECOND;
        
        printf("%d.%d\t%4u\t%4u\t%4d\t%3.0f\t%3ds\n",
               n->addr.u8[0], n->addr.u8[1],
               n->tx_count, n->rx_count,
               n->rssi, n->prr * 100,
               age);
    }
    
    if (neighbor_count == 0) {
        printf(">>> No neighbors detected! <<<\n");
        printf("Check:\n");
        printf("- All nodes use same broadcast port\n");
        printf("- Nodes are in radio range\n");
    }
    printf("My TX count: %u\n", my_tx_count);
    printf("==============================\n");
}

static void broadcast_recv(struct broadcast_conn *c, const rimeaddr_t *from) {
    if (packetbuf_datalen() < sizeof(uint16_t)) {
        return;
    }

    int16_t rssi = packetbuf_attr(PACKETBUF_ATTR_RSSI);
    
    neighbor_info_t *n = NULL;
    uint8_t i;
    for (i = 0; i < neighbor_count; i++) {
        if (rimeaddr_cmp(&neighbors[i].addr, from)) {
            n = &neighbors[i];
            break;
        }
    }
    
    if (!n) {
        if (neighbor_count >= MAX_NEIGHBORS) {
            return;
        }
        n = &neighbors[neighbor_count++];
        rimeaddr_copy(&n->addr, from);
        n->tx_count = 0;
        n->rx_count = 0;
        n->prr = 0;
    }
    
    n->rx_count++;
    n->last_seen = clock_time();
    n->rssi = rssi;
    
    uint16_t sender_tx;
    memcpy(&sender_tx, packetbuf_dataptr(), sizeof(uint16_t));
    n->tx_count = sender_tx;
    
    if (n->tx_count > 0) {
        n->prr = (double)n->rx_count / n->tx_count;
    }
    
    printf("RX from %d.%d: TX=%u RSSI=%d PRR=%.1f%%\n",
           from->u8[0], from->u8[1],
           sender_tx, rssi, n->prr * 100);
    
    leds_toggle(LEDS_RED);
}

static const struct broadcast_callbacks broadcast_call = {broadcast_recv};
static struct broadcast_conn broadcast;

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(example_broadcast_process, ev, data) {
    static struct etimer send_timer, print_timer;
    static uint16_t packet_data;

    PROCESS_EXITHANDLER(broadcast_close(&broadcast);)
    PROCESS_BEGIN();

    printf("Starting neighbor monitoring\n");
    printf("Broadcast port: %d\n", BROADCAST_PORT);
    
    broadcast_open(&broadcast, BROADCAST_PORT, &broadcast_call);
    
    etimer_set(&print_timer, PRINT_INTERVAL);
    etimer_set(&send_timer, CLOCK_SECOND * 3 + random_rand() % (CLOCK_SECOND * 3));

    while(1) {
        PROCESS_WAIT_EVENT();
        
        if (ev == PROCESS_EVENT_TIMER && data == &print_timer) {
            print_neighbor_table();
            etimer_reset(&print_timer);
        }
        else if (ev == PROCESS_EVENT_TIMER && data == &send_timer) {
            my_tx_count++;
            packet_data = my_tx_count;
            
            packetbuf_copyfrom(&packet_data, sizeof(packet_data));
            broadcast_send(&broadcast);
            
            printf("TX #%u\n", my_tx_count);
            leds_toggle(LEDS_GREEN);
            
            etimer_set(&send_timer, CLOCK_SECOND * 3 + random_rand() % (CLOCK_SECOND * 3));
        }
    }

    PROCESS_END();
}