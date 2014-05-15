package rina.PDUForwardingTable.api;

import rina.cdap.api.message.CDAPMessage;
import rina.ipcprocess.api.IPCProcess;

public interface PDUFTable {
	public void setIPCProcess(IPCProcess ipcProcess);
	
	public void setDIFConfiguration(RoutingAlgorithmInt routingAlgorithm, VertexInt sourceVertex);
	
	public void enrollmentToNeighbor(long address, boolean newMember, int portId);

	public void flowAllocated(long address, int portId, long neighborAddress, int neighborPortId);

	public boolean flowDeallocated(int portId);

	public boolean propagateFSDB();

	public void updateAge();

	public boolean writeMessageRecieved(CDAPMessage objectsToModify, int srcPort);

	public void forwardingTableUpdate ();
	
	public boolean readMessageRecieved(CDAPMessage objectsToModify, int srcPort);

}