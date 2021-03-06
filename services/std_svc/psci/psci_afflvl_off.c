/*
 * Copyright (c) 2013-2014, ARM Limited and Contributors. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <arch.h>
#include <arch_helpers.h>
#include <assert.h>
#include <string.h>
#include "psci_private.h"

typedef int (*afflvl_off_handler_t)(unsigned long, aff_map_node_t *);

/*******************************************************************************
 * The next three functions implement a handler for each supported affinity
 * level which is called when that affinity level is turned off.
 ******************************************************************************/
static int psci_afflvl0_off(unsigned long mpidr, aff_map_node_t *cpu_node)
{
	unsigned int index, plat_state;
	int rc = PSCI_E_SUCCESS;
	unsigned long sctlr;

	assert(cpu_node->level == MPIDR_AFFLVL0);

	/* State management: mark this cpu as turned off */
	psci_set_state(cpu_node, PSCI_STATE_OFF);

	/*
	 * Generic management: Get the index for clearing any lingering re-entry
	 * information and allow the secure world to switch itself off
	 */

	/*
	 * Call the cpu off handler registered by the Secure Payload Dispatcher
	 * to let it do any bookeeping. Assume that the SPD always reports an
	 * E_DENIED error if SP refuse to power down
	 */
	if (psci_spd_pm && psci_spd_pm->svc_off) {
		rc = psci_spd_pm->svc_off(0);
		if (rc)
			return rc;
	}

	index = cpu_node->data;
	memset(&psci_ns_entry_info[index], 0, sizeof(psci_ns_entry_info[index]));

	/*
	 * Arch. management. Perform the necessary steps to flush all
	 * cpu caches.
	 *
	 * TODO: This power down sequence varies across cpus so it needs to be
	 * abstracted out on the basis of the MIDR like in cpu_reset_handler().
	 * Do the bare minimal for the time being. Fix this before porting to
	 * Cortex models.
	 */
	sctlr = read_sctlr_el3();
	sctlr &= ~SCTLR_C_BIT;
	write_sctlr_el3(sctlr);

	/*
	 * CAUTION: This flush to the level of unification makes an assumption
	 * about the cache hierarchy at affinity level 0 (cpu) in the platform.
	 * Ideally the platform should tell psci which levels to flush to exit
	 * coherency.
	 */
	dcsw_op_louis(DCCISW);

	/*
	 * Plat. management: Perform platform specific actions to turn this
	 * cpu off e.g. exit cpu coherency, program the power controller etc.
	 */
	if (psci_plat_pm_ops->affinst_off) {

		/* Get the current physical state of this cpu */
		plat_state = psci_get_phys_state(cpu_node);
		rc = psci_plat_pm_ops->affinst_off(mpidr,
						   cpu_node->level,
						   plat_state);
	}

	return rc;
}

static int psci_afflvl1_off(unsigned long mpidr, aff_map_node_t *cluster_node)
{
	int rc = PSCI_E_SUCCESS;
	unsigned int plat_state;

	/* Sanity check the cluster level */
	assert(cluster_node->level == MPIDR_AFFLVL1);

	/* State management: Decrement the cluster reference count */
	psci_set_state(cluster_node, PSCI_STATE_OFF);

	/*
	 * Keep the physical state of this cluster handy to decide
	 * what action needs to be taken
	 */
	plat_state = psci_get_phys_state(cluster_node);

	/*
	 * Arch. Management. Flush all levels of caches to PoC if
	 * the cluster is to be shutdown
	 */
	if (plat_state == PSCI_STATE_OFF)
		dcsw_op_all(DCCISW);

	/*
	 * Plat. Management. Allow the platform to do its cluster
	 * specific bookeeping e.g. turn off interconnect coherency,
	 * program the power controller etc.
	 */
	if (psci_plat_pm_ops->affinst_off)
		rc = psci_plat_pm_ops->affinst_off(mpidr,
						   cluster_node->level,
						   plat_state);

	return rc;
}

static int psci_afflvl2_off(unsigned long mpidr, aff_map_node_t *system_node)
{
	int rc = PSCI_E_SUCCESS;
	unsigned int plat_state;

	/* Cannot go beyond this level */
	assert(system_node->level == MPIDR_AFFLVL2);

	/* State management: Decrement the system reference count */
	psci_set_state(system_node, PSCI_STATE_OFF);

	/*
	 * Keep the physical state of the system handy to decide what
	 * action needs to be taken
	 */
	plat_state = psci_get_phys_state(system_node);

	/* No arch. and generic bookeeping to do here currently */

	/*
	 * Plat. Management : Allow the platform to do its bookeeping
	 * at this affinity level
	 */
	if (psci_plat_pm_ops->affinst_off)
		rc = psci_plat_pm_ops->affinst_off(mpidr,
						   system_node->level,
						   plat_state);
	return rc;
}

static const afflvl_off_handler_t psci_afflvl_off_handlers[] = {
	psci_afflvl0_off,
	psci_afflvl1_off,
	psci_afflvl2_off,
};

/*******************************************************************************
 * This function takes an array of pointers to affinity instance nodes in the
 * topology tree and calls the off handler for the corresponding affinity
 * levels
 ******************************************************************************/
static int psci_call_off_handlers(mpidr_aff_map_nodes_t mpidr_nodes,
				  int start_afflvl,
				  int end_afflvl,
				  unsigned long mpidr)
{
	int rc = PSCI_E_INVALID_PARAMS, level;
	aff_map_node_t *node;

	for (level = start_afflvl; level <= end_afflvl; level++) {
		node = mpidr_nodes[level];
		if (node == NULL)
			continue;

		/*
		 * TODO: In case of an error should there be a way
		 * of restoring what we might have torn down at
		 * lower affinity levels.
		 */
		rc = psci_afflvl_off_handlers[level](mpidr, node);
		if (rc != PSCI_E_SUCCESS)
			break;
	}

	return rc;
}

/*******************************************************************************
 * Top level handler which is called when a cpu wants to power itself down.
 * It's assumed that along with turning the cpu off, higher affinity levels will
 * be turned off as far as possible. It traverses through all the affinity
 * levels performing generic, architectural, platform setup and state management
 * e.g. for a cluster that's to be powered off, it will call the platform
 * specific code which will disable coherency at the interconnect level if the
 * cpu is the last in the cluster. For a cpu it could mean programming the power
 * the power controller etc.
 *
 * The state of all the relevant affinity levels is changed prior to calling the
 * affinity level specific handlers as their actions would depend upon the state
 * the affinity level is about to enter.
 *
 * The affinity level specific handlers are called in ascending order i.e. from
 * the lowest to the highest affinity level implemented by the platform because
 * to turn off affinity level X it is neccesary to turn off affinity level X - 1
 * first.
 *
 * CAUTION: This function is called with coherent stacks so that coherency can
 * be turned off and caches can be flushed safely.
 ******************************************************************************/
int psci_afflvl_off(unsigned long mpidr,
		    int start_afflvl,
		    int end_afflvl)
{
	int rc = PSCI_E_SUCCESS;
	mpidr_aff_map_nodes_t mpidr_nodes;

	mpidr &= MPIDR_AFFINITY_MASK;;

	/*
	 * Collect the pointers to the nodes in the topology tree for
	 * each affinity instance in the mpidr. If this function does
	 * not return successfully then either the mpidr or the affinity
	 * levels are incorrect. In either case, we cannot return back
	 * to the caller as it would not know what to do.
	 */
	rc = psci_get_aff_map_nodes(mpidr,
				    start_afflvl,
				    end_afflvl,
				    mpidr_nodes);
	assert (rc == PSCI_E_SUCCESS);

	/*
	 * This function acquires the lock corresponding to each affinity
	 * level so that by the time all locks are taken, the system topology
	 * is snapshot and state management can be done safely.
	 */
	psci_acquire_afflvl_locks(mpidr,
				  start_afflvl,
				  end_afflvl,
				  mpidr_nodes);

	/* Perform generic, architecture and platform specific handling */
	rc = psci_call_off_handlers(mpidr_nodes,
				    start_afflvl,
				    end_afflvl,
				    mpidr);

	/*
	 * Release the locks corresponding to each affinity level in the
	 * reverse order to which they were acquired.
	 */
	psci_release_afflvl_locks(mpidr,
				  start_afflvl,
				  end_afflvl,
				  mpidr_nodes);

	return rc;
}
