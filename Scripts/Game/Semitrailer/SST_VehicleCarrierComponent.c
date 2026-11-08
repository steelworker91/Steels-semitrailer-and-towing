// Vehicle deck of a carrier semitrailer. Works like the cargo hold of the C-130 mod: a vehicle that is
// loaded loses its physics, becomes a child of the trailer and is put on the deck. Vehicles are packed
// from the headboard backwards, the last one may stick out at the rear. Unloading takes the rearmost
// vehicle off and puts it on the ground behind the trailer.
//
// The server decides, every machine applies the same load/unload (broadcast), late joiners get the
// list through RplSave/RplLoad and the vehicles are re-attached once they stream in.

[ComponentEditorProps(category: "SST/Semitrailer", description: "Vehicle deck of a carrier semitrailer")]
class SST_VehicleCarrierComponentClass : ScriptComponentClass
{
}

class SST_VehicleCarrierComponent : ScriptComponent
{
	[Attribute("0.52", UIWidgets.EditBox, "Deck height, trailer space [m]")]
	protected float m_fDeckY;

	[Attribute("1.05", UIWidgets.EditBox, "Front of the usable deck (behind the headboard) [m]")]
	protected float m_fDeckFront;

	[Attribute("-10.7", UIWidgets.EditBox, "Rear of the usable deck (in front of the ramps) [m]")]
	protected float m_fDeckRear;

	[Attribute("3.4", UIWidgets.EditBox, "Widest vehicle that is accepted [m]")]
	protected float m_fMaxWidth;

	[Attribute("4", UIWidgets.EditBox, "How far the last vehicle may stick out behind the deck [m]")]
	protected float m_fRearOverhang;

	[Attribute("0.3", UIWidgets.EditBox, "Gap between vehicles [m]")]
	protected float m_fGap;

	[Attribute("25", UIWidgets.EditBox, "A vehicle has to be this close to the rear of the trailer to be loaded [m]")]
	protected float m_fLoadRange;

	[Attribute("30000", UIWidgets.EditBox, "Max load [kg]")]
	protected float m_fMaxMass;

	[Attribute("1", UIWidgets.EditBox, "Max speed of the trailer for loading and unloading [m/s]")]
	protected float m_fMaxSpeed;

	protected ref array<RplId> m_aLoaded = {};
	protected ref array<vector> m_aLocalPos = {};
	protected ref array<float> m_aRearZ = {};
	protected ref array<float> m_aMass = {};

	protected SST_SemiTrailerComponent m_Trailer;
	protected vector m_vLastPos;
	protected vector m_vVelocity;
	protected float m_fCleanupTimer;
	protected ref array<vector> m_aNodePos = {};

	protected static ref array<SST_VehicleCarrierComponent> s_aCarriers = {};

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame() || !GetGame().InPlayMode())
			return;

		m_Trailer = SST_SemiTrailerComponent.Cast(owner.FindComponent(SST_SemiTrailerComponent));
		s_aCarriers.Insert(this);
		SetEventMask(owner, EntityEvent.FRAME);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		s_aCarriers.RemoveItem(this);

		// loaded vehicles are children and would be deleted with the trailer
		if (GetGame() && GetGame().InPlayMode())
		{
			for (int i = m_aLoaded.Count() - 1; i >= 0; i--)
			{
				IEntity vehicle = FindEntity(m_aLoaded[i]);
				if (!vehicle || vehicle.IsDeleted())
					continue;

				vector mat[4];
				vehicle.GetWorldTransform(mat);
				owner.RemoveChild(vehicle);
				vehicle.SetWorldTransform(mat);
				WakeUp(vehicle);
			}
		}

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	static array<SST_VehicleCarrierComponent> GetAll()
	{
		return s_aCarriers;
	}

	//------------------------------------------------------------------------------------------------
	int GetLoadedCount()
	{
		return m_aLoaded.Count();
	}

	//------------------------------------------------------------------------------------------------
	float GetLoadedMass()
	{
		float mass = 0;
		foreach (float m : m_aMass)
		{
			mass += m;
		}

		return mass;
	}

	//------------------------------------------------------------------------------------------------
	IEntity GetRearmostVehicle()
	{
		if (m_aLoaded.IsEmpty())
			return null;

		return FindEntity(m_aLoaded[m_aLoaded.Count() - 1]);
	}

	//------------------------------------------------------------------------------------------------
	bool IsStationary()
	{
		IEntity tractor;
		if (m_Trailer)
			tractor = m_Trailer.GetTractor();

		if (!tractor)
			return true;

		Physics phys = tractor.GetPhysics();
		return !phys || phys.GetVelocity().Length() <= m_fMaxSpeed;
	}

	//------------------------------------------------------------------------------------------------
	//! Distance of a world position to the rear of the trailer (where the ramps are)
	float DistanceToRamp(vector pos)
	{
		vector rear = GetOwner().CoordToParent(Vector(0, m_fDeckY, m_fDeckRear));
		return vector.Distance(pos, rear);
	}

	//------------------------------------------------------------------------------------------------
	//! Empty string when the vehicle can be loaded now, otherwise the reason
	string GetBlockReason(IEntity vehicle, out vector localPos)
	{
		IEntity owner = GetOwner();
		if (!Vehicle.Cast(vehicle) || vehicle == owner || vehicle.GetParent())
			return "Can't be loaded";

		if (IsDestroyed(vehicle))
			return "Vehicle is destroyed";

		if (DistanceToRamp(vehicle.GetOrigin()) > m_fLoadRange)
			return "Too far from the trailer";

		if (HasOccupants(vehicle))
			return "Vehicle is not empty";

		SST_SemiTractorComponent tractor = SST_SemiTractorComponent.Cast(vehicle.FindComponent(SST_SemiTractorComponent));
		if (tractor && tractor.GetTrailer())
			return "Uncouple its trailer first";

		if (m_Trailer && m_Trailer.GetTractor() == vehicle)
			return "This tractor is towing the trailer";

		if (!IsStationary())
			return "Trailer is moving";

		vector mins, maxs;
		vehicle.GetBounds(mins, maxs);
		if (maxs[0] - mins[0] > m_fMaxWidth)
			return "Too wide";

		float front = m_fDeckFront;
		if (!m_aRearZ.IsEmpty())
			front = m_aRearZ[m_aRearZ.Count() - 1] - m_fGap;

		float length = maxs[2] - mins[2];
		if (front - length < m_fDeckRear - m_fRearOverhang)
			return "No room left on the deck";

		// the last vehicle may stick out, but not past the ramps by more than the overhang
		if (front < m_fDeckRear)
			return "No room left on the deck";

		if (GetLoadedMass() + GetVehicleMass(vehicle) > m_fMaxMass)
			return "Too heavy";

		// origin on the deck, like the C-130 cargo bay: vehicle models have their origin at the ground
		localPos = Vector(0, m_fDeckY, front - maxs[2]);
		return string.Empty;
	}

	//------------------------------------------------------------------------------------------------
	//! Server
	bool Load(IEntity vehicle)
	{
		if (!Replication.IsServer() || !vehicle)
			return false;

		vector localPos;
		if (!GetBlockReason(vehicle, localPos).IsEmpty())
			return false;

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (!rpl)
			return false;

		vector mins, maxs;
		vehicle.GetBounds(mins, maxs);
		float rearZ = localPos[2] + mins[2];
		float mass = GetVehicleMass(vehicle);

		DoLoad(rpl.Id(), localPos, rearZ, mass);
		Rpc(RpcDo_Load, rpl.Id(), localPos, rearZ, mass);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Load(RplId id, vector localPos, float rearZ, float mass)
	{
		DoLoad(id, localPos, rearZ, mass);
	}

	//------------------------------------------------------------------------------------------------
	protected void DoLoad(RplId id, vector localPos, float rearZ, float mass)
	{
		if (m_aLoaded.Find(id) >= 0)
			return;

		m_aLoaded.Insert(id);
		m_aLocalPos.Insert(localPos);
		m_aRearZ.Insert(rearZ);
		m_aMass.Insert(mass);
		Attach(id, localPos);
	}

	//------------------------------------------------------------------------------------------------
	protected void Attach(RplId id, vector localPos)
	{
		IEntity vehicle = FindEntity(id);
		if (!vehicle)
			return;

		IEntity owner = GetOwner();
		Physics phys = vehicle.GetPhysics();
		if (phys)
		{
			phys.SetVelocity(vector.Zero);
			phys.SetAngularVelocity(vector.Zero);
			phys.ChangeSimulationState(SimulationState.NONE);
		}

		vector prevPos = vehicle.GetOrigin();
		if (vehicle.GetParent() != owner)
			owner.AddChild(vehicle, -1, EAddChildFlags.AUTO_TRANSFORM);

		vector mat[4];
		Math3D.MatrixIdentity4(mat);
		mat[3] = localPos;
		vehicle.SetLocalTransform(mat);

		// moved without networked movement, tell the replication scheduler where it is now
		if (Replication.IsServer())
		{
			RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
			if (rpl)
				rpl.ForceNodeMovement(prevPos);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Server: put the rearmost vehicle on the ground behind the trailer
	bool UnloadRearmost()
	{
		if (!Replication.IsServer() || m_aLoaded.IsEmpty() || !IsStationary())
			return false;

		int slot = m_aLoaded.Count() - 1;
		IEntity vehicle = FindEntity(m_aLoaded[slot]);
		if (!vehicle)
			return false;

		IEntity owner = GetOwner();
		vector mins, maxs;
		vehicle.GetBounds(mins, maxs);

		// facing like the trailer, front of the vehicle 1.5 m behind the deck
		vector fwd = owner.GetTransformAxis(2);
		fwd[1] = 0;
		fwd.Normalize();

		vector spot = owner.CoordToParent(Vector(0, m_fDeckY, m_fDeckRear - 1.5 - maxs[2]));
		float ground = GroundY(owner, spot);
		spot[1] = ground - mins[1] + 0.15;

		DoUnload(slot, spot, fwd);
		Rpc(RpcDo_Unload, slot, spot, fwd);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Unload(int slot, vector pos, vector fwd)
	{
		DoUnload(slot, pos, fwd);
	}

	//------------------------------------------------------------------------------------------------
	protected void DoUnload(int slot, vector pos, vector fwd)
	{
		if (!m_aLoaded.IsIndexValid(slot))
			return;

		RplId id = m_aLoaded[slot];
		RemoveSlot(slot);

		IEntity vehicle = FindEntity(id);
		if (!vehicle)
			return;

		vector prevPos = vehicle.GetOrigin();
		if (vehicle.GetParent() == GetOwner())
			GetOwner().RemoveChild(vehicle);

		vector up = Vector(0, 1, 0);
		vector mat[4];
		mat[0] = up * fwd;
		mat[1] = up;
		mat[2] = fwd;
		mat[3] = pos;
		vehicle.SetWorldTransform(mat);
		WakeUp(vehicle);

		if (!Replication.IsServer())
			return;

		GiveToDriver(vehicle);

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (rpl)
			rpl.ForceNodeMovement(prevPos);

		// it was a child of the trailer in the editor too
		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(vehicle.FindComponent(SCR_EditableEntityComponent));
		if (editable && editable.GetParentEntity())
			editable.SetParentEntity(null);
	}

	//------------------------------------------------------------------------------------------------
	protected void RemoveSlot(int slot)
	{
		if (m_aNodePos.IsIndexValid(slot))
			m_aNodePos.Remove(slot);

		m_aLoaded.Remove(slot);
		m_aLocalPos.Remove(slot);
		m_aRearZ.Remove(slot);
		m_aMass.Remove(slot);
	}

	//------------------------------------------------------------------------------------------------
	protected static void WakeUp(IEntity vehicle)
	{
		Physics phys = vehicle.GetPhysics();
		if (phys)
		{
			phys.ChangeSimulationState(SimulationState.SIMULATION);
			phys.SetVelocity(vector.Zero);
			phys.SetAngularVelocity(vector.Zero);
		}

		vehicle.Update();
	}

	//------------------------------------------------------------------------------------------------
	//! The engine gives a vehicle to its driver only when he gets in
	protected static void GiveToDriver(IEntity vehicle)
	{
		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (!rpl)
			return;

		RplIdentity owner = RplIdentity.Invalid();
		Vehicle veh = Vehicle.Cast(vehicle);
		if (veh && veh.GetPilotCompartmentSlot())
		{
			IEntity driver = veh.GetPilotCompartmentSlot().GetOccupant();
			if (driver)
			{
				RplComponent driverRpl = RplComponent.Cast(driver.FindComponent(RplComponent));
				if (driverRpl)
					owner = Replication.FindOwner(driverRpl.Id());
			}
		}

		rpl.GiveExt(owner, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Keeps the vehicles on the deck. Getting in and out turns the vehicle body back on, it is turned
	//! off again once nobody sits in it (never while somebody does).
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		vector pos = owner.GetOrigin();
		if (timeSlice > 0)
			m_vVelocity = (pos - m_vLastPos) / timeSlice;

		m_vLastPos = pos;

		if (m_aLoaded.IsEmpty())
			return;

		bool server = Replication.IsServer();
		bool cleanup = false;
		if (server)
		{
			m_fCleanupTimer += timeSlice;
			if (m_fCleanupTimer > 1)
			{
				m_fCleanupTimer = 0;
				cleanup = true;
			}
		}

		for (int i = m_aLoaded.Count() - 1; i >= 0; i--)
		{
			IEntity vehicle = FindEntity(m_aLoaded[i]);
			if (!vehicle)
			{
				// gone on the server = deleted or destroyed
				if (cleanup)
				{
					RemoveSlot(i);
					Rpc(RpcDo_Drop, i);
				}

				continue;
			}

			// a wreck is not carried, it comes off the deck before the wreck model is swapped in
			if (IsDestroyed(vehicle))
			{
				if (server)
				{
					RplId id = m_aLoaded[i];
					RemoveSlot(i);
					Release(vehicle);
					Rpc(RpcDo_Release, id);
				}
				else
				{
					Detach(vehicle);
				}

				continue;
			}

			if (vehicle.GetParent() != owner)
			{
				Attach(m_aLoaded[i], m_aLocalPos[i]);
				continue;
			}

			// carried along: keep the replication scheduler up to date (see Attach)
			if (cleanup)
				UpdateNode(i, vehicle);

			Physics phys = vehicle.GetPhysics();
			if (!phys)
				continue;

			SimulationState state = phys.GetSimulationState();
			if (state != SimulationState.NONE && !HasOccupants(vehicle))
			{
				phys.SetVelocity(vector.Zero);
				phys.SetAngularVelocity(vector.Zero);
				phys.ChangeSimulationState(SimulationState.NONE);
			}

			vector mat[4];
			Math3D.MatrixIdentity4(mat);
			mat[3] = m_aLocalPos[i];
			vehicle.SetLocalTransform(mat);

			// somebody sits in it: the body is simulated, carry it along with the trailer
			if (state == SimulationState.SIMULATION)
			{
				phys.SetVelocity(m_vVelocity);
				phys.SetAngularVelocity(vector.Zero);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void UpdateNode(int slot, IEntity vehicle)
	{
		while (m_aNodePos.Count() <= slot)
		{
			m_aNodePos.Insert(vehicle.GetOrigin());
		}

		vector pos = vehicle.GetOrigin();
		if (vector.DistanceSq(pos, m_aNodePos[slot]) < 1)
			return;

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (rpl)
			rpl.ForceNodeMovement(m_aNodePos[slot]);

		m_aNodePos[slot] = pos;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Release(RplId id)
	{
		int slot = m_aLoaded.Find(id);
		if (slot >= 0)
			RemoveSlot(slot);

		IEntity vehicle = FindEntity(id);
		if (vehicle)
			Release(vehicle);
	}

	//------------------------------------------------------------------------------------------------
	protected void Release(IEntity vehicle)
	{
		vector prevPos = vehicle.GetOrigin();
		Detach(vehicle);
		WakeUp(vehicle);

		if (!Replication.IsServer())
			return;

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (rpl)
			rpl.ForceNodeMovement(prevPos);

		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(vehicle.FindComponent(SCR_EditableEntityComponent));
		if (editable && editable.GetParentEntity())
			editable.SetParentEntity(null);
	}

	//------------------------------------------------------------------------------------------------
	//! Off the trailer hierarchy, staying where it is
	protected void Detach(IEntity vehicle)
	{
		if (vehicle.IsDeleted() || vehicle.GetParent() != GetOwner())
			return;

		vector mat[4];
		vehicle.GetWorldTransform(mat);
		GetOwner().RemoveChild(vehicle);
		vehicle.SetWorldTransform(mat);
	}

	//------------------------------------------------------------------------------------------------
	protected static bool IsDestroyed(IEntity vehicle)
	{
		DamageManagerComponent damage = DamageManagerComponent.Cast(vehicle.FindComponent(DamageManagerComponent));
		return damage && damage.GetState() == EDamageState.DESTROYED;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Drop(int slot)
	{
		if (m_aLoaded.IsIndexValid(slot))
			RemoveSlot(slot);
	}

	//------------------------------------------------------------------------------------------------
	override bool RplSave(ScriptBitWriter writer)
	{
		writer.WriteInt(m_aLoaded.Count());
		foreach (int i, RplId id : m_aLoaded)
		{
			writer.WriteRplId(id);
			writer.WriteVector(m_aLocalPos[i]);
			writer.WriteFloat(m_aRearZ[i]);
			writer.WriteFloat(m_aMass[i]);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool RplLoad(ScriptBitReader reader)
	{
		int count;
		if (!reader.ReadInt(count))
			return false;

		for (int i = 0; i < count; i++)
		{
			RplId id;
			vector localPos;
			float rearZ, mass;
			if (!reader.ReadRplId(id) || !reader.ReadVector(localPos) || !reader.ReadFloat(rearZ) || !reader.ReadFloat(mass))
				return false;

			// attached in EOnFrame once the vehicle exists here
			m_aLoaded.Insert(id);
			m_aLocalPos.Insert(localPos);
			m_aRearZ.Insert(rearZ);
			m_aMass.Insert(mass);
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	static SST_VehicleCarrierComponent FindCarrierFor(IEntity vehicle, out string reason)
	{
		SST_VehicleCarrierComponent best;
		float bestDist = float.MAX;
		vector pos = vehicle.GetOrigin();
		foreach (SST_VehicleCarrierComponent carrier : s_aCarriers)
		{
			if (!carrier)
				continue;

			float dist = carrier.DistanceToRamp(pos);
			if (dist > carrier.m_fLoadRange || dist >= bestDist)
				continue;

			vector localPos;
			string blocked = carrier.GetBlockReason(vehicle, localPos);
			if (!blocked.IsEmpty())
			{
				reason = blocked;
				continue;
			}

			best = carrier;
			bestDist = dist;
		}

		return best;
	}

	//------------------------------------------------------------------------------------------------
	static bool HasOccupants(IEntity vehicle)
	{
		SCR_BaseCompartmentManagerComponent compartments = SCR_BaseCompartmentManagerComponent.Cast(vehicle.FindComponent(SCR_BaseCompartmentManagerComponent));
		if (!compartments)
			return false;

		array<BaseCompartmentSlot> slots = {};
		compartments.GetCompartments(slots);
		foreach (BaseCompartmentSlot slot : slots)
		{
			if (slot && slot.GetOccupant())
				return true;
		}

		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static float GetVehicleMass(IEntity vehicle)
	{
		Physics phys = vehicle.GetPhysics();
		if (phys)
			return phys.GetMass();

		return 0;
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity FindEntity(RplId id)
	{
		if (!id.IsValid())
			return null;

		RplComponent rpl = RplComponent.Cast(Replication.FindItem(id));
		if (!rpl)
			return null;

		return rpl.GetEntity();
	}

	//------------------------------------------------------------------------------------------------
	protected static IEntity s_TraceSelf;

	protected static bool TraceFilter(notnull IEntity e)
	{
		IEntity root = e.GetRootParent();
		return root != s_TraceSelf && !ChimeraCharacter.Cast(root) && !Vehicle.Cast(root);
	}

	//------------------------------------------------------------------------------------------------
	protected float GroundY(IEntity owner, vector pos)
	{
		TraceParam param = new TraceParam();
		param.Start = pos + Vector(0, 5, 0);
		param.End = pos - Vector(0, 10, 0);
		param.Flags = TraceFlags.WORLD | TraceFlags.ENTS;
		param.LayerMask = EPhysicsLayerDefs.VehicleCast;

		s_TraceSelf = owner;
		float frac = owner.GetWorld().TraceMove(param, TraceFilter);
		if (frac >= 1)
			return pos[1] - m_fDeckY;

		return param.Start[1] + (param.End[1] - param.Start[1]) * frac;
	}
}
