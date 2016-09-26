/*
 * transport.c 
 *
 *	Project 3		
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"
#include "mysock_impl.h"


enum { CSTATE_ESTABLISHED };    /* you should have more states */


/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    tcp_seq current_sequence_num;
    tcp_seq initial_sequence_num_other;
    tcp_seq current_sequence_num_other;
    tcp_seq last_byte_acked;

    tcp_seq window_size;
    tcp_seq congestion_window;
    tcp_seq bytes_unacknowledged;

    /* any other connection-wide global variables go here */
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);


int host_to_network(STCPHeader* header){
  
  header->th_seq = htonl(header->th_seq);
  header->th_ack = htonl(header->th_ack);
  header->th_win = htons(header->th_win);

  return 0;
}

int network_to_host(STCPHeader* header){
  
  header->th_seq = ntohl(header->th_seq);
  header->th_ack = ntohl(header->th_ack);
  header->th_win = ntohs(header->th_win);

  return 0;
}



/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);
    ctx->congestion_window = 3072;
    ctx->window_size = 3072;
    ctx->bytes_unacknowledged = 0;

    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */

    STCPHeader *header;
    header = (STCPHeader *) calloc(1, sizeof(STCPHeader));

    if(is_active){
      header->th_flags = 0x02;
      header->th_seq = ctx->initial_sequence_num;
      host_to_network(header);
      stcp_network_send(sd, header, sizeof(STCPHeader),NULL);
      
      ctx->current_sequence_num_other +=1;

      stcp_network_recv(sd,header,sizeof(STCPHeader));
      network_to_host(header);
      if(header->th_flags!= (0x10|0x02))
        ;//some error
      ctx->initial_sequence_num_other = header->th_seq;
      ctx->current_sequence_num_other = header->th_seq+1;

      //ack
      header->th_flags = 0x10;
      header->th_ack = ctx->current_sequence_num_other;
      host_to_network(header);
      stcp_network_send(sd, header, sizeof(STCPHeader),NULL);

    } else {
      stcp_network_recv(sd,header,sizeof(STCPHeader));
      network_to_host(header);

      if(header->th_flags!= 0x10)
        ;//some error
      
      header->th_flags = 0x02 | 0x10;
      header->th_ack = header->th_seq + 1;
      ctx->initial_sequence_num_other = header->th_seq;
      ctx->current_sequence_num_other = header->th_seq+1;
      header->th_seq = ctx->initial_sequence_num;

      ctx->initial_sequence_num_other = header->th_ack;

      host_to_network(header);
      stcp_network_send(sd, header, sizeof(STCPHeader),NULL);

      //listen for ack
      stcp_network_recv(sd,header,sizeof(STCPHeader));
      network_to_host(header);
    }

    ctx->connection_state = CSTATE_ESTABLISHED;
    stcp_unblock_application(sd);

    DEBUG_LOG("completed the connection establishmend");
    control_loop(sd, ctx);

    /* do any cleanup here */
    free(ctx);
}


/* generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);

#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
    ctx->current_sequence_num = 1;
#else
    /* you have to fill this up */
    ctx->initial_sequence_num = rand()%256;
    ctx->current_sequence_num = ctx->initial_sequence_num;
#endif
}

/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);
    assert(!ctx->done);


    unsigned int offset = (sizeof(STCPHeader)%4==0) ? sizeof(STCPHeader)/4 : sizeof(STCPHeader) + 1;
    STCPHeader *header;
    header = (STCPHeader *)malloc(sizeof(STCPHeader));

    void *dst;
    dst = (void *)malloc(536 + 4*offset);
    unsigned int data_size = 536;

    //struct timeval *current_time;
    //current_time = (timeval *)malloc(sizeof(timeval));

    while (!ctx->done)
    {
        unsigned int event;

        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        //gettimeofday(current_time, NULL);
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

        /* check whether it was the network, app, or a close request */
        //app event
        /* printf("current bytes unack %d  and app data:%d \n",ctx->bytes_unacknowledged, event&APP_DATA); */
        if ((event & APP_DATA) && (ctx->bytes_unacknowledged + 536 < ctx->window_size))
        {
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
          /* printf("received app data\n"); */
          header->th_ack = ctx->current_sequence_num_other;
          header->th_seq = ctx->current_sequence_num;
          header->th_off = offset;
          header->th_flags = 0x0;
          header->th_win = ctx->congestion_window;
          unsigned int bytes_to_send = stcp_app_recv(sd, dst, data_size);
          host_to_network(header);
          stcp_network_send(sd, header, sizeof(STCPHeader), dst, bytes_to_send, NULL);

          ctx->bytes_unacknowledged += bytes_to_send;
          ctx->current_sequence_num += bytes_to_send;
        }

        //network event
        if (event & NETWORK_DATA)
        {
          unsigned int bytes_received = stcp_network_recv(sd, dst, data_size + offset*4);

          memcpy((void *)header, dst, sizeof(STCPHeader));
          network_to_host(header);
          /* printf("received network data \n"); */
          
          ctx->window_size = (header->th_win > ctx->congestion_window) ? ctx->congestion_window : header->th_win;

          if(header->th_flags == 0x10){
            ctx->last_byte_acked = header->th_seq - 1;
            ctx->bytes_unacknowledged = ctx->current_sequence_num - header->th_ack;
          }

          if(bytes_received - offset*4 > 0){

            ctx->current_sequence_num_other += bytes_received - offset*4;
            header->th_ack = ctx->current_sequence_num_other;
            header->th_seq = ctx->current_sequence_num;
            header->th_flags = 0x10;
            header->th_win = ctx->congestion_window;;

            host_to_network(header);
            stcp_network_send(sd, header, offset*4, NULL);

            /* ctx->current_sequence_num_other = header->th_seq + bytes_received - offset*4; */
            stcp_app_send(sd, (char *)dst+offset*4, bytes_received - offset*4);
          }

        }

        if(event == TIMEOUT){
          ctx->done = TRUE;
        }

        /* etc. */
   }
}


/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}



