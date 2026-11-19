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
	protected ref array<bool> m_aWreck = {};

	protected SST_SemiTrailerComponent m_Trailer;
	protected vector m_vLastPos;
	protected vector m_vVelocity;
	protected float m_fCleanupTimer;
	protected ref array<vector> m_aNodePos = {};
	protected ref map<RplId, bool> m_mNwkOn = new map<RplId, bool>();

	protected static ref array<SST_VehicleCarrierComponent> s_aCarriers = {};

	protected static const float WRECK_SETTLE_TIME = 5000; // ms
	protected static const float THROW_LEAN = 0.26; // rad, a vehicle that comes off leans over that far

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
				SetFree(vehicle, mat);
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
	//! Usable deck along the trailer, trailer space [m]
	void GetDeckSpan(out float front, out float rear)
	{
		front = m_fDeckFront;
		rear = m_fDeckRear;
	}

	//------------------------------------------------------------------------------------------------
	int GetLoadedCount()
	{
		return m_aLoaded.Count();
	}

	//------------------------------------------------------------------------------------------------
	float GetDeckY()
	{
		return m_fDeckY;
	}

	//------------------------------------------------------------------------------------------------
	float GetMaxMass()
	{
		return m_fMaxMass;
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
	//! Vehicles that sit on the deck right now
	void GetLoadedVehicles(notnull array<IEntity> vehicles)
	{
		IEntity owner = GetOwner();
		foreach (RplId id : m_aLoaded)
		{
			IEntity vehicle = FindEntity(id);
			if (vehicle && vehicle.GetParent() == owner)
				vehicles.Insert(vehicle);
		}
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

		// in a road train the vehicle at the front moves, the dolly or trailer in between has no speed
		Physics phys = SST_SemiTractorComponent.GetChainHead(tractor).GetPhysics();
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

		// wrecks can go on the deck too, once the wreck model is in (see SST_WreckTime.c)
		bool wreck = IsDestroyed(vehicle);
		if (wreck && !IsWreckSettled(vehicle))
			return "Wreck is still settling";

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

		// origin on the deck, like the C-130 cargo bay: vehicle models have their origin at the ground.
		// A wreck model has no wheels under it, its lowest point goes on the deck instead.
		if (wreck)
			localPos = Vector(-(mins[0] + maxs[0]) * 0.5, m_fDeckY - mins[1], front - maxs[2]);
		else
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

		bool wreck = IsDestroyed(vehicle);
		DoLoad(rpl.Id(), localPos, rearZ, mass, wreck);
		Rpc(RpcDo_Load, rpl.Id(), localPos, rearZ, mass, wreck);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Load(RplId id, vector localPos, float rearZ, float mass, bool wreck)
	{
		DoLoad(id, localPos, rearZ, mass, wreck);
	}

	//------------------------------------------------------------------------------------------------
	protected void DoLoad(RplId id, vector localPos, float rearZ, float mass, bool wreck)
	{
		if (m_aLoaded.Find(id) >= 0)
			return;

		m_aLoaded.Insert(id);
		m_aLocalPos.Insert(localPos);
		m_aRearZ.Insert(rearZ);
		m_aMass.Insert(mass);
		m_aWreck.Insert(wreck);
		Attach(id, localPos);
	}

	//------------------------------------------------------------------------------------------------
	protected void Attach(RplId id, vector localPos)
	{
		IEntity vehicle = FindEntity(id);
		if (!vehicle)
			return;

		IEntity owner = GetOwner();
		NwkMovementComponent nwk = NwkMovementComponent.Cast(vehicle.FindComponent(NwkMovementComponent));
		if (nwk && !m_mNwkOn.Contains(id))
			m_mNwkOn.Set(id, nwk.IsSimulationEnabled());

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
		if (!vehicle || SST_CargoStrapComponent.IsStrapped(vehicle))
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
		vector up = Vector(0, 1, 0);
		vector mat[4];
		mat[0] = up * fwd;
		mat[1] = up;
		mat[2] = fwd;
		mat[3] = pos;
		SetFree(vehicle, mat);

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
	//! Server: the straps did not hold, the vehicle goes over the side of the deck (side -1 left,
	//! 1 right) and keeps most of the speed of the trailer
	bool ThrowOff(IEntity vehicle, float side, vector velocity)
	{
		if (!Replication.IsServer() || !vehicle)
			return false;

		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		if (!rpl)
			return false;

		int slot = m_aLoaded.Find(rpl.Id());
		if (slot < 0)
			return false;

		IEntity owner = GetOwner();
		vector mins, maxs;
		vehicle.GetBounds(mins, maxs);

		// beside the deck, a bit above it and leaning over, it falls from there
		vector right = owner.GetTransformAxis(0);
		vector mat[4];
		vehicle.GetWorldTransform(mat);
		float offset = m_fMaxWidth * 0.5 + (maxs[0] - mins[0]) * 0.5 + 0.2;
		vector pos = mat[3] + right * (side * offset) + Vector(0, 0.3, 0);

		vector fwd = mat[2];
		vector up = mat[1] * Math.Cos(THROW_LEAN) + right * (side * Math.Sin(THROW_LEAN));
		up.Normalize();

		DoThrowOff(slot, pos, fwd, up);
		Rpc(RpcDo_ThrowOff, slot, pos, fwd, up);

		Physics phys = vehicle.GetPhysics();
		if (phys)
			phys.SetVelocity(velocity * 0.8 + right * (side * 1.5));

		GiveToDriver(vehicle);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_ThrowOff(int slot, vector pos, vector fwd, vector up)
	{
		DoThrowOff(slot, pos, fwd, up);
	}

	//------------------------------------------------------------------------------------------------
	protected void DoThrowOff(int slot, vector pos, vector fwd, vector up)
	{
		if (!m_aLoaded.IsIndexValid(slot))
			return;

		RplId id = m_aLoaded[slot];
		RemoveSlot(slot);

		IEntity vehicle = FindEntity(id);
		if (!vehicle)
			return;

		vector prevPos = vehicle.GetOrigin();
		vector side = up * fwd;
		side.Normalize();
		fwd = side * up;
		vector mat[4];
		mat[0] = side;
		mat[1] = up;
		mat[2] = fwd;
		mat[3] = pos;
		SetFree(vehicle, mat);

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
	protected void RemoveSlot(int slot)
	{
		if (m_aNodePos.IsIndexValid(slot))
			m_aNodePos.Remove(slot);

		m_aLoaded.Remove(slot);
		m_aLocalPos.Remove(slot);
		m_aRearZ.Remove(slot);
		m_aMass.Remove(slot);
		m_aWreck.Remove(slot);
	}

	//------------------------------------------------------------------------------------------------
	//! Off the deck and standing at mat, on every machine. Moved the way vanilla moves a vehicle
	//! (SCR_EditableEntityComponent.SetTransformOwner): Teleport, so the networked movement takes the
	//! jump instead of still having the vehicle where it was before, and no Update() after it.
	protected void SetFree(IEntity vehicle, vector mat[4])
	{
		if (vehicle.GetParent() == GetOwner())
			GetOwner().RemoveChild(vehicle, true);

		Physics phys = vehicle.GetPhysics();
		if (phys)
			phys.ChangeSimulationState(SimulationState.SIMULATION);

		// the networked movement as it was before the deck
		RplComponent rpl = RplComponent.Cast(vehicle.FindComponent(RplComponent));
		NwkMovementComponent nwk = NwkMovementComponent.Cast(vehicle.FindComponent(NwkMovementComponent));
		if (rpl && m_mNwkOn.Contains(rpl.Id()))
		{
			if (nwk && m_mNwkOn.Get(rpl.Id()) && !nwk.IsSimulationEnabled())
				nwk.EnableSimulation(true);

			m_mNwkOn.Remove(rpl.Id());
		}

		BaseGameEntity gameEntity = BaseGameEntity.Cast(vehicle);
		if (gameEntity)
			gameEntity.Teleport(mat);
		else
			vehicle.SetWorldTransform(mat);

		if (phys)
		{
			phys.SetVelocity(vector.Zero);
			phys.SetAngularVelocity(vector.Zero);
		}
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

			// destroyed while on the deck: it comes off before the wreck model is swapped in.
			// Wrecks that were loaded as wrecks stay.
			if (!m_aWreck[i] && IsDestroyed(vehicle))
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
		vector mat[4];
		vehicle.GetWorldTransform(mat);
		SetFree(vehicle, mat);

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
	//! Vanilla swaps in the wreck model a moment after the explosion (and waits for players to get out
	//! first), the vehicle must not be on the deck while that happens
	protected static bool IsWreckSettled(IEntity vehicle)
	{
		SCR_VehicleDamageManagerComponent damage = SCR_VehicleDamageManagerComponent.Cast(vehicle.FindComponent(SCR_VehicleDamageManagerComponent));
		return !damage || damage.SST_GetTimeSinceDestroyed() < 0 || damage.SST_GetTimeSinceDestroyed() > WRECK_SETTLE_TIME;
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
			writer.WriteBool(m_aWreck[i]);
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
			bool wreck;
			if (!reader.ReadRplId(id) || !reader.ReadVector(localPos) || !reader.ReadFloat(rearZ) || !reader.ReadFloat(mass) || !reader.ReadBool(wreck))
				return false;

			// attached in EOnFrame once the vehicle exists here
			m_aLoaded.Insert(id);
			m_aLocalPos.Insert(localPos);
			m_aRearZ.Insert(rearZ);
			m_aMass.Insert(mass);
			m_aWreck.Insert(wreck);
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
