#include <rte_config.h>
#include <rte_common.h>
#include <rte_ring.h>
#include <rte_mbuf.h>
#include <rte_ethdev.h> 
#include <rte_mempool.h>
#include <rte_ether.h>
#include <rte_cycles.h>
#include "ring.h"

namespace rate_limiter {
	constexpr int batch_size = 64;
	
	// FIXME: actually do the right thing
	static inline void main_loop(struct rte_ring* ring, uint8_t device, uint16_t queue) {
		struct rte_mbuf* bufs[batch_size];
		while (1) {
			int rc = ring_dequeue(ring, reinterpret_cast<void**>(bufs), batch_size);
			if (rc == 0) {
				uint32_t sent = 0;
				while (sent < batch_size) {
					sent += rte_eth_tx_burst(device, queue, bufs + sent, batch_size - sent);
				}
			}
		}
	}

	static inline void main_loop_cbr(struct rte_ring* ring, uint8_t device, uint16_t queue, uint32_t target) {
		uint64_t tsc_hz = rte_get_tsc_hz();
		uint64_t id_cycles = (uint64_t) (target / (1000000000.0 / ((double) tsc_hz)));
		uint64_t next_send = 0;
		struct rte_mbuf* bufs[batch_size];
		while (1) {
			int rc = ring_dequeue(ring, reinterpret_cast<void**>(bufs), batch_size);
			uint64_t cur = rte_get_tsc_cycles();
			// nothing sent for 10 ms, restart rate control
			if (cur - next_send > tsc_hz / 100) {
				next_send = cur;
			}
			if (rc == 0) {
				uint32_t sent = 0;
				while (sent < batch_size) {
					while ((cur = rte_get_tsc_cycles()) < next_send);
					next_send += id_cycles;
					sent += rte_eth_tx_burst(device, queue, bufs + sent, 1);
				}
			}
		}
	}
}

extern "C" {
	void rate_limiter_cbr_main_loop(rte_ring* ring, uint8_t device, uint16_t queue, uint32_t target) {
		rate_limiter::main_loop_cbr(ring, device, queue, target);
	}

	void rate_limiter_main_loop(rte_ring* ring, uint8_t device, uint16_t queue) {
		rate_limiter::main_loop(ring, device, queue);
	}
}

