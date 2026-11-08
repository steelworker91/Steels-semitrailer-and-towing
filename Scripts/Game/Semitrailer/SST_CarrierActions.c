// "Load onto trailer" sits on every vehicle (Vehicle_Base override) and shows up near a carrier
// semitrailer. On a wreck it sits on the vanilla salvage action entity (see SST_WreckLoad.c).
// "Unload vehicle" sits at the ramps of the carrier.

class SST_LoadOnCarrierAction : ScriptedUserAction
{
	protected IEntity m_Owner;
	protected IEntity m_Vehicle;
	protected SST_VehicleCarrierComponent m_Carrier;
	protected string m_sReason;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Owner = pOwnerEntity;
	}

	//------------------------------------------------------------------------------------------------
	protected void Resolve()
	{
		m_sReason = string.Empty;
		m_Carrier = null;

		// on the wreck action entity the vehicle is the parent
		m_Vehicle = m_Owner;
		if (m_Owner && !Vehicle.Cast(m_Owner))
			m_Vehicle = m_Owner.GetParent();

		if (!m_Vehicle || m_Vehicle.GetParent())
			return;

		m_Carrier = SST_VehicleCarrierComponent.FindCarrierFor(m_Vehicle, m_sReason);
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		Resolve();
		return m_Carrier != null || !m_sReason.IsEmpty();
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		Resolve();
		if (m_Carrier)
			return true;

		if (!m_sReason.IsEmpty())
			SetCannotPerformReason(m_sReason);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		if (m_Owner && !Vehicle.Cast(m_Owner))
			outName = "Load wreck onto trailer";
		else
			outName = "Load onto trailer";

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer())
			return;

		Resolve();
		if (m_Carrier)
			m_Carrier.Load(m_Vehicle);
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}
}

class SST_UnloadCarrierAction : ScriptedUserAction
{
	protected SST_VehicleCarrierComponent m_Carrier;

	//------------------------------------------------------------------------------------------------
	override void Init(IEntity pOwnerEntity, GenericComponent pManagerComponent)
	{
		m_Carrier = SST_VehicleCarrierComponent.Cast(pOwnerEntity.FindComponent(SST_VehicleCarrierComponent));
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return m_Carrier != null && m_Carrier.GetLoadedCount() > 0;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		if (!m_Carrier || !m_Carrier.GetRearmostVehicle())
			return false;

		if (!m_Carrier.IsStationary())
		{
			SetCannotPerformReason("Trailer is moving");
			return false;
		}

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		outName = "Unload vehicle";
		if (!m_Carrier)
			return true;

		IEntity vehicle = m_Carrier.GetRearmostVehicle();
		if (!vehicle)
			return true;

		SCR_EditableEntityComponent editable = SCR_EditableEntityComponent.Cast(vehicle.FindComponent(SCR_EditableEntityComponent));
		if (editable)
			outName = string.Format("Unload %1", WidgetManager.Translate(editable.GetDisplayName()));

		return true;
	}

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (Replication.IsServer() && m_Carrier)
			m_Carrier.UnloadRearmost();
	}

	//------------------------------------------------------------------------------------------------
	override bool HasLocalEffectOnlyScript()
	{
		return false;
	}
}
