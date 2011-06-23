/* net.h -- CoAP network interface
 *
 * Copyright (C) 2010,2011 Olaf Bergmann <bergmann@tzi.org>
 *
 * This file is part of the CoAP library libcoap. Please see
 * README for terms of use. 
 */

#ifndef _COAP_NET_H_
#define _COAP_NET_H_

#include "config.h"

#ifdef HAVE_ASSERT_H
#include <assert.h>
#else
#ifndef assert
#warn "assertions are disabled"
#  define assert(x)
#endif
#endif

#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#ifdef HAVE_TIME_H
#include <time.h>
#endif
#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include "option.h"
#include "pdu.h"

/**
 * @defgroup clock Clock Handling
 * Default implementation of internal clock. You should redefine this if
 * you do not have time() and gettimeofday().
 * @{
 */
typedef unsigned int coap_tick_t; 

#define COAP_TICKS_PER_SECOND 1024

/** Set at startup to initialize the internal clock (time in seconds). */
extern time_t clock_offset;

#ifndef coap_clock_init
static inline void
coap_clock_init_impl(void) {
#ifdef HAVE_TIME_H
  clock_offset = time(NULL);
#else
#warn "cannot initialize clock"
  clock_offset = 0;
#endif
}
#define coap_clock_init coap_clock_init_impl
#endif /* coap_clock_init */

#ifndef coap_ticks
static inline void
coap_ticks_impl(coap_tick_t *t) {
#ifdef HAVE_SYS_TIME_H
  struct timeval tv;
  gettimeofday(&tv, NULL);
  *t = (tv.tv_sec - clock_offset) * COAP_TICKS_PER_SECOND 
    + (tv.tv_usec >> 10) % COAP_TICKS_PER_SECOND;
#else
#error "clock not implemented"
#endif
}
#define coap_ticks coap_ticks_impl
#endif /* coap_ticks */

/** @} */

/** multi-purpose address abstraction */
#ifndef coap_address_t
typedef struct __coap_address_t {
  socklen_t size;		/**< size of addr */
  union {
    struct sockaddr     sa;
    struct sockaddr_storage st;
    struct sockaddr_in  sin;
    struct sockaddr_in6 sin6;
  } addr;
} __coap_address_t;

#define coap_address_t __coap_address_t

/** 
 * Resets the given coap_address_t object @p addr to its default
 * values.  In particular, the member size must be initialized to the
 * available size for storing addresses.
 * 
 * @param addr The coap_address_t object to initialize.
 */
static inline void
coap_address_init(coap_address_t *addr) {
  assert(addr);
  memset(addr, 0, sizeof(coap_address_t));
  addr->size = sizeof(struct sockaddr_storage);
}
#endif /* coap_address_t */

struct coap_queue_t;

typedef struct coap_queue_t {
  struct coap_queue_t *next;

  coap_tick_t t;	        /* when to send PDU for the next time */
  unsigned char retransmit_cnt;	/* retransmission counter, will be removed when zero */
  unsigned int timeout;		/* the randomized timeout value */

  coap_address_t remote;	/**< remote address */
  coap_tid_t id;		/**< unique transaction id */

  coap_pdu_t *pdu;		/**< the CoAP PDU to send */
} coap_queue_t;

/* adds node to given queue, ordered by specified order function */
int coap_insert_node(coap_queue_t **queue, coap_queue_t *node,
		     int (*order)(coap_queue_t *, coap_queue_t *node));

/* destroys specified node */
int coap_delete_node(coap_queue_t *node);

/* removes all items from given queue and frees the allocated storage */
void coap_delete_all(coap_queue_t *queue);

/* creates a new node suitable for adding to the CoAP sendqueue */
coap_queue_t *coap_new_node();

struct coap_resource_t;
struct coap_context_t;
#ifndef WITHOUT_ASYNC
struct coap_async_state_t;
#endif

/** Message handler that is used as call-back in coap_context_t */
typedef void (*coap_response_handler_t)(struct coap_context_t  *, 
					const coap_address_t *remote,
					coap_pdu_t *sent,
					coap_pdu_t *received,
					const coap_tid_t id);

/** The CoAP stack's global state is stored in a coap_context_t object */
typedef struct coap_context_t {
  coap_opt_filter_t known_options;
  struct coap_resource_t *resources; /**< hash table of known resources */
#ifndef WITHOUT_ASYNC
  /** list of asynchronous transactions */
  struct coap_async_state_t *async_state;
#endif
  coap_queue_t *sendqueue, *recvqueue;
  int sockfd;			/* send/receive socket */


  coap_response_handler_t response_handler;
} coap_context_t;

/**
 * Registers a new message handler that is called whenever a response
 * was received that matches an ongoing transaction. 
 * 
 * @param context The context to register the handler for.
 * @param handler The response handler to register.
 */
static inline void 
coap_register_response_handler(coap_context_t *context, 
			       coap_response_handler_t handler) {
  context->response_handler = handler;
}

/** 
 * Registers the option type @p type with the given context object @p
 * ctx.
 * 
 * @param ctx  The context to use.
 * @param type The option type to register.
 */
inline static void 
coap_register_option(coap_context_t *ctx, unsigned char type) {
  coap_option_setb(ctx->known_options, type);
}

/* Returns the next pdu to send without removing from sendqeue. */
coap_queue_t *coap_peek_next( coap_context_t *context );

/* Returns the next pdu to send and removes it from the sendqeue. */
coap_queue_t *coap_pop_next( coap_context_t *context );

/* Creates a new coap_context_t object that will hold the CoAP stack status.  */
coap_context_t *coap_new_context(const struct sockaddr *listen_addr, size_t addr_size);

/* CoAP stack context must be released with coap_free_context() */
void coap_free_context( coap_context_t *context );


/**
 * Sends a confirmed CoAP message to given destination. The memory
 * that is allocated by pdu will be released by
 * coap_send_confirmed(). The caller must not make any assumption on
 * the lifetime of pdu.
 *
 * @param context The CoAP context to use.
 * @param dst     The address to send to.
 * @param pdu     The CoAP PDU to send.
 * @return The message id of the sent message or @c COAP_INVALID_TID on error.
 */
coap_tid_t coap_send_confirmed(coap_context_t *context, 
			       const coap_address_t *dst,
			       coap_pdu_t *pdu);

/** 
 * Creates a new ACK PDU with specified error @p code. The options
 * specified by the filter expression @p opts will be copied from the
 * original request contained in @p request.  Unless @c
 * SHORT_ERROR_RESPONSE was defined at build time, the textual reason
 * phrase for @p code will be added as payload, with Content-Type @c
 * 0.  This function returns a pointer to the new response message, or
 * @c NULL on error. The storage allocated for the new message must be
 * relased with coap_free().
 * 
 * @param request Specification of the received (confirmable) request.
 * @param code The error code to set.
 * @param opts An option filter that specifies which options to copy
 *             from the original request in @p node.
 * 
 * @return A pointer to the new message or @c NULL on error.
 */
coap_pdu_t *coap_new_error_response(coap_pdu_t *request, 
				    unsigned char code, 
				    coap_opt_filter_t opts);
/**
 * Sends a non-confirmed CoAP message to given destination. The memory
 * that is allocated by pdu will be released by coap_send(). The
 * caller must not make any assumption on the lifetime of pdu.
 *
 * @param context The CoAP context to use.
 * @param dst     The address to send to.
 * @param pdu     The CoAP PDU to send.
 * @return The message id of the sent message or @c COAP_INVALID_TID on error.
 */
coap_tid_t coap_send(coap_context_t *context, 
		     const coap_address_t *dst, 
		     coap_pdu_t *pdu);

/** 
 * Sends an error response with code @p code for request @p request to
 * @p dst.  @p opts will be passed to coap_new_error_response() to
 * copy marked options from the request. This function returns the
 * transaction id if the message was sent, or @c COAP_INVALID_TID
 * otherwise.
 * 
 * @param context The context to use.
 * @param request The original request to respond to.
 * @param dst     The remote peer that sent the request.
 * @param code    The reponse code.
 * @param opts    A filter that specifies the options to copy from the 
 *                @p request.
 * 
 * @return The transaction id if the message was sent, or @c
 * COAP_INVALID_TID otherwise.
 */
coap_tid_t coap_send_error(coap_context_t *context, 
			   coap_pdu_t *request,
			   const coap_address_t *dst,
			   unsigned char code,
			   coap_opt_filter_t opts);
/** 
 * Sends an ACK message with code @c 0 for the specified @p request to
 * @p dst. This function returns the corresponding transaction id if
 * the message was sent or @c COAP_INVALID_TID on error.
 * 
 * @param context The context to use.
 * @param dst     The destination address.
 * @param request The request to be acknowledged.
 * 
 * @return The transaction id if ACK was sent or @c COAP_INVALID_TID
 * on error.
 */
static inline coap_tid_t
coap_send_ack(coap_context_t *context, 
	      const coap_address_t *dst,
	      coap_pdu_t *request) {
  coap_opt_filter_t f;

  if (request && request->hdr->type == COAP_MESSAGE_CON) {
    coap_option_filter_clear(f);  
    return coap_send_error(context, request, dst, 0, f);
  }
  return COAP_INVALID_TID;
}


/** Handles retransmissions of confirmable messages */
coap_tid_t coap_retransmit( coap_context_t *context, coap_queue_t *node );

/**
 * Reads data from the network and tries to parse as CoAP PDU. On success, 0 is returned
 * and a new node with the parsed PDU is added to the receive queue in the specified context
 * object.
 */
int coap_read( coap_context_t *context );

/** 
 * Calculates a unique transaction id from given arguments @p peer and
 * @p pdu. The id is returned in @p id.
 * 
 * @param peer The remote party who sent @p pdu.
 * @param pdu  The message that initiated the transaction.
 * @param id   Set to the new id.
 */
void coap_transaction_id(const coap_address_t *peer, const coap_pdu_t *pdu, 
			 coap_tid_t *id);

/** 
 * This function removes the element with given @p id from the list
 * given list. If @p id was found, @p node is updated to point to the
 * removed element. Note that the storage allocated by @p node is 
 * @b not released. The caller must do this manually using
 * coap_delete_node(). This function returns @c 1 if the element with
 * id @p id was found, @c 0 otherwise. For a return value of @c 0,
 * the contents of @p node is undefined.
 * 
 * @param queue The queue to search for @p id.
 * @param id    The node id to look for.
 * @param node  If found, @p node is updated to point to the 
 *   removed node. You must release the storage pointed to by
 *   @p node manually.
 * 
 * @return @c 1 if @p id was found, @c 0 otherwise.
 */
int coap_remove_from_queue(coap_queue_t **queue, 
			   coap_tid_t id, 
			   coap_queue_t **node);

/** 
 * Removes the transaction identified by @p id from given @p queue.
 * This is a convenience function for coap_remove_from_queue() with
 * automatic deletion of the removed node.
 * 
 * @param queue The queue to search for @p id.
 * @param id    The transaction id.
 * 
 * @return @c 1 if node was found, removed and destroyed, @c 0 otherwise.
 */
inline static int
coap_remove_transaction(coap_queue_t **queue, coap_tid_t id) {
  coap_queue_t *node;
  if (!coap_remove_from_queue(queue, id, &node)) 
    return 0;

  coap_delete_node(node);
  return 1;
}

/**
 * Retrieves transaction from queue.
 * @queue The transaction queue to be searched
 * @id Unique key of the transaction to find.
 * @return A pointer to the transaction object or NULL if not found
 */
coap_queue_t *coap_find_transaction(coap_queue_t *queue, coap_tid_t id);

/** Dispatches the PDUs from the receive queue in given context. */
void coap_dispatch( coap_context_t *context );

/** Returns 1 if there are no messages to send or to dispatch in the context's queues. */
int coap_can_exit( coap_context_t *context );

/**
 * Returns the current value of an internal tick counter. The counter
 * counts \c COAP_TICKS_PER_SECOND ticks every second. 
 */
void coap_ticks(coap_tick_t *);

/** 
 * Verifies that @p pdu contains no unknown critical options. Options
 * must be registered at @p ctx, using the function
 * coap_register_option(). A basic set of options is registered
 * automatically by coap_new_context(). This function returns @c 1 if
 * @p pdu is ok, @c 0 otherwise. The given filter object @p unknown
 * will be updated with the unknown options. As only @c COAP_MAX_OPT
 * options can be signalled this way, remaining options must be
 * examined manually. 
 *
 * @code
  coap_opt_filter_t f = COAP_OPT_NONE;
  coap_opt_iterator_t opt_iter;
  
  if (coap_option_check_critical(ctx, pdu, f) == 0) {
    coap_option_iterator_init(pdu, &opt_iter, f);

    while (coap_option_next(&opt_iter)) {
      if (opt_iter.type & 0x01) {
	... handle unknown critical option in opt_iter ...
      }
    }
  }
 * @endcode 
 *
 * @param ctx      The context where all known options are registered.
 * @param pdu      The PDU to check.
 * @param unknown  The output filter that will be updated to indicate the
 *                 unknown critical options found in @p pdu.
 * 
 * @return @c 1 if everything was ok, @c 0 otherwise.
 */
int coap_option_check_critical(coap_context_t *ctx, 
			       coap_pdu_t *pdu,
			       coap_opt_filter_t unknown);

/** 
 * @defgroup prng Pseudo Random Numbers
 * @{
 */

/**
 * Fills \p buf with \p len random bytes. This is the default
 * implementation for prng().  You might want to change prng() to use
 * a better PRNG on your specific platform.
 */
static inline int
coap_prng_impl(unsigned char *buf, size_t len) {
  while (len--)
    *buf++ = rand() & 0xFF;
  return 1;
}

#ifndef prng
/** 
 * Fills \p Buf with \p Length bytes of random data. 
 * 
 * @hideinitializer
 */
#define prng(Buf,Length) coap_prng_impl((Buf), (Length))
#endif

#ifndef prng_init
/** 
 * Called by dtls_new_context() to set the PRNG seed. You
 * may want to re-define this to allow for a better PRNG. 
 *
 * @hideinitializer
 */
#define prng_init(Value) srand((unsigned long)(Value))
#endif

/** @} */

#endif /* _COAP_NET_H_ */
