/*
 * IP VPN Manager. Manages creation and deletion of IP over RINA flows
 * and VPNs. Manages registration of IP prefixes as application names
 * in RINA DIFs, as well as allocation of flows between IP prefixes,
 * the creation of VPNs and the reconfiguration of the IP forwarding table.
 *
 *    Eduard Grasa <eduard.grasa@i2cat.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301  USA
 */

#define RINA_PREFIX     "ipcm.ip-vpn-manager"
#include <librina/logs.h>

#include "app-handlers.h"
#include "ip-vpn-manager.h"
#include "ipcm.h"

namespace rinad {

int IPVPNManager::add_registered_ip_prefix(const std::string& ip_prefix)
{
	rina::ScopedLock g(lock);

	if (__ip_prefix_registered(ip_prefix)) {
		LOG_ERR("IP prefix %s is already registered",
			ip_prefix.c_str());
		return -1;
	}

	reg_ip_prefixes.push_back(ip_prefix);

	return 0;
}

int IPVPNManager::remove_registered_ip_prefix(const std::string& ip_prefix)
{
	std::list<std::string>::iterator it;

	rina::ScopedLock g(lock);

	for (it = reg_ip_prefixes.begin(); it != reg_ip_prefixes.end(); ++it) {
		if ((*it) == ip_prefix) {
			reg_ip_prefixes.erase(it);
			return 0;
		}
	}

	LOG_ERR("IP prefix %s is not registered", ip_prefix.c_str());

	return -1;
}

bool IPVPNManager::ip_prefix_registered(const std::string& ip_prefix)
{
	rina::ScopedLock g(lock);

	return __ip_prefix_registered(ip_prefix);
}

bool IPVPNManager::__ip_prefix_registered(const std::string& ip_prefix)
{
	std::list<std::string>::iterator it;

	for (it = reg_ip_prefixes.begin(); it != reg_ip_prefixes.end(); ++it) {
		if ((*it) == ip_prefix) {
			return true;
		}
	}

	return false;
}

int IPVPNManager::iporina_flow_allocated(const rina::FlowRequestEvent& event)
{
	int res = 0;

	rina::ScopedLock g(lock);

	res = add_flow(event);
	if (res != 0) {
		return res;
	}

	//TODO Set the rina device associated to the flow up
	//TODO add entry to the IP forwarding table

	LOG_INFO("IP over RINA flow between %s and %s allocated, port-id: %d",
		  event.localApplicationName.processName.c_str(),
		  event.remoteApplicationName.processName.c_str(),
		  event.portId);

	return 0;
}

void IPVPNManager::iporina_flow_allocation_requested(const rina::FlowRequestEvent& event)
{
	int res = 0;

	rina::ScopedLock g(lock);

	if (!__ip_prefix_registered(event.localApplicationName.processName)) {
		LOG_INFO("Rejected IP over RINA flow between %s and %s",
			  event.localApplicationName.processName.c_str(),
			  event.remoteApplicationName.processName.c_str());
		IPCManager->allocate_iporina_flow_response(event, -1, true);
		return;
	}

	res = add_flow(event);
	if (res != 0) {
		LOG_ERR("Flow with port-id %d already exists", event.portId);
		IPCManager->allocate_iporina_flow_response(event, -1, true);
		return;
	}

	//TODO Set the rina device associated to the flow up
	//TODO add entry to the IP forwarding table

	LOG_INFO("Accepted IP over RINA flow between %s and %s; port-id: %d",
			event.localApplicationName.processName.c_str(),
			event.remoteApplicationName.processName.c_str(),
			event.portId);

	IPCManager->allocate_iporina_flow_response(event, 0, true);
}

int IPVPNManager::iporina_flow_deallocated(int port_id)
{
	int res = 0;
	rina::FlowRequestEvent event;

	rina::ScopedLock g(lock);

	event.portId = port_id;
	res = remove_flow(event);
	if (res != 0) {
		return res;
	}

	//TODO remove entry from the IP forwarding table
	//TODO set the RINA device associated to the flow down

	LOG_INFO("IP over RINA flow between %s and %s deallocated, port-id: %d",
		  event.localApplicationName.processName.c_str(),
		  event.remoteApplicationName.processName.c_str(),
		  event.portId);

	return 0;
}

int IPVPNManager::add_flow(const rina::FlowRequestEvent& event)
{
	std::map<int, rina::FlowRequestEvent>::iterator it;

	it = iporina_flows.find(event.portId);
	if (it != iporina_flows.end()) {
		LOG_ERR("Cannot add IP over RINA flow with port-id %d, already exists",
			 event.portId);
		return -1;
	}

	iporina_flows[event.portId] = event;

	return 0;
}

int IPVPNManager::remove_flow(rina::FlowRequestEvent& event)
{
	std::map<int, rina::FlowRequestEvent>::iterator it;

	it = iporina_flows.find(event.portId);
	if (it == iporina_flows.end()) {
		LOG_ERR("Cannot remove IP over RINA flow with port-id %d, not found",
			 event.portId);
		return -1;
	}

	event = it->second;
	iporina_flows.erase(it);

	return 0;
}

ipcm_res_t IPCManager_::register_ip_prefix_to_dif(Promise* promise,
						  const std::string& ip_range,
						  const rina::ApplicationProcessNamingInformation& dif_name)
{
	IPCMIPCProcess *slave_ipcp = NULL;
	APPregTransState* trans = NULL;
	rina::ApplicationRegistrationRequestEvent event;
	rina::ApplicationProcessNamingInformation daf_name;

	// Select an IPC process from the DIF specified in the request
	slave_ipcp = select_ipcp_by_dif(dif_name);
	if (!slave_ipcp) {
		LOG_ERR("Cannot find a suitable DIF to "
			"register IP range %s ", ip_range.c_str());
		return IPCM_FAILURE;
	}

	//Populate application name
	event.applicationRegistrationInformation.appName.processName = ip_range;
	event.applicationRegistrationInformation.appName.entityName = RINA_IP_FLOW_ENT_NAME;

	//Auto release the read lock
	rina::ReadScopedLock readlock(slave_ipcp->rwlock, false);

	//Perform the registration
	try {
		//Create a transaction
		trans = new APPregTransState(NULL, promise, slave_ipcp->get_id(), event);
		if(!trans){
			LOG_ERR("Unable to allocate memory for the transaction object. Out of memory! ");
			return IPCM_FAILURE;
		}

		//Store transaction
		if(add_transaction_state(trans) < 0){
			LOG_ERR("Unable to add transaction; out of memory? ");
			return IPCM_FAILURE;
		}

		slave_ipcp->registerApplication(event.applicationRegistrationInformation.appName,
						daf_name,
						0,
						trans->tid);

		LOG_INFO("Requested registration of IP prefix %s"
				" to IPC process %s ",
				ip_range.c_str(),
				slave_ipcp->get_name().toString().c_str());
	} catch (rina::IpcmRegisterApplicationException& e) {
		//Remove the transaction and return
		remove_transaction_state(trans->tid);

		LOG_ERR("Error while registering IP prefix %s",
			event.applicationRegistrationInformation.appName.toString().c_str());
		return IPCM_FAILURE;
	}

	return IPCM_PENDING;
}

ipcm_res_t IPCManager_::unregister_ip_prefix_from_dif(Promise* promise,
						      const std::string& ip_range,
						      const rina::ApplicationProcessNamingInformation& dif_name)
{
	IPCMIPCProcess *slave_ipcp;
	APPUnregTransState* trans;
	rina::ApplicationUnregistrationRequestEvent req_event;

	try
	{
		slave_ipcp = select_ipcp_by_dif(dif_name, true);

		if (!slave_ipcp)
		{
			LOG_ERR("Cannot find any IPC process belonging to DIF %s",
				dif_name.toString().c_str());
			return IPCM_FAILURE;
		}

		rina::WriteScopedLock swritelock(slave_ipcp->rwlock, false);

		//Create a transaction
		req_event.applicationName.processName = ip_range;
		req_event.applicationName.entityName = RINA_IP_FLOW_ENT_NAME;
		trans = new APPUnregTransState(NULL, promise, slave_ipcp->get_id(), req_event);
		if (!trans)
		{
			LOG_ERR("Unable to allocate memory for the transaction object. Out of memory! ");
			return IPCM_FAILURE;
		}

		//Store transaction
		if (add_transaction_state(trans) < 0)
		{
			LOG_ERR("Unable to add transaction; out of memory?");
			return IPCM_FAILURE;
		}

		// Forward the unregistration request to the IPC process
		// that the client IPC process is registered to
		slave_ipcp->unregisterApplication(req_event.applicationName, trans->tid);

		LOG_INFO("Requested unregistration of IP prefix %s"
				" from IPC process %s",
				ip_range.c_str(),
				slave_ipcp->get_name().toString().c_str());
	} catch (rina::ConcurrentException& e)
	{
		LOG_ERR("Error while unregistering IP prefix %s"
				" from IPC process %s, The operation timed out.",
				ip_range.c_str(),
				slave_ipcp->get_name().toString().c_str());
		remove_transaction_state(trans->tid);
		return IPCM_FAILURE;
	} catch (rina::IpcmUnregisterApplicationException& e)
	{
		LOG_ERR("Error while unregistering IP prefix %s"
				" from IPC process %s",
				ip_range.c_str(),
				slave_ipcp->get_name().toString().c_str());
		remove_transaction_state(trans->tid);
		return IPCM_FAILURE;
	} catch (rina::Exception& e)
	{
		LOG_ERR("Unknown error while requesting unregistering IPCP");
		remove_transaction_state(trans->tid);
		return IPCM_FAILURE;
	}

	return IPCM_PENDING;
}

ipcm_res_t IPCManager_::allocate_iporina_flow(Promise* promise,
				 	      const std::string& src_ip_range,
					      const std::string& dst_ip_range,
					      const std::string& dif_name,
					      const rina::FlowSpecification flow_spec)
{
	rina::FlowRequestEvent event;

	event.localRequest = true;
	event.localApplicationName.processName = src_ip_range;
	event.localApplicationName.entityName = RINA_IP_FLOW_ENT_NAME;
	event.remoteApplicationName.processName = dst_ip_range;
	event.remoteApplicationName.entityName = RINA_IP_FLOW_ENT_NAME;
	event.flowSpecification = flow_spec;
	event.DIFName.processName = dif_name;
	event.DIFName.processInstance = "";

	return flow_allocation_requested_event_handler(promise, &event);
}

void IPCManager_::allocate_iporina_flow_response(const rina::FlowRequestEvent& event,
				    	    	 int result,
						 bool notify_source)
{
	IPCMIPCProcess * ipcp;

	ipcp = lookup_ipcp_by_id(event.ipcProcessId);
	if (!ipcp) {
		LOG_ERR("Error: Could not retrieve IPC process with id %d "
			", to serve remote flow allocation request", event.ipcProcessId);
		return;
	}

	//Auto release the read lock
	rina::ReadScopedLock readlock(ipcp->rwlock, false);

	try {
		// Inform the IPC process about the response of the flow
		// allocation procedure
		ipcp->allocateFlowResponse(event, result, notify_source, 0);

		LOG_INFO("Informing IPC process %s about flow allocation from application %s"
			" to application %s in DIF %s [success = %d, port-id = %d]",
			 ipcp->proxy_->name.getProcessNamePlusInstance().c_str(),
			 event.localApplicationName.toString().c_str(),
			 event.remoteApplicationName.toString().c_str(),
			 ipcp->dif_name_.processName.c_str(), result, event.portId);
	} catch (rina::AllocateFlowException& e) {
		LOG_ERR("Error while informing IPC process %d"
			" about flow allocation", ipcp->proxy_->id);
	}
}

} //namespace rinad
