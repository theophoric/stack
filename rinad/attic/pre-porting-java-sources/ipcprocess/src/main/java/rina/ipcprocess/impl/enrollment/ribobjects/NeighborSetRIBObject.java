package rina.ipcprocess.impl.enrollment.ribobjects;

import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;

import eu.irati.librina.ApplicationProcessNamingInformation;
import eu.irati.librina.Neighbor;

import rina.cdap.api.CDAPSessionDescriptor;
import rina.cdap.api.message.CDAPMessage;
import rina.ipcprocess.api.IPCProcess;
import rina.ribdaemon.api.BaseRIBObject;
import rina.ribdaemon.api.ObjectInstanceGenerator;
import rina.ribdaemon.api.RIBDaemonException;
import rina.ribdaemon.api.RIBObject;
import rina.ribdaemon.api.RIBObjectNames;

/**
 * TO describe
 * @author eduardgrasa
 *
 */
public class NeighborSetRIBObject extends BaseRIBObject{
	
	public static final String NEIGHBOR_SET_RIB_OBJECT_NAME = RIBObjectNames.SEPARATOR + RIBObjectNames.DAF + 
            RIBObjectNames.SEPARATOR + RIBObjectNames.MANAGEMENT + RIBObjectNames.SEPARATOR + RIBObjectNames.NEIGHBORS;
    
    public static final String NEIGHBOR_SET_RIB_OBJECT_CLASS = "neighbor set";
	
	private static final Log log = LogFactory.getLog(NeighborSetRIBObject.class);
	
	public NeighborSetRIBObject(IPCProcess ipcProcess){
		super(ipcProcess, NEIGHBOR_SET_RIB_OBJECT_CLASS, 
				ObjectInstanceGenerator.getObjectInstance(), NEIGHBOR_SET_RIB_OBJECT_NAME);
		setRIBDaemon(ipcProcess.getRIBDaemon());
		setEncoder(ipcProcess.getEncoder());
	}
	
	@Override
	public RIBObject read() throws RIBDaemonException{
		return this;
	}
	
	@Override
	public void create(CDAPMessage cdapMessage, CDAPSessionDescriptor cdapSessionDescriptor) throws RIBDaemonException {
		if (cdapMessage.getObjValue() == null || cdapMessage.getObjValue().getByteval() == null) {
			return;
		}
		
		try{
			Neighbor[] neighbors = (Neighbor[])
				this.getEncoder().decode(cdapMessage.getObjValue().getByteval(), Neighbor[].class);
			
			if (neighbors == null) {
				log.warn("Got a null array after trying to decode a Neighbors[] object. " +
						"Object name: "+cdapMessage.getObjName() + "; object class: "
						+ cdapMessage.getObjClass());
				return;
			}
			
			//Only create the neighbors that we don't know about
			List<Neighbor> unknownNeighbors = new ArrayList<Neighbor>();
			for(int i=0; i<neighbors.length; i++){
				if (!contains(neighbors[i])){
					unknownNeighbors.add(neighbors[i]);
				}
			}
			this.getRIBDaemon().create(cdapMessage.getObjClass(), 
					cdapMessage.getObjInst(), 
					cdapMessage.getObjName(), 
					unknownNeighbors.toArray(new Neighbor[unknownNeighbors.size()]), 
					null);
		}catch(Exception ex){
			ex.printStackTrace();
			log.error("Problems processing remote CDAP create operation: " +ex.getMessage());
		}
	}
	
	private boolean contains(Neighbor neighbor){
		Neighbor candidate = null;
		for(int i=0; i<this.getChildren().size(); i++){
			candidate = (Neighbor) this.getChildren().get(i).getObjectValue();
			if (candidate.getName().getProcessName().equals(
					neighbor.getName().getProcessName())){
				return true;
			}
		}
		
		return false;
	}
	
	@Override
	public void create(String objectClass, long objectInstance, String objectName, Object objectValue) throws RIBDaemonException{
		if (objectValue instanceof Neighbor){
			this.createOrUpdateNeighbor(objectName, (Neighbor) objectValue);
		}else if (objectValue instanceof Neighbor[]){
			Neighbor[] neighbors = (Neighbor[]) objectValue;
			String candidateObjectName = null;
			
			for(int i=0; i<neighbors.length; i++){
				candidateObjectName = this.getObjectName() + RIBObjectNames.SEPARATOR + neighbors[i].getName().getProcessName();
				this.createOrUpdateNeighbor(candidateObjectName, neighbors[i]);
			}
		}else{
			throw new RIBDaemonException(RIBDaemonException.OBJECTCLASS_DOES_NOT_MATCH_OBJECTNAME, 
					"Object class ("+objectValue.getClass().getName()+") does not match object name "+objectName);
		}
	}
	
	/**
	 * Create or update a child Neighbor RIB Object
	 * @param objectName
	 * @param objectValue
	 */
	private synchronized void createOrUpdateNeighbor(String objectName, Neighbor neighbor) throws RIBDaemonException{
		//Avoid creating myself as a neighbor
		if (neighbor.getName().getProcessName().equals(
				getIPCProcess().getName().getProcessName())){
			return;
		}
		
		//Only create neighbours with whom I have an N-1 DIF in common
		Iterator<ApplicationProcessNamingInformation> iterator = neighbor.getSupportingDifs().iterator();
		boolean supportingDifInCommon = false;
		ApplicationProcessNamingInformation supportingDifName = null;
		while (iterator.hasNext()){
			supportingDifName = iterator.next();
			if (getIPCProcess().getResourceAllocator().getNMinus1FlowManager().isSupportingDIF(supportingDifName)){
				neighbor.setSupportingDifName(supportingDifName);
				supportingDifInCommon = true;
				break;
			}
		}
		
		if (!supportingDifInCommon) {
			log.info("Ignoring neighbor "+neighbor.getName().getProcessName() + "-" 
					+ neighbor.getName().getProcessInstance() + "" +" because we don't have an N-1 DIF in common");
			return;
		}
		
		RIBObject child = this.getChild(objectName);
		if (child == null){
			//Create the new RIBOBject
			child = new NeighborRIBObject(getIPCProcess(), objectName, neighbor);
			this.addChild(child);
			child.setParent(this);
			getRIBDaemon().addRIBObject(child);
		}else{
			//Update the existing RIBObject
			child.write(neighbor);
		}
	}

	@Override
	public synchronized void delete(Object objectValue) throws RIBDaemonException {
		String childName = null;
		List<String> childrenNames = new ArrayList<String>();
		
		for(int i=0; i<this.getChildren().size(); i++){
			childName = this.getChildren().get(i).getObjectName();
			childrenNames.add(childName);
			getRIBDaemon().delete(null, childName, null);
		}
		
		for(int i=0; i<childrenNames.size(); i++){
			this.removeChild(childrenNames.get(i));
		}
	}
	
	@Override
	public synchronized Object getObjectValue(){
		Neighbor[] dafMembers = new Neighbor[this.getChildren().size()];
		for(int i=0; i<dafMembers.length; i++){
			dafMembers[i] = (Neighbor) this.getChildren().get(i).getObjectValue();
		}
		
		return dafMembers;
	}
}